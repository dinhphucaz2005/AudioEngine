package com.example.audioengine

import android.content.ContentUris
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.util.LruCache
import android.util.Size
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.ConcurrentHashMap
import androidx.core.net.toUri

data class SongThumbRequest(
    val songId: Long,
    val audioUri: Uri,
    val albumId: Long,
    val dateModified: Long,
)

class ThumbnailRepository(context: Context) {

    private val appContext = context.applicationContext
    private val ioDispatcher = Dispatchers.IO.limitedParallelism(4)
    private val scope = CoroutineScope(SupervisorJob() + ioDispatcher)
    private val cacheDir = File(appContext.cacheDir, "playlist_thumbs").apply { mkdirs() }
    private val cacheWriteMutex = Mutex()
    private val memoryCacheMutex = Mutex()

    // Bitmap count is bounded because rows are small and images are downsampled.
    private val memoryCache = object : LruCache<String, Bitmap>(120) {}
    private val inFlight = ConcurrentHashMap<String, Deferred<Bitmap?>>()

    init {
        scope.async { trimDiskCache() }
    }

    suspend fun load(request: SongThumbRequest): Bitmap? {
        val key = cacheKey(request)
        memoryCacheMutex.withLock {
            memoryCache.get(key)?.let { return it }
        }

        val existing = inFlight[key]
        if (existing != null) {
            return existing.await()
        }

        val deferred = scope.async {
            loadFromDisk(key)?.also { bitmap ->
                memoryCacheMutex.withLock {
                    memoryCache.put(key, bitmap)
                }
                return@async bitmap
            }

            val extracted = extractThumbnail(request) ?: return@async null
            memoryCacheMutex.withLock {
                memoryCache.put(key, extracted)
            }
            saveToDisk(key, extracted)
            extracted
        }

        val running = inFlight.putIfAbsent(key, deferred) ?: deferred
        if (running !== deferred) {
            deferred.cancel()
        }

        return try {
            running.await()
        } finally {
            inFlight.remove(key, running)
        }
    }

    suspend fun prefetch(requests: List<SongThumbRequest>) = coroutineScope {
        requests.map { request ->
            async(ioDispatcher) { load(request) }
        }.awaitAll()
    }

    fun close() {
        scope.cancel()
        memoryCache.evictAll()
        inFlight.clear()
    }

    private fun cacheKey(request: SongThumbRequest): String {
        return "${request.songId}_${request.dateModified}"
    }

    private suspend fun loadFromDisk(key: String): Bitmap? = withContext(ioDispatcher) {
        val file = File(cacheDir, "$key.jpg")
        if (!file.exists()) return@withContext null
        BitmapFactory.decodeFile(file.absolutePath)
    }

    private suspend fun saveToDisk(key: String, bitmap: Bitmap) = withContext(ioDispatcher) {
        val file = File(cacheDir, "$key.jpg")
        cacheWriteMutex.withLock {
            FileOutputStream(file).use { out ->
                bitmap.compress(Bitmap.CompressFormat.JPEG, 82, out)
            }
        }
    }

    private suspend fun extractThumbnail(request: SongThumbRequest): Bitmap? = withContext(ioDispatcher) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val loaded = runCatching {
                appContext.contentResolver.loadThumbnail(
                    request.audioUri,
                    Size(128, 128),
                    null,
                )
            }.getOrNull()
            if (loaded != null) {
                return@withContext loaded
            }
        }

        val embedded = runCatching {
            val retriever = MediaMetadataRetriever()
            try {
                retriever.setDataSource(appContext, request.audioUri)
                retriever.embeddedPicture?.let { bytes ->
                    BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                }
            } finally {
                retriever.release()
            }
        }.getOrNull()

        if (embedded != null) {
            return@withContext embedded
        }

        val albumArtUri = ContentUris.withAppendedId(ALBUM_ART_CONTENT_URI, request.albumId)
        runCatching {
            appContext.contentResolver.openInputStream(albumArtUri)?.use { input ->
                BitmapFactory.decodeStream(input)
            }
        }.getOrNull()
    }

    private suspend fun trimDiskCache() = withContext(ioDispatcher) {
        val files = cacheDir.listFiles()?.filter { it.isFile } ?: return@withContext
        if (files.isEmpty()) return@withContext

        var totalBytes = files.sumOf { it.length() }
        val sortedByAge = files.sortedBy { it.lastModified() }

        for (file in sortedByAge) {
            val shouldDeleteByCount = cacheDir.listFiles()?.size?.let { it > MAX_DISK_FILES } == true
            val shouldDeleteBySize = totalBytes > MAX_DISK_BYTES
            if (!shouldDeleteByCount && !shouldDeleteBySize) break

            val fileBytes = file.length()
            val deleted = file.delete()
            if (deleted) {
                totalBytes -= fileBytes
            }
        }
    }

    companion object {
        private const val MAX_DISK_FILES = 420
        private const val MAX_DISK_BYTES = 72L * 1024L * 1024L
        private val ALBUM_ART_CONTENT_URI = "content://media/external/audio/albumart".toUri()
    }
}


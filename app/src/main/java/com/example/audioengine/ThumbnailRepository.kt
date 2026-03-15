package com.example.audioengine

import android.content.ContentUris
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaExtractor
import android.media.MediaFormat
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

class ThumbnailRepository(context: Context) {

    data class SongMetadataRequest(
        val songId: Long,
        val audioUri: Uri,
        val albumId: Long,
        val dateModified: Long,
        val fallbackTitle: String,
        val fallbackArtist: String,
        val fallbackAlbum: String,
        val fallbackDurationMs: Long,
    )

    data class SongMetadata(
        val title: String,
        val artist: String,
        val album: String,
        val durationMs: Long,
        val year: Int?,
        val trackNumber: Int?,
        val genre: String?,
        val albumArtist: String?,
        val composer: String?,
        val author: String?,
        val writer: String?,
        val mimeType: String?,
        val bitrate: String?,
        val sampleRate: String?,
        val bitsPerSample: String?,
        val channelCount: Int?,
        val thumbnail: Bitmap?,
    )

    private val appContext = context.applicationContext
    private val ioDispatcher = Dispatchers.IO.limitedParallelism(4)
    private val scope = CoroutineScope(SupervisorJob() + ioDispatcher)
    private val cacheDir = File(appContext.cacheDir, "playlist_thumbs").apply { mkdirs() }
    private val cacheWriteMutex = Mutex()
    private val thumbnailCacheMutex = Mutex()
    private val metadataCacheMutex = Mutex()

    // Bitmap count is bounded because rows are small and images are downsampled.
    private val thumbnailMemoryCache = object : LruCache<String, Bitmap>(120) {}
    private val metadataMemoryCache = object : LruCache<String, SongMetadata>(220) {}
    private val thumbnailInFlight = ConcurrentHashMap<String, Deferred<Bitmap?>>()
    private val metadataInFlight = ConcurrentHashMap<String, Deferred<SongMetadata>>()

    init {
        scope.async { trimDiskCache() }
    }

    suspend fun loadMetadata(request: SongMetadataRequest): SongMetadata {
        val key = cacheKey(request)
        metadataCacheMutex.withLock {
            metadataMemoryCache.get(key)?.let { return it }
        }

        val existing = metadataInFlight[key]
        if (existing != null) {
            return existing.await()
        }

        val deferred = scope.async {
            buildMetadata(request).also { metadata ->
                metadataCacheMutex.withLock {
                    metadataMemoryCache.put(key, metadata)
                }
            }
        }

        val running = metadataInFlight.putIfAbsent(key, deferred) ?: deferred
        if (running !== deferred) {
            deferred.cancel()
        }

        return try {
            running.await()
        } finally {
            metadataInFlight.remove(key, running)
        }
    }

    suspend fun prefetchMetadata(requests: List<SongMetadataRequest>) = coroutineScope {
        requests.map { request ->
            async(ioDispatcher) { loadMetadata(request) }
        }.awaitAll()
    }

    private suspend fun loadThumbnail(request: SongMetadataRequest): Bitmap? {
        val key = cacheKey(request)
        thumbnailCacheMutex.withLock {
            thumbnailMemoryCache.get(key)?.let { return it }
        }

        val existing = thumbnailInFlight[key]
        if (existing != null) {
            return existing.await()
        }

        val deferred = scope.async {
            loadFromDisk(key)?.also { bitmap ->
                thumbnailCacheMutex.withLock {
                    thumbnailMemoryCache.put(key, bitmap)
                }
                return@async bitmap
            }

            val extracted = extractThumbnail(request) ?: return@async null
            thumbnailCacheMutex.withLock {
                thumbnailMemoryCache.put(key, extracted)
            }
            saveToDisk(key, extracted)
            extracted
        }

        val running = thumbnailInFlight.putIfAbsent(key, deferred) ?: deferred
        if (running !== deferred) {
            deferred.cancel()
        }

        return try {
            running.await()
        } finally {
            thumbnailInFlight.remove(key, running)
        }
    }

    fun close() {
        scope.cancel()
        thumbnailMemoryCache.evictAll()
        metadataMemoryCache.evictAll()
        thumbnailInFlight.clear()
        metadataInFlight.clear()
    }

    private fun cacheKey(request: SongMetadataRequest): String {
        return "${request.songId}_${request.dateModified}"
    }

    private suspend fun buildMetadata(request: SongMetadataRequest): SongMetadata {
        val extracted = withContext(ioDispatcher) {
            runCatching {
                val retriever = MediaMetadataRetriever()
                try {
                    retriever.setDataSource(appContext, request.audioUri)
                    SongMetadata(
                        title = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE)
                            .orFallback(request.fallbackTitle),
                        artist = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST)
                            .orFallback(request.fallbackArtist),
                        album = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM)
                            .orFallback(request.fallbackAlbum),
                        durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                            ?.toLongOrNull() ?: request.fallbackDurationMs,
                        year = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_YEAR)
                            ?.toIntOrNull(),
                        trackNumber = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER)
                            .toTrackNumberOrNull(),
                        genre = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_GENRE),
                        albumArtist = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUMARTIST),
                        composer = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_COMPOSER),
                        author = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_AUTHOR),
                        writer = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_WRITER),
                        mimeType = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_MIMETYPE),
                        bitrate = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE),
                        sampleRate = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                            retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_SAMPLERATE)
                        } else {
                            null
                        },
                        bitsPerSample = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                            retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITS_PER_SAMPLE)
                        } else {
                            null
                        },
                        channelCount = null,
                        thumbnail = retriever.embeddedPicture?.let { bytes ->
                            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
                        },
                    )
                } finally {
                    retriever.release()
                }
            }.getOrNull()
        }

        if (extracted != null) {
            val thumbnail = extracted.thumbnail ?: loadThumbnail(request)
            val channelCount = extracted.channelCount ?: extractChannelCount(request.audioUri)
            return extracted.copy(
                channelCount = channelCount,
                thumbnail = thumbnail,
            )
        }

        return SongMetadata(
            title = request.fallbackTitle,
            artist = request.fallbackArtist,
            album = request.fallbackAlbum,
            durationMs = request.fallbackDurationMs,
            year = null,
            trackNumber = null,
            genre = null,
            albumArtist = null,
            composer = null,
            author = null,
            writer = null,
            mimeType = null,
            bitrate = null,
            sampleRate = null,
            bitsPerSample = null,
            channelCount = extractChannelCount(request.audioUri),
            thumbnail = loadThumbnail(request),
        )
    }

    private suspend fun extractChannelCount(audioUri: Uri): Int? = withContext(ioDispatcher) {
        runCatching {
            val extractor = MediaExtractor()
            try {
                extractor.setDataSource(appContext, audioUri, null)
                (0 until extractor.trackCount).firstNotNullOfOrNull { trackIndex ->
                    val format = extractor.getTrackFormat(trackIndex)
                    val mime = format.getString(MediaFormat.KEY_MIME).orEmpty()
                    if (!mime.startsWith("audio/")) {
                        return@firstNotNullOfOrNull null
                    }
                    if (!format.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
                        return@firstNotNullOfOrNull null
                    }
                    format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                }
            } finally {
                extractor.release()
            }
        }.getOrNull()
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

    private suspend fun extractThumbnail(request: SongMetadataRequest): Bitmap? =
        withContext(ioDispatcher) {
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
            val shouldDeleteByCount =
                cacheDir.listFiles()?.size?.let { it > MAX_DISK_FILES } == true
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

private fun String?.orFallback(fallback: String): String {
    val value = this?.trim().orEmpty()
    return value.ifEmpty { fallback }
}

private fun String?.toTrackNumberOrNull(): Int? {
    if (this == null) return null
    val normalized = substringBefore('/').trim()
    return normalized.toIntOrNull()
}


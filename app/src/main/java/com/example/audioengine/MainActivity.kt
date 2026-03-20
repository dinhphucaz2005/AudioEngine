@file:Suppress("COMPOSE_APPLIER_CALL_MISMATCH")

package com.example.audioengine

import android.Manifest
import android.content.ContentUris
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.util.Locale

class MainActivity : ComponentActivity() {

    private data class SongItem(
        val id: Long,
        val title: String,
        val artist: String,
        val album: String,
        val durationMs: Long,
        val uri: Uri,
        val albumId: Long,
        val dateModified: Long,
    )

    private lateinit var audioEngine: AudioEngine
    private lateinit var thumbnailRepository: ThumbnailRepository
    private var selectedFilterType by mutableStateOf(AudioEngine.FilterType.NONE)
    private var audioEngineIsReady by mutableStateOf(false)
    private var hasAudioPermission by mutableStateOf(false)
    private var songs by mutableStateOf<List<SongItem>>(emptyList())
    private var selectedSongId by mutableStateOf<Long?>(null)

    private val requestAudioPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            hasAudioPermission = granted
            songs = if (granted) queryDeviceSongs() else emptyList()
            if (granted) {
                prefetchPlaylistMetadata(songs)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        audioEngine = AudioEngine()
        thumbnailRepository = ThumbnailRepository(applicationContext)

        setContent {
            MaterialTheme {
                Scaffold { paddingValues ->
                    AudioEngineScreen(
                        modifier = Modifier
                            .padding(paddingValues),
                        audioEngine = audioEngine,
                        selectedFilterType = selectedFilterType,
                        hasAudioPermission = hasAudioPermission,
                        songs = songs,
                        selectedSongId = selectedSongId,
                        onFilterTypeChanged = { filterType ->
                            selectedFilterType = filterType
                            audioEngine.setFilterType(filterType)
                        },
                        onRequestPermission = { checkPermissions() },
                        onSongSelected = { song ->
                            selectedSongId = song.id
                            loadAudioFromUri(song.uri)
                        },
                    )
                }
            }
        }

        checkPermissions()
    }

    private fun checkPermissions() {
        val perm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.READ_MEDIA_AUDIO
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

        if (ContextCompat.checkSelfPermission(this, perm) == PackageManager.PERMISSION_GRANTED) {
            hasAudioPermission = true
            songs = queryDeviceSongs()
            prefetchPlaylistMetadata(songs)
        } else {
            requestAudioPermissionLauncher.launch(perm)
        }
    }

    private fun loadAudioFromUri(uri: Uri) {
        if (!audioEngineIsReady) {
            audioEngineIsReady = audioEngine.create()
        }

        audioEngine.loadAudioSource(contentResolver, uri) {
            audioEngine.play()
        }
    }

    private fun queryDeviceSongs(): List<SongItem> {
        val projection = arrayOf(
            MediaStore.Audio.Media._ID,
            MediaStore.Audio.Media.TITLE,
            MediaStore.Audio.Media.ARTIST,
            MediaStore.Audio.Media.ALBUM,
            MediaStore.Audio.Media.DURATION,
            MediaStore.Audio.Media.ALBUM_ID,
            MediaStore.Audio.Media.DATE_MODIFIED,
        )
        val selection = "${MediaStore.Audio.Media.IS_MUSIC} != 0"
        val sortOrder = "${MediaStore.Audio.Media.TITLE} ASC"
        val result = mutableListOf<SongItem>()

        contentResolver.query(
            MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
            projection,
            selection,
            null,
            sortOrder,
        )?.use { cursor ->
            val idIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
            val titleIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
            val artistIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
            val albumIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM)
            val durationIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
            val albumIdIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM_ID)
            val dateModifiedIndex =
                cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_MODIFIED)

            while (cursor.moveToNext()) {
                val id = cursor.getLong(idIndex)
                val title: String =
                    cursor.getString(titleIndex) ?: getString(R.string.unknown_title)
                val artistName: String =
                    cursor.getString(artistIndex) ?: getString(R.string.unknown_artist)
                val album: String =
                    cursor.getString(albumIndex) ?: getString(R.string.unknown_album)
                val durationMs: Long = cursor.getLong(durationIndex)
                val albumId = cursor.getLong(albumIdIndex)
                val dateModified = cursor.getLong(dateModifiedIndex)
                val uri =
                    ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id)

                result.add(
                    SongItem(
                        id,
                        title,
                        artistName,
                        album,
                        durationMs,
                        uri,
                        albumId,
                        dateModified,
                    )
                )
            }
        }

        return result
    }

    private fun prefetchPlaylistMetadata(items: List<SongItem>) {
        if (items.isEmpty()) return
        // Keep prefetch bounded to avoid spending too much work on very large libraries.
        val requests = items.take(160).map { song ->
            ThumbnailRepository.SongMetadataRequest(
                songId = song.id,
                audioUri = song.uri,
                albumId = song.albumId,
                dateModified = song.dateModified,
                fallbackTitle = song.title,
                fallbackArtist = song.artist,
                fallbackAlbum = song.album,
                fallbackDurationMs = song.durationMs,
            )
        }
        lifecycleScope.launch(Dispatchers.IO) {
            thumbnailRepository.prefetchMetadata(requests)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        audioEngine.release()
        thumbnailRepository.close()
    }

    @Composable
    private fun AudioEngineScreen(
        modifier: Modifier = Modifier,
        audioEngine: AudioEngine,
        selectedFilterType: AudioEngine.FilterType,
        hasAudioPermission: Boolean,
        songs: List<SongItem>,
        selectedSongId: Long?,
        onFilterTypeChanged: (AudioEngine.FilterType) -> Unit,
        onRequestPermission: () -> Unit,
        onSongSelected: (SongItem) -> Unit,
    ) {
        var detailSong by remember { mutableStateOf<SongItem?>(null) }
        fun clearDetailSong() {
            detailSong = null
        }

        Column(
            modifier = modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            FilterTypeDropdown(
                selectedFilterType = selectedFilterType,
                onFilterTypeChanged = onFilterTypeChanged,
            )

            if (audioEngineIsReady) {
                AndroidView(
                    factory = { context ->
                        VisualizerGLSurfaceView(context).apply {
                            setAudioEngine(audioEngine)
                        }
                    }, modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16f / 9)
                )
            }

            Text(
                text = stringResource(R.string.playlist),
                style = MaterialTheme.typography.titleMedium,
            )

            if (!hasAudioPermission) {
                Text(text = stringResource(R.string.audio_permission_required))
                Button(onClick = onRequestPermission, modifier = Modifier.fillMaxWidth()) {
                    Text(text = stringResource(R.string.grant_audio_permission))
                }
            } else if (songs.isEmpty()) {
                Text(text = stringResource(R.string.playlist_empty))
            } else {
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize(),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(songs, key = { it.id }) { song ->
                        SongRow(
                            song = song,
                            selectedSongId = selectedSongId,
                            thumbnailRepository = thumbnailRepository,
                            onSongSelected = onSongSelected,
                            onShowDetails = { detailSong = song },
                        )
                    }
                }
            }
        }

        detailSong?.let { song ->
            SongDetailsBottomSheet(
                song = song,
                thumbnailRepository = thumbnailRepository,
                onDismissRequest = { clearDetailSong() },
            )
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun FilterTypeDropdown(
        selectedFilterType: AudioEngine.FilterType,
        onFilterTypeChanged: (AudioEngine.FilterType) -> Unit,
    ) {
        var expanded by remember { mutableStateOf(false) }
        fun onExpandedChange(isExpanded: Boolean) {
            expanded = isExpanded
        }

        ExposedDropdownMenuBox(
            expanded = expanded,
            onExpandedChange = { onExpandedChange(it) },
        ) {
            OutlinedTextField(
                modifier = Modifier
                    .menuAnchor(MenuAnchorType.PrimaryNotEditable)
                    .fillMaxWidth(),
                value = filterTypeLabel(selectedFilterType),
                onValueChange = {},
                readOnly = true,
                label = { Text(text = stringResource(R.string.apply_filter)) },
                trailingIcon = {
                    ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded)
                },
                colors = ExposedDropdownMenuDefaults.outlinedTextFieldColors(),
            )

            ExposedDropdownMenu(
                expanded = expanded,
                onDismissRequest = { expanded = false },
            ) {
                AudioEngine.FilterType.entries.forEach { filterType ->
                    DropdownMenuItem(
                        text = { Text(text = filterTypeLabel(filterType)) },
                        onClick = {
                            onFilterTypeChanged(filterType)
                            expanded = false
                        },
                    )
                }
            }
        }
    }

    @Composable
    private fun filterTypeLabel(filterType: AudioEngine.FilterType): String {
        return when (filterType) {
            AudioEngine.FilterType.NONE -> stringResource(R.string.filter_none)
            AudioEngine.FilterType.LOW_PASS -> stringResource(R.string.filter_low_pass)
            AudioEngine.FilterType.HIGH_PASS -> stringResource(R.string.filter_high_pass)
            AudioEngine.FilterType.ECHO -> stringResource(R.string.filter_echo)
            AudioEngine.FilterType.REVERB -> stringResource(R.string.filter_reverb)
            AudioEngine.FilterType.PAN -> stringResource(R.string.filter_pan)
        }
    }

    @Composable
    private fun SongRow(
        song: SongItem,
        selectedSongId: Long?,
        thumbnailRepository: ThumbnailRepository,
        onSongSelected: (SongItem) -> Unit,
        onShowDetails: () -> Unit,
    ) {
        val metadataRequest = metadataRequestFor(song)
        val metadata by produceState<ThumbnailRepository.SongMetadata?>(
            initialValue = null,
            key1 = song.id,
            key2 = song.dateModified
        ) {
            value = thumbnailRepository.loadMetadata(metadataRequest)
        }
        val isSelected = selectedSongId == song.id
        val titlePrefix = if (isSelected) "▶ " else ""
        val title = metadata?.title ?: song.title
        val artist = metadata?.artist ?: song.artist
        val album = metadata?.album ?: song.album
        val durationText = formatDuration(metadata?.durationMs ?: song.durationMs)
        val dismissState = rememberSwipeToDismissBoxState(
            confirmValueChange = { value ->
                if (value != SwipeToDismissBoxValue.Settled) {
                    onShowDetails()
                    false
                } else {
                    true
                }
            }
        )

        SwipeToDismissBox(
            state = dismissState,
            backgroundContent = {
            }
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onSongSelected(song) }
                    .padding(vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                ThumbnailCell(bitmap = metadata?.thumbnail)
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "$titlePrefix$title",
                        style = MaterialTheme.typography.bodyLarge,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = artist,
                        style = MaterialTheme.typography.bodySmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = "$album - $durationText",
                        style = MaterialTheme.typography.labelSmall,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun SongDetailsBottomSheet(
        song: SongItem,
        thumbnailRepository: ThumbnailRepository,
        onDismissRequest: () -> Unit,
    ) {
        val metadata by produceState<ThumbnailRepository.SongMetadata?>(
            initialValue = null,
            key1 = song.id,
            key2 = song.dateModified,
        ) {
            value = thumbnailRepository.loadMetadata(metadataRequestFor(song))
        }
        val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

        ModalBottomSheet(
            onDismissRequest = onDismissRequest,
            sheetState = sheetState,
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Text(
                    text = stringResource(R.string.song_details_title),
                    style = MaterialTheme.typography.titleMedium,
                )

                ThumbnailCell(bitmap = metadata?.thumbnail)

                MetadataRow(stringResource(R.string.meta_title), metadata?.title ?: song.title)
                MetadataRow(stringResource(R.string.meta_artist), metadata?.artist ?: song.artist)
                MetadataRow(stringResource(R.string.meta_album), metadata?.album ?: song.album)
                MetadataRow(
                    stringResource(R.string.meta_duration),
                    formatDuration(metadata?.durationMs ?: song.durationMs),
                )
                MetadataRow(
                    stringResource(R.string.meta_track),
                    metadata?.trackNumber?.toString().orDash(),
                )
                MetadataRow(
                    stringResource(R.string.meta_year),
                    metadata?.year?.toString().orDash(),
                )
                MetadataRow(stringResource(R.string.meta_genre), metadata?.genre.orDash())
                MetadataRow(
                    stringResource(R.string.meta_album_artist),
                    metadata?.albumArtist.orDash(),
                )
                MetadataRow(stringResource(R.string.meta_composer), metadata?.composer.orDash())
                MetadataRow(stringResource(R.string.meta_author), metadata?.author.orDash())
                MetadataRow(stringResource(R.string.meta_writer), metadata?.writer.orDash())
                MetadataRow(stringResource(R.string.meta_mime_type), metadata?.mimeType.orDash())
                MetadataRow(stringResource(R.string.meta_bitrate), metadata?.bitrate.orDash())
                MetadataRow(
                    stringResource(R.string.meta_sample_rate),
                    metadata?.sampleRate.orDash(),
                )
                MetadataRow(
                    stringResource(R.string.meta_bits_per_sample),
                    metadata?.bitsPerSample.orDash(),
                )
                MetadataRow(
                    stringResource(R.string.meta_channels),
                    metadata?.channelCount?.toString().orDash(),
                )

                Spacer(modifier = Modifier.height(8.dp))
            }
        }
    }

    @Composable
    private fun MetadataRow(label: String, value: String) {
        Text(
            text = "$label: $value",
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Normal,
        )
    }

    private fun metadataRequestFor(song: SongItem): ThumbnailRepository.SongMetadataRequest {
        return ThumbnailRepository.SongMetadataRequest(
            songId = song.id,
            audioUri = song.uri,
            albumId = song.albumId,
            dateModified = song.dateModified,
            fallbackTitle = song.title,
            fallbackArtist = song.artist,
            fallbackAlbum = song.album,
            fallbackDurationMs = song.durationMs,
        )
    }

    private fun formatDuration(durationMs: Long): String {
        if (durationMs <= 0L) return "--:--"
        val totalSeconds = durationMs / 1000L
        val hours = totalSeconds / 3600L
        val minutes = (totalSeconds % 3600L) / 60L
        val seconds = totalSeconds % 60L
        return if (hours > 0L) {
            String.format(Locale.US, "%d:%02d:%02d", hours, minutes, seconds)
        } else {
            String.format(Locale.US, "%02d:%02d", minutes, seconds)
        }
    }

    @Composable
    private fun ThumbnailCell(bitmap: Bitmap?) {
        if (bitmap != null) {
            Image(
                bitmap = bitmap.asImageBitmap(),
                contentDescription = stringResource(R.string.song_thumbnail),
                modifier = Modifier.size(56.dp),
            )
        } else {
            Box(
                modifier = Modifier.size(56.dp),
            ) {
                Text(
                    text = stringResource(R.string.thumbnail_placeholder),
                    style = MaterialTheme.typography.labelSmall,
                    color = Color.Gray,
                )
            }
        }
    }
}

private fun String?.orDash(): String {
    return if (this.isNullOrBlank()) "--" else this
}

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
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private data class SongItem(
        val id: Long,
        val title: String,
        val artist: String,
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
                prefetchPlaylistThumbnails(songs)
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
            prefetchPlaylistThumbnails(songs)
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
            val albumIdIndex = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM_ID)
            val dateModifiedIndex =
                cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_MODIFIED)

            while (cursor.moveToNext()) {
                val id = cursor.getLong(idIndex)
                val title = cursor.getString(titleIndex) ?: getString(R.string.unknown_title)
                val artist = cursor.getString(artistIndex) ?: getString(R.string.unknown_artist)
                val albumId = cursor.getLong(albumIdIndex)
                val dateModified = cursor.getLong(dateModifiedIndex)
                val uri =
                    ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id)

                result.add(
                    SongItem(
                        id = id,
                        title = title,
                        artist = artist,
                        uri = uri,
                        albumId = albumId,
                        dateModified = dateModified,
                    )
                )
            }
        }

        return result
    }

    private fun prefetchPlaylistThumbnails(items: List<SongItem>) {
        if (items.isEmpty()) return
        // Keep prefetch bounded to avoid spending too much work on very large libraries.
        val requests = items.take(160).map { song ->
            SongThumbRequest(
                songId = song.id,
                audioUri = song.uri,
                albumId = song.albumId,
                dateModified = song.dateModified,
            )
        }
        lifecycleScope.launch(Dispatchers.IO) {
            thumbnailRepository.prefetch(requests)
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
                        .aspectRatio(16 / 9f)
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
                        )
                    }
                }
            }
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
    ) {
        val thumbRequest = SongThumbRequest(
            songId = song.id,
            audioUri = song.uri,
            albumId = song.albumId,
            dateModified = song.dateModified,
        )
        val thumbnail by produceState<Bitmap?>(
            initialValue = null,
            key1 = song.id,
            key2 = song.dateModified
        ) {
            value = thumbnailRepository.load(thumbRequest)
        }
        val isSelected = selectedSongId == song.id
        val titlePrefix = if (isSelected) "▶ " else ""

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onSongSelected(song) }
                .padding(vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            ThumbnailCell(bitmap = thumbnail)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "$titlePrefix${song.title}",
                    style = MaterialTheme.typography.bodyLarge,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = song.artist,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
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
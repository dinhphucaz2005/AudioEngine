package com.example.audioengine

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : ComponentActivity() {

    private lateinit var audioEngine: AudioEngine
    private var isPlayEnabled by mutableStateOf(false)
    private var isFilterEnabled by mutableStateOf(false)
    private var audioEngineIsReady by mutableStateOf(false)

    private val pickAudioLauncher =
        registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
            uri?.let {
                isPlayEnabled = false
                audioEngineIsReady = audioEngine.create()

                audioEngine.loadAudioSource(contentResolver, it) { success ->
                    isPlayEnabled = success
                }
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        audioEngine = AudioEngine()

        setContent {
            MaterialTheme {
                AudioEngineScreen(
                    audioEngine = audioEngine,
                    isPlayEnabled = isPlayEnabled,
                    isFilterEnabled = isFilterEnabled,
                    onSelectAudio = { pickAudioLauncher.launch("audio/*") },
                    onPlay = { audioEngine.play() },
                    onPause = { audioEngine.pause() },
                    onFilterChanged = { enabled ->
                        isFilterEnabled = enabled
                        audioEngine.setFilterEnabled(enabled)
                    },
                )
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

        if (ContextCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(perm), 1002)
        }
    }


    override fun onDestroy() {
        super.onDestroy()
        audioEngine.release()
    }

    @Composable
    private fun AudioEngineScreen(
        audioEngine: AudioEngine,
        isPlayEnabled: Boolean,
        isFilterEnabled: Boolean,
        onSelectAudio: () -> Unit,
        onPlay: () -> Unit,
        onPause: () -> Unit,
        onFilterChanged: (Boolean) -> Unit,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Button(onClick = onSelectAudio, modifier = Modifier.fillMaxWidth()) {
                Text(text = stringResource(R.string.select_audio))
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Button(onClick = onPlay, enabled = isPlayEnabled, modifier = Modifier.weight(1f)) {
                    Text(text = stringResource(R.string.play))
                }
                Button(onClick = onPause, modifier = Modifier.weight(1f)) {
                    Text(text = stringResource(R.string.pause))
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(text = stringResource(R.string.apply_filter))
                Switch(checked = isFilterEnabled, onCheckedChange = onFilterChanged)
            }
            if (audioEngineIsReady) {
                AndroidView(
                    factory = { context ->
                        VisualizerGLSurfaceView(context).apply {
                            setAudioEngine(audioEngine)
                        }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .aspectRatio(16 / 9f)
                )
            }
        }
    }
}
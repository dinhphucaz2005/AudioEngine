package com.example.audioengine

import android.content.ContentResolver
import android.net.Uri
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class AudioEngine {

    private val engineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    init {
        System.loadLibrary("audioengine")
    }

    private var nativeHandle: Long = 0L

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(audioEngineHandle: Long = nativeHandle)
    private external fun nativeSetAudioSource(
        fd: Int,
        offset: Long,
        length: Long,
        audioEngineHandle: Long = nativeHandle,
    ): Boolean

    private external fun nativePlay(nativeHandle: Long = this.nativeHandle)
    private external fun nativePause(nativeHandle: Long = this.nativeHandle)

    private external fun nativeVisualizerOnSurfaceCreated(nativeHandle: Long = this.nativeHandle)
    private external fun nativeVisualizerOnSurfaceChanged(
        width: Int,
        height: Int,
        nativeHandle: Long = this.nativeHandle,
    )

    private external fun nativeVisualizerOnDrawFrame(nativeHandle: Long = this.nativeHandle)

    private external fun nativeVisualizerSetTouch(
        xNorm: Float,
        yNorm: Float,
        isDownInt: Int,
        pressure: Float,
        radiusNorm: Float,
        nativeHandle: Long,
    )

    private external fun nativeSetFilterType(
        filterType: Int,
        nativeHandle: Long = this.nativeHandle,
    )

    fun loadAudioSource(
        contentResolver: ContentResolver,
        uri: Uri,
        onComplete: (Boolean) -> Unit,
    ) {
        engineScope.launch {
            val success = loadAudioSourceInternal(contentResolver, uri)
            withContext(Dispatchers.Main.immediate) {
                onComplete(success)
            }
        }
    }

    fun release() {
        engineScope.cancel()
        safeNativeCall { nativeDestroy() }
        nativeHandle = 0L
    }

    private fun loadAudioSourceInternal(contentResolver: ContentResolver, uri: Uri): Boolean {
        contentResolver.openAssetFileDescriptor(uri, "r")?.use { afd ->
            val resolvedLength = when {
                afd.length >= 0L -> afd.length
                afd.declaredLength >= 0L -> afd.declaredLength
                afd.parcelFileDescriptor.statSize >= 0L -> afd.parcelFileDescriptor.statSize - afd.startOffset
                else -> -1L
            }

            if (resolvedLength > 0L) {
                return safeNativeCall {
                    nativeSetAudioSource(
                        fd = afd.parcelFileDescriptor.fd,
                        offset = afd.startOffset,
                        length = resolvedLength
                    )
                } == true
            }
        }

        contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
            if (pfd.statSize > 0L) {
                return safeNativeCall {
                    nativeSetAudioSource(
                        fd = pfd.fd,
                        offset = 0L,
                        length = pfd.statSize
                    )
                } == true
            }
        }

        return false
    }

    private inline fun <T> safeNativeCall(action: () -> T): T? {
        return try {
            if (nativeHandle == 0L) {
                Log.w("AudioEngine", "Native handle is not initialized.")
                null
            } else {
                action()
            }
        } catch (e: Exception) {
            Log.e("AudioEngine", "Error during native call: ${e.message}", e)
            null
        }
    }

    fun play() {
        safeNativeCall { nativePlay() }
    }

    fun pause() {
        safeNativeCall { nativePause() }
    }

    fun isCreated(): Boolean = nativeHandle != 0L

    fun create(): Boolean {
        if (isCreated()) {
            Log.w("AudioEngine", "AudioEngine is already created.")
            return true
        }
        val result = nativeCreate()
        if (result != 0L) {
            nativeHandle = result
            Log.i("AudioEngine", "AudioEngine created successfully with handle: $nativeHandle")
            return true
        } else {
            Log.e("AudioEngine", "Failed to create AudioEngine.")
            return false
        }
    }

    fun visualizerOnSurfaceCreated() {
        safeNativeCall { nativeVisualizerOnSurfaceCreated() }
    }

    fun visualizerOnSurfaceChanged(width: Int, height: Int) {
        safeNativeCall { nativeVisualizerOnSurfaceChanged(width, height) }
    }


    fun visualizerOnDrawFrame() {
        safeNativeCall { nativeVisualizerOnDrawFrame() }
    }

    fun visualizerSetTouch(
        xNorm: Float,
        yNorm: Float,
        isDown: Boolean,
        pressure: Float,
        radiusNorm: Float,
    ) {
        safeNativeCall {
            nativeVisualizerSetTouch(
                xNorm = xNorm,
                yNorm = yNorm,
                isDownInt = if (isDown) 1 else 0,
                pressure = pressure,
                radiusNorm = radiusNorm,
                nativeHandle = nativeHandle,
            )
        }
    }

    enum class FilterType(val value: Int) {
        NONE(0),
        LOW_PASS(1),
        HIGH_PASS(2),
        ECHO(3),
        REVERB(4),
        PAN(5),
    }

    private var currentFilterType: FilterType = FilterType.NONE

    fun setFilterType(filterType: FilterType) {
        if (currentFilterType == filterType) {
            Log.d(
                "AudioEngine",
                "Filter type is already set to ${filterType.name}. No change needed."
            )
            return
        }
        val result = safeNativeCall { nativeSetFilterType(filterType.value) }
        if (result != null) {
            currentFilterType = filterType
        }
    }
}

package com.example.audioengine

import android.content.Context
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import android.util.AttributeSet
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class VisualizerGLSurfaceView(context: Context, attrs: AttributeSet? = null) :
    GLSurfaceView(context, attrs) {

    private var renderer: VisualizerRenderer
    private var audioEngine: AudioEngine? = null

    init {
        setEGLContextClientVersion(2)
        renderer = VisualizerRenderer()
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun setAudioEngine(engine: AudioEngine) {
        audioEngine = engine
        renderer.setAudioEngine(engine)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val widthPx = width.toFloat().coerceAtLeast(1f)
        val heightPx = height.toFloat().coerceAtLeast(1f)
        val xNorm = (event.x / widthPx).coerceIn(0f, 1f)
        val yNorm = (event.y / heightPx).coerceIn(0f, 1f)
        val pressure = event.pressure.coerceIn(0f, 1f)
        val radiusNorm = event.size.coerceIn(0.03f, 0.5f)
        val isDown = when (event.actionMasked) {
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL,
            MotionEvent.ACTION_POINTER_UP,
                -> false

            else -> true
        }

        audioEngine?.visualizerSetTouch(
            xNorm = xNorm,
            yNorm = yNorm,
            isDown = isDown,
            pressure = pressure,
            radiusNorm = radiusNorm,
        )
        if (event.actionMasked == MotionEvent.ACTION_UP) {
            performClick()
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private class VisualizerRenderer : Renderer {

        private var audioEngine: AudioEngine? = null

        fun setAudioEngine(engine: AudioEngine) {
            audioEngine = engine
        }

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            audioEngine?.visualizerOnSurfaceCreated()
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            audioEngine?.visualizerOnSurfaceChanged(width, height)
        }

        override fun onDrawFrame(gl: GL10?) {
            audioEngine?.visualizerOnDrawFrame()
        }
    }
}

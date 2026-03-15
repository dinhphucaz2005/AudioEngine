package com.example.audioengine

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class VisualizerGLSurfaceView(context: Context, attrs: AttributeSet? = null) :
    GLSurfaceView(context, attrs) {

    private var renderer: VisualizerRenderer

    init {
        setEGLContextClientVersion(2)
        renderer = VisualizerRenderer()
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun setAudioEngine(engine: AudioEngine) {
        renderer.setAudioEngine(engine)
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

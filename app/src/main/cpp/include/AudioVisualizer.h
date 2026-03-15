#pragma once

#include <complex>
#include <atomic>
#include <vector>
#include <GLES3/gl3.h>
#include "Util.h"


struct VisualizerPlain {
    size_t sample_rate = 0;
    float in_raw[FFT_SIZE]{};
    float in_win[FFT_SIZE]{};
    Float_Complex out_raw[FFT_SIZE];
    float out_log[FFT_SIZE]{};
    float out_smooth[FFT_SIZE]{};
    float out_smear[FFT_SIZE]{};
};

struct VisualizerVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

struct CircleVertex {
    float x;
    float y;
    float size;
    float r;
    float g;
    float b;
    float a;
};


class AudioVisualizer {
public:
    explicit AudioVisualizer();

    void visualizerOnSurfaceCreated();

    void visualizerOnSurfaceChanged(int32_t width, int32_t height);

    void visualizerOnDrawFrame();

    void pushAudioFrame(float frame);

    void setTouchState(float xNorm, float yNorm, bool isDown, float pressure, float radiusNorm);

    void clean();

    void setSampleRate(size_t sample_rate);

private:

    size_t fft_analyze(float dt);

    static GLuint compileShader(GLenum type, const char *source);

    static GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);

    void releaseVisualizerGl();

    VisualizerPlain visualizerPlain{};

    GLuint mVisualizerProgram = 0;
    GLuint mCircleProgram = 0;
    GLuint mVisualizerVbo = 0;
    GLuint mCircleVbo = 0;
    GLint mPositionHandle = -1;
    GLint mColorHandle = -1;
    GLint mCirclePositionHandle = -1;
    GLint mCircleSizeHandle = -1;
    GLint mCircleColorHandle = -1;
    GLint mCircleRadiusHandle = -1;
    GLint mCirclePowerHandle = -1;
    GLint mShockwaveCenterHandle = -1;
    GLint mShockwaveParamsHandle = -1;
    GLint mShockwaveScaleHandle = -1;
    GLint mCircleShockwaveCenterHandle = -1;
    GLint mCircleShockwaveParamsHandle = -1;
    GLint mCircleShockwaveScaleHandle = -1;
    int32_t mSurfaceWidth = 0;
    int32_t mSurfaceHeight = 0;
    std::chrono::steady_clock::time_point mLastRenderTime{};
    std::vector<VisualizerVertex> mBarVertices;
    std::vector<CircleVertex> mSmearCircleVertices;
    std::vector<CircleVertex> mCircleVertices;

    std::atomic<float> mTouchXNorm{0.5f};
    std::atomic<float> mTouchYNorm{0.5f};
    std::atomic<float> mTouchPressure{0.0f};
    std::atomic<float> mTouchRadiusNorm{0.1f};
    std::atomic<bool> mTouchDown{false};
    float mTouchEnvelope = 0.0f;
    float mShockwaveRadius = 0.0f;
    float mShockwaveEnergy = 0.0f;
    bool mWasTouchDown = false;
};
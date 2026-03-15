#include "AudioVisualizer.h"
#include "Util.h"

namespace {

    constexpr size_t
            kBars = 64;
    constexpr float kVisibleHeight = 4.0f / 3.0f; // Match 2/3 of viewport in NDC [-1, 1].
    constexpr float kTouchAttackHz = 18.0f;
    constexpr float kTouchReleaseHz = 6.0f;
    constexpr float kShockwaveBaseStrength = 0.07f;
    constexpr float kShockwavePressureStrength = 0.22f;
    constexpr float kShockwaveSpeed = 1.35f;
    constexpr float kShockwaveBandWidth = 0.20f;
    constexpr float kShockwaveDecayHz = 1.7f;

    void hsvToRgb(float h, float s, float v, float &r, float &g, float &b) {
        const float hh = h - floorf(h);
        const float scaled = hh * 6.0f;
        const int sector = static_cast<int>(scaled);
        const float f = scaled - static_cast<float>(sector);
        const float p = v * (1.0f - s);
        const float q = v * (1.0f - s * f);
        const float t = v * (1.0f - s * (1.0f - f));

        switch (sector % 6) {
            case 0:
                r = v;
                g = t;
                b = p;
                break;
            case 1:
                r = q;
                g = v;
                b = p;
                break;
            case 2:
                r = p;
                g = v;
                b = t;
                break;
            case 3:
                r = p;
                g = q;
                b = v;
                break;
            case 4:
                r = t;
                g = p;
                b = v;
                break;
            default:
                r = v;
                g = p;
                b = q;
                break;
        }
    }

    void appendQuad(std::vector<VisualizerVertex> &dst,
            float x0,
            float x1,
            float y0,
            float y1,
            float r,
            float g,
            float b,
            float a) {
        dst.insert(dst.end(), {
                {x0, y0, r, g, b, a},
                {x1, y0, r, g, b, a},
                {x0, y1, r, g, b, a},
                {x0, y1, r, g, b, a},
                {x1, y0, r, g, b, a},
                {x1, y1, r, g, b, a},
        });
    }

} // namespace

AudioVisualizer::AudioVisualizer() {
    mBarVertices.reserve(kBars * 6);
    mSmearCircleVertices.reserve(kBars * 8);
    mCircleVertices.reserve(kBars);
}

void AudioVisualizer::visualizerOnSurfaceCreated() {
    releaseVisualizerGl();

    static constexpr const char *kVertexShaderSrc =
            "attribute vec2 aPosition;\n"
            "attribute vec4 aColor;\n"
            "uniform vec2 uShockwaveCenter;\n"
            "uniform vec3 uShockwaveParams;\n"
            "uniform float uShockwaveScale;\n"
            "varying vec4 vColor;\n"
            "vec2 applyShockwave(vec2 p) {\n"
            "  float radius = uShockwaveParams.x;\n"
            "  float energy = uShockwaveParams.y;\n"
            "  float width = max(0.0001, uShockwaveParams.z);\n"
            "  if (energy <= 0.00001) return p;\n"
            "  vec2 d = p - uShockwaveCenter;\n"
            "  float dist = max(0.0001, length(d));\n"
            "  float ringDelta = dist - radius;\n"
            "  float front = exp(-(ringDelta * ringDelta) / (2.0 * width * width));\n"
            "  float tailDelta = ringDelta + width * 1.7;\n"
            "  float tailWidth = width * 1.8;\n"
            "  float tail = exp(-(tailDelta * tailDelta) / (2.0 * tailWidth * tailWidth));\n"
            "  float pulse = front - 0.55 * tail;\n"
            "  float push = pulse * energy * uShockwaveScale;\n"
            "  return p + (d / dist) * push;\n"
            "}\n"
            "void main() {\n"
            "  vec2 displaced = applyShockwave(aPosition);\n"
            "  gl_Position = vec4(displaced, 0.0, 1.0);\n"
            "  vColor = aColor;\n"
            "}\n";

    static constexpr const char *kFragmentShaderSrc =
            "precision mediump float;\n"
            "varying vec4 vColor;\n"
            "void main() {\n"
            "  gl_FragColor = vColor;\n"
            "}\n";

    static constexpr const char *kCircleVertexShaderSrc =
            "attribute vec2 aPosition;\n"
            "attribute float aPointSize;\n"
            "attribute vec4 aColor;\n"
            "uniform vec2 uShockwaveCenter;\n"
            "uniform vec3 uShockwaveParams;\n"
            "uniform float uShockwaveScale;\n"
            "varying vec4 vColor;\n"
            "vec2 applyShockwave(vec2 p) {\n"
            "  float radius = uShockwaveParams.x;\n"
            "  float energy = uShockwaveParams.y;\n"
            "  float width = max(0.0001, uShockwaveParams.z);\n"
            "  if (energy <= 0.00001) return p;\n"
            "  vec2 d = p - uShockwaveCenter;\n"
            "  float dist = max(0.0001, length(d));\n"
            "  float ringDelta = dist - radius;\n"
            "  float front = exp(-(ringDelta * ringDelta) / (2.0 * width * width));\n"
            "  float tailDelta = ringDelta + width * 1.7;\n"
            "  float tailWidth = width * 1.8;\n"
            "  float tail = exp(-(tailDelta * tailDelta) / (2.0 * tailWidth * tailWidth));\n"
            "  float pulse = front - 0.55 * tail;\n"
            "  float push = pulse * energy * uShockwaveScale;\n"
            "  return p + (d / dist) * push;\n"
            "}\n"
            "void main() {\n"
            "  vec2 displaced = applyShockwave(aPosition);\n"
            "  gl_Position = vec4(displaced, 0.0, 1.0);\n"
            "  gl_PointSize = aPointSize;\n"
            "  vColor = aColor;\n"
            "}\n";

    static constexpr const char *kCircleFragmentShaderSrc =
            "precision mediump float;\n"
            "varying vec4 vColor;\n"
            "uniform float uRadius;\n"
            "uniform float uPower;\n"
            "void main() {\n"
            "  vec2 p = gl_PointCoord - vec2(0.5);\n"
            "  float d = length(p);\n"
            "  if (d > 0.5) discard;\n"
            "  float s = d - uRadius;\n"
            "  if (s <= 0.0) {\n"
            "    gl_FragColor = vColor * 1.5;\n"
            "  } else {\n"
            "    float denom = max(0.0001, 0.5 - uRadius);\n"
            "    float t = clamp(1.0 - s / denom, 0.0, 1.0);\n"
            "    gl_FragColor = mix(vec4(vColor.xyz, 0.0), vColor * 1.5, pow(t, uPower));\n"
            "  }\n"
            "}\n";

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return;
    }

    mVisualizerProgram = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (mVisualizerProgram == 0) {
        return;
    }

    const GLuint circleVertexShader = compileShader(GL_VERTEX_SHADER, kCircleVertexShaderSrc);
    const GLuint circleFragmentShader = compileShader(GL_FRAGMENT_SHADER, kCircleFragmentShaderSrc);
    if (circleVertexShader == 0 || circleFragmentShader == 0) {
        if (circleVertexShader != 0) glDeleteShader(circleVertexShader);
        if (circleFragmentShader != 0) glDeleteShader(circleFragmentShader);
        releaseVisualizerGl();
        return;
    }

    mCircleProgram = linkProgram(circleVertexShader, circleFragmentShader);
    glDeleteShader(circleVertexShader);
    glDeleteShader(circleFragmentShader);

    if (mCircleProgram == 0) {
        releaseVisualizerGl();
        return;
    }

    mPositionHandle = glGetAttribLocation(mVisualizerProgram, "aPosition");
    mColorHandle = glGetAttribLocation(mVisualizerProgram, "aColor");
    mCirclePositionHandle = glGetAttribLocation(mCircleProgram, "aPosition");
    mCircleSizeHandle = glGetAttribLocation(mCircleProgram, "aPointSize");
    mCircleColorHandle = glGetAttribLocation(mCircleProgram, "aColor");
    mCircleRadiusHandle = glGetUniformLocation(mCircleProgram, "uRadius");
    mCirclePowerHandle = glGetUniformLocation(mCircleProgram, "uPower");
    mShockwaveCenterHandle = glGetUniformLocation(mVisualizerProgram, "uShockwaveCenter");
    mShockwaveParamsHandle = glGetUniformLocation(mVisualizerProgram, "uShockwaveParams");
    mShockwaveScaleHandle = glGetUniformLocation(mVisualizerProgram, "uShockwaveScale");
    mCircleShockwaveCenterHandle = glGetUniformLocation(mCircleProgram, "uShockwaveCenter");
    mCircleShockwaveParamsHandle = glGetUniformLocation(mCircleProgram, "uShockwaveParams");
    mCircleShockwaveScaleHandle = glGetUniformLocation(mCircleProgram, "uShockwaveScale");

    glGenBuffers(1, &mVisualizerVbo);
    glGenBuffers(1, &mCircleVbo);
    clean();
    mLastRenderTime = std::chrono::steady_clock::time_point{};
}

void AudioVisualizer::visualizerOnSurfaceChanged(int32_t width, int32_t height) {
    mSurfaceWidth = width;
    mSurfaceHeight = height;
    glViewport(0, 0, width, height);
}

void AudioVisualizer::visualizerOnDrawFrame() {
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (mVisualizerProgram == 0 || mCircleProgram == 0 || mVisualizerVbo == 0 || mCircleVbo == 0 ||
            mPositionHandle < 0 || mColorHandle < 0 || mCirclePositionHandle < 0 ||
            mCircleSizeHandle < 0 || mCircleColorHandle < 0 || mCircleRadiusHandle < 0 ||
            mCirclePowerHandle < 0 || mShockwaveCenterHandle < 0 || mShockwaveParamsHandle < 0 ||
            mShockwaveScaleHandle < 0 || mCircleShockwaveCenterHandle < 0 ||
            mCircleShockwaveParamsHandle < 0 || mCircleShockwaveScaleHandle < 0 ||
            mSurfaceWidth <= 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float dt = 1.0f / 60.0f;
    if (mLastRenderTime.time_since_epoch().count() != 0) {
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - mLastRenderTime).count();
        dt = std::clamp(static_cast<float>(elapsedNs) / 1'000'000'000.0f, 1.0f / 240.0f, 0.1f);
    }
    mLastRenderTime = now;

    const float touchX = std::clamp(mTouchXNorm.load(std::memory_order_relaxed), 0.0f, 1.0f) * 2.0f - 1.0f;
    const float touchY = 1.0f - std::clamp(mTouchYNorm.load(std::memory_order_relaxed), 0.0f, 1.0f) * 2.0f;
    const float touchPressure = std::clamp(mTouchPressure.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float touchRadius = std::clamp(mTouchRadiusNorm.load(std::memory_order_relaxed), 0.03f, 0.5f) * 1.8f;
    const float touchTarget = mTouchDown.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float responseHz = (touchTarget > mTouchEnvelope) ? kTouchAttackHz : kTouchReleaseHz;
    const float response = 1.0f - expf(-responseHz * dt);
    mTouchEnvelope += (touchTarget - mTouchEnvelope) * response;
    const bool touchDown = mTouchDown.load(std::memory_order_relaxed);
    const float touchStrength =
            mTouchEnvelope * (kShockwaveBaseStrength + kShockwavePressureStrength * touchPressure);

    if (touchDown && !mWasTouchDown) {
        mShockwaveRadius = 0.0f;
        mShockwaveEnergy = touchStrength;
    }
    mShockwaveRadius += kShockwaveSpeed * dt;
    mShockwaveEnergy *= expf(-kShockwaveDecayHz * dt);
    mWasTouchDown = touchDown;
    const float shockwaveWidth = std::max(0.04f, touchRadius * kShockwaveBandWidth);

    const size_t fftBins = fft_analyze(dt);
    if (fftBins == 0) {
        return;
    }

    const float cellWidth = 2.0f / static_cast<float>(kBars);
    mBarVertices.clear();
    mSmearCircleVertices.clear();
    mCircleVertices.clear();

    for (size_t i = 0; i < kBars; ++i) {
        const size_t sourceIndex = std::min(fftBins - 1, (i * fftBins) / kBars);
        const float smooth = std::clamp(visualizerPlain.out_smooth[sourceIndex], 0.0f, 1.0f);
        const float smear = std::clamp(visualizerPlain.out_smear[sourceIndex], 0.0f, 1.0f);

        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        hsvToRgb(static_cast<float>(i) / static_cast<float>(kBars), 0.75f, 1.0f, r, g, b);

        const float xCenter = -1.0f + (static_cast<float>(i) + 0.5f) * cellWidth;
        const float barHalfWidth = std::max(cellWidth * 0.08f, cellWidth / 6.0f * sqrtf(smooth));
        const float yBase = -1.0f;
        const float ySmooth = yBase + smooth * kVisibleHeight;
        const float ySmear = yBase + smear * kVisibleHeight;

        float barX0 = xCenter - barHalfWidth;
        float barX1 = xCenter + barHalfWidth;
        float barY0 = yBase;
        float barY1 = ySmooth;
        appendQuad(mBarVertices, barX0, barX1, barY0, barY1, r, g, b, 1.0f);

        // Build a vertical trail from smooth -> smear using circles that shrink and fade.
        const float dy = ySmear - ySmooth;
        const float distance = fabsf(dy);
        const int trailSteps = std::clamp(static_cast<int>(distance * 28.0f), 3, 8);
        for (int step = 0; step < trailSteps; ++step) {
            const float u = static_cast<float>(step + 1) / static_cast<float>(trailSteps + 1);
            const float trailY = ySmooth + dy * u;
            const float falloff = 1.0f - u;
            const float trailRadiusNdc = std::max(
                    cellWidth * 0.08f,
                    cellWidth * 2.2f * sqrtf(std::max(smooth, 1.0e-4f)) * (0.45f + 0.55f * falloff));
            const float trailPointSizePx = std::clamp(
                    trailRadiusNdc * static_cast<float>(mSurfaceWidth),
                    1.0f,
                    256.0f);
            const float trailAlpha = 0.18f + 0.30f * falloff;
            mSmearCircleVertices.push_back({xCenter, trailY, trailPointSizePx, r, g, b, trailAlpha});
        }

        const float circleRadiusNdc = std::max(cellWidth * 0.2f, cellWidth * 3.0f * sqrtf(smooth));
        const float pointSizePx = std::clamp(circleRadiusNdc * static_cast<float>(mSurfaceWidth), 1.0f, 256.0f);
        mCircleVertices.push_back({xCenter, ySmooth, pointSizePx, r, g, b, 0.9f});
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(mVisualizerProgram);
    glUniform2f(mShockwaveCenterHandle, touchX, touchY);
    glUniform3f(mShockwaveParamsHandle, mShockwaveRadius, mShockwaveEnergy, shockwaveWidth);
    glUniform1f(mShockwaveScaleHandle, 1.35f);
    glBindBuffer(GL_ARRAY_BUFFER, mVisualizerVbo);

    const auto barStride = static_cast<GLsizei>(sizeof(VisualizerVertex));
    glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mBarVertices.size() * sizeof(VisualizerVertex)),
            mBarVertices.data(),
            GL_STREAM_DRAW);
    glEnableVertexAttribArray(static_cast<GLuint>(mPositionHandle));
    glEnableVertexAttribArray(static_cast<GLuint>(mColorHandle));
    glVertexAttribPointer(static_cast<GLuint>(mPositionHandle), 2, GL_FLOAT, GL_FALSE, barStride, nullptr);
    glVertexAttribPointer(
            static_cast<GLuint>(mColorHandle),
            4,
            GL_FLOAT,
            GL_FALSE,
            barStride,
            reinterpret_cast<void *>(offsetof(VisualizerVertex, r)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mBarVertices.size()));

    glDisableVertexAttribArray(static_cast<GLuint>(mPositionHandle));
    glDisableVertexAttribArray(static_cast<GLuint>(mColorHandle));

    glUseProgram(mCircleProgram);
    glUniform2f(mCircleShockwaveCenterHandle, touchX, touchY);
    glUniform3f(mCircleShockwaveParamsHandle, mShockwaveRadius, mShockwaveEnergy, shockwaveWidth);
    glUniform1f(mCircleShockwaveScaleHandle, 1.55f);
    glBindBuffer(GL_ARRAY_BUFFER, mCircleVbo);

    const auto circleStride = static_cast<GLsizei>(sizeof(CircleVertex));
    glEnableVertexAttribArray(static_cast<GLuint>(mCirclePositionHandle));
    glEnableVertexAttribArray(static_cast<GLuint>(mCircleSizeHandle));
    glEnableVertexAttribArray(static_cast<GLuint>(mCircleColorHandle));
    glVertexAttribPointer(static_cast<GLuint>(mCirclePositionHandle), 2, GL_FLOAT, GL_FALSE, circleStride, nullptr);
    glVertexAttribPointer(
            static_cast<GLuint>(mCircleSizeHandle),
            1,
            GL_FLOAT,
            GL_FALSE,
            circleStride,
            reinterpret_cast<void *>(offsetof(CircleVertex, size)));
    glVertexAttribPointer(
            static_cast<GLuint>(mCircleColorHandle),
            4,
            GL_FLOAT,
            GL_FALSE,
            circleStride,
            reinterpret_cast<void *>(offsetof(CircleVertex, r)));

    glUniform1f(mCircleRadiusHandle, 0.30f);
    glUniform1f(mCirclePowerHandle, 3.0f);
    glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mSmearCircleVertices.size() * sizeof(CircleVertex)),
            mSmearCircleVertices.data(),
            GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mSmearCircleVertices.size()));

    glUniform1f(mCircleRadiusHandle, 0.07f);
    glUniform1f(mCirclePowerHandle, 5.0f);
    glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mCircleVertices.size() * sizeof(CircleVertex)),
            mCircleVertices.data(),
            GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(mCircleVertices.size()));
    glDisableVertexAttribArray(static_cast<GLuint>(mCirclePositionHandle));
    glDisableVertexAttribArray(static_cast<GLuint>(mCircleSizeHandle));
    glDisableVertexAttribArray(static_cast<GLuint>(mCircleColorHandle));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
}


size_t AudioVisualizer::fft_analyze(float dt) {
    if (visualizerPlain.sample_rate == 0) {
        return 0;
    }

    for (size_t i = 0; i < FFT_SIZE; ++i) {
        const float t = (float) i / (FFT_SIZE - 1);
        const float hann = 0.5f - 0.5f * cosf(2 * PI * t);
        visualizerPlain.in_win[i] = visualizerPlain.in_raw[i] * hann;
    }

    // FFT
    fft(visualizerPlain.in_win, visualizerPlain.out_raw, FFT_SIZE);

    // "Squash" into the Logarithmic Scale
    float step = 1.06;
    float lowf = 1.0f;
    size_t m = 0;
    float max_amp = 1.0f;
    float f = lowf;
    const size_t cutoff_bin = std::min(
            FFT_SIZE / 2,
            static_cast<size_t>(16000.0f * FFT_SIZE / static_cast<float>(visualizerPlain.sample_rate)));
    while ((size_t) f < cutoff_bin && m < FFT_SIZE) {
        float f1 = ceilf(f * step);
        float a = 0.0f;
        for (auto q = (size_t) f; q < cutoff_bin && q < (size_t) f1;
             ++q) {
            float b = amp(visualizerPlain.out_raw[q]);
            if (b > a) a = b;
        }
        if (max_amp < a) max_amp = a;
        visualizerPlain.out_log[m++] = a;
        f = f1;
    }
    for (size_t i = 0; i < m; ++i) {
        visualizerPlain.out_log[i] /= max_amp;
    }

    // Smooth out and smear the values
    for (size_t i = 0; i < m; ++i) {
        float smoothness = 8;
        visualizerPlain.out_smooth[i] += (visualizerPlain.out_log[i] - visualizerPlain.out_smooth[i]) * smoothness * dt;
        float smearness = 3;
        visualizerPlain.out_smear[i] += (visualizerPlain.out_smooth[i] - visualizerPlain.out_smear[i]) * smearness * dt;
    }

    return m;
}

GLuint AudioVisualizer::compileShader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        char infoLog[512] = {};
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        LOGE("Shader compile failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint AudioVisualizer::linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    const GLuint program = glCreateProgram();
    if (program == 0) {
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char infoLog[512] = {};
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        LOGE("Program link failed: %s", infoLog);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

void AudioVisualizer::releaseVisualizerGl() {
    if (mVisualizerVbo != 0) {
        glDeleteBuffers(1, &mVisualizerVbo);
        mVisualizerVbo = 0;
    }
    if (mCircleVbo != 0) {
        glDeleteBuffers(1, &mCircleVbo);
        mCircleVbo = 0;
    }
    if (mVisualizerProgram != 0) {
        glDeleteProgram(mVisualizerProgram);
        mVisualizerProgram = 0;
    }
    if (mCircleProgram != 0) {
        glDeleteProgram(mCircleProgram);
        mCircleProgram = 0;
    }
    mPositionHandle = -1;
    mColorHandle = -1;
    mCirclePositionHandle = -1;
    mCircleSizeHandle = -1;
    mCircleColorHandle = -1;
    mCircleRadiusHandle = -1;
    mCirclePowerHandle = -1;
    mShockwaveCenterHandle = -1;
    mShockwaveParamsHandle = -1;
    mShockwaveScaleHandle = -1;
    mCircleShockwaveCenterHandle = -1;
    mCircleShockwaveParamsHandle = -1;
    mCircleShockwaveScaleHandle = -1;
}

void AudioVisualizer::pushAudioFrame(float frame) {
    memmove(visualizerPlain.in_raw, visualizerPlain.in_raw + 1, (FFT_SIZE - 1) * sizeof(visualizerPlain.in_raw[0]));
    visualizerPlain.in_raw[FFT_SIZE - 1] = frame;
}

void AudioVisualizer::setTouchState(float xNorm, float yNorm, bool isDown, float pressure, float radiusNorm) {
    mTouchXNorm.store(std::clamp(xNorm, 0.0f, 1.0f), std::memory_order_relaxed);
    mTouchYNorm.store(std::clamp(yNorm, 0.0f, 1.0f), std::memory_order_relaxed);
    mTouchPressure.store(std::clamp(pressure, 0.0f, 1.0f), std::memory_order_relaxed);
    mTouchRadiusNorm.store(std::clamp(radiusNorm, 0.03f, 0.5f), std::memory_order_relaxed);
    mTouchDown.store(isDown, std::memory_order_relaxed);
}

void AudioVisualizer::clean() {
    memset(visualizerPlain.in_raw, 0, sizeof(visualizerPlain.in_raw));
    memset(visualizerPlain.in_win, 0, sizeof(visualizerPlain.in_win));
    memset(visualizerPlain.out_raw, 0, sizeof(visualizerPlain.out_raw));
    memset(visualizerPlain.out_log, 0, sizeof(visualizerPlain.out_log));
    memset(visualizerPlain.out_smooth, 0, sizeof(visualizerPlain.out_smooth));
    memset(visualizerPlain.out_smear, 0, sizeof(visualizerPlain.out_smear));
    mTouchEnvelope = 0.0f;
    mShockwaveRadius = 0.0f;
    mShockwaveEnergy = 0.0f;
    mWasTouchDown = false;
}

void AudioVisualizer::setSampleRate(size_t sample_rate) {
    visualizerPlain.sample_rate = sample_rate;
}
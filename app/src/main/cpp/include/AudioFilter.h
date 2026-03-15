#pragma once

#include <vector>
#include <thread>
#include <cstdint>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

enum class FilterType : int {
    None = 0,
    LowPass = 1,
    HighPass = 2,
    Echo = 3,
    Reverb = 4,
    Pan = 5
};

class AudioFilter {
public:
    explicit AudioFilter() = default;

    virtual void process(float audioFrame[], int numFrames, int channels) const = 0;
};

class LowPassFilter : public AudioFilter {
public:
    explicit LowPassFilter(float alpha) : mAlpha(alpha), mPrevSampleL(0.0f), mPrevSampleR(0.0f) {
    }

    static LowPassFilter *createDefault() {
        return new LowPassFilter(0.05f);
    }

    void process(float audioFrame[], int numFrames, int channels) const override {
        for (int i = 0; i < numFrames; i++) {
            const int idx = i * channels;

            const float inputL = audioFrame[idx];
            const float outputL = mAlpha * inputL + (1.0f - mAlpha) * mPrevSampleL;
            audioFrame[idx] = outputL;
            mPrevSampleL = outputL;

            if (channels > 1) {
                const float inputR = audioFrame[idx + 1];
                const float outputR = mAlpha * inputR + (1.0f - mAlpha) * mPrevSampleR;
                audioFrame[idx + 1] = outputR;
                mPrevSampleR = outputR;
            }
        }
    }

private:
    float mAlpha;
    mutable float mPrevSampleL;
    mutable float mPrevSampleR;
};

class HighPassFilter : public AudioFilter {
public:
    explicit HighPassFilter(float alpha)
            : mAlpha(alpha),
              mPrevSampleL(0.0f),
              mPrevSampleR(0.0f),
              mPrevInputL(0.0f),
              mPrevInputR(0.0f) {
    }

    static HighPassFilter *createDefault() {
        return new HighPassFilter(0.95f);
    }

    void process(float audioFrame[], int numFrames, int channels) const override {
        for (int i = 0; i < numFrames; i++) {
            const int idx = i * channels;

            const float inputL = audioFrame[idx];
            const float outputL = mAlpha * (mPrevSampleL + inputL - mPrevInputL);
            audioFrame[idx] = outputL;

            mPrevInputL = inputL;
            mPrevSampleL = outputL;

            // Right channel (nếu có)
            if (channels > 1) {
                const float inputR = audioFrame[idx + 1];
                const float outputR = mAlpha * (mPrevSampleR + inputR - mPrevInputR);
                audioFrame[idx + 1] = outputR;

                mPrevInputR = inputR;
                mPrevSampleR = outputR;
            }
        }
    }

private:
    float mAlpha;
    mutable float mPrevInputL;
    mutable float mPrevInputR;
    mutable float mPrevSampleL;
    mutable float mPrevSampleR;
};

class EchoFilter : public AudioFilter {
public:
    explicit EchoFilter(float delaySeconds, float decay, int sampleRate)
            : mDelaySamples(static_cast<int>(delaySeconds * static_cast<float>(sampleRate))),
              mDecay(decay),
              mBuffer(mDelaySamples * 2, 0.0f),
              mWritePos(0) {
    }

    static EchoFilter *createDefault(int sampleRate) {
        return new EchoFilter(0.5f, 0.5f, sampleRate);
    }

    void process(float audioFrame[], int numFrames, int channels) const override {
        for (int i = 0; i < numFrames; i++) {
            const int idx = i * channels;

            const float inputL = audioFrame[idx];
            const float echoL = mBuffer[mWritePos * channels] * mDecay;
            float outputL = inputL + echoL;

            outputL = std::max(-1.0f, std::min(1.0f, outputL));

            audioFrame[idx] = outputL;
            mBuffer[mWritePos * channels] = outputL;

            if (channels > 1) {
                const float inputR = audioFrame[idx + 1];
                const float echoR = mBuffer[mWritePos * channels + 1] * mDecay;
                float outputR = inputR + echoR;

                outputR = std::max(-1.0f, std::min(1.0f, outputR));

                audioFrame[idx + 1] = outputR;
                mBuffer[mWritePos * channels + 1] = outputR;
            }

            mWritePos = (mWritePos + 1) % mDelaySamples;
        }
    }

private:
    int mDelaySamples;
    float mDecay;
    mutable std::vector<float> mBuffer;
    mutable int mWritePos;
};

class ReverbFilter : public AudioFilter {
public:
    explicit ReverbFilter(int sampleRate)
            : mWritePos(0) {

        // Các delay ngắn (milliseconds → samples)
        mDelays = {
                static_cast<int>(0.0297f * (float) sampleRate),
                static_cast<int>(0.0371f * (float) sampleRate),
                static_cast<int>(0.0411f * (float) sampleRate),
                static_cast<int>(0.0437f * (float) sampleRate)
        };

        int maxDelay = *std::max_element(mDelays.begin(), mDelays.end());
        mBuffer.resize(maxDelay * 2, 0.0f);
    }

    static ReverbFilter *createDefault(int sampleRate) {
        return new ReverbFilter(sampleRate);
    }

    void process(float audioFrame[], int numFrames, int channels) const override {
        for (int i = 0; i < numFrames; i++) {
            const int idx = i * channels;

            // ===== LEFT / MONO =====
            float inputL = audioFrame[idx];
            float reverbL = 0.0f;

            for (int d: mDelays) {
                int readPos = (mWritePos - d + mBuffer.size() / channels) % (mBuffer.size() / channels);
                reverbL += mBuffer[readPos * channels];
            }

            reverbL *= 0.25f; // normalize số tap
            float outputL = inputL + reverbL * mDecay;

            outputL = std::max(-1.0f, std::min(1.0f, outputL));
            audioFrame[idx] = outputL;
            mBuffer[mWritePos * channels] = outputL;

            // ===== RIGHT =====
            if (channels > 1) {
                float inputR = audioFrame[idx + 1];
                float reverbR = 0.0f;

                for (int d: mDelays) {
                    int readPos = (mWritePos - d + mBuffer.size() / channels) % (mBuffer.size() / channels);
                    reverbR += mBuffer[readPos * channels + 1];
                }

                reverbR *= 0.25f;
                float outputR = inputR + reverbR * mDecay;

                outputR = std::max(-1.0f, std::min(1.0f, outputR));
                audioFrame[idx + 1] = outputR;
                mBuffer[mWritePos * channels + 1] = outputR;
            }

            mWritePos++;
            if (mWritePos >= mBuffer.size() / channels) {
                mWritePos = 0;
            }
        }
    }

private:
    float mDecay = 0.4f;

    std::vector<int> mDelays;
    mutable std::vector<float> mBuffer;
    mutable int mWritePos;
};


class PanFilter : public AudioFilter {
public:
    PanFilter(float gLL, float gLR, float gRL, float gRR, bool renorm = false)
            : mGLL(gLL), mGLR(gLR), mGRL(gRL), mGRR(gRR), mRenorm(renorm) {
        if (mRenorm) renormalize();
    }


    static PanFilter *createDefault() {
        float pan = std::max(-1.0f, std::min(1.0f, 0.6f));
        const float pan01 = 0.5f * (pan + 1.0f);
        const float angle = pan01 * (static_cast<float>(M_PI) * 0.5f);
        const float gL = std::cos(angle);
        const float gR = std::sin(angle);

        return new PanFilter(gL, 0.0f, 0.0f, gR, false);
    }

    void process(float audioFrame[], int numFrames, int channels) const override {
        if (channels < 2) return;

        for (int i = 0; i < numFrames; i++) {
            const int idx = i * channels;

            const float inL = audioFrame[idx];
            const float inR = audioFrame[idx + 1];

            const float outL = inL * mGLL + inR * mGLR;
            const float outR = inL * mGRL + inR * mGRR;

            audioFrame[idx] = std::max(-1.0f, std::min(1.0f, outL));
            audioFrame[idx + 1] = std::max(-1.0f, std::min(1.0f, outR));
        }
    }

private:
    void renormalize() {
        const float sumL = std::fabs(mGLL) + std::fabs(mGLR);
        const float sumR = std::fabs(mGRL) + std::fabs(mGRR);

        if (sumL > 1e-6f) {
            mGLL /= sumL;
            mGLR /= sumL;
        }
        if (sumR > 1e-6f) {
            mGRL /= sumR;
            mGRR /= sumR;
        }
    }

    float mGLL, mGLR, mGRL, mGRR;
    bool mRenorm;
};
#pragma once

#include "oboe/Oboe.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include "AudioDecoder.h"
#include "LockfreeBuffer.h"
#include "AudioVisualizer.h"


class AudioEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    AudioEngine();

    ~AudioEngine() override;

    bool setAudioSource(int fd, int64_t offset, int64_t length);

    void play();

    void pause();

    void setFilterEnabled(bool enabled);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) override;

    bool onError(oboe::AudioStream *audioStream, oboe::Result error) override;

    AudioVisualizer* getAudioVisualizer() {
        return &audioVisualizer;
    }

private:

    void decodeLoop();

    void stopWorkerThreads();

    oboe::AudioStream *mStream = nullptr;
    AudioDecoder *mDecoder = nullptr;

    int mSampleRate{};
    int mChannels{};

    std::atomic<bool> mIsPlaying{false};
    std::atomic<bool> mFilterEnabled{false};

    // Filter state
    float mPreviousSampleL = 0.0f;
    float mPreviousSampleR = 0.0f;

    std::unique_ptr<LockfreeBuffer<float>> mPlaybackBuffer;
    std::thread mDecodeThread;
    std::atomic<bool> mIsDecoding{false};
    std::mutex mStateMutex;

    AudioVisualizer audioVisualizer;
};

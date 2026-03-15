#include "AudioEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include "Util.h"

AudioEngine::AudioEngine() {
    mDecoder = new AudioDecoder();
}

AudioEngine::~AudioEngine() {
    if (mStream) {
        mStream->close();
        mStream = nullptr;
    }

    stopWorkerThreads();

    delete mDecoder;
}

void AudioEngine::stopWorkerThreads() {
    mIsDecoding.store(false, std::memory_order_release);

    if (mDecodeThread.joinable()) mDecodeThread.join();

    if (mPlaybackBuffer) mPlaybackBuffer->clear();
}

bool AudioEngine::setAudioSource(int fd, int64_t offset, int64_t length) {
    pause();

    if (mStream) {
        mStream->close();
        mStream = nullptr;
        mIsPlaying.store(false, std::memory_order_release);
    }

    stopWorkerThreads();

    const bool initialized = mDecoder->init(fd, offset, length, mSampleRate, mChannels);
    if (!initialized) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mPlaybackBuffer = std::make_unique<LockfreeBuffer<float>>(mSampleRate * mChannels);
        mPreviousSampleL = 0.0f;
        mPreviousSampleR = 0.0f;
    }

    LOGI("Audio stream ready to decode, SR: %d, Ch: %d", mSampleRate, mChannels);

    oboe::AudioStreamBuilder builder;
    builder.setChannelCount(mChannels)
            ->setSampleRate(mSampleRate)
            ->setFormat(oboe::AudioFormat::Float)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setDirection(oboe::Direction::Output)
            ->setDataCallback(this)
            ->setErrorCallback(this);

    const oboe::Result result = builder.openStream(&mStream);
    audioVisualizer.setSampleRate(mSampleRate);
    if (result != oboe::Result::OK) {
        LOGE("Failed to open stream: %s", oboe::convertToText(result));
        return false;
    }

    mIsDecoding.store(true, std::memory_order_release);
    mDecodeThread = std::thread(&AudioEngine::decodeLoop, this);

    return true;
}

void AudioEngine::decodeLoop() {
    constexpr int32_t kChunkFrames = 1024;
    std::vector<float> decodeBuffer(kChunkFrames * mChannels);

    while (mIsDecoding.load(std::memory_order_acquire)) {
        auto *playbackBuffer = mPlaybackBuffer.get();
        if (!playbackBuffer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const size_t writableSamples = playbackBuffer->availableToWrite();
        if (writableSamples < static_cast<size_t>(kChunkFrames * mChannels)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }

        const int32_t framesRead = mDecoder->readFrames(decodeBuffer.data(), kChunkFrames);
        if (framesRead > 0) {
            playbackBuffer->write(decodeBuffer.data(), static_cast<size_t>(framesRead * mChannels));
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}


void AudioEngine::play() {
    if (mStream && !mIsPlaying.load(std::memory_order_acquire)) {
        if (mStream->requestStart() == oboe::Result::OK) {
            mIsPlaying.store(true, std::memory_order_release);
        }
    }
}

void AudioEngine::pause() {
    if (mStream && mIsPlaying.load(std::memory_order_acquire)) {
        if (mStream->requestPause() == oboe::Result::OK) {
            mIsPlaying.store(false, std::memory_order_release);
        }
    }
}

void AudioEngine::setFilterEnabled(bool enabled) {
    mFilterEnabled.store(enabled, std::memory_order_release);
}


oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) {
    (void) audioStream;
    auto *output = static_cast<float *>(audioData);
    const int32_t samplesNeeded = numFrames * mChannels;

    size_t samplesRead = 0;
    if (mPlaybackBuffer) {
        samplesRead = mPlaybackBuffer->read(output, static_cast<size_t>(samplesNeeded));
    }

    if (samplesRead < static_cast<size_t>(samplesNeeded)) {
        std::memset(output + samplesRead, 0, sizeof(float) * (static_cast<size_t>(samplesNeeded) - samplesRead));
    }

    const bool filterEnabled = mFilterEnabled.load(std::memory_order_acquire);
    for (int32_t i = 0; i < samplesNeeded; ++i) {
        float sample = output[i];
        if (filterEnabled) {
            if (mChannels == 2) {
                if ((i & 1) == 0) {
                    sample = mPreviousSampleL + 0.05f * (sample - mPreviousSampleL);
                    mPreviousSampleL = sample;
                } else {
                    sample = mPreviousSampleR + 0.05f * (sample - mPreviousSampleR);
                    mPreviousSampleR = sample;
                }
            } else {
                sample = mPreviousSampleL + 0.05f * (sample - mPreviousSampleL);
                mPreviousSampleL = sample;
            }
            output[i] = sample;
        }
    }
    if (mChannels == 1) {
        for (int32_t i = 0; i < numFrames; ++i) {
            audioVisualizer.pushAudioFrame(output[i]);
        }
    } else {
        for (int32_t i = 0; i < samplesNeeded; i += 2) {
            audioVisualizer.pushAudioFrame(output[i]);
        }
    }
    return oboe::DataCallbackResult::Continue;
}


bool AudioEngine::onError(oboe::AudioStream *audioStream, oboe::Result error) {
    LOGE("Stream error: %s", oboe::convertToText(error));
    if (mStream) {
        mStream->close();
        mStream = nullptr;
    }
    mIsPlaying.store(false, std::memory_order_release);
    return true;
}
#include "AudioEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include "Util.h"

template<typename T>
std::shared_ptr<AudioFilter> makeFilter(T *ptr) {
    return std::shared_ptr<AudioFilter>(ptr);
}

AudioEngine::AudioEngine() {
    mDecoder = new AudioDecoder();
    mAudioVisualizer = new AudioVisualizer();
}

AudioEngine::~AudioEngine() {
    if (mStream) {
        mStream->close();
        mStream = nullptr;
    }

    stopWorkerThreads();

    delete mDecoder;
    delete mAudioVisualizer;
    std::atomic_store(&mAudioFilter, std::shared_ptr<AudioFilter>(nullptr));
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
    mAudioVisualizer->setSampleRate(mSampleRate);
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

    auto filter = std::atomic_load(&mAudioFilter);
    if (filter) {
        filter->process(output, numFrames, mChannels);
    }
    if (mChannels == 1) {
        for (int32_t i = 0; i < numFrames; ++i) {
            mAudioVisualizer->pushAudioFrame(output[i]);
        }
    } else {
        for (int32_t i = 0; i < samplesNeeded; i += 2) {
            mAudioVisualizer->pushAudioFrame(output[i]);
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

void AudioEngine::setAudioFilter(FilterType type) {
    std::shared_ptr<AudioFilter> newFilter;

    switch (type) {
        case FilterType::None:
            newFilter = nullptr;
            break;
        case FilterType::LowPass:
            newFilter = makeFilter(LowPassFilter::createDefault());
            break;
        case FilterType::HighPass:
            newFilter = makeFilter(HighPassFilter::createDefault());
            break;
        case FilterType::Echo:
            newFilter = makeFilter(EchoFilter::createDefault(mSampleRate));
            break;
        case FilterType::Reverb:
            newFilter = makeFilter(ReverbFilter::createDefault(mSampleRate));
            break;

        case FilterType::Pan:
            newFilter = makeFilter(PanFilter::createDefault());
            break;
    }

    std::atomic_store(&mAudioFilter, newFilter);
}

void AudioEngine::setVisualizerTouch(float xNorm, float yNorm, bool isDown, float pressure, float radiusNorm) {
    if (mAudioVisualizer) {
        mAudioVisualizer->setTouchState(xNorm, yNorm, isDown, pressure, radiusNorm);
    }
}

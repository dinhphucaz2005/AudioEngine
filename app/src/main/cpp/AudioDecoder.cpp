#include "AudioDecoder.h"
#include "Util.h"
#include <android/log.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>


AudioDecoder::AudioDecoder() = default;

AudioDecoder::~AudioDecoder() {
    close();
}

bool AudioDecoder::init(int fd, int64_t offset, int64_t length, int &outSampleRate, int &outChannels) {
    if (fd < 0) return false;

    // Clean up any existing state first
    close();

    mExtractor = AMediaExtractor_new();
    media_status_t status = AMediaExtractor_setDataSourceFd(mExtractor, fd, offset, length);
    if (status != AMEDIA_OK) {
        LOGE("Failed to set data source");
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
        return false;
    }

    int numTracks = AMediaExtractor_getTrackCount(mExtractor);
    AMediaFormat *format = nullptr;
    const char *mime = nullptr;
    int trackIndex = -1;

    for (int i = 0; i < numTracks; i++) {
        format = AMediaExtractor_getTrackFormat(mExtractor, i);
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        if (mime && strncmp(mime, "audio/", 6) == 0) {
            trackIndex = i;
            break;
        }
        AMediaFormat_delete(format);
        format = nullptr;
    }

    if (trackIndex < 0) {
        LOGE("No audio track found");
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
        return false;
    }

    AMediaExtractor_selectTrack(mExtractor, trackIndex);

    mSampleRate = 44100;
    mChannels = 2;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &mSampleRate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &mChannels);

    outSampleRate = mSampleRate;
    outChannels = mChannels;

    mCodec = AMediaCodec_createDecoderByType(mime);
    if (!mCodec) {
        LOGE("Failed to create codec");
        AMediaFormat_delete(format);
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
        return false;
    }

    AMediaCodec_configure(mCodec, format, nullptr, nullptr, 0);
    AMediaCodec_start(mCodec);
    AMediaFormat_delete(format);

    mIsEOF = false;
    mSawOutputEOF = false;
    mLeftoverBuffer.clear();
    mLeftoverPos = 0;

    return true;
}

int32_t AudioDecoder::readFrames(float *targetBuffer, int32_t targetFrames) {
    if (!mCodec || !mExtractor || mSawOutputEOF) {
        return 0; // EOF or not initialized
    }

    int32_t framesRead = 0;
    int32_t targetSamples = targetFrames * mChannels;

    // First, empty the leftover buffer
    if (mLeftoverPos < mLeftoverBuffer.size()) {
        int32_t remainingLeftover = mLeftoverBuffer.size() - mLeftoverPos;
        int32_t samplesToCopy = std::min(remainingLeftover, targetSamples);

        std::memcpy(targetBuffer, mLeftoverBuffer.data() + mLeftoverPos, samplesToCopy * sizeof(float));

        mLeftoverPos += samplesToCopy;
        framesRead += samplesToCopy / mChannels;

        if (framesRead == targetFrames) {
            return framesRead;
        }
    } else {
        // Leftover buffer is fully consumed, clear it to save memory allocations
        mLeftoverBuffer.clear();
        mLeftoverPos = 0;
    }

    // Now we need more samples directly from the codec
    while (framesRead < targetFrames && !mSawOutputEOF) {
        // 1. Feed the decoder
        if (!mIsEOF) {
            ssize_t inputBufId = AMediaCodec_dequeueInputBuffer(mCodec, 0); // non-blocking
            if (inputBufId >= 0) {
                size_t bufSize;
                uint8_t *buf = AMediaCodec_getInputBuffer(mCodec, inputBufId, &bufSize);
                ssize_t sampleSize = AMediaExtractor_readSampleData(mExtractor, buf, bufSize);

                if (sampleSize <= 0) {
                    mIsEOF = true;
                    AMediaCodec_queueInputBuffer(mCodec, inputBufId, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                } else {
                    int64_t presentationTimeUs = AMediaExtractor_getSampleTime(mExtractor);
                    AMediaCodec_queueInputBuffer(mCodec, inputBufId, 0, sampleSize, presentationTimeUs, 0);
                    AMediaExtractor_advance(mExtractor);
                }
            }
        }

        // 2. Consume decoded data
        AMediaCodecBufferInfo info;
        ssize_t outputBufId = AMediaCodec_dequeueOutputBuffer(mCodec, &info, 10000); // 10ms timeout

        if (outputBufId >= 0) {
            if (info.size > 0) {
                size_t outSize;
                uint8_t *outBuf = AMediaCodec_getOutputBuffer(mCodec, outputBufId, &outSize);
                auto *pcm16 = reinterpret_cast<int16_t *>(outBuf + info.offset);
                int numSamples = info.size / sizeof(int16_t);

                int32_t samplesNeeded = (targetFrames - framesRead) * mChannels;

                if (numSamples <= samplesNeeded) {
                    // Fits directly into targetBuffer
                    int32_t targetOffset = framesRead * mChannels;
                    for (int i = 0; i < numSamples; i++) {
                        targetBuffer[targetOffset + i] = static_cast<float>(pcm16[i]) / 32768.0f;
                    }
                    framesRead += numSamples / mChannels;
                } else {
                    // Decoded more than we need. Copy what we need and stash the rest.
                    int32_t targetOffset = framesRead * mChannels;
                    for (int i = 0; i < samplesNeeded; i++) {
                        targetBuffer[targetOffset + i] = static_cast<float>(pcm16[i]) / 32768.0f;
                    }
                    framesRead = targetFrames;

                    // Stash leftovers
                    int32_t numLeftover = numSamples - samplesNeeded;
                    mLeftoverBuffer.resize(numLeftover);
                    for (int i = 0; i < numLeftover; i++) {
                        mLeftoverBuffer[i] = static_cast<float>(pcm16[samplesNeeded + i]) / 32768.0f;
                    }
                    mLeftoverPos = 0;
                }
            }

            AMediaCodec_releaseOutputBuffer(mCodec, outputBufId, false);

            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                mSawOutputEOF = true;
            }
        } else if (outputBufId == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            // Ignored, format should not change mid-stream but if it does, 
            // updating mSampleRate/mChannels here could be destructive if RingBuffer is already allocated.
        } else if (outputBufId == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            // No output available right now, if we also hit EOF on input, just break to avoid infinite loop
            if (mIsEOF) {
                break;
            }
        }
    }

    return framesRead;
}

void AudioDecoder::close() {
    if (mCodec) {
        AMediaCodec_stop(mCodec);
        AMediaCodec_delete(mCodec);
        mCodec = nullptr;
    }
    if (mExtractor) {
        AMediaExtractor_delete(mExtractor);
        mExtractor = nullptr;
    }
    mLeftoverBuffer.clear();
    mLeftoverPos = 0;
}

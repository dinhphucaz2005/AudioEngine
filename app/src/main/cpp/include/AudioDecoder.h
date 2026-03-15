#pragma once

#include <vector>
#include <thread>
#include <cstdint>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

class AudioDecoder {
public:
    AudioDecoder();

    ~AudioDecoder();

    bool init(int fd, int64_t offset, int64_t length, int &outSampleRate, int &outChannels);

    // Reads up to numFrames. Returns the actual number of frames read.
    // A frame = (1 sample per channel). So 1 frame of Stereo = 2 floats.
    // Returns 0 if EOF or error.
    int32_t readFrames(float *targetBuffer, int32_t numFrames);

    void close();

private:
    AMediaExtractor *mExtractor = nullptr;
    AMediaCodec *mCodec = nullptr;

    int mSampleRate = 44100;
    int mChannels = 2;
    bool mIsEOF = false;           // Input to codec EOF
    bool mSawOutputEOF = false;    // Output from codec EOF

    // We might decode more samples from a Codec buffer than requested.
    // So we need a small internal buffer to hold leftovers between readFrames() calls.
    std::vector<float> mLeftoverBuffer;
    size_t mLeftoverPos = 0;
};

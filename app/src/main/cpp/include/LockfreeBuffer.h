#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

template<typename T>
class LockfreeBuffer {
public:
    explicit LockfreeBuffer(size_t capacity)
            : mCapacity(nextPowerOfTwo(capacity + 1)),
              mMask(mCapacity - 1),
              mBuffer(mCapacity) {
        mReadIndex.store(0, std::memory_order_relaxed);
        mWriteIndex.store(0, std::memory_order_relaxed);
    }

    size_t availableToWrite() const {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
        const size_t readIndex = mReadIndex.load(std::memory_order_acquire);

        return (mCapacity - 1) - (writeIndex - readIndex);
    }

    size_t availableToRead() const {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_acquire);
        const size_t readIndex = mReadIndex.load(std::memory_order_relaxed);

        return writeIndex - readIndex;
    }

    size_t write(const T *source, size_t elementCount) {
        const size_t writable = std::min(elementCount, availableToWrite());
        if (writable == 0) return 0;

        const size_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
        const size_t index = writeIndex & mMask;

        const size_t firstChunk = std::min(writable, mCapacity - index);

        std::memcpy(mBuffer.data() + index, source, firstChunk * sizeof(T));
        if (firstChunk < writable) {
            std::memcpy(mBuffer.data(), source + firstChunk, (writable - firstChunk) * sizeof(T));
        }

        mWriteIndex.store(writeIndex + writable, std::memory_order_release);
        return writable;
    }

    size_t read(T *target, size_t elementCount) {
        const size_t readable = std::min(elementCount, availableToRead());
        if (readable == 0) return 0;

        const size_t readIndex = mReadIndex.load(std::memory_order_relaxed);
        const size_t index = readIndex & mMask;

        const size_t firstChunk = std::min(readable, mCapacity - index);

        std::memcpy(target, mBuffer.data() + index, firstChunk * sizeof(T));
        if (firstChunk < readable) {
            std::memcpy(target + firstChunk, mBuffer.data(), (readable - firstChunk) * sizeof(T));
        }

        mReadIndex.store(readIndex + readable, std::memory_order_release);
        return readable;
    }

    void clear() {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_acquire);
        mReadIndex.store(writeIndex, std::memory_order_release);
    }

private:
    static size_t nextPowerOfTwo(size_t n) {
        if (n == 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return n + 1;
    }

private:
    size_t mCapacity;
    size_t mMask;
    std::vector<T> mBuffer;
    std::atomic<size_t> mReadIndex{};
    std::atomic<size_t> mWriteIndex{};
};


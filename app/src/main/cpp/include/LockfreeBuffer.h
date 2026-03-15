#pragma once

#include "../../../../../../../Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1/algorithm"
#include "../../../../../../../Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1/atomic"
#include "../../../../../../../Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1/cstddef"
#include "../../../../../../../Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1/cstring"
#include "../../../../../../../Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1/vector"

template<typename T>
class LockfreeBuffer {
public:
    explicit LockfreeBuffer(size_t capacity)
            : mCapacity(capacity + 1),
              mBuffer(mCapacity) {
        mReadIndex.store(0, std::memory_order_relaxed);
        mWriteIndex.store(0, std::memory_order_relaxed);
    }

    size_t capacity() const {
        return mCapacity - 1;
    }

    size_t availableToWrite() const {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
        const size_t readIndex = mReadIndex.load(std::memory_order_acquire);

        if (writeIndex >= readIndex) {
            return mCapacity - (writeIndex - readIndex) - 1;
        }
        return readIndex - writeIndex - 1;
    }

    size_t availableToRead() const {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_acquire);
        const size_t readIndex = mReadIndex.load(std::memory_order_relaxed);

        if (writeIndex >= readIndex) {
            return writeIndex - readIndex;
        }
        return mCapacity - (readIndex - writeIndex);
    }

    size_t write(const T *source, size_t elementCount) {
        const size_t writable = std::min(elementCount, availableToWrite());
        if (writable == 0) {
            return 0;
        }

        const size_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
        const size_t firstChunk = std::min(writable, mCapacity - writeIndex);

        std::memcpy(mBuffer.data() + writeIndex, source, firstChunk * sizeof(T));
        if (firstChunk < writable) {
            std::memcpy(mBuffer.data(), source + firstChunk, (writable - firstChunk) * sizeof(T));
        }

        mWriteIndex.store((writeIndex + writable) % mCapacity, std::memory_order_release);
        return writable;
    }

    size_t read(T *target, size_t elementCount) {
        const size_t readable = std::min(elementCount, availableToRead());
        if (readable == 0) {
            return 0;
        }

        const size_t readIndex = mReadIndex.load(std::memory_order_relaxed);
        const size_t firstChunk = std::min(readable, mCapacity - readIndex);

        std::memcpy(target, mBuffer.data() + readIndex, firstChunk * sizeof(T));
        if (firstChunk < readable) {
            std::memcpy(target + firstChunk, mBuffer.data(), (readable - firstChunk) * sizeof(T));
        }

        mReadIndex.store((readIndex + readable) % mCapacity, std::memory_order_release);
        return readable;
    }

    void clear() {
        const size_t writeIndex = mWriteIndex.load(std::memory_order_acquire);
        mReadIndex.store(writeIndex, std::memory_order_release);
    }

private:
    size_t mCapacity;
    std::vector<T> mBuffer;
    std::atomic<size_t> mReadIndex{};
    std::atomic<size_t> mWriteIndex{};
};



#pragma once

#include <android/log.h>
#include <complex>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AudioEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AudioEngine", __VA_ARGS__)

using Float_Complex = std::complex<float>;
#define cbuild(re, im) ((Float_Complex){re, im})
#define creal(re) Float_Complex{re, 0.0f}
#define cmul(a, b) ((a) * (b))
#define cadd(a, b) ((a) + (b))
#define csub(a, b) ((a) - (b))
const float PI = 3.14159265358979323846f;
static constexpr size_t FFT_SIZE = (1 << 13);

static float amp(Float_Complex z) {
    float a = z.real();
    float b = z.imag();
    return logf(a * a + b * b);
}

static void fft(float in[], Float_Complex out[], size_t n) {
    if (n == 0 || (n & (n - 1)) != 0) {
        return;
    }

    std::vector<Float_Complex> data(n);
    for (size_t i = 0; i < n; ++i) {
        data[i] = creal(in[i]);
    }

    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * PI / static_cast<float>(len);
        const Float_Complex wLen = cbuild(cosf(angle), sinf(angle));
        for (size_t i = 0; i < n; i += len) {
            Float_Complex w = creal(1.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const Float_Complex u = data[i + k];
                const Float_Complex v = cmul(data[i + k + len / 2], w);
                data[i + k] = cadd(u, v);
                data[i + k + len / 2] = csub(u, v);
                w = cmul(w, wLen);
            }
        }
    }

    std::copy(data.begin(), data.end(), out);
}

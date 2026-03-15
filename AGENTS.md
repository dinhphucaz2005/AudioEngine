# AGENTS.md

## Scope
- Ap dung cho toan bo repo, uu tien sua trong `app/src/main/java/com/example/audioengine` va `app/src/main/cpp`.
- Khong sua file sinh tu dong trong `app/build/` va `build/`.

## Kien truc tong quan
- **UI + lifecycle**: `MainActivity.kt` (Compose UI, xin quyen, file picker, play/pause, filter, khoi tao `VisualizerGLSurfaceView`).
- **Kotlin bridge**: `AudioEngine.kt`
  - Load thu vien native `audioengine`.
  - Quan ly `nativeHandle` theo tung instance (khong dung singleton toan cuc).
  - Resolve `Uri -> (fd, offset, length)` bat dong bo bang coroutine (`AssetFileDescriptor` truoc, fallback `ParcelFileDescriptor`).
- **JNI bridge**: `audio_engine_jni.cpp`
  - Mapping 1-1 voi externals trong `AudioEngine.kt` (`nativeCreate`, `nativeSetAudioSource`, `nativePlay`, `nativePause`, `nativeSetFilterEnabled`, visualizer lifecycle methods).
  - Chuyen `nativeHandle` thanh `AudioEngine*` moi lan goi.
- **Native playback**: `AudioEngine.cpp` + `include/AudioEngine.h`
  - Decode PCM tren decode thread (`decodeLoop`) vao `LockfreeBuffer<float>`.
  - Phat audio qua Oboe callback `onAudioReady`.
  - Low-pass filter (tuy chon) duoc ap dung truc tiep trong callback.
  - Day sample mono vao `AudioVisualizer::pushAudioFrame(...)` de phuc vu FFT/ve.
- **Native visualizer OpenGL + FFT**: `AudioVisualizer.cpp` + `include/AudioVisualizer.h`
  - Render tren GL thread qua `visualizerOnSurfaceCreated/Changed/DrawFrame`.
  - Tinh FFT theo frame trong `fft_analyze(dt)` su dung `dt` thuc te (ns -> seconds).
- **Decoder**: `AudioDecoder.cpp` (NDK `AMediaExtractor` + `AMediaCodec`).

## Luong du lieu va thread quan trong
1. User chon file trong `MainActivity.kt` -> `AudioEngine.loadAudioSource(...)`.
2. `AudioEngine.kt` mo FD va goi `nativeSetAudioSource(fd, offset, length, handle)`.
3. `AudioEngine::setAudioSource(...)` reset stream/cu, init lai decoder, tao `LockfreeBuffer`, start decode thread.
4. Decode thread doc chunk PCM va ghi vao ring buffer.
5. `play()` -> Oboe start stream -> `onAudioReady(...)` doc ring buffer, filter (neu bat), xuat loa.
6. Trong `onAudioReady(...)`, sample mono duoc push vao `AudioVisualizer`.
7. GL thread (`VisualizerGLSurfaceView.Renderer.onDrawFrame`) goi native draw frame; native tu tinh FFT + smooth + render.

## Convention theo project
- Giu `AudioEngine::onAudioReady` realtime-safe: khong block, khong cap phat nang, khong JNI call.
- Giao tiep decode thread <-> audio callback qua `LockfreeBuffer<T>` (SPSC).
- Ownership FD o Kotlin (`use {}` tu dong dong). Khong `close(fd)` ben native.
- Visualizer chi render tren GL thread cua `GLSurfaceView`; khong dua logic GL sang audio thread.
- Khi them JNI method, cap nhat dong bo ca `AudioEngine.kt` va `audio_engine_jni.cpp` (ten + signature + tham so handle).
- Neu sua filter/FFT, uu tien on dinh am thanh truoc (khong lam tang xruns/dropout).

## Build va diem tich hop
- Android config: `app/build.gradle.kts` (SDK, Compose, external CMake, Oboe dependency).
- Native link: `app/src/main/cpp/CMakeLists.txt` (`oboe`, `android`, `mediandk`, `log`, `GLESv3`).
- Permission media: `app/src/main/AndroidManifest.xml` (`READ_MEDIA_AUDIO`, fallback `READ_EXTERNAL_STORAGE`).
- Chua co test sources (`app/src/test`, `app/src/androidTest`): validate bang chay app + log co trong tam.

## Fast change map
- **UI/interaction**: `app/src/main/java/com/example/audioengine/MainActivity.kt`.
- **Kotlin <-> native API surface**: `app/src/main/java/com/example/audioengine/AudioEngine.kt`.
- **GLSurfaceView wiring**: `app/src/main/java/com/example/audioengine/VisualizerGLSurfaceView.kt`.
- **JNI mapping**: `app/src/main/cpp/audio_engine_jni.cpp`.
- **Playback + stream lifecycle + filter + decode thread**: `app/src/main/cpp/AudioEngine.cpp`, `app/src/main/cpp/include/AudioEngine.h`.
- **FFT + shader + visual effects**: `app/src/main/cpp/AudioVisualizer.cpp`, `app/src/main/cpp/include/AudioVisualizer.h`.
- **Codec/decode behavior**: `app/src/main/cpp/AudioDecoder.cpp`.


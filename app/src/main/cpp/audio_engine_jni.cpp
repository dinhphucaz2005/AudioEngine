#include <jni.h>
#include "include/AudioEngine.h"
#include "include/Util.h"


extern "C" JNIEXPORT jlong JNICALL
Java_com_example_audioengine_AudioEngine_nativeCreate(JNIEnv *env, jobject thiz) {
    auto *engine = new AudioEngine();
    return reinterpret_cast<jlong>(engine);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativeDestroy(JNIEnv *env, jobject thiz, jlong audio_engine_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(audio_engine_handle);
    delete engine;
}
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_audioengine_AudioEngine_nativeSetAudioSource(JNIEnv *env, jobject thiz, jint fd, jlong offset, jlong length, jlong audio_engine_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(audio_engine_handle);
    if (engine) {
        return engine->setAudioSource(fd, offset, length) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativePlay(JNIEnv *env, jobject thiz, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->play();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativePause(JNIEnv *env, jobject thiz, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->pause();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativeVisualizerOnSurfaceCreated(JNIEnv *env, jobject thiz, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->getAudioVisualizer()->visualizerOnSurfaceCreated();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativeVisualizerOnSurfaceChanged(JNIEnv *env, jobject thiz, jint width, jint height, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->getAudioVisualizer()->visualizerOnSurfaceChanged(width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativeVisualizerOnDrawFrame(JNIEnv *env, jobject thiz, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->getAudioVisualizer()->visualizerOnDrawFrame();
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_audioengine_AudioEngine_nativeSetFilterType(JNIEnv *env, jobject thiz, jint filter_type, jlong native_handle) {
    auto *engine = reinterpret_cast<AudioEngine *>(native_handle);
    if (engine) engine->setAudioFilter(static_cast<FilterType>(filter_type));
}
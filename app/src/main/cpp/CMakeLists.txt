cmake_minimum_required(VERSION 3.22.1)
project("audioengine")

find_package(oboe REQUIRED CONFIG)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(${CMAKE_PROJECT_NAME} SHARED
        AudioDecoder.cpp
        AudioVisualizer.cpp
        AudioEngine.cpp
        audio_engine_jni.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        oboe::oboe
        android
        mediandk
        log
        GLESv3)

project(cam_ncnn_win)

cmake_minimum_required(VERSION 3.10.2)

set(CMAKE_VERBOSE_MAKEFILE on)

#set(ncnn_DIR ${CMAKE_SOURCE_DIR}/ncnn-20210720-android-vulkan/${ANDROID_ABI}/lib/cmake/ncnn)
set(ncnn_DIR C:/Users/yyang/source/repos/prj_ncnn/build-android-armv7/install/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
add_library(app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Werror")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

add_library(cam_ncnn_win SHARED main_activity_jni.cpp)

target_link_libraries(
        cam_ncnn_win
        ncnn
        android
        log
        m
        app_glue
        camera2ndk
        mediandk)

#!/usr/bin/env bash

# Linux 版 Android SDK 和 NDK 保存在 Windows D 盘。
export ANDROID_HOME=/mnt/d/Android/SDK_WSL
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/26.3.11579264"
export CMAKE_PATH="$ANDROID_HOME/cmake/3.22.1"

export CMAKE_BUILD_PARALLEL_LEVEL=6
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$CMAKE_PATH/bin:$PATH"

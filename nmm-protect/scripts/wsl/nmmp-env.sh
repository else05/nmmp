#!/usr/bin/env bash

# Linux 版 Android SDK、NDK 和 O-MVLL 均保存在 Windows D 盘。
export ANDROID_HOME=/mnt/d/Android/SDK_WSL
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/26.3.11579264"
export CMAKE_PATH="$ANDROID_HOME/cmake/3.22.1"

export OMVLL_HOME="$ANDROID_HOME/omvll/1.8.0"
export OMVLL_PLUGIN="$OMVLL_HOME/omvll_ndk_r26d.so"
export OMVLL_CONFIG="$OMVLL_HOME/config.py"
export OMVLL_PYTHONPATH="$OMVLL_HOME/Python-3.10.7/Lib"

export CMAKE_BUILD_PARALLEL_LEVEL=6
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$CMAKE_PATH/bin:$PATH"

# O-MVLL 依赖 NDK 附带的 Linux libc++ 运行库。
export LD_LIBRARY_PATH="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

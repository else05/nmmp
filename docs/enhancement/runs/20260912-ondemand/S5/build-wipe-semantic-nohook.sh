#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
root=/mnt/e/OtherProject/safe-toolchain/nmmp
out=$root/nmm-protect/build/on-demand-20260912
for variant in Release; do
    "$CMAKE_PATH/bin/cmake" -S "$root/nmmvm/nmmvm/src/test/semantic" -B "$out/wipe-semantic-nohook-$variant" -GNinja -DCMAKE_MAKE_PROGRAM="$CMAKE_PATH/bin/ninja" -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE="$variant" -DNMMP_CPP_ROOT="$out/wipe-semantic-src" -DNMMP_TEST_DEMAND=ON -DNMMP_TEST_DECODE_WIPE=OFF -DNMMP_TEST_NATIVE_VM=ON -DNMMP_GENERATED_INIT="$out/s4-generated-init/jni_init.c" > "$out/wipe-semantic-nohook-$variant.log" 2>&1
    "$CMAKE_PATH/bin/cmake" --build "$out/wipe-semantic-nohook-$variant" --parallel 5 >> "$out/wipe-semantic-nohook-$variant.log" 2>&1
done

#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/codec3-only-20260913
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
for mode in Debug Release Sanitized; do
    extra=()
    kind=$mode
    if [ "$mode" = Sanitized ]; then
        kind=Debug
        extra=(-DCMAKE_C_FLAGS=-fsanitize=address,undefined -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined)
    fi
    cmake -S "$root/nmmvm/nmmvm/src/test/cpp" -B "$work/host-$mode" -GNinja \
        -DCMAKE_BUILD_TYPE="$kind" -DNMMP_NATIVE_TEST_CONFIG="$root/nmmvm/nmmvm/src/main/cpp/vm/include" \
        -DNMMP_NATIVE_VECTORS="$root/nmm-protect/build/on-demand-20260912/s4-vectors/native-program-vectors.bin" \
        "${extra[@]}" > "$work/host-$mode.log" 2>&1
    cmake --build "$work/host-$mode" --parallel 5 >> "$work/host-$mode.log" 2>&1
    ctest --test-dir "$work/host-$mode" --output-on-failure >> "$work/host-$mode.log" 2>&1
    echo "Host $mode passed"
done
for mode in Release Debug; do
    "$CMAKE_PATH/bin/cmake" -S "$root/nmmvm/nmmvm/src/test/semantic" -B "$work/semantic-$mode" -GNinja \
        -DCMAKE_MAKE_PROGRAM="$CMAKE_PATH/bin/ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE="$mode" \
        -DNMMP_TEST_DEMAND=ON -DNMMP_TEST_DECODE_WIPE=ON -DNMMP_TEST_NATIVE_VM=ON \
        > "$work/semantic-$mode.log" 2>&1
    "$CMAKE_PATH/bin/cmake" --build "$work/semantic-$mode" --parallel 5 >> "$work/semantic-$mode.log" 2>&1
    echo "ARM64 $mode built"
done

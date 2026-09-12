#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/on-demand-20260912
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
python3 -m unittest discover -s "$root/nmm-protect/mksrc/loader/tests" -p 'test_*.py' > "$work/p5-python-all.log" 2>&1
cmake -S "$root/nmm-protect/mksrc/loader/tests" -B "$work/p5-host" -GNinja -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DNMMP_STAGE0_FIXTURE="$work/p5-vectors/stage0-programs.c" > "$work/p5-host.log" 2>&1
cmake --build "$work/p5-host" --parallel 5 >> "$work/p5-host.log" 2>&1
ctest --test-dir "$work/p5-host" --output-on-failure >> "$work/p5-host.log" 2>&1
"$work/host-reader/native_vm_test" "$work/p5-vectors/stage0-vectors.bin" > "$work/p5-native-vectors.log" 2>&1
for variant in Release Debug; do
 "$CMAKE_PATH/bin/cmake" -S "$root/nmm-protect/mksrc/loader/tests" -B "$work/p5-android-$variant" -GNinja -DCMAKE_MAKE_PROGRAM="$CMAKE_PATH/bin/ninja" -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE="$variant" -DNMMP_GENERATED_INIT_TESTS="$work/p5-init-fixtures" -DNMMP_STAGE0_FIXTURE="$work/p5-vectors/stage0-programs.c" > "$work/p5-android-$variant.log" 2>&1
 "$CMAKE_PATH/bin/cmake" --build "$work/p5-android-$variant" --parallel 5 >> "$work/p5-android-$variant.log" 2>&1
done

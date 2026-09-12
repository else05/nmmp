#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=5
export OMVLL_CONFIG="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_TEST_SEED=0123456789abcdef
base="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5/seed1"
harness="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness"
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=ON NMMP_PRIVATE_STAGE0_VM=ON
cd "$base/B"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo "seed1 B APK compiled"
java -cp "$harness/generator-classes:$base/hotspot-B/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-B/build" > "$base/hotspot-B/build.log" 2>&1
echo "seed1 B hotspot compiled"
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=OFF NMMP_PRIVATE_STAGE0_VM=ON
cd "$base/C"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo "seed1 C APK compiled"
java -cp "$harness/generator-classes:$base/hotspot-C/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-C/build" > "$base/hotspot-C/build.log" 2>&1
echo "seed1 C hotspot compiled"
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=OFF NMMP_PRIVATE_STAGE0_VM=ON
mkdir -p "$base/A/input/build" "$base/hotspot-A/build"
cp -a "$base/B/input/build/dex2c" "$base/A/input/build/"
cp -a "$base/hotspot-B/build/dex2c" "$base/hotspot-A/build/"
java -cp "$harness/generator-classes:$base/A/vm-protect.jar" BuildRuntime "$base/A/input/build" > "$base/A/build.log" 2>&1
echo "seed1 A APK runtime compiled"
java -cp "$harness/generator-classes:$base/hotspot-A/vm-protect.jar" BuildRuntime "$base/hotspot-A/build" > "$base/hotspot-A/build.log" 2>&1
echo "seed1 A hotspot compiled"
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON NMMP_PRIVATE_STAGE0_VM=ON
mkdir -p "$base/D/input/build" "$base/hotspot-D/build"
cp -a "$base/C/input/build/dex2c" "$base/D/input/build/"
cp -a "$base/hotspot-C/build/dex2c" "$base/hotspot-D/build/"
java -cp "$harness/generator-classes:$base/D/vm-protect.jar" BuildRuntime "$base/D/input/build" > "$base/D/build.log" 2>&1
echo "seed1 D APK runtime compiled"
java -cp "$harness/generator-classes:$base/hotspot-D/vm-protect.jar" BuildRuntime "$base/hotspot-D/build" > "$base/hotspot-D/build.log" 2>&1
echo "seed1 D hotspot compiled"

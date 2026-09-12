#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/on-demand-20260912
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export OMVLL_CONFIG="$root/nmm-protect/scripts/wsl/omvll-config.py"
export CMAKE_BUILD_PARALLEL_LEVEL=5 NMMP_PRIVATE_STAGE0_VM=ON
unset NMMP_TEST_SEED
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON
cd "$work/final-wipe-acceptance"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo 'Post-review unseeded demand/private APK built'
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=OFF
cd "$work/wipe-legacy-default"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo 'Post-review default legacy APK built'
export NMMP_TEST_SEED=0123456789abcdef NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON
cd "$work/wipe-hotspot-D"
java -cp "$work/s5-harness/generator-classes:$PWD/vm-protect.jar" GenerateBench "$work/s5-harness/impl-dex/classes.dex" "$PWD/build" > build.log 2>&1
echo 'Post-review seed1 ART hotspot library built'

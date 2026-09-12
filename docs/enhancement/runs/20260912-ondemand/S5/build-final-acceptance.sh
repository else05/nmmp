#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/on-demand-20260912/final-acceptance
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
unset NMMP_TEST_SEED
export NMMP_PRIVATE_LINKER=ON NMMP_PRIVATE_STAGE0_VM=ON NMMP_VM_DECODE_MODE=on-demand-v1 CMAKE_BUILD_PARALLEL_LEVEL=5
export OMVLL_CONFIG="$root/nmm-protect/scripts/wsl/omvll-config.py"
cd "$work"
java -jar "$work/vm-protect.jar" apk "$work/input/original.apk" /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > "$work/build.log" 2>&1

#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/codec3-only-20260913
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export OMVLL_CONFIG="$root/nmm-protect/scripts/wsl/omvll-config.py"
export CMAKE_BUILD_PARALLEL_LEVEL=5
unset NMMP_TEST_SEED NMMP_VM_DECODE_MODE NMMP_PRIVATE_LINKER NMMP_PRIVATE_STAGE0_VM
cd "$work/delivery"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo 'Default codec3/private/stage0 APK built'

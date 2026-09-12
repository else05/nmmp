#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/on-demand-20260912/s5-harness
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export OMVLL_CONFIG="$root/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_PRIVATE_LINKER=OFF NMMP_VM_DECODE_MODE=on-demand-v1 CMAKE_BUILD_PARALLEL_LEVEL=5
java -cp "$work/generator-classes:$work/trial-C/vm-protect.jar" GenerateBench "$work/impl-dex/classes.dex" "$work/trial-C/build" > "$work/trial-C/build.log" 2>&1

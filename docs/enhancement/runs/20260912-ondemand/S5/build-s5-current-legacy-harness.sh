#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=5
export OMVLL_CONFIG="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_TEST_SEED=0123456789abcdef
base="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5/seed1"
harness="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness"
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=OFF
java -cp "$harness/generator-classes:$harness/current-legacy/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$harness/current-legacy/build" > "$harness/current-legacy/build.log" 2>&1

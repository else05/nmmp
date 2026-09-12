#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=5 NMMP_TEST_SEED=0123456789abcdef NMMP_PRIVATE_LINKER=OFF
export OMVLL_CONFIG="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py"
harness="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness"
jar="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5/seed1/C/vm-protect.jar"
export NMMP_VM_DECODE_MODE=legacy
java -cp "$harness/generator-classes:$jar" BuildRuntime "$harness/opt-invariant-L/build" > "$harness/opt-invariant-L/build.log" 2>&1
export NMMP_VM_DECODE_MODE=on-demand-v1
java -cp "$harness/generator-classes:$jar" BuildRuntime "$harness/opt-invariant-C/build" > "$harness/opt-invariant-C/build.log" 2>&1

#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=5
export OMVLL_CONFIG="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_TEST_SEED=0123456789abcdef
base="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5/seed1"
harness="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness"
export NMMP_PRIVATE_LINKER=OFF NMMP_VM_DECODE_MODE=legacy
java -cp "$harness/generator-classes:$base/A/vm-protect.jar" BuildRuntime "$harness/probe-A/build" > "$harness/probe-A/build.log" 2>&1
echo "probe A compiled"
export NMMP_PRIVATE_LINKER=OFF NMMP_VM_DECODE_MODE=on-demand-v1
java -cp "$harness/generator-classes:$base/C/vm-protect.jar" BuildRuntime "$harness/probe-C/build" > "$harness/probe-C/build.log" 2>&1
echo "probe C compiled"

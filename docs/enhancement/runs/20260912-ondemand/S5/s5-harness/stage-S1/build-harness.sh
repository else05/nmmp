#!/usr/bin/env bash
set -euo pipefail
repo=/mnt/e/OtherProject/safe-toolchain/nmmp
w="$repo/nmm-protect/build/on-demand-20260912"
stage="$w/s5-harness/stage-S1"
source "$repo/nmm-protect/scripts/wsl/nmmp-env.sh"
export OMVLL_CONFIG="$repo/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_PRIVATE_LINKER=OFF
export NMMP_TEST_SEED=0123456789abcdef
export CMAKE_BUILD_PARALLEL_LEVEL=5
unset NMMP_VM_DECODE_MODE
test ! -e "$stage/build/dex2c"
cd "$stage"
java -version
java -Dnmmp.testSeed=0123456789abcdef \
    -cp "$stage/generator-classes:$stage/vm-protect.jar" \
    GenerateBench "$w/s5-harness/impl-dex/classes.dex" "$stage/build"

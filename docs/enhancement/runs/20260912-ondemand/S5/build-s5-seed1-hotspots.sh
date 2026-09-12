#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=5
export OMVLL_CONFIG="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py"
export NMMP_TEST_SEED=0123456789abcdef
base="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5/seed1"
harness="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness"
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=ON NMMP_PRIVATE_STAGE0_VM=ON
java -cp "$harness/generator-classes:$base/hotspot-B/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-B/build" > "$base/hotspot-B/build.log" 2>&1
echo "expanded B hotspot compiled"
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=OFF NMMP_PRIVATE_STAGE0_VM=ON
java -cp "$harness/generator-classes:$base/hotspot-C/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-C/build" > "$base/hotspot-C/build.log" 2>&1
echo "expanded C hotspot compiled"
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=OFF NMMP_PRIVATE_STAGE0_VM=ON
java -cp "$harness/generator-classes:$base/hotspot-A/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-A/build" > "$base/hotspot-A/build.log" 2>&1
echo "expanded A hotspot compiled"
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON NMMP_PRIVATE_STAGE0_VM=ON
java -cp "$harness/generator-classes:$base/hotspot-D/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-D/build" > "$base/hotspot-D/build.log" 2>&1
echo "expanded D hotspot compiled"

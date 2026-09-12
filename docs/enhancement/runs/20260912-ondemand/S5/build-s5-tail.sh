#!/usr/bin/env bash
set -euo pipefail
bash /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/build-final-acceptance.sh
echo "final unseeded APK completed"
bash /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/build-s5-probes.sh
echo "allocation and read probes completed"
bash /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/build-s5-negatives.sh
echo "binding negative APK runtimes completed"

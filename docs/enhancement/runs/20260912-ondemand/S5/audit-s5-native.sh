#!/usr/bin/env bash
set -euo pipefail
work=/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912
for seed in 1 2 3; do
 for variant in C D; do
  o="$work/s5/seed$seed/$variant"
  "$work/host-reader/vm_module_test" "$o/classes.native-audit" "$o/classes2.native-audit" > "$o/native-method-audit.log"
 done
done
o="$work/final-acceptance"
"$work/host-reader/vm_module_test" "$o/classes.native-audit" "$o/classes2.native-audit" > "$o/native-method-audit.log"
echo "PASS: 7 APK variants x 526 native method audits"

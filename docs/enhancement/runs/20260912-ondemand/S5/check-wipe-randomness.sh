#!/usr/bin/env bash
set -euo pipefail
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
unset NMMP_TEST_SEED
work=/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912
mkdir "$work/wipe-randomness-classes"
command -v java > "$work/wipe-randomness-java-path.log"
javac -cp "$work/final-wipe-acceptance/vm-protect.jar" -d "$work/wipe-randomness-classes" "$work/RandomnessCheck.java"
java -cp "$work/wipe-randomness-classes:$work/final-wipe-acceptance/vm-protect.jar" RandomnessCheck > "$work/wipe-randomness-probe.log" 2>&1

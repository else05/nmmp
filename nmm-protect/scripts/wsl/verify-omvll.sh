#!/usr/bin/env bash
set -euo pipefail

ENV_FILE="/mnt/d/Android/SDK_WSL/nmmp-env.sh"
SOURCE_FILE="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/smoke/OmvllSmoke.cpp"
OUTPUT_FILE="/tmp/nmmp-omvll-smoke.o"
LOG_ROOT="/mnt/d/Android/SDK_WSL/omvll/1.8.0/omvll-logs/omvll-module-logs/aarch64"

# shellcheck source=/dev/null
source "$ENV_FILE"

CLANG="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"
SYSROOT="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot"

cd "$OMVLL_HOME"

"$CLANG" \
    --target=aarch64-none-linux-android21 \
    --sysroot="$SYSROOT" \
    "-fpass-plugin=$OMVLL_PLUGIN" \
    -O3 \
    -c "$SOURCE_FILE" \
    -o "$OUTPUT_FILE"

[[ -s "$OUTPUT_FILE" ]]

LOG_FILE="$(
    find "$LOG_ROOT" \
        -maxdepth 1 \
        -type f \
        -name 'OmvllSmoke.cpp-omvll-*.log' \
        -printf '%T@ %p\n' |
        sort -nr |
        head -n 1 |
        cut -d' ' -f2-
)"

[[ -n "$LOG_FILE" && -f "$LOG_FILE" ]]
grep -Eq '\[omvll::BreakControlFlow\] Visiting function omvllSmokeEntry' "$LOG_FILE"
grep -Eq '\[omvll::OpaqueConstants\] Changes +applied' "$LOG_FILE"
grep -Eq '\[omvll::Arithmetic\] Visiting function .*omvllSmokeArithmetic' "$LOG_FILE"

echo "[nmmp-wsl] O-MVLL whitelist passed: $OUTPUT_FILE"
echo "[nmmp-wsl] O-MVLL log: $LOG_FILE"

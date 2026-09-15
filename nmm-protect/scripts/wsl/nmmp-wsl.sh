#!/usr/bin/env bash
set -euo pipefail

CALL_DIR="$(pwd -P)"
ENV_FILE="/mnt/d/Android/SDK_WSL/nmmp-env.sh"
JAR_DIR="/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/libs"

show_help() {
    cat <<'EOF'
NMMP APK protector for WSL + O-MVLL

Usage:
  nmmp-wsl <apk> [-r <rules>] [-m <mapping>]

Options:
  -r  Conversion rules. Defaults to ./convertRules.txt when it exists.
  -m  ProGuard/R8 mapping file. Requires rules.
  -h  Show this help.

Output:
  <calling-dir>/<original-name>_nmmpyyyyMMddHHmmss.apk

Examples:
  nmmp-wsl app-release.apk
  nmmp-wsl app-release.apk -r convertRules.txt
  nmmp-wsl app-release.apk -r convertRules.txt -m mapping.txt

Use WSL paths such as /mnt/d/... and /mnt/e/....
EOF
}

if [[ $# -eq 0 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    show_help
    exit 0
fi

APK="$1"
RULES=""
MAPPING=""
shift

while [[ $# -gt 0 ]]; do
    case "$1" in
        -r)
            [[ $# -ge 2 ]] || {
                echo "[nmmp-wsl] Missing value after -r."
                exit 2
            }
            RULES="$2"
            shift 2
            ;;
        -m)
            [[ $# -ge 2 ]] || {
                echo "[nmmp-wsl] Missing value after -m."
                exit 2
            }
            MAPPING="$2"
            shift 2
            ;;
        *)
            echo "[nmmp-wsl] Unknown option: $1"
            exit 2
            ;;
    esac
done

APK="$(realpath -e "$APK")"
APK_DIR="$(dirname "$APK")"
APK_FILE="$(basename "$APK")"
APK_NAME="${APK_FILE%.*}"

if [[ -z "$RULES" && -f "$CALL_DIR/convertRules.txt" ]]; then
    RULES="$CALL_DIR/convertRules.txt"
fi

if [[ -n "$RULES" ]]; then
    RULES="$(realpath -e "$RULES")"
fi

if [[ -n "$MAPPING" ]]; then
    MAPPING="$(realpath -e "$MAPPING")"
fi

if [[ -n "$MAPPING" && -z "$RULES" ]]; then
    echo "[nmmp-wsl] -m requires -r or ./convertRules.txt."
    exit 2
fi

[[ -f "$ENV_FILE" ]] || {
    echo "[nmmp-wsl] Environment file not found: $ENV_FILE"
    exit 3
}

# shellcheck source=/dev/null
source "$ENV_FILE"

for variable in \
    ANDROID_HOME \
    ANDROID_NDK_HOME \
    CMAKE_PATH; do
    [[ -n "${!variable:-}" ]] || {
        echo "[nmmp-wsl] Missing environment: $variable"
        exit 3
    }
done

command -v java >/dev/null || {
    echo "[nmmp-wsl] Linux Java not found."
    exit 3
}

[[ -x "$CMAKE_PATH/bin/cmake" ]] || {
    echo "[nmmp-wsl] Linux CMake not found: $CMAKE_PATH/bin/cmake"
    exit 3
}

[[ -x "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" ]] || {
    echo "[nmmp-wsl] Linux NDK Clang not found."
    exit 3
}

JAR="$(
    find "$JAR_DIR" \
        -maxdepth 1 \
        -type f \
        -name 'vm-protect-*.jar' \
        -printf '%T@ %p\n' |
        sort -nr |
        head -n 1 |
        cut -d' ' -f2-
)"

[[ -n "$JAR" && -f "$JAR" ]] || {
    echo "[nmmp-wsl] No vm-protect-*.jar found: $JAR_DIR"
    exit 3
}

echo "[nmmp-wsl] Work dir: $CALL_DIR"
echo "[nmmp-wsl] APK:      $APK"
[[ -n "$RULES" ]] && echo "[nmmp-wsl] Rules:    $RULES"
[[ -n "$MAPPING" ]] && echo "[nmmp-wsl] Mapping:  $MAPPING"
echo "[nmmp-wsl] JAR:      $JAR"
echo "[nmmp-wsl] NDK:      $ANDROID_NDK_HOME"
echo

cd "$CALL_DIR"

JAVA_ARGS=(apk "$APK")

if [[ -n "$RULES" ]]; then
    JAVA_ARGS+=("$RULES")
fi

if [[ -n "$MAPPING" ]]; then
    JAVA_ARGS+=("$MAPPING")
fi

java -jar "$JAR" "${JAVA_ARGS[@]}"

DEFAULT_OUTPUT="$APK_DIR/build/$APK_NAME-protect.apk"

[[ -f "$DEFAULT_OUTPUT" ]] || {
    echo "[nmmp-wsl] Expected output not found: $DEFAULT_OUTPUT"
    exit 4
}

STAMP="$(date +%Y%m%d%H%M%S)"
FINAL_OUTPUT="$CALL_DIR/${APK_NAME}_nmmp${STAMP}.apk"

mv -f "$DEFAULT_OUTPUT" "$FINAL_OUTPUT"

echo
echo "[nmmp-wsl] Output: $FINAL_OUTPUT"

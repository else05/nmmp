#!/bin/sh

# 以存储模式重新创建 vmsrc.zip，避免旧条目残留
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
VM_DIR="${SCRIPT_DIR}/../../nmmvm/nmmvm/src/main/cpp"

VM_SRC="vm cutils ConstantPool.c ConstantPool.h"

OUT="${SCRIPT_DIR}/../apkprotect/src/main/resources/vmsrc.zip"
rm -f "${OUT}"

(cd "${VM_DIR}" && zip -0 -r -D "${OUT}" ${VM_SRC})
# 同名 CMake 模板必须强制覆盖，不能依赖 zip -u 的文件时间戳。
zip -d "${OUT}" vm/CMakeLists.txt >/dev/null
(cd "${SCRIPT_DIR}" && zip -0 -D "${OUT}" vm/CMakeLists.txt CMakeLists.txt)

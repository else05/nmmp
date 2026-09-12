#ifndef NMMP_NATIVE_VM_H
#define NMMP_NATIVE_VM_H
#include <stdint.h>
#include <stdbool.h>
#include "NativeFormats.h"

typedef struct {
    const uint8_t *code;
    uint32_t size;
    uint64_t key;
    uint8_t opcodes[NMMP_NATIVE_OP_COUNT];
    uint32_t hash;
} NmmpNativeProgram;

#ifdef __cplusplus
extern "C" {
#endif
bool nmmpNativeRun(const NmmpNativeProgram *program, const uint64_t *inputs,
                   uint32_t inputCount, uint64_t *output);
#ifdef __cplusplus
}
#endif
#endif

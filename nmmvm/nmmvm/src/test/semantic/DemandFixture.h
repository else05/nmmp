#pragma once
#include "vm.h"

// Direct records exist only to exercise reader corruption and JNI semantics.
// Production callers use token modules.
typedef struct {
    const u1 *code;
    u4 codeBytes;
    const u1 *tries;
    u4 triesBytes;
    const u1 *boundaries;
    u4 boundariesBytes;
    u4 methodId;
    u8 descriptorTag;
    u4 registersSize;
    u4 insSize;
    u4 codeHash;
    u4 triesHash;
    u4 boundariesHash;
    u4 contextHash;
    int state;
} vmDemandCode;

extern "C" bool vmPrepareDemandCode(JNIEnv *env, vmDemandCode *code);
extern "C" jvalue vmExecuteDemand(JNIEnv *env, const vmDemandCode *code, regptr_t *regs,
                                 u1 *regFlags, u4 registerCapacity, const vmResolver *resolver);

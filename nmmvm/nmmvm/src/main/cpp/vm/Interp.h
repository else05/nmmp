//
// Created by mao on 20-8-17.
//

#ifndef DEX_EDITOR_INTERP_H
#define DEX_EDITOR_INTERP_H

#include <jni.h>
#include "Common.h"
#include "VmReader.h"
s4 dvmInterpHandlePackedSwitch(JNIEnv *env, VmReader &reader, int64_t pc, s4 testVal);
s4 dvmInterpHandleSparseSwitch(JNIEnv *env, VmReader &reader, int64_t pc, s4 testVal);
bool dvmInterpHandleFillArrayData(JNIEnv *env, jarray arrayObj, VmReader &reader, int64_t pc);
#endif

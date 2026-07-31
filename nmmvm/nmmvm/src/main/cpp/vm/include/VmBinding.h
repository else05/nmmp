#ifndef NMMP_VM_BINDING_H
#define NMMP_VM_BINDING_H

#include <jni.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool vmBindingActivate(JNIEnv *env, jobject context);

#ifdef __cplusplus
}
#endif

#endif

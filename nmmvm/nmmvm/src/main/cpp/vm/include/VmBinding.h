#ifndef NMMP_VM_BINDING_H
#define NMMP_VM_BINDING_H

#include <jni.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool vmBindingActivate(JNIEnv *env, jobject context);
bool vmBindingMatchesExpectedIdentity(const char *packageName,
                                      const uint8_t signerDigest[32],
                                      bool signatureBound);

#ifdef __cplusplus
}
#endif

#endif

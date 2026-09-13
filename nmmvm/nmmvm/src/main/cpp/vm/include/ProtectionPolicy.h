#ifndef NMMP_PROTECTION_POLICY_H
#define NMMP_PROTECTION_POLICY_H

#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include "ProtectionPolicyTypes.h"

#ifdef __cplusplus
extern "C" {
#endif
bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context);
bool nmmpProtectionAllowCall(JNIEnv *env);
void nmmpProtectionMarkIntegrityFailure(void);
NmmpProtectionState nmmpProtectionLastState(void);
uint32_t nmmpProtectionLastReasons(void);
#ifdef __cplusplus
}
#endif

#endif

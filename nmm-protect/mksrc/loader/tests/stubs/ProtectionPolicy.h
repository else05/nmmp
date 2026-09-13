#ifndef NMMP_TEST_PROTECTION_POLICY_H
#define NMMP_TEST_PROTECTION_POLICY_H
#include <jni.h>
#include <stdbool.h>
bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context);
bool nmmpProtectionAllowCall(JNIEnv *env);
#endif

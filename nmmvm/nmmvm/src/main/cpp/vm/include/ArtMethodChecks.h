#ifndef NMMP_ART_METHOD_CHECKS_H
#define NMMP_ART_METHOD_CHECKS_H
#include <jni.h>
#include <stdbool.h>
#include "NativeIntegrity.h"
#ifdef __cplusplus
#include "EnvironmentChecks.h"
extern "C" {
#endif
// Calibrate only against a freshly registered, independent generated helper.
// Unsupported ART layouts remain unavailable; jmethodID is never a pointer.
void nmmpArtCalibrate(JNIEnv *env, jclass clazz, const JNINativeMethod *helper);
NmmpNativeIntegrityResult nmmpArtRegister(JNIEnv *env, jclass clazz,
        const JNINativeMethod *method, bool isStatic);
#ifdef __cplusplus
NmmpNativeIntegrityResult nmmpVerifyArtMethods(NmmpCheckStatus *entryOwners);
}
#endif
#endif

#ifndef NMMP_JAVA_ENVIRONMENT_CHECKS_H
#define NMMP_JAVA_ENVIRONMENT_CHECKS_H
#include <jni.h>
#include "EnvironmentChecks.h"

// Absence means only that the named classes were not visible to this loader.
NmmpCheckStatus nmmpCheckFrameworkClasses(JNIEnv *env, jobject context);
NmmpCheckStatus nmmpCheckFrameworkStack(JNIEnv *env);
NmmpCheckStatus nmmpCheckPackageCreator(JNIEnv *env);
#endif

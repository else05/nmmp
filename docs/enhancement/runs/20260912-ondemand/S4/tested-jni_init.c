#include <jni.h>
#include <stdbool.h>
#include <pthread.h>
#include "GlobalCache.h"
#include "VmBinding.h"
#include "VmInit.h"

extern void classes_setup(JNIEnv *env);
extern bool classes_setup_activate(JNIEnv *env);
extern bool classes_setup_finish(JNIEnv *env);
extern void classes2_setup(JNIEnv *env);
extern bool classes2_setup_activate(JNIEnv *env);
extern bool classes2_setup_finish(JNIEnv *env);

static VmInit gNmmpInit = NMMP_VM_INIT;
static VmInit gNmmpRegister = NMMP_VM_INIT;
void nmmp_vm_fail(void) { vmInitFail(&gNmmpInit); }
bool nmmp_vm_failed(void) { return __atomic_load_n(&gNmmpInit.state, __ATOMIC_ACQUIRE) == 3; }
typedef struct { JNIEnv *env; jobject context; } InitArguments;
bool nmmp_vm_is_ready(void) { return __atomic_load_n(&gNmmpInit.state, __ATOMIC_ACQUIRE) == 2; }
bool nmmp_vm_require_ready(void) { return vmInitRequireReady(&gNmmpInit); }
static bool nmmp_initialize(void *argument) {
    InitArguments *args = (InitArguments *) argument;
    JNIEnv *env = args->env;
    if (!vmBindingActivate(env, args->context)) goto failed;
    if (!classes_setup_activate(env) || (*env)->ExceptionCheck(env)) goto failed;
    if (!classes2_setup_activate(env) || (*env)->ExceptionCheck(env)) goto failed;
    return true;
failed:
    return false;
}

static bool nmmp_register_pending(void *argument) {
    JNIEnv *env = (JNIEnv *) argument;
    if (!classes_setup_finish(env)) return false;
    if (!classes2_setup_finish(env)) return false;
    return nmmp_vm_is_ready() && !(*env)->ExceptionCheck(env);
}

bool nmmp_vm_activate(JNIEnv *env, jobject context) {
    InitArguments args = {env, context};
    if (!vmInitRun(&gNmmpInit, nmmp_initialize, &args)) return false;
    if (!vmInitIsOwner(&gNmmpRegister) && !vmInitRun(&gNmmpRegister, nmmp_register_pending, env)) {
        nmmp_vm_fail(); return false;
    }
    return nmmp_vm_is_ready() && !(*env)->ExceptionCheck(env);
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    cacheInitial(env);
    if ((*env)->ExceptionCheck(env)) return JNI_ERR;
    classes_setup(env);
    if ((*env)->ExceptionCheck(env)) return JNI_ERR;
    classes2_setup(env);
    if ((*env)->ExceptionCheck(env)) return JNI_ERR;
    return JNI_VERSION_1_6;
}

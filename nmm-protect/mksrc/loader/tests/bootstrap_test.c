#include "Bootstrap.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int nmmp_inner_bootstrap_v1(JavaVM *, void *, const NmmpHostV1 *, NmmpInnerResultV1 *);
static int stage, pending, binding_calls, mode;
static void check(int ok) { if (!ok) abort(); }
static jboolean exception_check(JNIEnv *env) { (void)env; return pending ? JNI_TRUE : JNI_FALSE; }
static void exception_clear(JNIEnv *env) { (void)env; pending = 0; }
static const struct JNINativeInterface native_table = {.ExceptionCheck=exception_check, .ExceptionClear=exception_clear};
static JNIEnv environment = &native_table;
static jint get_env(JavaVM *vm, void **out, jint version) {
    (void)vm;
    check(version == JNI_VERSION_1_6);
    *out = &environment;
    return mode == 3 ? JNI_ERR : JNI_OK;
}
void cacheInitial(JNIEnv *env) { check(env == &environment && !stage); stage = 1; if (mode == 4) pending = 1; }
void classes_setup(JNIEnv *env) { (void)env; check(stage == 1); stage = 2; if (mode == 5) pending = 1; }
void classes2_setup(JNIEnv *env) { (void)env; check(stage == 2); stage = 3; }
bool vmBindingActivate(JNIEnv *env, jobject context) { check(env == &environment && context); ++binding_calls; return mode != 6; }
bool classes_setup_activate(JNIEnv *env) { (void)env; check(stage == 3); stage = 4; return true; }
bool classes2_setup_activate(JNIEnv *env) { (void)env; check(stage == 4); stage = 5; return true; }
#if NMMP_TEST_BOUND
extern bool nmmp_vm_activate(JNIEnv *, jobject);
extern bool nmmp_vm_is_ready(void);
#endif
int main(int argc, char **argv) {
    check(argc == 2);
    mode = atoi(argv[1]);
    int failed = 0;
    NmmpHostV1 host = {1, sizeof(host), {0}, &failed, &failed};
    NmmpInnerResultV1 result = {1, sizeof(result), {0}, JNI_ERR, -1};
    const struct JNIInvokeInterface vm_table = {.GetEnv=get_env};
    JavaVM vm = &vm_table;
    if (mode == 1) host.struct_size--;
    if (mode == 2) host.build_id[0] = 1;
    int status = nmmp_inner_bootstrap_v1(&vm, NULL, &host, &result);
    if (mode >= 1 && mode <= 5) {
        check(status != 0 && !binding_calls);
        check(stage == (mode < 4 ? 0 : mode == 4 ? 1 : 2));
        check(mode < 4 || pending); /* Original pending JNI exceptions survive bootstrap. */
    } else {
        check(!status && result.jni_version == JNI_VERSION_1_6 && !result.error && stage == 3 && !binding_calls);
#if NMMP_TEST_BOUND
        check(!nmmp_vm_is_ready());
        bool activated = nmmp_vm_activate(&environment, (jobject)&host);
        check(activated == (mode != 6));
        check(nmmp_vm_is_ready() == activated && binding_calls == 1);
        check(nmmp_vm_activate(&environment, (jobject)&host) == activated && binding_calls == 1);
        check(stage == (activated ? 5 : 3));
#endif
    }
    puts("bootstrap ABI, generated JNI branch/order and Context activation checks passed");
    return 0;
}

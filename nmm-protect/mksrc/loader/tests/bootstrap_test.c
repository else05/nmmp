#include "Bootstrap.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int runtime_inner_bootstrap_v1(JavaVM *, void *, const NmmpHostV1 *, NmmpInnerResultV1 *);
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
bool vmBindingActivate(JNIEnv *env, jobject context) {
    check(env == &environment);
#if NMMP_TEST_BOUND
    check(context != NULL);
#else
    check(!context);
#endif
    ++binding_calls;
    return mode != 6;
}
bool nmmpProtectionActivate(const uint8_t *manifest, size_t manifest_size,
                            const uint8_t tag[32], const uint8_t key_xor[32],
                            const uint8_t expected_id[16]) {
    check(manifest && manifest_size && tag && key_xor && expected_id);
    return mode != 7;
}
bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context) {
    check(env == &environment);
    (void)context;
    return mode != 8;
}
bool nmmpProtectionAllowCall(JNIEnv *env) { check(env == &environment); return true; }
bool classes_setup_activate(JNIEnv *env) { (void)env; check(stage == 3); stage = 4; return true; }
bool classes2_setup_activate(JNIEnv *env) { (void)env; check(stage == 4); stage = 5; return true; }
bool classes_setup_finish(JNIEnv *env) { (void)env; check(stage == 5); return true; }
bool classes2_setup_finish(JNIEnv *env) { (void)env; check(stage == 5); return true; }
bool vmCodecActivate(uint64_t mask) { check(!stage && !mask); return true; }
#if NMMP_TEST_BOUND
extern bool nmmp_vm_activate(JNIEnv *, jobject);
extern bool nmmp_vm_is_ready(void);
#endif
int main(int argc, char **argv) {
    check(argc == 2);
    mode = atoi(argv[1]);
    int failed = 0;
    NmmpImageSegment segment = {(uintptr_t)&runtime_inner_bootstrap_v1, 1, 1, NMMP_IMAGE_READ | NMMP_IMAGE_EXEC, 0};
    NmmpHostV1 host = {
            .abi_version = NMMP_PRIVATE_BOOTSTRAP_ABI,
            .struct_size = sizeof(host),
            .outer_anchor = &failed,
            .failure_state = &failed,
            .image_start = (const void *)&runtime_inner_bootstrap_v1,
            .image_size = 1,
            .segments = &segment,
            .segment_count = 1
    };
    NmmpInnerResultV1 result = {
            .abi_version = NMMP_PRIVATE_BOOTSTRAP_ABI,
            .struct_size = sizeof(result),
            .jni_version = JNI_ERR,
            .error = -1
    };
    const struct JNIInvokeInterface vm_table = {.GetEnv=get_env};
    JavaVM vm = &vm_table;
    if (mode == 1) host.struct_size--;
    if (mode == 2) host.build_id[0] = 1;
    int status = runtime_inner_bootstrap_v1(&vm, NULL, &host, &result);
    if (mode >= 1 && mode <= 5) {
        check(status != 0 && !binding_calls);
        check(stage == (mode < 4 ? 0 : mode == 4 ? 1 : 2));
        check(mode < 4 || pending); /* Original pending JNI exceptions survive bootstrap. */
    } else {
#if NMMP_TEST_BOUND
        check(!status && result.jni_version == JNI_VERSION_1_6 && !result.error && stage == 3 && !binding_calls);
#else
        const bool expected = mode != 6 && mode != 7 && mode != 8;
        check((status == 0) == expected);
        check(result.jni_version == (expected ? JNI_VERSION_1_6 : JNI_ERR));
        check(stage == (expected ? 3 : 1) && binding_calls == 1);
#endif
#if NMMP_TEST_BOUND
        check(!nmmp_vm_is_ready());
        bool activated = nmmp_vm_activate(&environment, (jobject)&host);
        check(activated == (mode != 6 && mode != 7 && mode != 8));
        check(nmmp_vm_is_ready() == activated && binding_calls == 1);
        check(nmmp_vm_activate(&environment, (jobject)&host) == activated && binding_calls == 1);
        check(stage == (activated ? 5 : 3));
#endif
    }
    puts("bootstrap ABI, generated JNI branch/order and Context activation checks passed");
    return 0;
}

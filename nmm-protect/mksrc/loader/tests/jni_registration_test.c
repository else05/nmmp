/* Execute generated JNI functions with deterministic JNI/allocation failures. */
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
typedef uint32_t u4;
typedef struct { u4 classIdx, offset, count; } NativeMethodData;
typedef struct { u4 nameIdx, sigIdx, entryId; void *fnPtr; } MyNativeMethod;
static const NativeMethodData gNativeRegisterData[] = {{0, 0, 1}, {0, 0, 9}};
static const MyNativeMethod gNativeMethods[9] = {{0}};
#define NMMP_REGISTER_DATA_COUNT 2u
#define STRING_BY_CLASS_ID(i) "test/Class"
#define STRING_BY_ID(i) "test"
static bool pending, failed, noMemory, missingClass, registrationError, registrationException;
static int allocations, registrations, lookups;
static void nmmp_vm_fail(void) { failed = true; }
static bool nmmp_require_ready(JNIEnv *env) { (void)env; if (failed) pending = true; return !failed; }
static bool nmmp_verify_protected_data(void) { return true; }
static void nmmpProtectionMarkIntegrityFailure(void) { failed = true; }
static bool resolver_init(JNIEnv *env) { (void)env; return true; }
static bool nmmp_prepare_demand(JNIEnv *env) { (void)env; return true; }
static jboolean exceptionCheck(JNIEnv *env) { (void)env; return pending; }
static jclass findClass(JNIEnv *env, const char *name) {
    (void)env; (void)name; CHECK(!pending); ++lookups;
    if (missingClass) { pending = true; return NULL; }
    return (jclass)(uintptr_t)1;
}
static jint registerNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint count) {
    (void)env; CHECK(!pending); CHECK(clazz != NULL); CHECK(methods != NULL); CHECK(count > 0);
    ++registrations;
    if (registrationException) pending = true;
    return registrationError ? JNI_ERR : JNI_OK;
}
static void deleteLocalRef(JNIEnv *env, jobject ref) { (void)env; CHECK(ref != NULL); }
static void *testMalloc(size_t size) {
    if (noMemory) return NULL;
    void *result = malloc(size); CHECK(result); ++allocations; return result;
}
static void testFree(void *pointer) { if (pointer) --allocations; free(pointer); }
#define malloc testMalloc
#define free testFree
#include "register.inc"
#include "unbound_setup.inc"
#include "bound_setup.inc"
#undef malloc
#undef free

int main(int argc, char **argv) {
    CHECK(argc == 2);
#ifdef __ANDROID__
    const struct JNINativeInterface functions = {
#else
    const struct JNINativeInterface_ functions = {
#endif
        .FindClass = findClass, .ExceptionCheck = exceptionCheck,
        .RegisterNatives = registerNatives, .DeleteLocalRef = deleteLocalRef
    };
    JNIEnv env = &functions;
    const char *scenario = argv[1];
    int index = 1;
    if (!strcmp(scenario, "negative")) index = -1;
    else if (!strcmp(scenario, "past-end")) index = 2;
    else if (!strcmp(scenario, "huge")) index = INT32_MAX;
    else if (!strcmp(scenario, "oom")) noMemory = true;
    else if (!strcmp(scenario, "missing-class")) missingClass = true;
    else if (!strcmp(scenario, "register-error")) registrationError = true;
    else if (!strcmp(scenario, "register-exception")) registrationError = registrationException = true;
    else if (!strcmp(scenario, "pending")) pending = true;
    else if (!strcmp(scenario, "small-ok")) index = 0;
    else if (!strcmp(scenario, "large-ok")) index = 1;
    else if (strstr(scenario, "setup-")) {
        missingClass = strstr(scenario, "missing") != NULL;
        registrationError = strstr(scenario, "error") != NULL;
        pending = strstr(scenario, "pending") != NULL;
        if (!strncmp(scenario, "bound-", 6)) bound_setup(&env);
        else unbound_setup(&env);
        CHECK(failed && pending);
        CHECK(registrations == (registrationError ? 1 : 0));
        CHECK(allocations == 0);
        return 0;
    } else CHECK(false);
    fixture_register(&env, (jclass)(uintptr_t)1, index);
    CHECK(allocations == 0);
    if (strstr(scenario, "-ok")) CHECK(!failed && !pending && registrations == 1);
    else {
        CHECK(failed && pending);
        CHECK(registrations == (registrationError ? 1 : 0));
        if (index < 0 || index >= 2 || noMemory || !strcmp(scenario, "pending")) CHECK(lookups == 0);
    }
    return 0;
}

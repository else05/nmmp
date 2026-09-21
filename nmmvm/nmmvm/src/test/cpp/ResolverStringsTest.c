/* Execute the Java-generated resolver with JNI failures, reentry and races. */
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "GlobalCache.h"
#include "VmCodec.h"
#include "DexOpcodes.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); abort(); } } while (0)
static pthread_mutex_t allocationLock = PTHREAD_MUTEX_INITIALIZER;
static int liveAllocations, decodeAllocations, failAllocation = -1;
static __thread int pending;
static int failure;
static bool reenter;
static jstring dvmConstantString(JNIEnv *, u4);
static int globalRefs;

static void *trackedMalloc(size_t size) {
    pthread_mutex_lock(&allocationLock);
    bool fail = failAllocation == 0;
    if (failAllocation >= 0) --failAllocation;
    void *result = fail ? NULL : malloc(size + sizeof(size_t));
    if (result) {
        *(size_t *)result = size;
        ++liveAllocations; ++decodeAllocations;
    }
    pthread_mutex_unlock(&allocationLock);
    return result ? (size_t *)result + 1 : NULL;
}
static void trackedFree(void *pointer) {
    if (!pointer) return;
    size_t *allocation = (size_t *)pointer - 1;
    // The production release must wipe every byte before calling free.
    for (size_t i = 0; i < *allocation; ++i) CHECK(((unsigned char *)pointer)[i] == 0);
    pthread_mutex_lock(&allocationLock);
    --liveAllocations;
    pthread_mutex_unlock(&allocationLock);
    free(allocation);
}
#define malloc trackedMalloc
#define free trackedFree
#include "resolver.inc"
#undef malloc
#undef free
#include "indices.inc"

vmGlobals gVm;
jclass getCacheClass(JNIEnv *env, const char *type) { (void)env; (void)type; return NULL; }
bool nmmpProtectionDigestEqual(const uint8_t a[32], const uint8_t b[32]) { return !memcmp(a, b, 32); }

static jboolean exceptionCheck(JNIEnv *env) { (void)env; return pending != 0; }
static jthrowable exceptionOccurred(JNIEnv *env) { (void)env; return pending ? (jthrowable)(uintptr_t)5 : NULL; }
static void exceptionClear(JNIEnv *env) { (void)env; pending = 0; }
static jint throwObject(JNIEnv *env, jthrowable exception) { (void)env; CHECK(exception); pending = 1; return JNI_OK; }
static jint throwNew(JNIEnv *env, jclass clazz, const char *message) {
    (void)env; (void)clazz; CHECK(message); pending = 1; return JNI_OK;
}
static jclass findClass(JNIEnv *env, const char *name) {
    CHECK(!pending && name && *name);
    CHECK(!strcmp(name, "test/Strings") || !strcmp(name, "java/lang/String"));
    if (reenter) {
        reenter = false;
        CHECK(dvmConstantString(env, TEST_STRING));
        CHECK(!strcmp(name, "test/Strings"));
    }
    if (failure == 1) { pending = 1; return NULL; }
    return (jclass)(uintptr_t)1;
}
static jfieldID getField(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    (void)env; CHECK(!pending && clazz); CHECK(!strcmp(name, "count") && !strcmp(sig, "I"));
    if (failure == 2) { pending = 1; return NULL; }
    return (jfieldID)(uintptr_t)2;
}
static jmethodID getMethod(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    (void)env; CHECK(!pending && clazz);
    CHECK(!strcmp(name, "run") && !strcmp(sig, "([Ljava/lang/String;)Ljava/lang/String;"));
    if (failure == 3) { pending = 1; return NULL; }
    return (jmethodID)(uintptr_t)3;
}
static jobject newGlobal(JNIEnv *env, jobject ref) {
    (void)env; CHECK(!pending && ref);
    if (failure == 4) { pending = 1; return NULL; }
    __atomic_add_fetch(&globalRefs, 1, __ATOMIC_RELAXED);
    return ref;
}
static jobject newLocal(JNIEnv *env, jobject ref) { (void)env; CHECK(!pending && ref); return ref; }
static void deleteGlobal(JNIEnv *env, jobject ref) {
    (void)env; CHECK(ref); __atomic_sub_fetch(&globalRefs, 1, __ATOMIC_RELAXED);
}
static void deleteLocal(JNIEnv *env, jobject ref) { (void)env; CHECK(ref); }
static jstring newString(JNIEnv *env, const char *text) {
    (void)env; CHECK(!pending && text);
    CHECK(!*text || !strcmp(text, (const char *)expectedString));
    if (failure == 5) { pending = 1; return NULL; }
    return (jstring)(uintptr_t)4;
}
static jobjectArray newObjectArray(JNIEnv *env, jsize size, jclass clazz, jobject initial) {
    (void)env; (void)size; (void)initial; CHECK(clazz && !pending);
    if (failure == 6) { pending = 1; return NULL; }
    return (jobjectArray)(uintptr_t)6;
}
static const char *invalidArrayType(JNIEnv *env, u4 idx) {
    (void)env; (void)idx;
    uint8_t value[] = {'[', 'J', 0};
    vmCodecTransform(value, sizeof(value), 1, NMMP_VM_DOMAIN_STRING);
    return vmStringDecode(value, sizeof(value), 1, 0);
}
static void exercise(JNIEnv *env) {
    CHECK(resolver_init(env));
    CHECK(dvmResolveField(env, 0, true));
    const vmMethod *method = dvmResolveMethod(env, 0, true);
    CHECK(method && !strcmp(method->shorty, "LL"));
    CHECK(dvmConstantString(env, TEST_STRING));
    CHECK(dvmConstantString(env, TEST_EMPTY));
    CHECK(dvmResolveClass(env, TEST_CLASS));
    const char *type = dvmResolveTypeUtf(env, TEST_ARRAY);
    CHECK(type && !strcmp(type, "[Ljava/lang/String;"));
    dvmResolver.dvmReleaseTypeUtf(type);
    CHECK(dvmFindClass(env, "Ljava/lang/String;"));
}
static void *worker(void *argument) { exercise((JNIEnv *)argument); return NULL; }

int main(int argc, char **argv) {
    CHECK(argc == 2);
#ifdef __ANDROID__
    const struct JNINativeInterface functions = {
#else
    const struct JNINativeInterface_ functions = {
#endif
        .FindClass = findClass, .ExceptionCheck = exceptionCheck, .ThrowNew = throwNew,
        .ExceptionOccurred = exceptionOccurred, .ExceptionClear = exceptionClear, .Throw = throwObject,
        .GetStaticFieldID = getField, .GetFieldID = getField,
        .GetStaticMethodID = getMethod, .GetMethodID = getMethod,
        .NewGlobalRef = newGlobal, .NewLocalRef = newLocal,
        .DeleteGlobalRef = deleteGlobal, .DeleteLocalRef = deleteLocal, .NewStringUTF = newString,
        .NewObjectArray = newObjectArray
    };
    JNIEnv env = &functions;
    CHECK(vmCodecActivate(0));
    CHECK(nmmp_verify_resolver_manifest(expectedManifest));
    CHECK(resolver_init(&env));
    CHECK(decodeAllocations == 0); // Initialization never decrypts a string.
    uint8_t malformed[] = {'x', 'y'};
    vmCodecTransform(malformed, sizeof(malformed), 1, NMMP_VM_DOMAIN_STRING);
    CHECK(!vmStringDecode(malformed, sizeof(malformed), 1, 0));
    CHECK(liveAllocations == 0);
    if (!strncmp(argv[1], "array-", 6)) {
        failure = !strcmp(argv[1], "array-class-fail") ? 1 : !strcmp(argv[1], "array-new-fail") ? 6 : 0;
        vmResolver resolver = dvmResolver;
        bool invalid = !strcmp(argv[1], "array-invalid");
        if (invalid) resolver.dvmResolveTypeUtf = invalidArrayType;
        for (int filled = invalid ? 1 : 0; filled <= 1; ++filled) {
            u2 normal[] = {OP_NEW_ARRAY | (1u << 12), TEST_ARRAY, OP_RETURN_OBJECT};
            u2 packed[] = {OP_FILLED_NEW_ARRAY, TEST_ARRAY, 0, OP_MOVE_RESULT_OBJECT, OP_RETURN_OBJECT};
            regptr_t regs[2] = {0, 2}; u1 flags[2] = {0};
            vmCode code = {filled ? packed : normal, filled ? 5 : 3, regs, flags, NULL, 0, NULL, 2};
            jvalue value = vmInterpret(&env, &code, &resolver);
            if (failure || invalid) CHECK(pending);
            else CHECK(!pending && value.l);
            CHECK(liveAllocations == 0); // Includes NEW_ARRAY's early failure path.
            pending = 0;
        }
        puts("array type lifetime: PASS");
        return 0;
    } else if (!strcmp(argv[1], "concurrent")) {
        pthread_t threads[12];
        for (unsigned i = 0; i < 12; ++i) CHECK(!pthread_create(&threads[i], NULL, worker, &env));
        for (unsigned i = 0; i < 12; ++i) CHECK(!pthread_join(threads[i], NULL));
    } else if (!strncmp(argv[1], "oom-", 4)) {
        failAllocation = atoi(argv[1] + 4);
        dvmResolveMethod(&env, 0, true);
        failAllocation = -1; pending = 0;
        exercise(&env); // Failed publication must remain retryable.
    } else if (!strncmp(argv[1], "jni-", 4)) {
        failure = atoi(argv[1] + 4);
        if (failure == 2) CHECK(!dvmResolveField(&env, 0, true));
        else if (failure == 5) CHECK(!dvmConstantString(&env, TEST_STRING));
        else CHECK(!dvmResolveMethod(&env, 0, true));
        CHECK(pending && liveAllocations == 0);
        pending = 0; failure = 0;
        exercise(&env);
    } else {
        reenter = true;
        exercise(&env);
    }
    int allocationsBefore = decodeAllocations;
    CHECK(dvmResolveMethod(&env, 0, true));
    CHECK(dvmResolveField(&env, 0, true));
    CHECK(dvmResolveClass(&env, TEST_CLASS));
    CHECK(dvmConstantString(&env, TEST_STRING));
    CHECK(decodeAllocations == allocationsBefore); // Warm calls do not decode.
    CHECK(liveAllocations == 1); // Only the invocation shorty is retained.
    CHECK(nmmp_verify_resolver_manifest(expectedManifest)); // Ciphertext unchanged.
    CHECK(!dvmResolveTypeUtf(&env, UINT32_MAX) && pending);
    pending = 0;
    vmStringRelease(gMethods[0].shorty);
    for (u4 i = 0; i < gClassIds_COUNT; ++i) if (gClassReady[i]) deleteGlobal(&env, gClasses[i]);
    for (u4 i = 0; i < gStringConstantIds_COUNT; ++i) if (gStringReady[i]) deleteGlobal(&env, gStringConstants[i]);
    CHECK(liveAllocations == 0 && globalRefs == 0);
    puts("resolver strings: PASS");
    return 0;
}

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ArtMethodChecks.h"
#include "ModuleOrigins.h"
#include "ProcessMemory.h"
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
#include "CheckLog.h"
#define NMMP_ART_LOG(...) nmmpCheckLog("NMMP_ART", __VA_ARGS__)
#else
#define NMMP_ART_LOG(...) ((void)0)
#endif

namespace {
struct Frame {
    JNIEnv *env;
    bool ready;
    explicit Frame(JNIEnv *value) : env(value), ready(value && !value->ExceptionCheck()
            && value->PushLocalFrame(12) == 0) {}
    ~Frame() { if (ready) env->PopLocalFrame(nullptr); }
};
struct MethodState { uint32_t flags; uintptr_t native, quick; };
struct Entry { uintptr_t method, expected; uint32_t flags; jobject holder; };
static jfieldID artField;
static pthread_mutex_t registryLock = PTHREAD_MUTEX_INITIALIZER;
static const Entry *entries[20];
static size_t entryCount;
static uint32_t requested, incomplete, observedMismatch;

bool readState(uintptr_t address, MethodState &state) {
    // AOSP O/O-MR1, ARM64: flags=4, data_=32, quick entry=40, size=48.
    // Read through the kernel; stale/unmapped metadata must not crash the VM.
    uint8_t bytes[48];
    if (!address || (address & 7) || address > UINTPTR_MAX - sizeof(bytes)) return false;
    iovec local = {bytes, sizeof(bytes)};
    iovec remote = {reinterpret_cast<void *>(address), sizeof(bytes)};
    ssize_t result; unsigned interruptions = 0;
    do { result = nmmpReadSelfMemory(&local, &remote); }
    while (result < 0 && errno == EINTR && ++interruptions <= 64);
    if (result != sizeof(bytes)) return false;
    std::memcpy(&state.flags, bytes + 4, sizeof(state.flags));
    std::memcpy(&state.native, bytes + 32, sizeof(state.native));
    std::memcpy(&state.quick, bytes + 40, sizeof(state.quick));
    return true;
}
bool stableState(uintptr_t address, MethodState &state) {
    MethodState second;
    return readState(address, state) && readState(address, second)
            && state.flags == second.flags && state.native == second.native && state.quick == second.quick;
}
jobject reflected(JNIEnv *env, jclass clazz, const JNINativeMethod *method, bool isStatic) {
    if (!clazz || !method || !method->name || !method->signature || !method->fnPtr) return nullptr;
    jmethodID id = isStatic ? env->GetStaticMethodID(clazz, method->name, method->signature)
                           : env->GetMethodID(clazz, method->name, method->signature);
    return id && !env->ExceptionCheck() ? env->ToReflectedMethod(clazz, id, isStatic) : nullptr;
}
NmmpNativeIntegrityResult unavailable() {
    __atomic_store_n(&incomplete, 1, __ATOMIC_RELEASE);
    NMMP_ART_LOG("registration result=unavailable");
    return NMMP_NATIVE_UNAVAILABLE;
}
NmmpNativeIntegrityResult mismatch() {
    __atomic_store_n(&observedMismatch, 1, __ATOMIC_RELEASE);
    NMMP_ART_LOG("registration result=mismatch");
    return NMMP_NATIVE_MISMATCH;
}
}

void nmmpArtCalibrate(JNIEnv *env, jclass clazz, const JNINativeMethod *helper) {
    if (__atomic_load_n(&artField, __ATOMIC_ACQUIRE)) return;
    Frame frame(env);
    if (!frame.ready || sizeof(uintptr_t) != 8) return;
    jclass build = env->FindClass("android/os/Build$VERSION");
    if (!build || env->ExceptionCheck()) return;
    jfieldID sdkField = env->GetStaticFieldID(build, "SDK_INT", "I");
    if (!sdkField || env->ExceptionCheck()) return;
    const jint sdk = env->GetStaticIntField(build, sdkField);
    if (env->ExceptionCheck() || (sdk != 26 && sdk != 27)) return;
    jclass executable = env->FindClass("java/lang/reflect/Executable");
    jclass missingField = env->ExceptionCheck() ? nullptr : env->FindClass("java/lang/NoSuchFieldError");
    if (!executable || !missingField || env->ExceptionCheck()) return;
    jfieldID field = env->GetFieldID(executable, "artMethod", "J");
    if (env->ExceptionCheck()) {
        jthrowable exception = env->ExceptionOccurred();
        env->ExceptionClear();
        if (exception && !env->IsInstanceOf(exception, missingField)) env->Throw(exception);
        return;
    }
    if (!field) return;
    jobject method = reflected(env, clazz, helper, true);
    if (!method || env->ExceptionCheck()) return;
    const uintptr_t address = static_cast<uintptr_t>(env->GetLongField(method, field));
    MethodState state;
    if (env->ExceptionCheck() || !stableState(address, state) || (state.flags & 0x108u) != 0x108u
            || state.native != reinterpret_cast<uintptr_t>(helper->fnPtr) || !state.quick) return;
    jfieldID expected = nullptr;
    if (__atomic_compare_exchange_n(&artField, &expected, field, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        NMMP_ART_LOG("calibration result=match sdk=%d", sdk);
}

NmmpNativeIntegrityResult nmmpArtRegister(JNIEnv *env, jclass clazz,
        const JNINativeMethod *method, bool isStatic) {
    __atomic_store_n(&requested, 1, __ATOMIC_RELEASE);
    const jfieldID field = __atomic_load_n(&artField, __ATOMIC_ACQUIRE);
    Frame frame(env);
    if (!frame.ready || !field) return unavailable();
    jobject object = reflected(env, clazz, method, isStatic);
    if (!object || env->ExceptionCheck()) return unavailable();
    const uintptr_t address = static_cast<uintptr_t>(env->GetLongField(object, field));
    MethodState state;
    if (env->ExceptionCheck() || !stableState(address, state)) return unavailable();
    const uintptr_t target = reinterpret_cast<uintptr_t>(method->fnPtr);
    const uint32_t flags = isStatic ? 0x108u : 0x100u;
    if ((state.flags & 0x108u) != flags || state.native != target) return mismatch();
    jobject holder = env->NewGlobalRef(object);
    if (!holder || env->ExceptionCheck()) return unavailable();
    // Pin the reflected Method (and its declaring class) so ART cannot unload
    // metadata while a snapshot is read. No JNI calls while holding the lock.
    pthread_mutex_lock(&registryLock);
    for (size_t i = 0; i < entryCount; ++i) {
        const Entry &entry = *entries[i];
        if (entry.method == address) {
            const bool same = entry.expected == target && entry.flags == flags;
            pthread_mutex_unlock(&registryLock);
            env->DeleteGlobalRef(holder);
            return same ? NMMP_NATIVE_MATCH : mismatch();
        }
    }
    void *memory = entryCount < 20 ? mmap(nullptr, sizeof(Entry), PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    if (memory != MAP_FAILED) {
        const Entry entry = {address, target, flags, holder};
        std::memcpy(memory, &entry, sizeof(entry));
        if (mprotect(memory, sizeof(Entry), PROT_READ)) {
            munmap(memory, sizeof(Entry));
            memory = MAP_FAILED;
        } else {
            entries[entryCount++] = static_cast<const Entry *>(memory);
            NMMP_ART_LOG("registration result=match count=%zu", entryCount);
        }
    }
    pthread_mutex_unlock(&registryLock);
    if (memory == MAP_FAILED) { env->DeleteGlobalRef(holder); return unavailable(); }
    return NMMP_NATIVE_MATCH;
}

NmmpNativeIntegrityResult nmmpVerifyArtMethods(NmmpCheckStatus *entryOwners) {
    if (!entryOwners) return NMMP_NATIVE_UNAVAILABLE;
    *entryOwners = NMMP_CHECK_NOT_APPLICABLE;
    if (!__atomic_load_n(&requested, __ATOMIC_ACQUIRE)) return NMMP_NATIVE_NOT_APPLICABLE;
    *entryOwners = NMMP_CHECK_UNKNOWN;
    if (__atomic_load_n(&observedMismatch, __ATOMIC_ACQUIRE)) return NMMP_NATIVE_MISMATCH;
    if (!__atomic_load_n(&artField, __ATOMIC_ACQUIRE)) return NMMP_NATIVE_UNAVAILABLE;
    const Entry *snapshot[20]; size_t count;
    pthread_mutex_lock(&registryLock);
    count = entryCount;
    std::memcpy(snapshot, entries, count * sizeof(snapshot[0]));
    pthread_mutex_unlock(&registryLock);
    if (!count) return NMMP_NATIVE_UNAVAILABLE;
    uintptr_t quick[20] = {};
    NmmpNativeIntegrityResult result = __atomic_load_n(&incomplete, __ATOMIC_ACQUIRE)
            ? NMMP_NATIVE_UNAVAILABLE : NMMP_NATIVE_MATCH;
    for (size_t i = 0; i < count; ++i) {
        MethodState state;
        if (!stableState(snapshot[i]->method, state)) {
            NMMP_ART_LOG("method_index=%zu status=UNAVAILABLE", i);
            result = NMMP_NATIVE_UNAVAILABLE; continue;
        }
        NMMP_ART_LOG("method_index=%zu flags_status=%s jni_status=%s", i,
                     (state.flags & 0x108u) == snapshot[i]->flags ? "PASS" : "MISMATCH",
                     state.native == snapshot[i]->expected ? "PASS" : "MISMATCH");
        if ((state.flags & 0x108u) != snapshot[i]->flags || state.native != snapshot[i]->expected)
            return NMMP_NATIVE_MISMATCH;
        quick[i] = state.quick;
    }
    auto *maps = static_cast<NmmpMapsSnapshot *>(std::malloc(sizeof(NmmpMapsSnapshot)));
    auto *modules = static_cast<NmmpLoadedModules *>(std::malloc(sizeof(NmmpLoadedModules)));
    if (maps && modules) {
        nmmpReadProcessMaps(maps);
        nmmpReadLoadedModules(modules, 0);
        if (maps->status == NMMP_MAPS_COMPLETE && modules->complete) {
            *entryOwners = NMMP_CHECK_PASS;
            for (size_t i = 0; i < count; ++i) {
                const auto owner = nmmpInspectAddress(maps, modules, nullptr, 0, quick[i]);
                NMMP_ART_LOG("method_index=%zu quick_owner=%d permissions=0x%x", i, (int)owner.owner, owner.permissions);
                if (!(owner.permissions & NMMP_MAP_EXEC) || owner.owner == NMMP_OWNER_UNKNOWN) {
                    if (*entryOwners != NMMP_CHECK_SIGNAL) *entryOwners = NMMP_CHECK_UNKNOWN;
                } else if (owner.owner != NMMP_OWNER_ART && owner.owner != NMMP_OWNER_OAT
                        && owner.owner != NMMP_OWNER_JIT) *entryOwners = NMMP_CHECK_SIGNAL;
            }
        }
    }
    std::free(maps); std::free(modules);
    NMMP_ART_LOG("verification result=%d owners=%d count=%zu", result, *entryOwners, count);
    return result;
}

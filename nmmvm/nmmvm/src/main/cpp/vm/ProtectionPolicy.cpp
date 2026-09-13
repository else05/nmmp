#include "ProtectionPolicy.h"

#include "Arm64Syscall.h"
#include "ProtectionManifest.h"
#include "ProtectionPolicyInternal.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <cstring>

namespace {

const size_t kMaximumProcBytes = 64U * 1024U;

static uint32_t gState = NMMP_PROTECTION_UNKNOWN;
static uint32_t gReasons;
static uint64_t gLastCheckNanos;
static uint32_t gInitialized;

static uint64_t monotonicNanos() {
    struct timespec value = {};
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0
           ? static_cast<uint64_t>(value.tv_sec) * UINT64_C(1000000000) + value.tv_nsec
           : 0;
}

static bool readProc(const char *path, char *buffer, size_t capacity, size_t *size) {
    if (!path || !buffer || capacity < 2 || !size) return false;
    const long descriptor = nmmpRawOpenAt(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (descriptor < 0) return false;
    size_t used = 0;
    bool valid = true;
    while (used + 1 < capacity) {
        long count = nmmpRawRead(static_cast<int>(descriptor), buffer + used, capacity - used - 1);
        if (count == -EINTR) continue;
        if (count < 0) { valid = false; break; }
        if (count == 0) break;
        used += static_cast<size_t>(count);
    }
    nmmpRawClose(static_cast<int>(descriptor));
    buffer[used] = '\0';
    *size = used;
    return valid;
}

static bool tracerPid(bool *traced) {
    char buffer[16384];
    size_t size = 0;
    if (!readProc("/proc/self/status", buffer, sizeof(buffer), &size)) return false;
    const char *field = std::strstr(buffer, "TracerPid:");
    if (!field) return false;
    field += sizeof("TracerPid:") - 1;
    while (*field == ' ' || *field == '\t') ++field;
    unsigned value = 0;
    bool digit = false;
    while (*field >= '0' && *field <= '9') {
        digit = true;
        if (value > 1000000U) return false;
        value = value * 10U + static_cast<unsigned>(*field++ - '0');
    }
    *traced = digit && value != 0;
    return digit;
}

static uint8_t lower(uint8_t value) {
    return value >= 'A' && value <= 'Z' ? static_cast<uint8_t>(value + ('a' - 'A')) : value;
}

static bool containsEncoded(const char *data,
                            size_t size,
                            const uint8_t *encoded,
                            size_t encodedSize) {
    if (encodedSize == 0 || encodedSize > size) return false;
    for (size_t i = 0; i + encodedSize <= size; ++i) {
        size_t j = 0;
        while (j < encodedSize && (lower(static_cast<uint8_t>(data[i + j])) ^ 0x5a) == encoded[j]) ++j;
        if (j == encodedSize) return true;
    }
    return false;
}

static bool injectedMaps(bool *injected) {
    char buffer[kMaximumProcBytes + 1];
    size_t size = 0;
    if (!readProc("/proc/self/maps", buffer, sizeof(buffer), &size)) return false;
    static const uint8_t patterns[][9] = {
            {0x3c,0x28,0x33,0x3e,0x3b},
            {0x22,0x2a,0x35,0x29,0x3f,0x3e},
            {0x29,0x2f,0x38,0x29,0x2e,0x28,0x3b,0x2e,0x3f},
            {0x20,0x23,0x3d,0x33,0x29,0x31},
            {0x37,0x3b,0x3d,0x33,0x29,0x31},
            {0x29,0x3b,0x34,0x3e,0x32,0x35,0x35,0x31}
    };
    static const uint8_t lengths[] = {5, 6, 9, 6, 6, 8};
    *injected = false;
    for (size_t i = 0; i < sizeof(lengths); ++i) {
        if (containsEncoded(buffer, size, patterns[i], lengths[i])) {
            *injected = true;
            break;
        }
    }
    std::memset(buffer, 0, sizeof(buffer));
    return true;
}

static bool javaDebugState(JNIEnv *env, bool *debugged) {
    if (!env || env->ExceptionCheck()) return false;
    jclass debugClass = env->FindClass("android/os/Debug");
    if (!debugClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    jmethodID connected = env->GetStaticMethodID(debugClass, "isDebuggerConnected", "()Z");
    jmethodID waiting = env->GetStaticMethodID(debugClass, "waitingForDebugger", "()Z");
    if (!connected || !waiting || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(debugClass);
        return false;
    }
    const bool connectedValue = env->CallStaticBooleanMethod(debugClass, connected);
    bool valid = !env->ExceptionCheck();
    if (!valid) env->ExceptionClear();
    const bool waitingValue = valid && env->CallStaticBooleanMethod(debugClass, waiting);
    valid = valid && !env->ExceptionCheck();
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(debugClass);
    *debugged = valid && (connectedValue || waitingValue);
    return valid;
}

static bool applicationDebuggable(JNIEnv *env, jobject context, bool *debuggable) {
    if (!env || !context || env->ExceptionCheck()) return false;
    jclass contextClass = env->GetObjectClass(context);
    if (!contextClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    jmethodID method = env->GetMethodID(
            contextClass, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    env->DeleteLocalRef(contextClass);
    if (!method || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    jobject info = env->CallObjectMethod(context, method);
    if (!info || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    jclass infoClass = env->GetObjectClass(info);
    jfieldID flags = infoClass ? env->GetFieldID(infoClass, "flags", "I") : nullptr;
    if (!infoClass || !flags || env->ExceptionCheck()) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (infoClass) env->DeleteLocalRef(infoClass);
        env->DeleteLocalRef(info);
        return false;
    }
    *debuggable = (env->GetIntField(info, flags) & 2) != 0;
    const bool valid = !env->ExceptionCheck();
    if (!valid) env->ExceptionClear();
    env->DeleteLocalRef(infoClass);
    env->DeleteLocalRef(info);
    return valid;
}

static void sample(JNIEnv *env, jobject context) {
    const uint32_t flags = nmmpProtectionPolicyFlags();
    NmmpProtectionEvidence evidence = {};
    if (flags & NMMP_POLICY_CHECK_DEBUG) {
        bool traced = false, javaDebugged = false, debuggable = false;
        const bool processValid = tracerPid(&traced);
        const bool javaValid = javaDebugState(env, &javaDebugged);
        const bool appValid = applicationDebuggable(env, context, &debuggable);
        evidence.debugValid = processValid || javaValid || appValid;
        evidence.debugSignal = (processValid && traced) || (javaValid && javaDebugged)
                               || (appValid && debuggable);
    }
    if (flags & NMMP_POLICY_CHECK_MAPS) {
        bool injected = false;
        evidence.mapsValid = injectedMaps(&injected);
        evidence.injectionSignal = evidence.mapsValid && injected;
    }
    const NmmpProtectionDecision result = nmmpProtectionClassify(flags, false, evidence);
    __atomic_store_n(&gReasons, result.reasons, __ATOMIC_RELEASE);
    __atomic_store_n(&gState, result.state, __ATOMIC_RELEASE);
}

static bool decision() {
    const uint32_t state = __atomic_load_n(&gState, __ATOMIC_ACQUIRE);
    const uint32_t reasons = __atomic_load_n(&gReasons, __ATOMIC_ACQUIRE);
    return nmmpProtectionAllowState(nmmpProtectionPolicyFlags(),
                                    static_cast<NmmpProtectionState>(state), reasons);
}

}  // namespace

extern "C" bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context) {
    if (nmmpProtectionRecheckMillis() == 0) return false;
    sample(env, context);
    __atomic_store_n(&gLastCheckNanos, monotonicNanos(), __ATOMIC_RELEASE);
    __atomic_store_n(&gInitialized, 1, __ATOMIC_RELEASE);
    return decision();
}

extern "C" bool nmmpProtectionAllowCall(JNIEnv *env) {
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) return false;
    if (__atomic_load_n(&gState, __ATOMIC_ACQUIRE) == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    const uint64_t now = monotonicNanos();
    const uint64_t previous = __atomic_load_n(&gLastCheckNanos, __ATOMIC_ACQUIRE);
    const uint64_t interval = static_cast<uint64_t>(nmmpProtectionRecheckMillis()) * UINT64_C(1000000);
    if (now != 0 && (now < previous || now - previous >= interval)) {
        uint64_t expected = previous;
        if (__atomic_compare_exchange_n(&gLastCheckNanos, &expected, now, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            sample(env, nullptr);
        }
    }
    return decision();
}

extern "C" void nmmpProtectionMarkIntegrityFailure(void) {
    __atomic_store_n(&gReasons, NMMP_REASON_INTEGRITY, __ATOMIC_RELEASE);
    __atomic_store_n(&gState, NMMP_PROTECTION_INTEGRITY_FAILURE, __ATOMIC_RELEASE);
}

extern "C" NmmpProtectionState nmmpProtectionLastState(void) {
    return static_cast<NmmpProtectionState>(__atomic_load_n(&gState, __ATOMIC_ACQUIRE));
}

extern "C" uint32_t nmmpProtectionLastReasons(void) {
    return __atomic_load_n(&gReasons, __ATOMIC_ACQUIRE);
}

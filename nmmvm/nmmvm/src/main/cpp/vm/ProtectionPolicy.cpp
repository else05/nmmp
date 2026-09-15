#include "ProtectionPolicy.h"

#include "Arm64Syscall.h"
#include "ProtectionManifest.h"
#include "ProtectionPolicyInternal.h"
#include "ProcessMaps.h"
#include "NativeIntegrity.h"
#include "EnvironmentChecks.h"
#include "JavaEnvironmentChecks.h"
#include "ModuleOrigins.h"
#include "OuterIntegrity.h"
#include "ArtMethodChecks.h"
#include "VmCodecConfig.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

namespace {

// Low two bits hold state; the remaining bits hold reasons. Integrity failure
// is terminal: a sampler may never replace it with an earlier observation.
static uint32_t gDecision = NMMP_PROTECTION_UNKNOWN;
static uint64_t gLastCheckNanos;
static uint32_t gInitialized;
static uint32_t gSampling;
// A failed clock read invalidates freshness, not content integrity. Retry only
// the clock until it recovers; one sampler then establishes a fresh result.
static uint32_t gClockUnavailable;
static bool gAppDebugValid;
static bool gAppDebuggable;
static int gNativeIntegrity = NMMP_NATIVE_NOT_APPLICABLE;
static NmmpProtectionEvidence gStartupEnvironment = {};

static void recordCheck(NmmpProtectionEvidence *evidence, uint32_t bit, NmmpCheckStatus status) {
    if (status == NMMP_CHECK_PASS || status == NMMP_CHECK_SIGNAL) evidence->valid |= bit;
    if (status == NMMP_CHECK_SIGNAL) evidence->signals |= bit;
    if (status == NMMP_CHECK_NOT_APPLICABLE) evidence->notApplicable |= bit;
}

static void publish(const NmmpProtectionDecision &result) {
    uint32_t previous = __atomic_load_n(&gDecision, __ATOMIC_ACQUIRE);
    const uint32_t next = (result.reasons << 2U) | result.state;
    while ((previous & 3U) != NMMP_PROTECTION_INTEGRITY_FAILURE) {
        if (__atomic_compare_exchange_n(&gDecision, &previous, next, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    }
}

static void applyArtEvidence(NmmpProtectionDecision *result) {
    NmmpCheckStatus owners;
    const auto art = nmmpVerifyArtMethods(&owners);
    if (art == NMMP_NATIVE_MISMATCH) {
        // Experimental ART observations never become a default integrity
        // latch. Preserve independent debugger restrictions when present.
        result->reasons |= NMMP_REASON_ART_ENTRY;
        if (result->state == NMMP_PROTECTION_CLEAN) result->state = NMMP_PROTECTION_UNKNOWN;
        return;
    }
    if (art == NMMP_NATIVE_UNAVAILABLE || owners == NMMP_CHECK_UNKNOWN) {
        result->reasons |= NMMP_REASON_ART_UNAVAILABLE;
        if (result->state == NMMP_PROTECTION_CLEAN) result->state = NMMP_PROTECTION_UNKNOWN;
    }
    if (owners == NMMP_CHECK_SIGNAL) {
        result->state = NMMP_PROTECTION_SUSPICIOUS;
        result->reasons |= NMMP_REASON_ART_ENTRY;
    }
}

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
    if (valid && used + 1 == capacity) {
        char extra;
        long count;
        do {
            count = nmmpRawRead(static_cast<int>(descriptor), &extra, 1);
        } while (count == -EINTR);
        valid = count == 0;
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

static bool injectedMaps(bool *injected, NmmpCheckStatus *origins) {
    *origins = NMMP_CHECK_UNKNOWN;
    auto *snapshot = static_cast<NmmpMapsSnapshot *>(std::malloc(sizeof(NmmpMapsSnapshot)));
    if (!snapshot) return false;
    nmmpReadProcessMaps(snapshot);
    if (snapshot->status != NMMP_MAPS_COMPLETE) {
        std::free(snapshot);
        return false;
    }
    static const uint8_t patterns[][9] = {
            {0x3c,0x28,0x33,0x3e,0x3b},
            {0x22,0x2a,0x35,0x29,0x3f,0x3e},
            {0x29,0x2f,0x38,0x29,0x2e,0x28,0x3b,0x2e,0x3f},
            {0x20,0x23,0x3d,0x33,0x29,0x31},
            {0x37,0x3b,0x3d,0x33,0x29,0x31},
            {0x29,0x3b,0x34,0x3e,0x32,0x35,0x35,0x31}
    };
    static const uint8_t lengths[] = {5, 6, 9, 6, 6, 8};
    *origins = nmmpCheckModuleOrigins(snapshot);
    *injected = false;
    for (size_t entry = 0; entry < snapshot->count && !*injected; ++entry) {
        const char *name = nmmpMappingName(snapshot, snapshot->entries + entry);
        const size_t size = std::strlen(name);
        for (size_t i = 0; i < sizeof(lengths); ++i) {
            if (containsEncoded(name, size, patterns[i], lengths[i])) {
                *injected = true;
                break;
            }
        }
    }
    std::free(snapshot);
    return true;
}

static bool javaDebugState(JNIEnv *env, bool *debugged) {
    if (!env || env->ExceptionCheck()) return false;
    jclass debugClass = env->FindClass("android/os/Debug");
    if (!debugClass) return false;
    jmethodID connected = env->GetStaticMethodID(debugClass, "isDebuggerConnected", "()Z");
    jmethodID waiting = connected && !env->ExceptionCheck()
            ? env->GetStaticMethodID(debugClass, "waitingForDebugger", "()Z") : nullptr;
    if (!connected || !waiting || env->ExceptionCheck()) {
        env->DeleteLocalRef(debugClass);
        return false;
    }
    const bool connectedValue = env->CallStaticBooleanMethod(debugClass, connected);
    bool valid = !env->ExceptionCheck();
    const bool waitingValue = valid && env->CallStaticBooleanMethod(debugClass, waiting);
    valid = valid && !env->ExceptionCheck();
    env->DeleteLocalRef(debugClass);
    *debugged = valid && (connectedValue || waitingValue);
    return valid;
}

static bool applicationDebuggable(JNIEnv *env, jobject context, bool *debuggable) {
    if (!env || !context || env->ExceptionCheck()) return false;
    jclass contextClass = env->GetObjectClass(context);
    if (!contextClass) return false;
    jmethodID method = env->GetMethodID(
            contextClass, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    env->DeleteLocalRef(contextClass);
    if (!method || env->ExceptionCheck()) {
        return false;
    }
    jobject info = env->CallObjectMethod(context, method);
    if (!info || env->ExceptionCheck()) {
        if (info) env->DeleteLocalRef(info);
        return false;
    }
    jclass infoClass = env->GetObjectClass(info);
    jfieldID flags = infoClass ? env->GetFieldID(infoClass, "flags", "I") : nullptr;
    if (!infoClass || !flags || env->ExceptionCheck()) {
        if (infoClass) env->DeleteLocalRef(infoClass);
        env->DeleteLocalRef(info);
        return false;
    }
    *debuggable = (env->GetIntField(info, flags) & 2) != 0;
    const bool valid = !env->ExceptionCheck();
    env->DeleteLocalRef(infoClass);
    env->DeleteLocalRef(info);
    return valid;
}

static NmmpNativeIntegrityResult verifyImages() {
    const NmmpNativeIntegrityResult inner = nmmpVerifyPrivateImage();
    if (inner == NMMP_NATIVE_UNAVAILABLE || inner == NMMP_NATIVE_MISMATCH) return inner;
#if defined(NMMP_PRIVATE_LINKER) && NMMP_VM_SIGNATURE_BINDING
    return nmmpVerifyOuterImage();
#else
    return inner;
#endif
}

static void sample(JNIEnv *env, jobject context) {
    const uint32_t flags = nmmpProtectionPolicyFlags();
    NmmpProtectionEvidence evidence = {};
    if (flags & NMMP_POLICY_CHECK_ENVIRONMENT) {
        if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) {
            recordCheck(&gStartupEnvironment, NMMP_CHECK_THREADS, nmmpCheckThreadNames());
            recordCheck(&gStartupEnvironment, NMMP_CHECK_PORT_PRIMARY, nmmpProbeLoopbackPort(27042));
            recordCheck(&gStartupEnvironment, NMMP_CHECK_PORT_SECONDARY, nmmpProbeLoopbackPort(27043));
            recordCheck(&gStartupEnvironment, NMMP_CHECK_FRAMEWORK_CLASSES, nmmpCheckFrameworkClasses(env, context));
            recordCheck(&gStartupEnvironment, NMMP_CHECK_PACKAGE_CREATOR, nmmpCheckPackageCreator(env));
        }
        evidence = gStartupEnvironment;
    }
    if (flags & NMMP_POLICY_CHECK_DEBUG) {
        bool traced = false, javaDebugged = false;
        const bool processValid = tracerPid(&traced);
        const bool javaValid = javaDebugState(env, &javaDebugged);
        if (context) gAppDebugValid = applicationDebuggable(env, context, &gAppDebuggable);
        if (processValid) evidence.valid |= NMMP_CHECK_TRACER;
        if (javaValid) evidence.valid |= NMMP_CHECK_JAVA_DEBUG;
        if (gAppDebugValid) evidence.valid |= NMMP_CHECK_APP_DEBUG;
        if (processValid && traced) evidence.signals |= NMMP_CHECK_TRACER;
        if (javaValid && javaDebugged) evidence.signals |= NMMP_CHECK_JAVA_DEBUG;
        if (gAppDebugValid && gAppDebuggable) evidence.signals |= NMMP_CHECK_APP_DEBUG;
    }
    if (flags & NMMP_POLICY_CHECK_MAPS) {
        bool injected = false;
        NmmpCheckStatus origins;
        if (injectedMaps(&injected, &origins)) {
            evidence.valid |= NMMP_CHECK_MAPS;
            if (injected) evidence.signals |= NMMP_CHECK_MAPS;
        }
        recordCheck(&evidence, NMMP_CHECK_MODULE_ORIGINS, origins);
    }
    NmmpProtectionDecision result = nmmpProtectionClassify(flags, false, evidence);
    applyArtEvidence(&result);
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE) || evidence.signals
            || (result.reasons & NMMP_REASON_ART_ENTRY)
            || __atomic_load_n(&gNativeIntegrity, __ATOMIC_ACQUIRE) == NMMP_NATIVE_UNAVAILABLE) {
        const NmmpNativeIntegrityResult native = verifyImages();
        __atomic_store_n(&gNativeIntegrity, native, __ATOMIC_RELEASE);
        if (native == NMMP_NATIVE_MISMATCH) {
            nmmpProtectionMarkIntegrityFailure();
            return;
        }
        if (native == NMMP_NATIVE_UNAVAILABLE) {
            result.state = NMMP_PROTECTION_UNKNOWN;
            result.reasons |= NMMP_REASON_NATIVE_UNAVAILABLE;
        }
    }
    publish(result);
}

static bool decision() {
    if (__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)) return false;
    if (__atomic_load_n(&gNativeIntegrity, __ATOMIC_ACQUIRE) == NMMP_NATIVE_UNAVAILABLE) return false;
    const uint32_t value = __atomic_load_n(&gDecision, __ATOMIC_ACQUIRE);
    return nmmpProtectionAllowState(nmmpProtectionPolicyFlags(),
                                    static_cast<NmmpProtectionState>(value & 3U), value >> 2U);
}

}  // namespace

extern "C" bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context) {
    if (env && env->ExceptionCheck()) return false;
    if (nmmpProtectionRecheckMillis() == 0) return false;
    sample(env, context);
    const uint64_t completed = monotonicNanos();
    __atomic_store_n(&gLastCheckNanos, completed, __ATOMIC_RELEASE);
    __atomic_store_n(&gClockUnavailable, completed == 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gInitialized, 1, __ATOMIC_RELEASE);
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" bool nmmpProtectionAllowCall(JNIEnv *env) {
    if (env && env->ExceptionCheck()) return false;
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) return false;
    if (nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    const uint64_t now = monotonicNanos();
    if (!now) {
        __atomic_store_n(&gClockUnavailable, 1, __ATOMIC_RELEASE);
        return false;
    }
    const uint64_t previous = __atomic_load_n(&gLastCheckNanos, __ATOMIC_ACQUIRE);
    const uint64_t interval = static_cast<uint64_t>(nmmpProtectionRecheckMillis()) * UINT64_C(1000000);
    if (__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)
            || now < previous || now - previous >= interval) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&gSampling, &expected, 1, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            // Another sampler may have completed between the first time check
            // and acquiring ownership. Recheck before performing any I/O.
            const uint64_t completed = __atomic_load_n(&gLastCheckNanos, __ATOMIC_ACQUIRE);
            const uint64_t current = monotonicNanos();
            if (!current) {
                __atomic_store_n(&gClockUnavailable, 1, __ATOMIC_RELEASE);
            } else if (__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)
                    || current < completed || current - completed >= interval) {
                sample(env, nullptr);
                const uint64_t finished = monotonicNanos();
                if (finished) __atomic_store_n(&gLastCheckNanos, finished, __ATOMIC_RELEASE);
                __atomic_store_n(&gClockUnavailable, finished == 0, __ATOMIC_RELEASE);
            }
            __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
        }
    }
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" bool nmmpProtectionVerifySensitiveCall(JNIEnv *env) {
    if (!nmmpProtectionAllowCall(env)) return false;
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&gSampling, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    const NmmpCheckStatus stack = nmmpCheckFrameworkStack(env);
    const NmmpNativeIntegrityResult native = verifyImages();
    __atomic_store_n(&gNativeIntegrity, native, __ATOMIC_RELEASE);
    if (native == NMMP_NATIVE_MISMATCH) {
        nmmpProtectionMarkIntegrityFailure();
    } else {
        const uint32_t previous = __atomic_load_n(&gDecision, __ATOMIC_ACQUIRE);
        NmmpProtectionDecision result = {static_cast<NmmpProtectionState>(previous & 3U), previous >> 2U, false};
        if (stack == NMMP_CHECK_SIGNAL) {
            result.state = NMMP_PROTECTION_SUSPICIOUS;
            result.reasons |= NMMP_REASON_ENVIRONMENT;
        }
        if (native == NMMP_NATIVE_UNAVAILABLE) {
            result.state = NMMP_PROTECTION_UNKNOWN;
            result.reasons |= NMMP_REASON_NATIVE_UNAVAILABLE;
        }
        applyArtEvidence(&result);
        publish(result);
    }
    __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" void nmmpProtectionMarkIntegrityFailure(void) {
    __atomic_store_n(&gDecision, (NMMP_REASON_INTEGRITY << 2U)
                                | NMMP_PROTECTION_INTEGRITY_FAILURE, __ATOMIC_RELEASE);
}

extern "C" NmmpProtectionState nmmpProtectionLastState(void) {
    const auto state = static_cast<NmmpProtectionState>(__atomic_load_n(&gDecision, __ATOMIC_ACQUIRE) & 3U);
    if (state != NMMP_PROTECTION_INTEGRITY_FAILURE
            && __atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)) return NMMP_PROTECTION_UNKNOWN;
    return state;
}

extern "C" uint32_t nmmpProtectionLastReasons(void) {
    const uint32_t value = __atomic_load_n(&gDecision, __ATOMIC_ACQUIRE);
    const uint32_t clockReason = (value & 3U) != NMMP_PROTECTION_INTEGRITY_FAILURE
            && __atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE) ? NMMP_REASON_CLOCK_UNAVAILABLE : 0;
    return (value >> 2U) | clockReason;
}

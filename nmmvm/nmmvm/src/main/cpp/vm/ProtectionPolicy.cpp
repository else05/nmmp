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
#include "ArtRuntimeIntegrity.h"
#include "VmCodecConfig.h"
#include "CheckLog.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <pthread.h>

namespace {

// Low two bits hold state; the remaining bits hold reasons. Integrity failure
// is terminal: a sampler may never replace it with an earlier observation.
static uint32_t gDecision = NMMP_PROTECTION_UNKNOWN;
static uint64_t gLastCheckNanos;
static uint64_t gStartedNanos;
static uint32_t gInitialized;
static uint32_t gSampling;
static uint64_t gRetryNanos;
static JavaVM *gJavaVm;
static NmmpCheckStatus gPendingStack = NMMP_CHECK_NOT_APPLICABLE;
// A failed clock read invalidates freshness, not content integrity. Retry only
// the clock until it recovers; one sampler then establishes a fresh result.
static uint32_t gClockUnavailable;
static bool gAppDebugValid;
static bool gAppDebuggable;
static int gNativeIntegrity = NMMP_NATIVE_NOT_APPLICABLE;
static NmmpProtectionEvidence gStartupEnvironment = {};
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
static uint64_t gDiagnosticRound;
static void logEvidence(const NmmpProtectionEvidence &evidence, uint32_t flags, bool startup) {
    struct Point { uint32_t bit, flag; const char *name; bool cached; };
    static const Point points[] = {
        {NMMP_CHECK_TRACER, NMMP_POLICY_CHECK_DEBUG, "TracerPid", false},
        {NMMP_CHECK_JAVA_DEBUG, NMMP_POLICY_CHECK_DEBUG, "JavaDebugger", false},
        {NMMP_CHECK_APP_DEBUG, NMMP_POLICY_CHECK_DEBUG, "ApplicationDebuggable", true},
        {NMMP_CHECK_MAPS, NMMP_POLICY_CHECK_MAPS, "InjectedMaps", false},
        {NMMP_CHECK_MODULE_ORIGINS, NMMP_POLICY_CHECK_MAPS, "ModuleOrigins", false},
        {NMMP_CHECK_THREADS, NMMP_POLICY_CHECK_ENVIRONMENT, "ThreadNames", false},
        {NMMP_CHECK_PORT_PRIMARY, NMMP_POLICY_CHECK_ENVIRONMENT, "Loopback27042", false},
        {NMMP_CHECK_PORT_SECONDARY, NMMP_POLICY_CHECK_ENVIRONMENT, "Loopback27043", false},
        {NMMP_CHECK_FRAMEWORK_CLASSES, NMMP_POLICY_CHECK_ENVIRONMENT, "FrameworkClasses", true},
        {NMMP_CHECK_PACKAGE_CREATOR, NMMP_POLICY_CHECK_ENVIRONMENT, "PackageCreator", true}
    };
    unsigned executed = 0, cached = 0, disabled = 0, pass = 0, signal = 0, unknown = 0, na = 0;
    for (const auto &point : points) {
        const bool enabled = (flags & point.flag) != 0;
        const char *mode = !enabled ? "DISABLED" : point.cached && !startup ? "CACHED" : "EXECUTED";
        const char *status = !enabled ? "SKIPPED" : evidence.notApplicable & point.bit ? "NOT_APPLICABLE"
                : !(evidence.valid & point.bit) ? "UNKNOWN" : evidence.signals & point.bit ? "SIGNAL" : "PASS";
        if (!enabled) ++disabled;
        else {
            if (point.cached && !startup) ++cached; else ++executed;
            if (evidence.notApplicable & point.bit) ++na;
            else if (!(evidence.valid & point.bit)) ++unknown;
            else if (evidence.signals & point.bit) ++signal; else ++pass;
        }
        NMMP_CHECK_LOG("round=%llu point=%s mode=%s status=%s", (unsigned long long)gDiagnosticRound, point.name, mode, status);
    }
    NMMP_CHECK_LOG("round=%llu environment_points=10 executed=%u cached=%u disabled=%u pass=%u signal=%u unknown=%u not_applicable=%u",
                   (unsigned long long)gDiagnosticRound, executed, cached, disabled, pass, signal, unknown, na);
}
#endif

static uint64_t monotonicNanos() {
    struct timespec value = {};
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0
           ? static_cast<uint64_t>(value.tv_sec) * UINT64_C(1000000000) + value.tv_nsec
           : 0;
}

#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
class NmmpDiagnosticTimer {
public:
    explicit NmmpDiagnosticTimer(const char *point)
            : point_(point), started_(monotonicNanos()) {}

    ~NmmpDiagnosticTimer() {
        const uint64_t finished = monotonicNanos();
        const bool valid = started_ != 0 && finished >= started_;
        const long long elapsed = valid
                ? static_cast<long long>((finished - started_) / UINT64_C(1000)) : -1;
        NMMP_CHECK_LOG("[NMMP_TIMING] round=%llu point=%s elapsed_us=%lld valid=%d",
                       (unsigned long long)gDiagnosticRound, point_, elapsed, valid ? 1 : 0);
    }

private:
    const char *point_;
    uint64_t started_;
};

#define NMMP_DIAGNOSTIC_TIMER_NAME2(prefix, line) prefix##line
#define NMMP_DIAGNOSTIC_TIMER_NAME(prefix, line) NMMP_DIAGNOSTIC_TIMER_NAME2(prefix, line)
#define NMMP_CHECK_TIMER(point) \
    NmmpDiagnosticTimer NMMP_DIAGNOSTIC_TIMER_NAME(nmmpDiagnosticTimer_, __LINE__)(point)
#else
#define NMMP_CHECK_TIMER(point)
#endif

static uint64_t intervalNanos(uint64_t now) {
    const uint64_t start = __atomic_load_n(&gStartedNanos, __ATOMIC_ACQUIRE);
    const uint64_t elapsed = start && now >= start ? now - start : 0;
    return elapsed < UINT64_C(180000000000) ? UINT64_C(5000000000)
            : elapsed < UINT64_C(300000000000) ? UINT64_C(10000000000)
            : UINT64_C(30000000000);
}

static void recordCheck(NmmpProtectionEvidence *evidence, uint32_t bit, NmmpCheckStatus status) {
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    const char *name = bit == NMMP_CHECK_THREADS ? "threads"
            : bit == NMMP_CHECK_PORT_PRIMARY ? "port27042"
            : bit == NMMP_CHECK_PORT_SECONDARY ? "port27043"
            : bit == NMMP_CHECK_FRAMEWORK_CLASSES ? "frameworkClasses"
            : bit == NMMP_CHECK_PACKAGE_CREATOR ? "packageCreator" : "moduleOrigins";
    const char *result = status == NMMP_CHECK_PASS ? "PASS"
            : status == NMMP_CHECK_SIGNAL ? "SIGNAL"
            : status == NMMP_CHECK_UNKNOWN ? "UNKNOWN" : "NOT_APPLICABLE";
    NMMP_CHECK_LOG("check=%s status=%s", name, result);
#endif
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
    NmmpNativeIntegrityResult art;
    {
        NMMP_CHECK_TIMER("ArtMethod");
        art = nmmpVerifyArtMethods(&owners);
    }
    NMMP_CHECK_LOG("point=RegisteredArtMethods status=%d (0=MATCH 1=MISMATCH -1=UNAVAILABLE 2=NOT_APPLICABLE)", (int)art);
    NMMP_CHECK_LOG("point=QuickEntryOwners status=%d (0=PASS 1=SIGNAL 2=UNKNOWN 3=NOT_APPLICABLE)", (int)owners);
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
    {
        NMMP_CHECK_TIMER("ProcessMaps");
        nmmpReadProcessMaps(snapshot);
    }
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
    {
        NMMP_CHECK_TIMER("ModuleOrigins");
        *origins = nmmpCheckModuleOrigins(snapshot);
    }
    {
        NMMP_CHECK_TIMER("InjectedMaps");
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
    NMMP_CHECK_LOG("point=PrivateImage BEGIN child_checks=GOT,PF_X");
    NmmpNativeIntegrityResult inner;
    {
        NMMP_CHECK_TIMER("PrivateImage");
        inner = nmmpVerifyPrivateImage();
    }
    NMMP_CHECK_LOG("point=PrivateImage status=%d (0=MATCH 1=MISMATCH -1=UNAVAILABLE 2=NOT_APPLICABLE)", (int)inner);
    if (inner == NMMP_NATIVE_UNAVAILABLE || inner == NMMP_NATIVE_MISMATCH) {
        NMMP_CHECK_LOG("point=OuterImage status=SKIPPED reason=private_image_failed");
        return inner;
    }
#if defined(NMMP_PRIVATE_LINKER) && NMMP_VM_SIGNATURE_BINDING
    NMMP_CHECK_LOG("point=OuterImage BEGIN child_checks=GOT,PF_X");
    NmmpNativeIntegrityResult outer;
    {
        NMMP_CHECK_TIMER("OuterImage");
        outer = nmmpVerifyOuterImage();
    }
    NMMP_CHECK_LOG("point=OuterImage status=%d (0=MATCH 1=MISMATCH -1=UNAVAILABLE 2=NOT_APPLICABLE)", (int)outer);
    return outer;
#else
    NMMP_CHECK_LOG("point=OuterImage status=NOT_APPLICABLE");
    return inner;
#endif
}

static void sample(JNIEnv *env, jobject context, bool forceNative = false,
                   NmmpCheckStatus stack = NMMP_CHECK_NOT_APPLICABLE) {
    const uint32_t flags = nmmpProtectionPolicyFlags();
    NMMP_CHECK_LOG("round=%llu BEGIN groups=18 (environment=10 ART_entries=3 ArtMethod=1 entryOwners=1 callerStack=1 images=2) flags=0x%x native_codes=0:MATCH,1:MISMATCH,-1:UNAVAILABLE,2:NOT_APPLICABLE",
                   (unsigned long long)++gDiagnosticRound, flags);
    NMMP_CHECK_TIMER("RoundTotal");
    NmmpProtectionEvidence evidence = {};
    if (flags & NMMP_POLICY_CHECK_ENVIRONMENT) {
        if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) {
            NmmpCheckStatus frameworkClasses;
            {
                NMMP_CHECK_TIMER("FrameworkClasses");
                frameworkClasses = nmmpCheckFrameworkClasses(env, context);
            }
            recordCheck(&gStartupEnvironment, NMMP_CHECK_FRAMEWORK_CLASSES, frameworkClasses);
            NmmpCheckStatus packageCreator;
            {
                NMMP_CHECK_TIMER("PackageCreator");
                packageCreator = nmmpCheckPackageCreator(env);
            }
            recordCheck(&gStartupEnvironment, NMMP_CHECK_PACKAGE_CREATOR, packageCreator);
        }
        evidence = gStartupEnvironment;
        NmmpCheckStatus threads;
        {
            NMMP_CHECK_TIMER("ThreadNames");
            threads = nmmpCheckThreadNames();
        }
        recordCheck(&evidence, NMMP_CHECK_THREADS, threads);
        NmmpCheckStatus primaryPort;
        {
            NMMP_CHECK_TIMER("Loopback27042");
            primaryPort = nmmpProbeLoopbackPort(27042);
        }
        recordCheck(&evidence, NMMP_CHECK_PORT_PRIMARY, primaryPort);
        NmmpCheckStatus secondaryPort;
        {
            NMMP_CHECK_TIMER("Loopback27043");
            secondaryPort = nmmpProbeLoopbackPort(27043);
        }
        recordCheck(&evidence, NMMP_CHECK_PORT_SECONDARY, secondaryPort);
    }
    if (flags & NMMP_POLICY_CHECK_DEBUG) {
        bool traced = false, javaDebugged = false;
        bool processValid;
        {
            NMMP_CHECK_TIMER("TracerPid");
            processValid = tracerPid(&traced);
        }
        bool javaValid;
        {
            NMMP_CHECK_TIMER("JavaDebugger");
            javaValid = javaDebugState(env, &javaDebugged);
        }
        if (context) {
            NMMP_CHECK_TIMER("ApplicationDebuggable");
            gAppDebugValid = applicationDebuggable(env, context, &gAppDebuggable);
        }
        if (processValid) evidence.valid |= NMMP_CHECK_TRACER;
        if (javaValid) evidence.valid |= NMMP_CHECK_JAVA_DEBUG;
        if (gAppDebugValid) evidence.valid |= NMMP_CHECK_APP_DEBUG;
        if (processValid && traced) evidence.signals |= NMMP_CHECK_TRACER;
        if (javaValid && javaDebugged) evidence.signals |= NMMP_CHECK_JAVA_DEBUG;
        if (gAppDebugValid && gAppDebuggable) evidence.signals |= NMMP_CHECK_APP_DEBUG;
        NMMP_CHECK_LOG("debug tracer_valid=%d tracer=%d java_valid=%d java=%d app_valid=%d app=%d",
                       processValid, traced, javaValid, javaDebugged, gAppDebugValid, gAppDebuggable);
    }
    if (flags & NMMP_POLICY_CHECK_MAPS) {
        bool injected = false;
        NmmpCheckStatus origins;
        bool mapsValid;
        {
            NMMP_CHECK_TIMER("MapsTotal");
            mapsValid = injectedMaps(&injected, &origins);
        }
        if (mapsValid) {
            evidence.valid |= NMMP_CHECK_MAPS;
            if (injected) evidence.signals |= NMMP_CHECK_MAPS;
        }
        recordCheck(&evidence, NMMP_CHECK_MODULE_ORIGINS, origins);
        NMMP_CHECK_LOG("maps valid=%d injected=%d", (evidence.valid & NMMP_CHECK_MAPS) != 0, injected);
    }
#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
    logEvidence(evidence, flags, !__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE));
#endif
    NMMP_CHECK_LOG("point=CallerStack status=%d (0=PASS 1=SIGNAL 2=UNKNOWN 3=NOT_APPLICABLE)", (int)stack);
    NmmpProtectionDecision result = nmmpProtectionClassify(flags, false, evidence);
    ArtRuntimeReport artRuntime;
    {
        NMMP_CHECK_TIMER("ArtRuntime");
        artRuntime = nmmpCheckArtRuntimeIntegrity();
    }
    NMMP_CHECK_LOG("art_runtime result=%d checked=%u modified=%u external_targets=%u",
                   static_cast<int>(artRuntime.result), artRuntime.checked,
                   artRuntime.modified, artRuntime.externalTargets);
    if (artRuntime.result == ArtIntegrityResult::MODIFIED) {
        result.state = NMMP_PROTECTION_SUSPICIOUS;
        result.reasons |= NMMP_REASON_ART_RUNTIME_MODIFIED;
    } else if (artRuntime.result == ArtIntegrityResult::UNSUPPORTED) {
        result.reasons |= NMMP_REASON_ART_RUNTIME_UNAVAILABLE;
        if (result.state == NMMP_PROTECTION_CLEAN) result.state = NMMP_PROTECTION_UNKNOWN;
    }
    if (stack == NMMP_CHECK_SIGNAL) {
        result.state = NMMP_PROTECTION_SUSPICIOUS;
        result.reasons |= NMMP_REASON_ENVIRONMENT;
    }
    applyArtEvidence(&result);
    if (forceNative || !__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE) || evidence.signals
            || (result.reasons & NMMP_REASON_ART_ENTRY)
            || __atomic_load_n(&gNativeIntegrity, __ATOMIC_ACQUIRE) == NMMP_NATIVE_UNAVAILABLE) {
        NmmpNativeIntegrityResult native;
        {
            NMMP_CHECK_TIMER("NativeImagesTotal");
            native = verifyImages();
        }
        NMMP_CHECK_LOG("native=%d", static_cast<int>(native));
        __atomic_store_n(&gNativeIntegrity, native, __ATOMIC_RELEASE);
        if (native == NMMP_NATIVE_MISMATCH) {
            nmmpProtectionMarkIntegrityFailure();
            NMMP_CHECK_LOG("round=%llu END state=INTEGRITY_FAILURE allowed=0", (unsigned long long)gDiagnosticRound);
            return;
        }
        if (native == NMMP_NATIVE_UNAVAILABLE) {
            result.state = NMMP_PROTECTION_UNKNOWN;
            result.reasons |= NMMP_REASON_NATIVE_UNAVAILABLE;
        }
    }
    publish(result);
    NMMP_CHECK_LOG("round=%llu END state=%d reasons=0x%x (state 0=CLEAN 1=SUSPICIOUS 2=UNKNOWN 3=INTEGRITY_FAILURE)",
                   (unsigned long long)gDiagnosticRound, (int)nmmpProtectionLastState(), nmmpProtectionLastReasons());
    NMMP_CHECK_LOG("sample valid=0x%x signals=0x%x notApplicable=0x%x state=%d reasons=0x%x",
                   evidence.valid, evidence.signals, evidence.notApplicable,
                   static_cast<int>(nmmpProtectionLastState()), nmmpProtectionLastReasons());
}

static bool decision() {
    if (__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)) return false;
    if (__atomic_load_n(&gNativeIntegrity, __ATOMIC_ACQUIRE) == NMMP_NATIVE_UNAVAILABLE) return false;
    const uint32_t value = __atomic_load_n(&gDecision, __ATOMIC_ACQUIRE);
    return nmmpProtectionAllowState(nmmpProtectionPolicyFlags(),
                                    static_cast<NmmpProtectionState>(value & 3U), value >> 2U);
}

static void *runCheck(void *) {
    NMMP_CHECK_TIMER("AsyncWorkerTotal");
    JNIEnv *env = nullptr;
    bool attached = false;
    if (gJavaVm) {
#if defined(__ANDROID__)
        attached = gJavaVm->AttachCurrentThread(&env, nullptr) == JNI_OK;
#else
        attached = gJavaVm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr) == JNI_OK;
#endif
        if (!attached) {
            NMMP_CHECK_LOG("worker attach failed; retaining previous decision");
            __atomic_store_n(&gRetryNanos, monotonicNanos(), __ATOMIC_RELEASE);
            __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
            return nullptr;
        }
    }
    const uint64_t current = monotonicNanos();
    if (current && nmmpProtectionLastState() != NMMP_PROTECTION_INTEGRITY_FAILURE) {
        if (!__atomic_load_n(&gStartedNanos, __ATOMIC_ACQUIRE))
            __atomic_store_n(&gStartedNanos, current, __ATOMIC_RELEASE);
        NMMP_CHECK_LOG("periodic interval_ms=%llu stack=%d",
                       static_cast<unsigned long long>(intervalNanos(current) / 1000000),
                       static_cast<int>(gPendingStack));
        // Native integrity is now checked in every background round. The caller
        // never waits for scans, JNI checks or diagnostic file writes.
        sample(env, nullptr, true, gPendingStack);
        const uint64_t finished = monotonicNanos();
        if (finished) __atomic_store_n(&gLastCheckNanos, finished, __ATOMIC_RELEASE);
        __atomic_store_n(&gClockUnavailable, finished == 0, __ATOMIC_RELEASE);
        NMMP_CHECK_LOG("periodic allowed=%d clock_valid=%d", decision(), finished != 0);
    } else if (!current) {
        __atomic_store_n(&gClockUnavailable, 1, __ATOMIC_RELEASE);
    }
    if (attached) gJavaVm->DetachCurrentThread();
    __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
    return nullptr;
}

static void requestCheck(JNIEnv *env, bool sensitive) {
    const uint64_t now = monotonicNanos();
    if (!now) {
        __atomic_store_n(&gClockUnavailable, 1, __ATOMIC_RELEASE);
        return;
    }
    const uint64_t previous = __atomic_load_n(&gLastCheckNanos, __ATOMIC_ACQUIRE);
    const uint64_t retry = __atomic_load_n(&gRetryNanos, __ATOMIC_ACQUIRE);
    if (retry && now >= retry && now - retry < intervalNanos(now)) return;
    if (!__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)
            && now >= previous && now - previous < intervalNanos(now)) return;
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&gSampling, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    const uint64_t completed = __atomic_load_n(&gLastCheckNanos, __ATOMIC_ACQUIRE);
    const uint64_t current = monotonicNanos();
    if (!current || (!__atomic_load_n(&gClockUnavailable, __ATOMIC_ACQUIRE)
            && current >= completed && current - completed < intervalNanos(current))) {
        if (!current) __atomic_store_n(&gClockUnavailable, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
        return;
    }
    // Stack evidence must come from the business thread, not the worker. Only
    // the winning requester takes this bounded snapshot, once per interval.
    if (sensitive) {
        NMMP_CHECK_TIMER("CallerStack");
        gPendingStack = nmmpCheckFrameworkStack(env);
    } else {
        gPendingStack = NMMP_CHECK_NOT_APPLICABLE;
    }
    if (env && env->ExceptionCheck()) {
        __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
        return;
    }
    pthread_attr_t attributes;
    __atomic_store_n(&gRetryNanos, 0, __ATOMIC_RELEASE);
    int error = pthread_attr_init(&attributes);
    if (!error) {
        error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        pthread_t worker;
        if (!error) error = pthread_create(&worker, &attributes, runCheck, nullptr);
        pthread_attr_destroy(&attributes);
    }
    if (error) {
        __atomic_store_n(&gRetryNanos, current, __ATOMIC_RELEASE);
        __atomic_store_n(&gSampling, 0, __ATOMIC_RELEASE);
        // No synchronous fallback on a business thread.
    }
}

}  // namespace

extern "C" bool nmmpProtectionPolicyInitialize(JNIEnv *env, jobject context) {
    NMMP_CHECK_TIMER("StartupInitialize");
    if (env && env->ExceptionCheck()) return false;
    if (nmmpProtectionRecheckMillis() == 0) return false;
    if (env && env->GetJavaVM(&gJavaVm) != JNI_OK) return false;
    sample(env, context);
    const uint64_t completed = monotonicNanos();
    __atomic_store_n(&gStartedNanos, completed, __ATOMIC_RELEASE);
    __atomic_store_n(&gLastCheckNanos, completed, __ATOMIC_RELEASE);
    __atomic_store_n(&gClockUnavailable, completed == 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gInitialized, 1, __ATOMIC_RELEASE);
    NMMP_CHECK_LOG("startup interval_ms=5000 allowed=%d", decision());
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" bool nmmpProtectionAllowCall(JNIEnv *env) {
    if (env && env->ExceptionCheck()) return false;
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) return false;
    if (nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    requestCheck(env, false);
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" bool nmmpProtectionVerifySensitiveCall(JNIEnv *env) {
    if (env && env->ExceptionCheck()) return false;
    if (!__atomic_load_n(&gInitialized, __ATOMIC_ACQUIRE)) return false;
    if (nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    requestCheck(env, true);
    return (!env || !env->ExceptionCheck()) && decision();
}

extern "C" void nmmpProtectionMarkIntegrityFailure(void) {
    __atomic_store_n(&gDecision, (NMMP_REASON_INTEGRITY << 2U)
                                | NMMP_PROTECTION_INTEGRITY_FAILURE, __ATOMIC_RELEASE);
    NMMP_CHECK_LOG("integrity_failure allowed=0");
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

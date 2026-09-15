#include <ctime>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <type_traits>
#include <atomic>

static uint64_t testNow = UINT64_C(1000000000);
static bool clockFails;
static unsigned failClockAt;
static std::atomic<unsigned> clockCalls{0};
static int testClock(clockid_t, struct timespec *value) {
    if (++clockCalls == failClockAt || clockFails) return -1;
    value->tv_sec = testNow / UINT64_C(1000000000);
    value->tv_nsec = testNow % UINT64_C(1000000000);
    return 0;
}
#define clock_gettime testClock
#include "ProtectionPolicy.cpp"
#undef clock_gettime

static std::string contents = "1000-2000 r-xp 00000000 00:00 0 /system/lib64/libc.so\n";
static size_t position;
static unsigned opens;
static bool pauseRead, reading, resumeRead;
static std::mutex mutex;
static std::condition_variable changed;
long nmmpRawOpenAt(int, const char *path, int, unsigned int) {
    position = 0;
    ++opens;
    return !std::strcmp(path, "/proc/self/status") ? 43 : 42;
}
long nmmpRawRead(int fd, void *buffer, size_t size) {
    if (pauseRead) {
        std::unique_lock<std::mutex> lock(mutex);
        reading = true;
        changed.notify_all();
        changed.wait(lock, [] { return resumeRead; });
    }
    static const std::string status = "TracerPid: 0\n";
    const std::string &source = fd == 43 ? status : contents;
    const size_t count = source.size() - position < size ? source.size() - position : size;
    std::memcpy(buffer, source.data() + position, count);
    position += count;
    return static_cast<long>(count);
}
long nmmpRawClose(int) { return 0; }
static uint32_t policyFlags = NMMP_POLICY_CHECK_MAPS;
static NmmpNativeIntegrityResult nativeResult = NMMP_NATIVE_MATCH;
static unsigned nativeChecks;
NmmpNativeIntegrityResult nmmpVerifyPrivateImage() { ++nativeChecks; return nativeResult; }
static NmmpNativeIntegrityResult artResult = NMMP_NATIVE_NOT_APPLICABLE;
NmmpNativeIntegrityResult nmmpVerifyArtMethods(NmmpCheckStatus *owners) {
    *owners = artResult == NMMP_NATIVE_NOT_APPLICABLE ? NMMP_CHECK_NOT_APPLICABLE : NMMP_CHECK_UNKNOWN;
    return artResult;
}
static unsigned environmentChecks;
NmmpCheckStatus nmmpCheckFrameworkClasses(JNIEnv *, jobject) { return NMMP_CHECK_NOT_APPLICABLE; }
NmmpCheckStatus nmmpCheckPackageCreator(JNIEnv *) { return NMMP_CHECK_NOT_APPLICABLE; }
NmmpCheckStatus nmmpCheckFrameworkStack(JNIEnv *) { return NMMP_CHECK_NOT_APPLICABLE; }
NmmpCheckStatus nmmpCheckModuleOrigins(const NmmpMapsSnapshot *) { return NMMP_CHECK_NOT_APPLICABLE; }
NmmpCheckStatus nmmpCheckThreadNames() { ++environmentChecks; return NMMP_CHECK_SIGNAL; }
NmmpCheckStatus nmmpProbeLoopbackPort(uint16_t port) {
    ++environmentChecks;
    if (port != 27042 && port != 27043) std::abort();
    return NMMP_CHECK_NOT_APPLICABLE;
}
extern "C" uint32_t nmmpProtectionPolicyFlags(void) { return policyFlags; }
extern "C" uint32_t nmmpProtectionRecheckMillis(void) { return 20000; }
static void check(bool value) { if (!value) std::abort(); }
static bool pendingException, failMethod, failCall;
static unsigned methodCalls, booleanCalls, deletedRefs, appReads;
static jboolean JNICALL exceptionCheck(JNIEnv *) { return pendingException; }
static jclass JNICALL findClass(JNIEnv *, const char *) { return reinterpret_cast<jclass>(1); }
static jmethodID JNICALL staticMethod(JNIEnv *, jclass, const char *, const char *) {
    ++methodCalls;
    if (failMethod) { pendingException = true; return nullptr; }
    return reinterpret_cast<jmethodID>(1);
}
static jboolean JNICALL booleanMethod(JNIEnv *, jclass, jmethodID, va_list) {
    ++booleanCalls;
    if (failCall) pendingException = true;
    return false;
}
static void JNICALL deleteRef(JNIEnv *, jobject) { ++deletedRefs; }
static jclass JNICALL objectClass(JNIEnv *, jobject) { return reinterpret_cast<jclass>(1); }
static jmethodID JNICALL method(JNIEnv *, jclass, const char *, const char *) { return reinterpret_cast<jmethodID>(1); }
static jobject JNICALL objectMethod(JNIEnv *, jobject, jmethodID, va_list) { return reinterpret_cast<jobject>(1); }
static jfieldID JNICALL field(JNIEnv *, jclass, const char *, const char *) { return reinterpret_cast<jfieldID>(1); }
static jint JNICALL intField(JNIEnv *, jobject, jfieldID) { ++appReads; return 2; }

int main(int argc, char **argv) {
    check(argc == 2);
    JNIEnv environment = {};
    std::remove_const<std::remove_pointer<decltype(environment.functions)>::type>::type functions = {};
    functions.ExceptionCheck = exceptionCheck;
    functions.FindClass = findClass;
    functions.GetStaticMethodID = staticMethod;
    functions.CallStaticBooleanMethodV = booleanMethod;
    functions.DeleteLocalRef = deleteRef;
    functions.GetObjectClass = objectClass;
    functions.GetMethodID = method;
    functions.CallObjectMethodV = objectMethod;
    functions.GetFieldID = field;
    functions.GetIntField = intField;
    environment.functions = &functions;
    if (!std::strcmp(argv[1], "truncation")) {
        contents.assign(128, 'a');
        char buffer[16];
        size_t size;
        check(!readProc("fixture", buffer, sizeof(buffer), &size));
        contents.assign(15, 'a');
        check(readProc("fixture", buffer, sizeof(buffer), &size) && size == 15);
    } else if (!std::strcmp(argv[1], "integrity-race")) {
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        testNow += UINT64_C(20000000000);
        pauseRead = true;
        std::thread sampler([] { nmmpProtectionAllowCall(nullptr); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [] { return reading; });
        }
        nmmpProtectionMarkIntegrityFailure();
        {
            std::lock_guard<std::mutex> lock(mutex);
            resumeRead = true;
        }
        changed.notify_all();
        sampler.join();
        check(nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE);
        check(nmmpProtectionLastReasons() == NMMP_REASON_INTEGRITY);
        check(!nmmpProtectionAllowCall(nullptr));
    } else if (!std::strcmp(argv[1], "interval")) {
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        unsigned previous = opens;
        testNow += UINT64_C(19999999999);
        check(nmmpProtectionAllowCall(nullptr) && opens == previous);
        ++testNow;
        check(nmmpProtectionAllowCall(nullptr) && opens == previous + 1);
        check(nmmpProtectionAllowCall(nullptr) && opens == previous + 1);
    } else if (!std::strcmp(argv[1], "clock-startup")
            || !std::strcmp(argv[1], "clock-periodic")
            || !std::strcmp(argv[1], "clock-acquired")
            || !std::strcmp(argv[1], "clock-completion")) {
        const bool startup = !std::strcmp(argv[1], "clock-startup");
        clockFails = startup;
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr) == !startup);
        const unsigned previous = opens;
        if (!startup) {
            testNow += UINT64_C(20000000000);
            const unsigned offset = !std::strcmp(argv[1], "clock-periodic") ? 1
                    : !std::strcmp(argv[1], "clock-acquired") ? 2 : 3;
            failClockAt = clockCalls + offset;
            check(!nmmpProtectionAllowCall(nullptr));
            check(opens == previous + (offset == 3 ? 1 : 0));
        }
        check(nmmpProtectionLastState() == NMMP_PROTECTION_UNKNOWN);
        check(nmmpProtectionLastReasons() & NMMP_REASON_CLOCK_UNAVAILABLE);
        clockFails = true;
        const unsigned beforeRecovery = opens;
        for (unsigned i = 0; i < 100; ++i) check(!nmmpProtectionAllowCall(nullptr));
        check(opens == beforeRecovery);
        clockFails = false;
        check(nmmpProtectionAllowCall(nullptr));
        check(opens == beforeRecovery + 1);
        check(nmmpProtectionLastState() == NMMP_PROTECTION_CLEAN);
        check(!(nmmpProtectionLastReasons() & NMMP_REASON_CLOCK_UNAVAILABLE));
        check(nmmpProtectionAllowCall(nullptr) && opens == beforeRecovery + 1);
        nmmpProtectionMarkIntegrityFailure();
        clockFails = true;
        check(!nmmpProtectionAllowCall(nullptr));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE);
        check(nmmpProtectionLastReasons() == NMMP_REASON_INTEGRITY);
    } else if (!std::strcmp(argv[1], "single-sampler")) {
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        const unsigned previous = opens;
        testNow += UINT64_C(20000000000);
        pauseRead = true;
        std::thread sampler([] { nmmpProtectionAllowCall(nullptr); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [] { return reading; });
        }
        // Even a long sample spanning another interval must remain the sole
        // reader. The completion time, not its start, begins the next interval.
        testNow += UINT64_C(20000000000);
        for (unsigned i = 0; i < 100; ++i) check(nmmpProtectionAllowCall(nullptr));
        check(opens == previous + 1);
        {
            std::lock_guard<std::mutex> lock(mutex);
            resumeRead = true;
        }
        changed.notify_all();
        sampler.join();
        pauseRead = false;
        testNow += UINT64_C(19999999999);
        check(nmmpProtectionAllowCall(nullptr) && opens == previous + 1);
        ++testNow;
        check(nmmpProtectionAllowCall(nullptr) && opens == previous + 2);
        check(nmmpProtectionAllowCall(nullptr) && opens == previous + 2);
    } else if (!std::strcmp(argv[1], "maps-tail")) {
        contents.clear();
        for (unsigned i = 1; i < 2000; ++i) {
            char line[160];
            std::snprintf(line, sizeof(line), "%x-%x r-xp 00000000 00:00 0 /system/lib64/%s\n",
                          i * 4096, (i + 1) * 4096, i == 1999 ? "FrIdA-agent.so" : "libfixture.so");
            contents += line;
        }
        check(contents.size() > 65536);
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nmmpProtectionLastReasons() == NMMP_REASON_INJECTION);
    } else if (!std::strcmp(argv[1], "maps-invalid")) {
        contents = "not a maps record\n";
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_UNKNOWN);
    } else if (!std::strcmp(argv[1], "jni-pending")) {
        pendingException = true;
        bool debugged = false;
        check(!javaDebugState(&environment, &debugged));
        check(!nmmpProtectionPolicyInitialize(&environment, nullptr));
        check(!nmmpProtectionAllowCall(&environment));
        check(pendingException && methodCalls == 0 && booleanCalls == 0 && opens == 0);
    } else if (!std::strcmp(argv[1], "jni-method-error")) {
        failMethod = true;
        bool debugged = false;
        check(!javaDebugState(&environment, &debugged));
        check(pendingException && methodCalls == 1 && booleanCalls == 0 && deletedRefs == 1);
    } else if (!std::strcmp(argv[1], "jni-call-error")) {
        failCall = true;
        bool debugged = false;
        check(!javaDebugState(&environment, &debugged));
        check(pendingException && methodCalls == 2 && booleanCalls == 1 && deletedRefs == 1);
    } else if (!std::strcmp(argv[1], "app-debug-retained")) {
        policyFlags = NMMP_POLICY_CHECK_DEBUG;
        check(nmmpProtectionPolicyInitialize(&environment, reinterpret_cast<jobject>(1)));
        check(nmmpProtectionLastReasons() == NMMP_REASON_DEBUG && appReads == 1);
        testNow += UINT64_C(20000000000);
        check(nmmpProtectionAllowCall(&environment));
        check(nmmpProtectionLastReasons() == NMMP_REASON_DEBUG && appReads == 1);
    } else if (!std::strcmp(argv[1], "environment-startup")) {
        policyFlags = NMMP_POLICY_CHECK_ENVIRONMENT | NMMP_POLICY_ENFORCE;
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nmmpProtectionLastReasons() == NMMP_REASON_ENVIRONMENT && environmentChecks == 3);
        check(nativeChecks == 1);
        testNow += UINT64_C(20000000000);
        check(nmmpProtectionAllowCall(nullptr));
        check(environmentChecks == 3 && nativeChecks == 2);
    } else if (!std::strcmp(argv[1], "art-diagnostic")) {
        artResult = NMMP_NATIVE_MISMATCH;
        policyFlags |= NMMP_POLICY_ENFORCE;
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_UNKNOWN);
        check(nmmpProtectionLastReasons() & NMMP_REASON_ART_ENTRY);
        check(nmmpProtectionVerifySensitiveCall(nullptr));
        artResult = NMMP_NATIVE_UNAVAILABLE;
        testNow += UINT64_C(20000000000);
        check(nmmpProtectionAllowCall(nullptr));
        check(nmmpProtectionLastReasons() & NMMP_REASON_ART_UNAVAILABLE);
    } else if (!std::strcmp(argv[1], "art-debug-preserved")) {
        policyFlags = NMMP_POLICY_CHECK_DEBUG | NMMP_POLICY_ENFORCE;
        artResult = NMMP_NATIVE_UNAVAILABLE;
        check(!nmmpProtectionPolicyInitialize(&environment, reinterpret_cast<jobject>(1)));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_SUSPICIOUS);
        check((nmmpProtectionLastReasons() & (NMMP_REASON_DEBUG | NMMP_REASON_ART_UNAVAILABLE))
                == (NMMP_REASON_DEBUG | NMMP_REASON_ART_UNAVAILABLE));
        check(!nmmpProtectionAllowCall(&environment));
    } else if (!std::strcmp(argv[1], "native-mismatch")) {
        check(nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nativeChecks == 1);
        nativeResult = NMMP_NATIVE_MISMATCH;
        contents = "1000-2000 r-xp 0 00:00 0 /fixture/frida.so\n";
        testNow += UINT64_C(20000000000);
        check(!nmmpProtectionAllowCall(nullptr));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_INTEGRITY_FAILURE && nativeChecks == 2);
        nativeResult = NMMP_NATIVE_MATCH;
        check(!nmmpProtectionAllowCall(nullptr) && nativeChecks == 2);
    } else if (!std::strcmp(argv[1], "native-unavailable")) {
        nativeResult = NMMP_NATIVE_UNAVAILABLE;
        check(!nmmpProtectionPolicyInitialize(nullptr, nullptr));
        check(nmmpProtectionLastState() == NMMP_PROTECTION_UNKNOWN);
        check(nmmpProtectionLastReasons() & NMMP_REASON_NATIVE_UNAVAILABLE);
        check(!nmmpProtectionAllowCall(nullptr));
    } else {
        return 2;
    }
}

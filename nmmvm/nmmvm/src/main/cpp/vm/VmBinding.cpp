#include "VmBinding.h"

#include "VmInit.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

#include "ApkV2Signer.h"
#include "Arm64Syscall.h"
#include "Sha256.h"
#include "VmCodec.h"
#include "PrivateLoaderState.h"

namespace {

const size_t kSignerDigestSize = 32;
const size_t kMaximumCertificateSize = 1024U * 1024U;
const uint8_t kSignerDigestXorMask[kSignerDigestSize] = {
        0x6d, 0x21, 0xa7, 0x4c, 0xf3, 0x98, 0x0b, 0xd5,
        0x7e, 0xc2, 0x36, 0x91, 0x5a, 0xe8, 0x44, 0xbf,
        0x13, 0x79, 0xcd, 0x02, 0xa6, 0x5f, 0xe1, 0x88,
        0x3c, 0xb4, 0x69, 0xd7, 0x20, 0xfa, 0x95, 0x4e
};

static VmInit gBindingInit = NMMP_VM_INIT;

static uint64_t updateHash(uint64_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ data[i]) * UINT64_C(0x100000001b3);
    }
    return hash;
}

static uint64_t mix64(uint64_t value) {
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

static void secureZero(void *memory, size_t size) {
    volatile uint8_t *data = static_cast<volatile uint8_t *>(memory);
    for (size_t i = 0; i < size; ++i) {
        data[i] = 0;
    }
}

static bool constantTimeEqual(const uint8_t *left, const uint8_t *right, size_t size) {
    uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

static void decodeExpectedSignerDigest(uint8_t digest[kSignerDigestSize]) {
    const volatile uint8_t *encodedDigest = NMMP_VM_EXPECTED_SIGNER_XOR;
    for (size_t i = 0; i < kSignerDigestSize; ++i) {
        digest[i] = encodedDigest[i] ^ kSignerDigestXorMask[i];
    }
}

static jint sdkInt(JNIEnv *env) {
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass == nullptr) return -1;
    jfieldID field = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (field == nullptr) {
        env->DeleteLocalRef(versionClass);
        return -1;
    }
    const jint value = env->GetStaticIntField(versionClass, field);
    env->DeleteLocalRef(versionClass);
    return value;
}

static jobject getPackageInfo(JNIEnv *env,
                              jobject packageManager,
                              jstring packageName,
                              jint flags) {
    jclass managerClass = env->GetObjectClass(packageManager);
    if (managerClass == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(
            managerClass,
            "getPackageInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    env->DeleteLocalRef(managerClass);
    if (method == nullptr) return nullptr;
    return env->CallObjectMethod(packageManager, method, packageName, flags);
}

static jobjectArray currentSignatures(JNIEnv *env,
                                      jobject packageManager,
                                      jstring packageName) {
    jobject packageInfo = getPackageInfo(
            env, packageManager, packageName, 0x00000040);
    if (packageInfo == nullptr) return nullptr;

    jclass infoClass = env->GetObjectClass(packageInfo);
    if (infoClass == nullptr) {
        env->DeleteLocalRef(packageInfo);
        return nullptr;
    }
    jfieldID field = env->GetFieldID(
            infoClass, "signatures", "[Landroid/content/pm/Signature;");
    jobjectArray result = field == nullptr ? nullptr : static_cast<jobjectArray>(
            env->GetObjectField(packageInfo, field));
    env->DeleteLocalRef(infoClass);
    env->DeleteLocalRef(packageInfo);
    return result;
}

static jobject packageManager(JNIEnv *env, jobject context) {
    jclass contextClass = env->GetObjectClass(context);
    if (contextClass == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(
            contextClass,
            "getPackageManager",
            "()Landroid/content/pm/PackageManager;");
    env->DeleteLocalRef(contextClass);
    return method == nullptr ? nullptr : env->CallObjectMethod(context, method);
}

static jstring packageName(JNIEnv *env, jobject context) {
    jclass contextClass = env->GetObjectClass(context);
    if (contextClass == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(
            contextClass, "getPackageName", "()Ljava/lang/String;");
    env->DeleteLocalRef(contextClass);
    return method == nullptr ? nullptr : static_cast<jstring>(
            env->CallObjectMethod(context, method));
}

static jbyteArray certificateBytes(JNIEnv *env, jobject signature) {
    jclass signatureClass = env->GetObjectClass(signature);
    if (signatureClass == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(signatureClass, "toByteArray", "()[B");
    env->DeleteLocalRef(signatureClass);
    return method == nullptr ? nullptr : static_cast<jbyteArray>(
            env->CallObjectMethod(signature, method));
}

static bool copyCertificate(JNIEnv *env,
                            jbyteArray certificate,
                            uint8_t **certificateData,
                            size_t *certificateSize) {
    const jsize size = env->GetArrayLength(certificate);
    if (size <= 0 || static_cast<size_t>(size) > kMaximumCertificateSize) return false;
    uint8_t *data = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(size)));
    if (data == nullptr) return false;
    env->GetByteArrayRegion(
            certificate, 0, size, reinterpret_cast<jbyte *>(data));
    if (env->ExceptionCheck()) {
        secureZero(data, static_cast<size_t>(size));
        std::free(data);
        return false;
    }
    *certificateData = data;
    *certificateSize = static_cast<size_t>(size);
    return true;
}

static uint64_t deriveBindingMask(const char *packageNameValue,
                                  const uint8_t *certificate,
                                  size_t certificateSize) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    hash = updateHash(
            hash,
            reinterpret_cast<const uint8_t *>(packageNameValue),
            std::strlen(packageNameValue));
    const uint8_t separator = 0;
    hash = updateHash(hash, &separator, 1);
    hash = updateHash(hash, certificate, certificateSize);
    uint8_t buildIdBytes[8];
    for (uint32_t i = 0; i < 8; ++i) {
        buildIdBytes[i] = static_cast<uint8_t>(NMMP_VM_BUILD_ID >> (i * 8U));
    }
    hash = updateHash(hash, buildIdBytes, sizeof(buildIdBytes));
    secureZero(buildIdBytes, sizeof(buildIdBytes));
    return mix64(hash);
}

static bool verifyApkAndActivate(const char *packageNameValue,
                                 const uint8_t *packageManagerCertificate,
                                 size_t packageManagerCertificateSize) {
#if !defined(__aarch64__)
    (void) packageNameValue;
    (void) packageManagerCertificate;
    (void) packageManagerCertificateSize;
    return false;
#else
    uint8_t expectedDigest[kSignerDigestSize];
    uint8_t packageManagerDigest[kSignerDigestSize];
    uint8_t apkDigest[kSignerDigestSize];
    decodeExpectedSignerDigest(expectedDigest);
    nmmpSha256(
            packageManagerCertificate, packageManagerCertificateSize, packageManagerDigest);
    if (!constantTimeEqual(
            expectedDigest, packageManagerDigest, kSignerDigestSize)) {
        secureZero(expectedDigest, sizeof(expectedDigest));
        secureZero(packageManagerDigest, sizeof(packageManagerDigest));
        return false;
    }

    char apkPath[4096];
    if (!nmmpFindMappedBaseApk(packageNameValue, apkPath, sizeof(apkPath))) {
        secureZero(apkPath, sizeof(apkPath));
        secureZero(expectedDigest, sizeof(expectedDigest));
        secureZero(packageManagerDigest, sizeof(packageManagerDigest));
        return false;
    }

    const long descriptor = nmmpRawOpenAt(
            AT_FDCWD, apkPath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW, 0);
    secureZero(apkPath, sizeof(apkPath));
    if (descriptor < 0) {
        secureZero(expectedDigest, sizeof(expectedDigest));
        secureZero(packageManagerDigest, sizeof(packageManagerDigest));
        return false;
    }

    struct stat status = {};
    NmmpSignerCertificate apkCertificate = {nullptr, 0};
    const bool parsed = nmmpRawFstat(static_cast<int>(descriptor), &status) == 0
                        && S_ISREG(status.st_mode)
                        && status.st_size > 0
                        && nmmpReadApkV2SignerCertificate(
                                static_cast<int>(descriptor),
                                static_cast<uint64_t>(status.st_size),
                                &apkCertificate);
    nmmpRawClose(static_cast<int>(descriptor));
    if (!parsed) {
        nmmpFreeSignerCertificate(&apkCertificate);
        secureZero(&status, sizeof(status));
        secureZero(expectedDigest, sizeof(expectedDigest));
        secureZero(packageManagerDigest, sizeof(packageManagerDigest));
        return false;
    }

    nmmpSha256(apkCertificate.data, apkCertificate.size, apkDigest);
    const bool matched = constantTimeEqual(
            expectedDigest, apkDigest, kSignerDigestSize)
                         && constantTimeEqual(
            packageManagerDigest, apkDigest, kSignerDigestSize);
    uint64_t bindingMask = matched
                                 ? deriveBindingMask(
                                         packageNameValue,
                                         apkCertificate.data,
                                         apkCertificate.size)
                                 : 0;
    nmmpFreeSignerCertificate(&apkCertificate);
    secureZero(&status, sizeof(status));
    secureZero(expectedDigest, sizeof(expectedDigest));
    secureZero(packageManagerDigest, sizeof(packageManagerDigest));
    secureZero(apkDigest, sizeof(apkDigest));
    const bool activated = matched && vmCodecActivate(bindingMask);
    secureZero(&bindingMask, sizeof(bindingMask));
    return activated;
#endif
}

}  // namespace

extern "C"
bool vmBindingMatchesExpectedIdentity(const char *packageNameValue,
                                      const uint8_t signerDigest[32],
                                      bool signatureBound) {
    if (!packageNameValue || !signerDigest
            || signatureBound != (NMMP_VM_SIGNATURE_BINDING != 0)
            || std::strcmp(packageNameValue, NMMP_VM_PACKAGE_NAME) != 0) return false;
    if (!signatureBound) {
        uint8_t zero[kSignerDigestSize] = {};
        const bool matched = constantTimeEqual(zero, signerDigest, sizeof(zero));
        secureZero(zero, sizeof(zero));
        return matched;
    }
    uint8_t expected[kSignerDigestSize];
    decodeExpectedSignerDigest(expected);
    const bool matched = constantTimeEqual(expected, signerDigest, sizeof(expected));
    secureZero(expected, sizeof(expected));
    return matched;
}

struct BindingArguments { JNIEnv *env; jobject context; };
static bool initializeBinding(void *argument) {
    auto *args = static_cast<BindingArguments *>(argument);
    JNIEnv *env = args->env;
    jobject context = args->context;
#if !NMMP_VM_SIGNATURE_BINDING
    (void)env; (void)context;
    return vmCodecActivate(0);
#else
    if (!context) return false;
    jstring name = nullptr;
    const char *nameChars = nullptr;
    jobject manager = nullptr;
    jobjectArray signatures = nullptr;
    jobject signature = nullptr;
    jbyteArray certificate = nullptr;
    uint8_t *certificateData = nullptr;
    size_t certificateSize = 0;
    bool result = false;

    const jint apiLevel = sdkInt(env);
    if ((apiLevel != 26 && apiLevel != 27) || env->ExceptionCheck()) goto cleanup;

    name = packageName(env, context);
    if (name == nullptr) goto cleanup;
    nameChars = env->GetStringUTFChars(name, nullptr);
    if (nameChars == nullptr
        || std::strcmp(nameChars, NMMP_VM_PACKAGE_NAME) != 0) {
        goto cleanup;
    }

    manager = packageManager(env, context);
    if (manager == nullptr) goto cleanup;
    signatures = currentSignatures(env, manager, name);
    if (signatures == nullptr
        || env->GetArrayLength(signatures) != 1
        || env->ExceptionCheck()) {
        goto cleanup;
    }

    signature = env->GetObjectArrayElement(signatures, 0);
    if (signature == nullptr) goto cleanup;
    certificate = certificateBytes(env, signature);
    if (certificate == nullptr
        || !copyCertificate(
                env, certificate, &certificateData, &certificateSize)) {
        goto cleanup;
    }
    result = verifyApkAndActivate(nameChars, certificateData, certificateSize);

cleanup:
    if (certificateData != nullptr) {
        secureZero(certificateData, certificateSize);
        std::free(certificateData);
    }
    if (certificate != nullptr) env->DeleteLocalRef(certificate);
    if (signature != nullptr) env->DeleteLocalRef(signature);
    if (signatures != nullptr) env->DeleteLocalRef(signatures);
    if (manager != nullptr) env->DeleteLocalRef(manager);
    if (nameChars != nullptr) env->ReleaseStringUTFChars(name, nameChars);
    if (name != nullptr) env->DeleteLocalRef(name);
    if (env->ExceptionCheck()) result = false;
    return result;
#endif
}

extern "C"
bool vmBindingActivate(JNIEnv *env, jobject context) {
#if defined(NMMP_PRIVATE_LINKER)
    if (nmmpPrivateLoaderFailed()) return false;
#endif
    BindingArguments arguments = {env, context};
    return vmInitRun(&gBindingInit, initializeBinding, &arguments) && vmCodecIsActivated();
}

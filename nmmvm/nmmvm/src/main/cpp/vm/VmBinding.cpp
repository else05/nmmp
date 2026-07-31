#include "VmBinding.h"

#include <cstring>

#include "VmCodec.h"

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

static bool fail(JNIEnv *env) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return false;
}

static jint sdkInt(JNIEnv *env) {
    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (versionClass == nullptr) return -1;
    jfieldID field = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (field == nullptr) return -1;
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
                                      jstring packageName,
                                      jint apiLevel) {
    const jint flags = apiLevel >= 28 ? 0x08000000 : 0x00000040;
    jobject packageInfo = getPackageInfo(env, packageManager, packageName, flags);
    if (packageInfo == nullptr) return nullptr;

    jclass infoClass = env->GetObjectClass(packageInfo);
    if (infoClass == nullptr) return nullptr;
    if (apiLevel < 28) {
        jfieldID field = env->GetFieldID(
                infoClass, "signatures", "[Landroid/content/pm/Signature;");
        jobjectArray result = field == nullptr ? nullptr : static_cast<jobjectArray>(
                env->GetObjectField(packageInfo, field));
        env->DeleteLocalRef(infoClass);
        env->DeleteLocalRef(packageInfo);
        return result;
    }

    jfieldID field = env->GetFieldID(
            infoClass, "signingInfo", "Landroid/content/pm/SigningInfo;");
    jobject signingInfo = field == nullptr ? nullptr : env->GetObjectField(packageInfo, field);
    env->DeleteLocalRef(infoClass);
    env->DeleteLocalRef(packageInfo);
    if (signingInfo == nullptr) return nullptr;

    jclass signingInfoClass = env->GetObjectClass(signingInfo);
    if (signingInfoClass == nullptr) return nullptr;
    jmethodID method = env->GetMethodID(
            signingInfoClass,
            "getApkContentsSigners",
            "()[Landroid/content/pm/Signature;");
    jobjectArray result = method == nullptr ? nullptr : static_cast<jobjectArray>(
            env->CallObjectMethod(signingInfo, method));
    env->DeleteLocalRef(signingInfoClass);
    env->DeleteLocalRef(signingInfo);
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

static bool activateWith(JNIEnv *env, jstring packageNameValue, jbyteArray certificate) {
    const char *packageChars = env->GetStringUTFChars(packageNameValue, nullptr);
    if (packageChars == nullptr) return fail(env);
    if (std::strcmp(packageChars, NMMP_VM_PACKAGE_NAME) != 0) {
        env->ReleaseStringUTFChars(packageNameValue, packageChars);
        return false;
    }

    jbyte *certificateData = env->GetByteArrayElements(certificate, nullptr);
    if (certificateData == nullptr) {
        env->ReleaseStringUTFChars(packageNameValue, packageChars);
        return fail(env);
    }
    const jsize certificateSize = env->GetArrayLength(certificate);

    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    hash = updateHash(hash, reinterpret_cast<const uint8_t *>(packageChars),
                      std::strlen(packageChars));
    const uint8_t separator = 0;
    hash = updateHash(hash, &separator, 1);
    hash = updateHash(hash, reinterpret_cast<const uint8_t *>(certificateData),
                      static_cast<size_t>(certificateSize));
    uint8_t buildIdBytes[8];
    for (uint32_t i = 0; i < 8; ++i) {
        buildIdBytes[i] = static_cast<uint8_t>(NMMP_VM_BUILD_ID >> (i * 8U));
    }
    hash = updateHash(hash, buildIdBytes, sizeof(buildIdBytes));

    env->ReleaseByteArrayElements(certificate, certificateData, JNI_ABORT);
    env->ReleaseStringUTFChars(packageNameValue, packageChars);
    return vmCodecActivate(mix64(hash));
}

extern "C"
bool vmBindingActivate(JNIEnv *env, jobject context) {
#if !NMMP_VM_SIGNATURE_BINDING
    (void) env;
    (void) context;
    return true;
#else
    if (context == nullptr) return false;
    jstring name = packageName(env, context);
    jobject manager = packageManager(env, context);
    const jint apiLevel = sdkInt(env);
    if (name == nullptr || manager == nullptr || apiLevel < 0) return fail(env);

    jobjectArray signatures = currentSignatures(env, manager, name, apiLevel);
    env->DeleteLocalRef(manager);
    if (signatures == nullptr || env->GetArrayLength(signatures) != 1) return fail(env);

    jobject signature = env->GetObjectArrayElement(signatures, 0);
    env->DeleteLocalRef(signatures);
    if (signature == nullptr) return fail(env);
    jbyteArray certificate = certificateBytes(env, signature);
    env->DeleteLocalRef(signature);
    if (certificate == nullptr) return fail(env);

    const bool result = activateWith(env, name, certificate);
    env->DeleteLocalRef(certificate);
    env->DeleteLocalRef(name);
    return result && !env->ExceptionCheck();
#endif
}

#include "JavaEnvironmentChecks.h"
#include <cstring>

namespace {
// Preserve exceptions present on entry and unexpected failures (including OOM).
// Each diagnostic owns a local frame so every early return releases its refs.
struct Frame {
    JNIEnv *env;
    bool ready;
    explicit Frame(JNIEnv *value) : env(value), ready(value && !value->ExceptionCheck()
            && value->PushLocalFrame(24) == 0) {}
    ~Frame() { if (ready) env->PopLocalFrame(nullptr); }
};
bool frameworkName(const char *name) {
    static const char *const prefixes[] = {
            "de.robv.android.xposed.", "org.lsposed.", "org.meowcat.edxposed."};
    for (const char *prefix : prefixes)
        if (!std::strncmp(name, prefix, std::strlen(prefix))) return true;
    return false;
}
}

NmmpCheckStatus nmmpCheckFrameworkClasses(JNIEnv *env, jobject context) {
    Frame frame(env);
    if (!frame.ready || !context) return NMMP_CHECK_UNKNOWN;
    jclass contextClass = env->GetObjectClass(context);
    if (!contextClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID getLoader = env->GetMethodID(contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getLoader || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jobject loader = env->CallObjectMethod(context, getLoader);
    if (!loader || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jclass classClass = env->FindClass("java/lang/Class");
    if (!classClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID forName = env->GetStaticMethodID(classClass, "forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    if (!forName || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jclass missingClass = env->FindClass("java/lang/ClassNotFoundException");
    if (!missingClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    static const char *const names[] = {"de.robv.android.xposed.XposedBridge",
            "org.lsposed.lspd.core.Main", "org.lsposed.lspd.nativebridge.HookBridge"};
    for (const char *name : names) {
        jstring text = env->NewStringUTF(name);
        if (!text || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
        jobject found = env->CallStaticObjectMethod(classClass, forName, text, JNI_FALSE, loader);
        env->DeleteLocalRef(text);
        if (env->ExceptionCheck()) {
            jthrowable failure = env->ExceptionOccurred();
            env->ExceptionClear();
            const bool missing = failure && env->IsInstanceOf(failure, missingClass);
            if (!missing) {
                if (failure) env->Throw(failure);
                return NMMP_CHECK_UNKNOWN;
            }
            env->DeleteLocalRef(failure);
        } else {
            if (!found) return NMMP_CHECK_UNKNOWN;
            return NMMP_CHECK_SIGNAL;
        }
    }
    return NMMP_CHECK_PASS;
}

NmmpCheckStatus nmmpCheckFrameworkStack(JNIEnv *env) {
    Frame frame(env);
    if (!frame.ready) return NMMP_CHECK_UNKNOWN;
    jclass threadClass = env->FindClass("java/lang/Thread");
    if (!threadClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID current = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
    if (!current || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID getStack = env->GetMethodID(threadClass, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
    if (!getStack || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jclass elementClass = env->FindClass("java/lang/StackTraceElement");
    if (!elementClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID getName = env->GetMethodID(elementClass, "getClassName", "()Ljava/lang/String;");
    if (!getName || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jobject thread = env->CallStaticObjectMethod(threadClass, current);
    if (!thread || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    auto stack = static_cast<jobjectArray>(env->CallObjectMethod(thread, getStack));
    if (!stack || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    const jsize count = env->GetArrayLength(stack);
    if (env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    for (jsize i = 0; i < count && i < 64; ++i) {
        jobject element = env->GetObjectArrayElement(stack, i);
        if (!element || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
        auto name = static_cast<jstring>(env->CallObjectMethod(element, getName));
        env->DeleteLocalRef(element);
        if (!name || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
        if (env->GetStringLength(name) > 1024) return NMMP_CHECK_UNKNOWN;
        const char *text = env->GetStringUTFChars(name, nullptr);
        if (!text || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
        const bool found = frameworkName(text);
        env->ReleaseStringUTFChars(name, text);
        env->DeleteLocalRef(name);
        if (found) return NMMP_CHECK_SIGNAL;
    }
    return count > 64 ? NMMP_CHECK_UNKNOWN : NMMP_CHECK_PASS;
}

NmmpCheckStatus nmmpCheckPackageCreator(JNIEnv *env) {
    Frame frame(env);
    if (!frame.ready) return NMMP_CHECK_UNKNOWN;
    jclass packageClass = env->FindClass("android/content/pm/PackageInfo");
    if (!packageClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jfieldID field = env->GetStaticFieldID(packageClass, "CREATOR", "Landroid/os/Parcelable$Creator;");
    if (!field || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jobject creator = env->GetStaticObjectField(packageClass, field);
    if (!creator || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jclass creatorClass = env->GetObjectClass(creator);
    if (!creatorClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jclass classClass = env->FindClass("java/lang/Class");
    if (!classClass || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jmethodID getLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getLoader || env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jobject expected = env->CallObjectMethod(packageClass, getLoader);
    if (env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    jobject actual = env->CallObjectMethod(creatorClass, getLoader);
    if (env->ExceptionCheck()) return NMMP_CHECK_UNKNOWN;
    // A shared bootstrap loader is normal, including implementations using null.
    // Different loaders are diagnostic evidence, not proof of signature bypass.
    return env->IsSameObject(expected, actual) ? NMMP_CHECK_PASS : NMMP_CHECK_SIGNAL;
}

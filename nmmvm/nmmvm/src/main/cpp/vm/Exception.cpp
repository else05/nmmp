//
// Created by mao on 20-8-17.
//
#include <cstdlib>
#include "Exception.h"
#include "ScopedLocalRef.h"
#include "GlobalCache.h"

static void throwExceptionByName(JNIEnv *env, const char *name, const char *msg) {
    ScopedLocalRef<jclass> cls(env, env->FindClass(name));
    if (cls.get() != nullptr) {
        env->ThrowNew(cls.get(), msg);
    }
}

void dvmThrowExceptionFmtV(JNIEnv *env, const char *name,
                           const char *fmt, va_list args) {
    char msgBuf[512];

    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    throwExceptionByName(env, name, msgBuf);
}


void dvmThrowArithmeticException(JNIEnv *env, const char *msg) {
    env->ThrowNew(gVm.exArithmeticException, msg);
}

void dvmThrowInternalError(JNIEnv *env, const char *msg) {
    env->ThrowNew(gVm.exInternalError, msg);
}

void dvmThrowArrayIndexOutOfBoundsException(JNIEnv *env, int length, int index) {
    char buf[64];
    snprintf(buf, sizeof(buf), "length=%d; index=%d", length, index);
    env->ThrowNew(gVm.exArrayIndexOutOfBoundsException, buf);
}

void dvmThrowNegativeArraySizeException(JNIEnv *env, s4 size) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", size);
    env->ThrowNew(gVm.exNegativeArraySizeException, buf);
}

static jmethodID javaClassGetNameMethod = NULL;

static char *getClassName(JNIEnv *env, jclass clazz) {

    if (javaClassGetNameMethod == NULL) {
        ScopedLocalRef<jclass> javaClassClass(env, env->FindClass("java/lang/Class"));
        javaClassGetNameMethod = env->GetMethodID(javaClassClass.get(),
                                                  "getName",
                                                  "()Ljava/lang/String;");
    }

    auto className = (jstring) env->CallObjectMethod(clazz, javaClassGetNameMethod);


    jsize utfLen = env->GetStringUTFLength(className);
    char *cname = (char *) malloc(utfLen + 1);

    env->GetStringUTFRegion(className, 0, env->GetStringLength(className), cname);
    cname[utfLen] = 0;
    return cname;
}

void dvmThrowClassCastException(JNIEnv *env, jobject jobj, jclass clazz2) {
//    ScopedLocalRef<jclass> clazz1(env, env->GetObjectClass(jobj));

//    char *name1 = getClassName(env, clazz1.get());
//    char *name2 = getClassName(env, clazz2);

    //todo 觉得获得class名太麻烦,所以消息直接设置为空,可能需要改进

    env->ThrowNew(gVm.exClassCastException, NULL);

//    free(name1);
//    free(name2);
}


void dvmThrowRuntimeException(JNIEnv *env, const char *msg) {
    env->ThrowNew(gVm.exRuntimeException, msg);
}

void dvmThrowNullPointerException(JNIEnv *env, const char *msg) {
    env->ThrowNew(gVm.exNullPointerException, msg);
}

void dvmThrowClassNotFoundException(JNIEnv *env, const char *name) {
    env->ThrowNew(gVm.exClassNotFoundException, name);
}


/*
 * Search the method's list of exceptions for a match.
 *
 * Returns the offset of the catch block on success, or -1 on failure.
 */
#include "VmReader.h"

int dvmFindCatchBlockReader(JNIEnv *env, const vmResolver *resolver, uint32_t relPc,
                           jthrowable exception, VmReader &r) {
    if (!r.triesBytes()) return -1;
    uint32_t count = r.try16(0);
    uint64_t handlers = 4 + uint64_t(count) * 8;
    if (!r.range(0, handlers, r.triesBytes())) return -1;
    int lo = 0, hi = int(count) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint64_t item = 4 + uint64_t(mid) * 8;
        uint32_t start = r.try32(item), length = r.try16(item + 4);
        if (relPc < start) { hi = mid - 1; continue; }
        if (uint64_t(relPc) >= uint64_t(start) + length) { lo = mid + 1; continue; }
        uint64_t offset = handlers + r.try16(item + 6);
        if (!r.range(offset, 1, r.triesBytes())) return -1;
        uint32_t cursor = uint32_t(offset);
        int32_t signedCount = r.sleb(cursor);
        uint64_t typed = signedCount < 0 ? -int64_t(signedCount) : signedCount;
        if (r.failed() || typed > r.triesBytes() / 2) { r.fail(); return -1; }
        for (uint64_t i = 0; i < typed; ++i) {
            uint32_t type = r.uleb(cursor), address = r.uleb(cursor);
            if (r.failed() || !r.target(address)) return -1;
            ScopedLocalRef<jclass> clazz(env, resolver->dvmResolveClass(env, type));
            if (!clazz.get()) { env->ExceptionClear(); continue; }
            bool match = env->IsInstanceOf(exception, clazz.get());
            if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
            if (match) return int(address);
        }
        if (signedCount <= 0) {
            uint32_t address = r.uleb(cursor);
            if (!r.failed() && r.target(address)) return int(address);
        }
        return -1;
    }
    return -1;
}

static int
findCatchInMethod(JNIEnv *env,
                  const vmResolver *resolver,
                  int relPc,
                  TryCatchHandler *pHandler,
                  jthrowable exceptObj) {

    LOGVV("findCatchInMethod ");

    VmCatchIterator iterator;

    if (vmFindCatchHandler(&iterator, pHandler, relPc)) {
        for (;;) {
            VmCatchHandler *handler = vmCatchIteratorNext(&iterator);

            if (handler == NULL) {
                break;
            }

            if (handler->typeIdx == kNoIndex) {
                /* catch-all */
                ALOGV("Match on catch-all block at 0x%02x in %p",
                      relPc, exceptObj);
                return handler->address;
            }

            ScopedLocalRef<jclass> throwable(env,
                                             resolver->dvmResolveClass(env, handler->typeIdx));
            if (throwable.get() == NULL) {
                ALOGV("Could not resolve class ref'ed in exception "
                      "catch list (class index %d, exception %p)",
                      handler->typeIdx,
                      exceptObj);
                env->ExceptionClear();
                continue;
            }

            //ALOGD("ADDR MATCH, check %s instanceof %s",
            //    exceptObj->descriptor, pEntry->exceptObj->descriptor);

            if (env->IsInstanceOf(exceptObj, throwable.get())) {
                ALOGV("Match on catch block at 0x%02x in for %p",
                      relPc, exceptObj);
                return handler->address;
            }
        }
    }

    ALOGV("No matching catch block at 0x%02x ", relPc);
    return -1;
}


int dvmFindCatchBlock(JNIEnv *env, const vmResolver *resolver, int relPc, jthrowable exception,
                      TryCatchHandler *pHandler) {
    int catchAddr = -1;
    if (pHandler == NULL) {
        return catchAddr;
    }


    catchAddr = findCatchInMethod(env, resolver, relPc, pHandler,
                                  exception);

    /*
     * The class resolution in findCatchInMethod() could cause an exception.
     * Clear it to be safe.
     */
    env->ExceptionClear();

    return catchAddr;
}


// Payload readers preserve the existing Dalvik switch and array semantics.
#include <cstring>
#include "Interp.h"
#include "DexOpcodes.h"
#include "Exception.h"

static void payloadError(JNIEnv *env, VmReader &reader) {
    reader.fail();
    if (!env->ExceptionCheck()) dvmThrowInternalError(env, "Invalid VM payload");
}

s4 dvmInterpHandlePackedSwitch(JNIEnv *env, VmReader &r, int64_t pc, s4 testVal) {
    if (!r.payloadRange(pc, 8) || r.payload16(pc) != kPackedSwitchSignature) {
        payloadError(env, r); return 3;
    }
    uint32_t size = r.payload16(pc + 1);
    int32_t first = int32_t(r.payload32(pc + 2));
    if (!r.payloadRange(pc, 8 + uint64_t(size) * 4)) {
        payloadError(env, r); return 3;
    }
    int64_t index = int64_t(testVal) - first;
    if (index < 0 || uint64_t(index) >= size) return 3;
    return int32_t(r.payload32(pc + 4 + index * 2));
}

s4 dvmInterpHandleSparseSwitch(JNIEnv *env, VmReader &r, int64_t pc, s4 testVal) {
    if (!r.payloadRange(pc, 4) || r.payload16(pc) != kSparseSwitchSignature) {
        payloadError(env, r); return 3;
    }
    uint32_t size = r.payload16(pc + 1);
    if (!r.payloadRange(pc, 4 + uint64_t(size) * 8)) {
        payloadError(env, r); return 3;
    }
    int lo = 0, hi = int(size) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int32_t key = int32_t(r.payload32(pc + 2 + mid * 2));
        if (testVal < key) hi = mid - 1;
        else if (testVal > key) lo = mid + 1;
        else return int32_t(r.payload32(pc + 2 + size * 2 + mid * 2));
    }
    return 3;
}

bool dvmInterpHandleFillArrayData(JNIEnv *env, jarray arrayObj, VmReader &r, int64_t pc) {
    if (!arrayObj) { dvmThrowNullPointerException(env, nullptr); return false; }
    if (!r.payloadRange(pc, 8) || r.payload16(pc) != kArrayDataSignature) {
        payloadError(env, r); return false;
    }
    uint32_t width = r.payload16(pc + 1), size = r.payload32(pc + 2);
    if ((width != 1 && width != 2 && width != 4 && width != 8)
            || !r.payloadRange(pc, 8 + uint64_t(width) * size)) {
        payloadError(env, r); return false;
    }
    const char *types = width == 1 ? "ZB" : width == 2 ? "SC" : width == 4 ? "IF" : "JD";
    bool matched = false;
    for (unsigned i = 0; i < 2 && !matched; ++i) {
        char descriptor[] = {'[', types[i], 0};
        jclass type = env->FindClass(descriptor);
        if (!type) return false;
        matched = env->IsInstanceOf(arrayObj, type);
        env->DeleteLocalRef(type);
        if (env->ExceptionCheck()) return false;
    }
    if (!matched) { payloadError(env, r); return false; }
    jsize length = env->GetArrayLength(arrayObj);
    if (env->ExceptionCheck()) return false;
    if (size > uint32_t(length)) {
        dvmThrowArrayIndexOutOfBoundsException(env, length, size); return false;
    }
    // Decode before entering the JNI critical region; no JNI calls while pinned.
    uint8_t batch[256];
    const uint64_t total = uint64_t(width) * size;
    bool ok = true;
    for (uint64_t pos = 0; pos < total; pos += sizeof(batch)) {
        size_t count = size_t(total - pos < sizeof(batch) ? total - pos : sizeof(batch));
        for (size_t i = 0; i < count; ++i) batch[i] = r.payloadByte(uint64_t(pc) * 2 + 8 + pos + i);
        if (r.failed()) { payloadError(env, r); ok = false; break; }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        for (size_t i = 0; i < count; i += width)
            for (size_t j = 0; j < width / 2; ++j) {
                uint8_t t = batch[i + j]; batch[i + j] = batch[i + width - 1 - j];
                batch[i + width - 1 - j] = t;
            }
#endif
        void *dst = env->GetPrimitiveArrayCritical(arrayObj, nullptr);
        if (!dst) { ok = false; break; }
        std::memcpy(static_cast<uint8_t *>(dst) + pos, batch, count);
        env->ReleasePrimitiveArrayCritical(arrayObj, dst, 0);
        if (env->ExceptionCheck()) { ok = false; break; }
    }
    volatile uint8_t *wipe = batch;
    for (size_t i = 0; i < sizeof(batch); ++i) wipe[i] = 0;
    return ok;
}

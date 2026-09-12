#include "vm.h"
#include "VmCodec.h"
#include "VmReader.h"
#include "PrivateLoaderState.h"
#include "Exception.h"
#if defined(__ANDROID__)
#include <android/api-level.h>
#endif

static void demandError(JNIEnv *env) {
    if (!env->ExceptionCheck()) dvmThrowInternalError(env, "Invalid on-demand VM state or record");
}
static uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint64_t methodSeed(uint64_t root, const vmDemandCode *c) {
    return mix(root ^ (uint64_t(c->methodId) * UINT64_C(0x9e3779b97f4a7c15))
               ^ c->descriptorTag ^ (uint64_t(c->registersSize) << 32) ^ c->insSize);
}
static unsigned kind(const vmDemandCode *c, uint32_t pc) {
    return (c->boundaries[pc / 2] >> ((pc & 1) * 4)) & 15;
}
static uint32_t contextHash(uint64_t root, const vmDemandCode *c) {
    uint64_t seed = methodSeed(root, c);
    const uint32_t fields[] = {3, c->methodId, uint32_t(c->descriptorTag), uint32_t(c->descriptorTag >> 32),
        c->registersSize, c->insSize, c->codeBytes, c->triesBytes, c->boundariesBytes,
        c->codeHash, c->triesHash, c->boundariesHash, uint32_t(seed), uint32_t(seed >> 32)};
    uint32_t hash = UINT32_C(0x811c9dc5);
    for (uint32_t field : fields) for (unsigned b = 0; b < 4; ++b) {
        hash ^= uint8_t(field >> (b * 8)); hash *= UINT32_C(0x01000193);
    }
    return hash;
}

bool vmPrepareDemandCode(JNIEnv *env, vmDemandCode *c) {
    if (!c) { demandError(env); return false; }
    int state = __atomic_load_n(&c->state, __ATOMIC_ACQUIRE);
    if (state == 2) return true;
    if (state) { demandError(env); return false; }
    __atomic_store_n(&c->state, 1, __ATOMIC_RELEASE);
    bool ok = false;
    uint64_t root = 0;
    do {
        if (NMMP_VM_CODEC_VERSION != 3 || !vmCodecGetSeed(&root)) break;
        if (contextHash(root, c) != c->contextHash) break;
#if defined(__ANDROID__)
        const int api = android_get_device_api_level();
        if (api != 26 && api != 27) break;
#if !defined(__aarch64__)
        break;
#endif
#endif
        if (!c->code || !c->codeBytes || (c->codeBytes & 1) || !c->boundaries
                || c->boundariesBytes != (uint64_t(c->codeBytes) + 3) / 4
                || (c->triesBytes && !c->tries) || c->insSize > c->registersSize
                || c->registersSize > 65535) break;
        if (vmCodecHash(c->code, c->codeBytes) != c->codeHash
                || vmCodecHash(c->tries, c->triesBytes) != c->triesHash
                || vmCodecHash(c->boundaries, c->boundariesBytes) != c->boundariesHash) break;
        if (kind(c, 0) != NMMP_READER_FETCH) break;
        ok = true;
        for (uint32_t pc = 0; pc < c->codeBytes / 2; ++pc)
            if (kind(c, pc) > NMMP_READER_PAYLOAD) { ok = false; break; }
    } while (false);
    volatile uint64_t *wipe = &root; *wipe = 0;
    __atomic_store_n(&c->state, ok ? 2 : 3, __ATOMIC_RELEASE);
    if (!ok) demandError(env);
    return ok;
}

jvalue vmExecuteDemand(JNIEnv *env, const vmDemandCode *c, regptr_t *regs,
                       u1 *regFlags, u4 registerCapacity, const vmResolver *resolver) {
#if defined(NMMP_PRIVATE_LINKER)
    if (nmmpPrivateLoaderFailed()) { demandError(env); return {}; }
#endif
    uint64_t root = 0;
    if (!c || !resolver || __atomic_load_n(&c->state, __ATOMIC_ACQUIRE) != 2
            || !vmCodecGetSeed(&root)) { demandError(env); return {}; }
    if (registerCapacity != c->registersSize || !regs || !regFlags || contextHash(root, c) != c->contextHash) {
        volatile uint64_t *wipe = &root; *wipe = 0;
        demandError(env); return {};
    }
    VmReader reader(c->code, c->codeBytes, c->tries, c->triesBytes, c->boundaries, methodSeed(root, c));
    volatile uint64_t *wipe = &root; *wipe = 0;
    const vmCode frame = {nullptr, c->codeBytes / 2, regs, regFlags, nullptr, c->triesBytes, &reader, registerCapacity};
    return vmInterpretReader(env, &frame, resolver, &reader);
}

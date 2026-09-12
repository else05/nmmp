#include "VmCodec.h"

#include "VmInit.h"
#include <sys/mman.h>
#if NMMP_VM_CODEC_VERSION == 3
#include "NativeProgramConfig.h"
#endif

struct SeedContext { uint64_t seed, maskTag; };
static VmInit gCodecInit = NMMP_VM_INIT;
#if !NMMP_VM_SIGNATURE_BINDING && NMMP_VM_CODEC_VERSION == 2
static const SeedContext gUnbound = {NMMP_VM_SEED_DATA, 0};
static const SeedContext *gSeed = &gUnbound;
#else
static const SeedContext *gSeed = nullptr;
#endif
static void wipe(void *data, size_t size) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size--) *p++ = 0;
}
static uint64_t maskTag(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static bool initializeSeed(void *argument) {
    uint64_t mask = *static_cast<uint64_t *>(argument), seed = 0;
#if NMMP_VM_CODEC_VERSION == 3
    // This host entry is reached only after binding verification, or explicit unbound setup.
    uint64_t inputs[4] = {1, NMMP_VM_SEED_DATA, mask, NMMP_VM_BUILD_ID};
    bool valid = nmmpNativeRun(&NMMP_ROOT_PROGRAM, inputs, 4, &seed);
    wipe(inputs, sizeof(inputs));
    if (!valid) { wipe(&mask, sizeof(mask)); return false; }
#else
    seed = NMMP_VM_SEED_DATA ^ mask;
#endif
    void *memory = mmap(nullptr, sizeof(SeedContext), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) { wipe(&seed, sizeof(seed)); wipe(&mask, sizeof(mask)); return false; }
    auto *context = static_cast<SeedContext *>(memory);
    context->seed = seed; context->maskTag = maskTag(mask);
    wipe(&seed, sizeof(seed)); wipe(&mask, sizeof(mask));
    if (mprotect(memory, sizeof(SeedContext), PROT_READ)) {
        wipe(memory, sizeof(SeedContext)); munmap(memory, sizeof(SeedContext)); return false;
    }
    gSeed = context;
    return true;
}

// 密钥流算法只保留在当前翻译单元，避免生成代码中出现未保护副本。
static uint8_t vmCodecKeyByte(uint64_t seed, uint32_t id,
                              uint32_t domain,
                              uint32_t byteIndex) {
    const uint64_t blockIndex = ((uint64_t) byteIndex) >> 3U;
    uint64_t value = seed
            ^ ((uint64_t) id * UINT64_C(0x9e3779b97f4a7c15))
            ^ ((uint64_t) domain * UINT64_C(0xd6e8feb86659fd93))
            ^ blockIndex;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return (uint8_t) ((value >> ((byteIndex & 7U) * 8U)) & UINT64_C(0xff));
}

extern "C"
bool vmCodecActivate(uint64_t bindingMask) {
#if !NMMP_VM_SIGNATURE_BINDING && NMMP_VM_CODEC_VERSION == 2
    return gSeed->seed == (NMMP_VM_SEED_DATA ^ bindingMask);
#else
    bool ready = vmInitRun(&gCodecInit, initializeSeed, &bindingMask);
    bool matched = ready && gSeed->maskTag == maskTag(bindingMask);
    wipe(&bindingMask, sizeof(bindingMask));
    return matched;
#endif
}

extern "C"
bool vmCodecGetSeed(uint64_t *seed) {
    if (!seed || !vmCodecIsActivated()) return false;
    *seed = gSeed->seed;
    return true;
}

extern "C"
bool vmCodecIsActivated(void) {
#if !NMMP_VM_SIGNATURE_BINDING && NMMP_VM_CODEC_VERSION == 2
    return true;
#else
    return __atomic_load_n(&gCodecInit.state, __ATOMIC_ACQUIRE) == 2;
#endif
}

extern "C"
void vmCodecTransform(uint8_t *data,
                      uint32_t size,
                      uint32_t id,
                      uint32_t domain) {
    uint64_t seed = 0;
    if (!vmCodecGetSeed(&seed)) return;
    for (uint32_t i = 0; i < size; ++i) {
        data[i] ^= vmCodecKeyByte(seed, id, domain, i);
    }
    wipe(&seed, sizeof(seed));
}

extern "C"
uint32_t vmCodecHash(const uint8_t *data, uint32_t size) {
    uint32_t hash = UINT32_C(0x811c9dc5);
    for (uint32_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT32_C(0x01000193);
    }
    return hash;
}

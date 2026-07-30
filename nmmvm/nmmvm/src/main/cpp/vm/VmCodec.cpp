#include "VmCodec.h"

// 密钥流算法只保留在当前翻译单元，避免生成代码中出现未保护副本。
static uint8_t vmCodecKeyByte(uint32_t id,
                              uint32_t domain,
                              uint32_t byteIndex) {
    const uint64_t blockIndex = ((uint64_t) byteIndex) >> 3U;
    uint64_t value = NMMP_VM_BUILD_SEED
            ^ ((uint64_t) id * UINT64_C(0x9e3779b97f4a7c15))
            ^ ((uint64_t) domain * UINT64_C(0xd6e8feb86659fd93))
            ^ blockIndex;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return (uint8_t) ((value >> ((byteIndex & 7U) * 8U)) & UINT64_C(0xff));
}

extern "C"
void vmCodecTransform(uint8_t *data,
                      uint32_t size,
                      uint32_t id,
                      uint32_t domain) {
    for (uint32_t i = 0; i < size; ++i) {
        data[i] ^= vmCodecKeyByte(id, domain, i);
    }
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

#ifndef NMMP_VM_CODEC_H
#define NMMP_VM_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#include "VmCodecConfig.h"

#if NMMP_VM_CODEC_VERSION != 3
#error Only codec3 is supported
#endif

#ifdef __cplusplus
extern "C" {
#endif
bool vmCodecGetSeed(uint64_t *seed);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

void vmCodecTransform(uint8_t *data,
                      uint32_t size,
                      uint32_t id,
                      uint32_t domain);

bool vmCodecActivate(uint64_t bindingMask);

bool vmCodecIsActivated(void);

uint32_t vmCodecHash(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif

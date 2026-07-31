#ifndef NMMP_VM_CODEC_H
#define NMMP_VM_CODEC_H

#include <stdint.h>

#include "VmCodecConfig.h"

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

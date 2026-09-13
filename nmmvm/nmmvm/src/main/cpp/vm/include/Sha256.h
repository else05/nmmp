#ifndef NMMP_SHA256_H
#define NMMP_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t totalSize;
    uint8_t buffer[64];
    size_t bufferSize;
} NmmpSha256Context;

#ifdef __cplusplus
extern "C" {
#endif
void nmmpSha256Init(NmmpSha256Context *context);
void nmmpSha256Update(NmmpSha256Context *context, const uint8_t *data, size_t size);
void nmmpSha256UpdateU32(NmmpSha256Context *context, uint32_t value);
void nmmpSha256Final(NmmpSha256Context *context, uint8_t digest[32]);
void nmmpSha256(const uint8_t *data, size_t size, uint8_t digest[32]);
#ifdef __cplusplus
}
#endif

#endif

#ifndef NMMP_PROTECTION_MANIFEST_H
#define NMMP_PROTECTION_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMMP_MANIFEST_DIGEST_SIZE 32u

typedef struct {
    uint32_t moduleId;
    uint32_t moduleSize;
    uint32_t methodCount;
    uint32_t resolverItemCount;
    uint32_t registerCount;
    uint8_t moduleDigest[NMMP_MANIFEST_DIGEST_SIZE];
    uint8_t resolverDigest[NMMP_MANIFEST_DIGEST_SIZE];
    uint8_t registerDigest[NMMP_MANIFEST_DIGEST_SIZE];
} NmmpManifestEntry;

#ifdef __cplusplus
extern "C" {
#endif
bool nmmpProtectionActivate(const uint8_t *manifest,
                            size_t manifestSize,
                            const uint8_t tag[32],
                            const uint8_t keyXor[32],
                            const uint8_t expectedId[16]);
bool nmmpProtectionGetEntry(uint32_t moduleId, NmmpManifestEntry *entry);
bool nmmpProtectionDigestEqual(const uint8_t left[32], const uint8_t right[32]);
bool nmmpProtectionPointerInImage(const void *pointer);
uint32_t nmmpProtectionPolicyFlags(void);
uint32_t nmmpProtectionRecheckMillis(void);
#ifdef __cplusplus
}
#endif

#endif

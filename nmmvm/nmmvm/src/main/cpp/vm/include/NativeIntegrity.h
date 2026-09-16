#ifndef NMMP_NATIVE_INTEGRITY_H
#define NMMP_NATIVE_INTEGRITY_H
#include "PrivateImageLayout.h"

typedef enum {
    NMMP_NATIVE_UNAVAILABLE = -1,
    NMMP_NATIVE_MATCH = 0,
    NMMP_NATIVE_MISMATCH = 1,
    NMMP_NATIVE_NOT_APPLICABLE = 2
} NmmpNativeIntegrityResult;
#ifdef __cplusplus
extern "C" {
#endif
// Only loader-owned, authenticated segment descriptors may be supplied.
NmmpNativeIntegrityResult nmmpVerifyExecutableSegments(const NmmpImageSegment *segments, size_t count);
NmmpNativeIntegrityResult nmmpVerifyExecutableSegmentsShard(
        const NmmpImageSegment *segments, size_t count, uint32_t shard);
NmmpNativeIntegrityResult nmmpVerifyImportSlots(const NmmpImportSlot *slots, size_t count);
NmmpNativeIntegrityResult nmmpVerifyPrivateImage();
NmmpNativeIntegrityResult nmmpVerifyPrivateImageShard(uint32_t shard);
#ifdef __cplusplus
}
#endif
#endif

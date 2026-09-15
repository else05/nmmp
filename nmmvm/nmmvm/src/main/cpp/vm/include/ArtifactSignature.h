#ifndef NMMP_ARTIFACT_SIGNATURE_H
#define NMMP_ARTIFACT_SIGNATURE_H
#include "ArtifactInventory.h"
#ifdef __cplusplus
extern "C" {
#endif
// Public key must come from the trusted build configuration, never the envelope.
// Successful entries borrow immutable envelope storage, as for the parser.
bool nmmpAuthenticateArtifactInventory(const uint8_t *envelope, size_t envelope_size,
                                      const uint8_t *public_key, size_t public_key_size,
                                      uint64_t build_id, const uint8_t *package, size_t package_size,
                                      NmmpArtifactEntry *entries, size_t capacity, size_t *count);
bool nmmpVerifySignedArtifactApk(int fd, const uint8_t *public_key, size_t public_key_size,
                               uint64_t build_id, const uint8_t *package, size_t package_size);
#ifdef __cplusplus
}
#endif
#endif

#ifndef NMMP_ARTIFACT_INVENTORY_H
#define NMMP_ARTIFACT_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NMMP_ARTIFACT_MAX_ENTRIES 2048u
#define NMMP_ARTIFACT_MAX_NAME 1024u
#define NMMP_ARTIFACT_MAX_ENTRY_BYTES (UINT64_C(256) * 1024 * 1024)
#define NMMP_ARTIFACT_MAX_TOTAL_BYTES (UINT64_C(1024) * 1024 * 1024)
#define NMMP_ARTIFACT_MAX_BODY (28u + NMMP_ARTIFACT_MAX_NAME + NMMP_ARTIFACT_MAX_ENTRIES * (44u + NMMP_ARTIFACT_MAX_NAME))

typedef struct {
    const uint8_t *name;
    size_t name_size;
    uint64_t size;
    const uint8_t *sha256;
} NmmpArtifactEntry;

#ifdef __cplusplus
extern "C" {
#endif
// Parses structure and expected identity only; does NOT authenticate the body.
// Entries borrow immutable body storage. Failure always sets count to zero;
// output entries may be partially written and must not be consumed on failure.
bool nmmpParseArtifactInventory(const uint8_t *body, size_t body_size,
                               uint64_t expected_build_id,
                               const uint8_t *expected_package, size_t package_size,
                               NmmpArtifactEntry *entries, size_t capacity, size_t *count);
#ifdef __cplusplus
}
#endif
#endif

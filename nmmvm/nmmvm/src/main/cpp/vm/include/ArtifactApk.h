#ifndef NMMP_ARTIFACT_APK_H
#define NMMP_ARTIFACT_APK_H
#include "ArtifactInventory.h"
#define NMMP_ARTIFACT_APK_ENTRY "assets/nmmp/artifact.sig"
#ifdef __cplusplus
extern "C" {
#endif
// Caller supplies parsed, authenticated entries and retains their immutable
// storage throughout this synchronous operation. Does not close or seek fd.
// False means verification did not complete successfully, not necessarily tampering.
bool nmmpVerifyArtifactApk(int fd, const NmmpArtifactEntry *entries, size_t count);
// Reads the bounded STORED signature envelope and checks its ZIP metadata.
// Caller owns the malloc buffer; failure returns nullptr/zero.
bool nmmpReadArtifactEnvelope(int fd, uint8_t **envelope, size_t *size);
// Reads one authenticated core image, checking ZIP metadata and its digest.
// Caller owns the malloc buffer. The temporary image is limited to 64 MiB.
bool nmmpReadArtifactImage(int fd, const NmmpArtifactEntry *entry, uint8_t **image);
#ifdef __cplusplus
}
#endif
#endif

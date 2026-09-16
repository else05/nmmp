#ifndef NMMP_OUTER_INTEGRITY_H
#define NMMP_OUTER_INTEGRITY_H
#include "ArtifactInventory.h"
#include "ModuleOrigins.h"
#include "NativeIntegrity.h"

// Input must be the authenticated final ELF bytes. No baseline is taken from
// current process code; only PT_LOAD/PF_X file-backed bytes are hashed.
bool nmmpBuildOuterCodeBaseline(const uint8_t *image, size_t size,
        const NmmpLoadedModule *module, NmmpImageSegment *segments, size_t *count);
bool nmmpBuildOuterImportBaseline(const uint8_t *image, size_t size, const NmmpLoadedModule *outer,
        const uint8_t *libcImage, size_t libcSize, const NmmpLoadedModule *libcModule,
        NmmpImportSlot *slots, size_t *count);
bool nmmpReadSystemLibc(const NmmpLoadedModule *module, uint8_t **image, size_t *size);
bool nmmpPrepareOuterImageFromApk(int fd, const NmmpArtifactEntry *entries,
        size_t count, uintptr_t anchor);
NmmpNativeIntegrityResult nmmpVerifyOuterImage();
NmmpNativeIntegrityResult nmmpVerifyOuterImageShard(uint32_t shard);
#endif

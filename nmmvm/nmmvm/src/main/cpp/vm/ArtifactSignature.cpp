#include "ArtifactSignature.h"
#include "ArtifactApk.h"
#include "Arm64Syscall.h"
#include "VmCodecConfig.h"
#if defined(NMMP_PRIVATE_LINKER) && NMMP_VM_SIGNATURE_BINDING
#include "OuterIntegrity.h"
#include "PrivateLoaderState.h"
#endif
#include "monocypher-ed25519.h"
#include <cstring>
#include <cstdlib>

namespace {
uint32_t big32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
}

bool nmmpVerifySignedArtifactApk(int fd, const uint8_t *public_key, size_t public_key_size,
                               uint64_t build_id, const uint8_t *package, size_t package_size) {
    struct stat before = {}, after = {};
    if (fd < 0 || !public_key || public_key_size != 32 || nmmpRawFstat(fd, &before)) return false;
    uint8_t *envelope = nullptr;
    size_t envelope_size = 0, count = 0;
    if (!nmmpReadArtifactEnvelope(fd, &envelope, &envelope_size)) return false;
    auto *entries = static_cast<NmmpArtifactEntry *>(std::calloc(NMMP_ARTIFACT_MAX_ENTRIES, sizeof(NmmpArtifactEntry)));
    const bool verified = entries && nmmpAuthenticateArtifactInventory(envelope, envelope_size, public_key, public_key_size,
            build_id, package, package_size, entries, NMMP_ARTIFACT_MAX_ENTRIES, &count)
            && nmmpVerifyArtifactApk(fd, entries, count)
#if defined(NMMP_PRIVATE_LINKER) && NMMP_VM_SIGNATURE_BINDING
            && nmmpPrepareOuterImageFromApk(fd, entries, count, reinterpret_cast<uintptr_t>(nmmp_private_failure_state))
#endif
            && !nmmpRawFstat(fd, &after)
            && before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
            && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
            && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    std::free(entries);
    std::free(envelope);
    return verified;
}

bool nmmpAuthenticateArtifactInventory(const uint8_t *envelope, size_t envelope_size,
                                      const uint8_t *public_key, size_t public_key_size,
                                      uint64_t build_id, const uint8_t *package, size_t package_size,
                                      NmmpArtifactEntry *entries, size_t capacity, size_t *count) {
    if (!count) return false;
    *count = 0;
    if (!envelope || !public_key || public_key_size != 32 || envelope_size < 80
            || envelope_size > NMMP_ARTIFACT_MAX_BODY + 80u || std::memcmp(envelope, "ARTSIG01", 8)
            || big32(envelope + 8) != 1) return false;
    const uint32_t size = big32(envelope + 12);
    if (!size || size > NMMP_ARTIFACT_MAX_BODY || envelope_size - 80 != size) return false;
    if (crypto_ed25519_check(envelope + 16 + size, public_key, envelope, 16 + size)) return false;
    return nmmpParseArtifactInventory(envelope + 16, size, build_id, package, package_size, entries, capacity, count);
}

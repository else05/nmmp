#include "ProtectionManifest.h"

#include "PrivateLoaderState.h"
#include "Sha256.h"
#include "VmBinding.h"
#include "VmCodecConfig.h"
#include "VmInit.h"

#include <cstring>
#include <sys/mman.h>

namespace {

const size_t kHeaderSize = 96;
const size_t kEntrySize = 120;
const size_t kMaximumManifestSize = 64U * 1024U;
const uint32_t kMaximumEntries = 256;
const uint32_t kPolicyVersion = 8;
const uint32_t kAllowedPolicyFlags = 15;
const uint8_t kKeyXorMask[32] = {
        0x91, 0x37, 0xe4, 0x2b, 0x6d, 0xa8, 0x53, 0xc1,
        0x0f, 0x72, 0xb9, 0x44, 0xde, 0x18, 0x85, 0x6a,
        0x3c, 0xf0, 0x27, 0x9d, 0x51, 0xcb, 0x04, 0x7e,
        0xa3, 0x68, 0xd5, 0x12, 0xbc, 0x49, 0xf7, 0x20
};

struct ManifestContext {
    uint32_t count;
    uint32_t policyFlags;
    uint32_t recheckMillis;
    uint8_t id[16];
    NmmpManifestEntry entries[1];
};

struct ActivationArguments {
    const uint8_t *manifest;
    size_t manifestSize;
    const uint8_t *tag;
    const uint8_t *keyXor;
    const uint8_t *expectedId;
};

static VmInit gManifestInit = NMMP_VM_INIT;
static const ManifestContext *gManifest;

static uint32_t u32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8U
           | static_cast<uint32_t>(data[2]) << 16U | static_cast<uint32_t>(data[3]) << 24U;
}

static uint64_t u64(const uint8_t *data) {
    return static_cast<uint64_t>(u32(data)) | static_cast<uint64_t>(u32(data + 4)) << 32U;
}

static void wipe(void *memory, size_t size) {
    volatile uint8_t *data = static_cast<volatile uint8_t *>(memory);
    while (size-- != 0) *data++ = 0;
}

static bool equal(const uint8_t *left, const uint8_t *right, size_t size) {
    uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

static void hmacSha256(const uint8_t key[32],
                       const uint8_t *data,
                       size_t size,
                       uint8_t tag[32]) {
    uint8_t innerPad[64], outerPad[64], inner[32];
    for (size_t i = 0; i < sizeof(innerPad); ++i) {
        const uint8_t value = i < 32 ? key[i] : 0;
        innerPad[i] = value ^ 0x36;
        outerPad[i] = value ^ 0x5c;
    }
    NmmpSha256Context hash;
    nmmpSha256Init(&hash);
    nmmpSha256Update(&hash, innerPad, sizeof(innerPad));
    nmmpSha256Update(&hash, data, size);
    nmmpSha256Final(&hash, inner);
    nmmpSha256Init(&hash);
    nmmpSha256Update(&hash, outerPad, sizeof(outerPad));
    nmmpSha256Update(&hash, inner, sizeof(inner));
    nmmpSha256Final(&hash, tag);
    wipe(innerPad, sizeof(innerPad));
    wipe(outerPad, sizeof(outerPad));
    wipe(inner, sizeof(inner));
}

static bool initializeManifest(void *argument) {
    auto *args = static_cast<ActivationArguments *>(argument);
    if (!args || !args->manifest || !args->tag || !args->keyXor || !args->expectedId
            || args->manifestSize < kHeaderSize || args->manifestSize > kMaximumManifestSize
            || std::memcmp(args->manifest, "PRTMAN01", 8) != 0
            || u32(args->manifest + 8) != 1 || u32(args->manifest + 12) != args->manifestSize
            || u64(args->manifest + 16) != NMMP_VM_BUILD_ID
            || u32(args->manifest + 24) != NMMP_VM_CODEC_VERSION
            || u32(args->manifest + 28) != NMMP_VM_TEMPLATE_VERSION
            || u32(args->manifest + 32) != kPolicyVersion) return false;

    const uint32_t policyFlags = u32(args->manifest + 36);
    const uint32_t recheckMillis = u32(args->manifest + 40);
    const uint32_t count = u32(args->manifest + 44);
    const uint32_t packageSize = u32(args->manifest + 48);
    const bool signatureBound = u32(args->manifest + 52) != 0;
    if ((policyFlags & ~kAllowedPolicyFlags) != 0 || recheckMillis != 5000
            || count == 0 || count > kMaximumEntries || packageSize > 1024
            || u32(args->manifest + 56) != 0 || u32(args->manifest + 60) != 0
            || kHeaderSize + packageSize > args->manifestSize
            || static_cast<size_t>(count) > (args->manifestSize - kHeaderSize - packageSize) / kEntrySize
            || kHeaderSize + packageSize + static_cast<size_t>(count) * kEntrySize != args->manifestSize) {
        return false;
    }

    uint8_t key[32], actualTag[32], digest[32];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = args->keyXor[i] ^ kKeyXorMask[i];
    hmacSha256(key, args->manifest, args->manifestSize, actualTag);
    nmmpSha256(args->manifest, args->manifestSize, digest);
    const bool authenticated = equal(actualTag, args->tag, 32)
                               && equal(digest, args->expectedId, 16);
    wipe(key, sizeof(key));
    wipe(actualTag, sizeof(actualTag));
    wipe(digest, sizeof(digest));
    if (!authenticated) return false;

    char packageName[1025];
    std::memcpy(packageName, args->manifest + kHeaderSize, packageSize);
    packageName[packageSize] = '\0';
    if (std::memchr(packageName, 0, packageSize) != nullptr
            || !vmBindingMatchesExpectedIdentity(
                    packageName, args->manifest + 64, signatureBound)) {
        wipe(packageName, sizeof(packageName));
        return false;
    }
    wipe(packageName, sizeof(packageName));

    const size_t allocation = sizeof(ManifestContext)
                              + (static_cast<size_t>(count) - 1) * sizeof(NmmpManifestEntry);
    void *memory = mmap(nullptr, allocation, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return false;
    auto *context = static_cast<ManifestContext *>(memory);
    context->count = count;
    context->policyFlags = policyFlags;
    context->recheckMillis = recheckMillis;
    std::memcpy(context->id, args->expectedId, sizeof(context->id));
    const uint8_t *entry = args->manifest + kHeaderSize + packageSize;
    uint32_t previous = 0;
    bool valid = true;
    for (uint32_t i = 0; i < count; ++i, entry += kEntrySize) {
        NmmpManifestEntry *target = context->entries + i;
        target->moduleId = u32(entry);
        target->moduleSize = u32(entry + 4);
        target->methodCount = u32(entry + 8);
        target->resolverItemCount = u32(entry + 12);
        target->registerCount = u32(entry + 16);
        if ((i != 0 && target->moduleId <= previous) || target->moduleSize == 0
                || u32(entry + 20) != 0) {
            valid = false;
            break;
        }
        previous = target->moduleId;
        std::memcpy(target->moduleDigest, entry + 24, 32);
        std::memcpy(target->resolverDigest, entry + 56, 32);
        std::memcpy(target->registerDigest, entry + 88, 32);
    }
    if (!valid || mprotect(memory, allocation, PROT_READ) != 0) {
        wipe(memory, allocation);
        munmap(memory, allocation);
        return false;
    }
    gManifest = context;
    return true;
}

}  // namespace

extern "C" bool nmmpProtectionActivate(const uint8_t *manifest,
                                         size_t manifestSize,
                                         const uint8_t tag[32],
                                         const uint8_t keyXor[32],
                                         const uint8_t expectedId[16]) {
    if (!manifest || !tag || !keyXor || !expectedId
            || manifestSize < kHeaderSize || manifestSize > kMaximumManifestSize) return false;
    ActivationArguments arguments = {manifest, manifestSize, tag, keyXor, expectedId};
    const bool ready = vmInitRun(&gManifestInit, initializeManifest, &arguments);
    return ready && gManifest && equal(gManifest->id, expectedId, 16);
}

extern "C" bool nmmpProtectionGetEntry(uint32_t moduleId, NmmpManifestEntry *entry) {
    if (!entry || __atomic_load_n(&gManifestInit.state, __ATOMIC_ACQUIRE) != 2 || !gManifest) return false;
    uint32_t low = 0, high = gManifest->count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (gManifest->entries[middle].moduleId < moduleId) low = middle + 1; else high = middle;
    }
    if (low == gManifest->count || gManifest->entries[low].moduleId != moduleId) return false;
    *entry = gManifest->entries[low];
    return true;
}

extern "C" bool nmmpProtectionDigestEqual(const uint8_t left[32], const uint8_t right[32]) {
    return left && right && equal(left, right, 32);
}

extern "C" bool nmmpProtectionPointerInImage(const void *pointer) {
#if defined(NMMP_PRIVATE_LINKER)
    if (!pointer || !nmmp_private_image_start || nmmp_private_image_size == 0) return false;
    const uintptr_t value = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t start = reinterpret_cast<uintptr_t>(nmmp_private_image_start);
    return value >= start && value - start < nmmp_private_image_size
           && nmmpImageExecutable(nmmp_private_segments, nmmp_private_segment_count, value);
#else
    return pointer != nullptr;
#endif
}

extern "C" uint32_t nmmpProtectionPolicyFlags(void) {
    return __atomic_load_n(&gManifestInit.state, __ATOMIC_ACQUIRE) == 2 && gManifest ? gManifest->policyFlags : 0;
}

extern "C" uint32_t nmmpProtectionRecheckMillis(void) {
    return __atomic_load_n(&gManifestInit.state, __ATOMIC_ACQUIRE) == 2 && gManifest ? gManifest->recheckMillis : 0;
}

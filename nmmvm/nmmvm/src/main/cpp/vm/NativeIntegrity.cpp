#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "NativeIntegrity.h"
#include "PrivateLoaderState.h"
#include "Sha256.h"
#include "CheckLog.h"
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>

NmmpNativeIntegrityResult nmmpVerifyExecutableSegments(const NmmpImageSegment *segments, size_t count) {
    if (!segments || !count || count > NMMP_PRIVATE_MAX_SEGMENTS) return NMMP_NATIVE_UNAVAILABLE;
    size_t checked = 0, total = 0;
    uint8_t buffer[4096];
    for (size_t i = 0; i < count; ++i) {
        const NmmpImageSegment &segment = segments[i];
        if (!(segment.flags & NMMP_IMAGE_EXEC)) continue;
        if (segment.reserved || (segment.flags & ~7u) || (segment.flags & NMMP_IMAGE_WRITE) || !(segment.flags & NMMP_IMAGE_READ)
                || !segment.file_size || segment.file_size > segment.memory_size
                || segment.file_size > NMMP_PRIVATE_MAX_IMAGE_BYTES - total
                || segment.memory_size > UINTPTR_MAX - segment.start) return NMMP_NATIVE_UNAVAILABLE;
        total += segment.file_size;
        NmmpSha256Context hash;
        nmmpSha256Init(&hash);
        size_t offset = 0;
        unsigned interruptions = 0;
        while (offset < segment.file_size) {
            const size_t size = segment.file_size - offset < sizeof(buffer) ? segment.file_size - offset : sizeof(buffer);
            struct iovec local = {buffer, size};
            struct iovec remote = {reinterpret_cast<void *>(segment.start + offset), size};
            const ssize_t result = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
            if (result < 0 && errno == EINTR && ++interruptions <= 64) continue;
            if (result <= 0 || static_cast<size_t>(result) > size) return NMMP_NATIVE_UNAVAILABLE;
            nmmpSha256Update(&hash, buffer, static_cast<size_t>(result));
            offset += static_cast<size_t>(result);
        }
        uint8_t digest[32], difference = 0;
        nmmpSha256Final(&hash, digest);
        for (size_t b = 0; b < sizeof(digest); ++b) difference |= digest[b] ^ segment.executable_digest[b];
        NMMP_CHECK_LOG("native_segment_index=%zu executable_bytes=%zu status=%s", i, (size_t)segment.file_size, difference ? "MISMATCH" : "PASS");
        if (difference) return NMMP_NATIVE_MISMATCH;
        ++checked;
    }
    return checked ? NMMP_NATIVE_MATCH : NMMP_NATIVE_UNAVAILABLE;
}

NmmpNativeIntegrityResult nmmpVerifyImportSlots(const NmmpImportSlot *slots, size_t count) {
    if (count > NMMP_PRIVATE_MAX_IMPORT_SLOTS || (count && !slots)) return NMMP_NATIVE_UNAVAILABLE;
    if (!count) return NMMP_NATIVE_NOT_APPLICABLE;
    for (size_t i = 0; i < count; ++i) {
        const NmmpImportSlot &slot = slots[i];
        if (slot.reserved || slot.symbol_id < 1 || slot.symbol_id > 6 || (slot.address & (sizeof(uintptr_t) - 1))
                || slot.address > UINTPTR_MAX - sizeof(uintptr_t)) return NMMP_NATIVE_UNAVAILABLE;
        uintptr_t actual;
        struct iovec local = {&actual, sizeof(actual)};
        struct iovec remote = {reinterpret_cast<void *>(slot.address), sizeof(actual)};
        ssize_t result;
        unsigned interruptions = 0;
        do { result = process_vm_readv(getpid(), &local, 1, &remote, 1, 0); }
        while (result < 0 && errno == EINTR && ++interruptions <= 64);
        if (result != sizeof(actual)) return NMMP_NATIVE_UNAVAILABLE;
        NMMP_CHECK_LOG("native_import_index=%zu symbol_id=%u status=%s", i, slot.symbol_id, actual == slot.expected ? "PASS" : "MISMATCH");
        if (actual != slot.expected) return NMMP_NATIVE_MISMATCH;
    }
    return NMMP_NATIVE_MATCH;
}

NmmpNativeIntegrityResult nmmpVerifyPrivateImage() {
#if defined(NMMP_PRIVATE_LINKER)
    if (nmmpPrivateLoaderFailed()) return NMMP_NATIVE_UNAVAILABLE;
    const NmmpNativeIntegrityResult imports = nmmpVerifyImportSlots(nmmp_private_import_slots, nmmp_private_import_slot_count);
    if (imports == NMMP_NATIVE_UNAVAILABLE || imports == NMMP_NATIVE_MISMATCH) return imports;
    return nmmpVerifyExecutableSegments(nmmp_private_segments, nmmp_private_segment_count);
#else
    return NMMP_NATIVE_NOT_APPLICABLE;
#endif
}

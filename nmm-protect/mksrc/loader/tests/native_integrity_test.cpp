#include "NativeIntegrity.h"
#include "Sha256.h"
#include "PrivateLoaderState.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
static void check(bool value) { if (!value) std::abort(); }
extern "C" {
const int *nmmp_private_failure_state;
const void *nmmp_private_image_start;
size_t nmmp_private_image_size;
const NmmpImageSegment *nmmp_private_segments;
size_t nmmp_private_segment_count;
const NmmpImportSlot *nmmp_private_import_slots;
size_t nmmp_private_import_slot_count;
}
int main() {
    check(nmmpVerifyPrivateImage() == NMMP_NATIVE_UNAVAILABLE);
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    auto *memory = static_cast<uint8_t *>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    check(memory != MAP_FAILED);
    std::memset(memory, 0x5a, page * 2);
    NmmpImageSegment segments[2] = {};
    segments[0] = {reinterpret_cast<uintptr_t>(memory), page, page, NMMP_IMAGE_READ | NMMP_IMAGE_EXEC, 0, {0}};
    segments[1] = {reinterpret_cast<uintptr_t>(memory + page), page, page, NMMP_IMAGE_READ | NMMP_IMAGE_WRITE, 0, {0}};
    nmmpSha256(memory, page, segments[0].executable_digest);
    int failure = 0;
    nmmp_private_failure_state = &failure;
    nmmp_private_segments = segments;
    nmmp_private_segment_count = 2;
    check(nmmpVerifyPrivateImage() == NMMP_NATIVE_MATCH);
    failure = 1;
    check(nmmpVerifyPrivateImage() == NMMP_NATIVE_UNAVAILABLE);
    failure = 0;
    auto *slotValue = reinterpret_cast<uintptr_t *>(memory + page);
    *slotValue = 0; // A legitimately unresolved weak import stays zero.
    NmmpImportSlot slot = {reinterpret_cast<uintptr_t>(slotValue), 0, 1, 0};
    nmmp_private_import_slots = &slot;
    nmmp_private_import_slot_count = 1;
    for (uint32_t id = 1; id <= 6; ++id) {
        slot.symbol_id = id;
        check(nmmpVerifyImportSlots(&slot, 1) == NMMP_NATIVE_MATCH);
    }
    check(nmmpVerifyPrivateImage() == NMMP_NATIVE_MATCH);
    *slotValue = 1;
    check(nmmpVerifyPrivateImage() == NMMP_NATIVE_MISMATCH);
    *slotValue = 0;
    check(nmmpVerifyImportSlots(nullptr, 0) == NMMP_NATIVE_NOT_APPLICABLE);
    check(nmmpVerifyImportSlots(nullptr, 1) == NMMP_NATIVE_UNAVAILABLE);
    check(nmmpVerifyImportSlots(&slot, NMMP_PRIVATE_MAX_IMPORT_SLOTS + 1) == NMMP_NATIVE_UNAVAILABLE);
    slot.symbol_id = 7;
    check(nmmpVerifyImportSlots(&slot, 1) == NMMP_NATIVE_UNAVAILABLE);
    slot.symbol_id = 1;
    check(!mprotect(memory + page, page, PROT_NONE));
    check(nmmpVerifyImportSlots(&slot, 1) == NMMP_NATIVE_UNAVAILABLE);
    check(!mprotect(memory + page, page, PROT_READ | PROT_WRITE));
    check(nmmpVerifyExecutableSegments(segments, 2) == NMMP_NATIVE_MATCH);
    memory[page + 10] ^= 1;
    check(nmmpVerifyExecutableSegments(segments, 2) == NMMP_NATIVE_MATCH);
    memory[page - 1] ^= 1;
    check(nmmpVerifyExecutableSegments(segments, 2) == NMMP_NATIVE_MISMATCH);
    memory[page - 1] ^= 1;
    check(!mprotect(memory, page, PROT_NONE));
    check(nmmpVerifyExecutableSegments(segments, 2) == NMMP_NATIVE_UNAVAILABLE);
    check(!mprotect(memory, page, PROT_READ | PROT_WRITE));
    check(nmmpVerifyExecutableSegments(nullptr, 1) == NMMP_NATIVE_UNAVAILABLE);
    check(nmmpVerifyExecutableSegments(segments, NMMP_PRIVATE_MAX_SEGMENTS + 1) == NMMP_NATIVE_UNAVAILABLE);
    check(nmmpVerifyExecutableSegments(segments + 1, 1) == NMMP_NATIVE_UNAVAILABLE);
    segments[0].file_size = page + 1;
    check(nmmpVerifyExecutableSegments(segments, 1) == NMMP_NATIVE_UNAVAILABLE);
    check(!munmap(memory, page * 2));
}

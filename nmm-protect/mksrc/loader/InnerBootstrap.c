#include "Bootstrap.h"
#include "BuildId.h"
#include "ProtectionManifestConfig.h"
#include "PrivateLoaderState.h"
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

extern jint nmmp_inner_on_load(JavaVM *vm, void *reserved);
const int *nmmp_private_failure_state;
const void *nmmp_private_image_start;
size_t nmmp_private_image_size;
const NmmpImageSegment *nmmp_private_segments;
size_t nmmp_private_segment_count;
const NmmpImportSlot *nmmp_private_import_slots;
size_t nmmp_private_import_slot_count;

__attribute__((visibility("default")))
int nmmp_inner_bootstrap_v1(JavaVM *vm, void *reserved, const NmmpHostV1 *host, NmmpInnerResultV1 *out) {
    if (nmmp_private_segments || !host || !out || host->abi_version != NMMP_PRIVATE_BOOTSTRAP_ABI || host->struct_size != sizeof(*host) ||
        out->abi_version != NMMP_PRIVATE_BOOTSTRAP_ABI || out->struct_size != sizeof(*out) || !host->outer_anchor ||
        !host->failure_state || !host->image_start || !host->image_size || host->image_size > NMMP_PRIVATE_MAX_IMAGE_BYTES ||
        !host->segments || !host->segment_count || host->segment_count > NMMP_PRIVATE_MAX_SEGMENTS ||
        host->import_slot_count > NMMP_PRIVATE_MAX_IMPORT_SLOTS || (host->import_slot_count && !host->import_slots) ||
        memcmp(host->build_id, nmmp_build_id, 16) ||
        memcmp(host->manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16)) return -1;
    const uintptr_t entry = (uintptr_t)&nmmp_inner_bootstrap_v1;
    const uintptr_t start = (uintptr_t)host->image_start;
    if (host->image_size > UINTPTR_MAX - start || entry < start || entry - start >= host->image_size) return -1;
    for (size_t i = 0; i < host->segment_count; ++i) {
        const NmmpImageSegment *s = host->segments + i;
        if (s->reserved || (s->flags & ~7u) || !s->memory_size || s->file_size > s->memory_size
                || s->start < start || s->start - start >= host->image_size
                || s->memory_size > host->image_size - (s->start - start)
                || ((s->flags & NMMP_IMAGE_EXEC) && (s->flags & NMMP_IMAGE_WRITE))) return -1;
        for (size_t j = 0; j < i; ++j) {
            const NmmpImageSegment *previous = host->segments + j;
            if (s->start < previous->start + previous->memory_size
                    && previous->start < s->start + s->memory_size) return -1;
        }
    }
    if (!nmmpImageExecutable(host->segments, host->segment_count, entry)) return -1;
    for (size_t i = 0; i < host->import_slot_count; ++i) {
        const NmmpImportSlot *slot = host->import_slots + i;
        if (slot->reserved || slot->symbol_id < 1 || slot->symbol_id > 4 || (slot->address & (sizeof(uintptr_t) - 1))) return -1;
        int contained = 0;
        for (size_t s = 0; s < host->segment_count; ++s) {
            const NmmpImageSegment *segment = host->segments + s;
            if ((segment->flags & NMMP_IMAGE_WRITE) && !(segment->flags & NMMP_IMAGE_EXEC)
                    && slot->address >= segment->start && slot->address - segment->start <= segment->memory_size
                    && sizeof(uintptr_t) <= segment->memory_size - (slot->address - segment->start)) contained = 1;
        }
        if (!contained) return -1;
        for (size_t previous = 0; previous < i; ++previous)
            if (host->import_slots[previous].address == slot->address) return -1;
    }
    const size_t segments_size = host->segment_count * sizeof(NmmpImageSegment);
    const size_t slots_size = host->import_slot_count * sizeof(NmmpImportSlot);
    const size_t layout_size = segments_size + slots_size;
    void *layout = mmap(NULL, layout_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (layout == MAP_FAILED) return -1;
    memcpy(layout, host->segments, segments_size);
    if (slots_size) memcpy((uint8_t *)layout + segments_size, host->import_slots, slots_size);
    if (mprotect(layout, layout_size, PROT_READ)) { munmap(layout, layout_size); return -1; }
    nmmp_private_failure_state = host->failure_state;
    nmmp_private_image_start = host->image_start;
    nmmp_private_image_size = host->image_size;
    nmmp_private_segments = layout;
    nmmp_private_segment_count = host->segment_count;
    nmmp_private_import_slots = (const NmmpImportSlot *)((const uint8_t *)layout + segments_size);
    nmmp_private_import_slot_count = host->import_slot_count;
    memcpy(out->build_id, nmmp_build_id, 16);
    memcpy(out->manifest_id, NMMP_PROTECTION_MANIFEST_ID, 16);
    out->jni_version = nmmp_inner_on_load(vm, reserved);
    out->error = out->jni_version == JNI_VERSION_1_6 ? 0 : -2;
    return out->error;
}

#include "Bootstrap.h"
#include "PrivateLoaderState.h"
#include <stdlib.h>
#include <string.h>
extern int runtime_inner_bootstrap_v1(JavaVM *, void *, const NmmpHostV1 *, NmmpInnerResultV1 *);
static unsigned calls;
jint nmmp_inner_on_load(JavaVM *vm, void *reserved) {
    (void)vm; (void)reserved;
    ++calls;
    return JNI_VERSION_1_6;
}
static void check(int ok) { if (!ok) abort(); }
int main(void) {
    int failed = 0;
    const uintptr_t start = (uintptr_t)&runtime_inner_bootstrap_v1;
    NmmpImageSegment segment = {start, 128, 256, NMMP_IMAGE_READ | NMMP_IMAGE_EXEC, 0};
    NmmpHostV1 host = {0};
    host.abi_version = NMMP_PRIVATE_BOOTSTRAP_ABI;
    host.struct_size = sizeof(host);
    host.outer_anchor = &failed;
    host.failure_state = &failed;
    host.image_start = (void *)start;
    host.image_size = 256;
    host.segments = &segment;
    host.segment_count = 1;
    NmmpInnerResultV1 result = {0};
    result.abi_version = NMMP_PRIVATE_BOOTSTRAP_ABI;
    result.struct_size = sizeof(result);
    for (unsigned mode = 0; mode < 11; ++mode) {
        NmmpHostV1 altered = host;
        NmmpImageSegment bad = segment;
        altered.segments = &bad;
        switch (mode) {
            case 0: altered.abi_version = 2; break;
            case 1: altered.segment_count = 0; break;
            case 2: altered.segment_count = NMMP_PRIVATE_MAX_SEGMENTS + 1; break;
            case 3: altered.segments = NULL; break;
            case 4: bad.memory_size = 257; break;
            case 5: bad.file_size = 257; break;
            case 6: bad.flags |= NMMP_IMAGE_WRITE; break;
            case 7: bad.flags = NMMP_IMAGE_READ; break;
            case 8: bad.reserved = 1; break;
            case 9: altered.abi_version = 3; break;
            case 10: altered.abi_version = 4; break;
        }
        check(runtime_inner_bootstrap_v1(NULL, NULL, &altered, &result) != 0 && calls == 0);
        check(nmmp_private_segments == NULL);
    }
    NmmpImageSegment overlapping[2] = {segment, segment};
    host.segments = overlapping;
    host.segment_count = 2;
    check(runtime_inner_bootstrap_v1(NULL, NULL, &host, &result) != 0 && calls == 0);
    host.segments = &segment;
    host.segment_count = 1;
    NmmpImageSegment registered[2] = {segment, {start + 256, 128, 128, NMMP_IMAGE_READ | NMMP_IMAGE_WRITE, 0, {0}}};
    NmmpImportSlot slots[2] = {{(start + 263) & ~(uintptr_t)7, 0, 1, 0}, {0}};
    slots[1] = slots[0];
    host.segments = registered;
    host.segment_count = 2;
    host.image_size = 384;
    host.import_slots = slots;
    host.import_slot_count = 1;
    for (unsigned mode = 0; mode < 8; ++mode) {
        NmmpHostV1 altered = host;
        NmmpImportSlot bad[2] = {slots[0], slots[1]};
        altered.import_slots = bad;
        switch (mode) {
            case 0: altered.import_slot_count = NMMP_PRIVATE_MAX_IMPORT_SLOTS + 1; break;
            case 1: altered.import_slots = NULL; break;
            case 2: bad[0].address = start; break; // executable segment
            case 3: bad[0].address = start + 384; break;
            case 4: bad[0].symbol_id = 5; break;
            case 5: bad[0].reserved = 1; break;
            case 6: ++bad[0].address; break;
            case 7: altered.import_slot_count = 2; break;
        }
        check(runtime_inner_bootstrap_v1(NULL, NULL, &altered, &result) != 0 && calls == 0);
        check(nmmp_private_segments == NULL && nmmp_private_import_slots == NULL);
    }
    check(!runtime_inner_bootstrap_v1(NULL, NULL, &host, &result) && calls == 1);
    check(nmmp_private_segment_count == 2 && nmmp_private_segments[0].start == start);
    check(nmmp_private_import_slot_count == 1 && nmmp_private_import_slots[0].address == slots[0].address);
    check(nmmpImageExecutable(nmmp_private_segments, 1, start + 127));
    check(!nmmpImageExecutable(nmmp_private_segments, 1, start + 128));
    check(runtime_inner_bootstrap_v1(NULL, NULL, &host, &result) != 0 && calls == 1);
    return 0;
}

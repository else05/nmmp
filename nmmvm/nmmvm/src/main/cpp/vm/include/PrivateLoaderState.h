#ifndef NMMP_PRIVATE_LOADER_STATE_H
#define NMMP_PRIVATE_LOADER_STATE_H
#include <stddef.h>
#include "PrivateImageLayout.h"
#if defined(NMMP_PRIVATE_LINKER)
#ifdef __cplusplus
extern "C" {
#endif
extern const int *nmmp_private_failure_state;
extern const void *nmmp_private_image_start;
extern size_t nmmp_private_image_size;
extern const NmmpImageSegment *nmmp_private_segments;
extern size_t nmmp_private_segment_count;
extern const NmmpImportSlot *nmmp_private_import_slots;
extern size_t nmmp_private_import_slot_count;
#ifdef __cplusplus
}
#endif
static inline int nmmpPrivateLoaderFailed(void) {
    return nmmp_private_failure_state == 0 ||
           __atomic_load_n(nmmp_private_failure_state, __ATOMIC_ACQUIRE);
}
#endif
#endif

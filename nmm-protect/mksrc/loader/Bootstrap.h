#ifndef NMMP_PRIVATE_BOOTSTRAP_H
#define NMMP_PRIVATE_BOOTSTRAP_H
#include <jni.h>
#include <stddef.h>
#include <stdint.h>
#include "PrivateImageLayout.h"

#define NMMP_PRIVATE_BOOTSTRAP_ABI 5u

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t build_id[16];
    uint8_t manifest_id[16];
    const void *outer_anchor;
    const int *failure_state;
    const void *image_start;
    size_t image_size;
    const NmmpImageSegment *segments;
    size_t segment_count;
    const NmmpImportSlot *import_slots;
    size_t import_slot_count;
} NmmpHostV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t build_id[16];
    uint8_t manifest_id[16];
    int32_t jni_version;
    int32_t error;
} NmmpInnerResultV1;

typedef int (*NmmpBootstrapV1)(JavaVM *, void *, const NmmpHostV1 *, NmmpInnerResultV1 *);
#endif

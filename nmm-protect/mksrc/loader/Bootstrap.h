#ifndef NMMP_PRIVATE_BOOTSTRAP_H
#define NMMP_PRIVATE_BOOTSTRAP_H
#include <jni.h>
#include <stdint.h>

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t build_id[16];
    const void *outer_anchor;
    const int *failure_state;
} NmmpHostV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint8_t build_id[16];
    int32_t jni_version;
    int32_t error;
} NmmpInnerResultV1;

typedef int (*NmmpBootstrapV1)(JavaVM *, void *, const NmmpHostV1 *, NmmpInnerResultV1 *);
#endif

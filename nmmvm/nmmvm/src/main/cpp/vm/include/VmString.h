#ifndef NMMP_VM_STRING_H
#define NMMP_VM_STRING_H

#include "VmCodec.h"
#include <stdlib.h>
#include <string.h>

// Each allocation owns its length, so cleanup also works after partial decoding
// and never relies on a potentially damaged NUL terminator.
static inline void vmStringRelease(const char *text) {
    if (!text) return;
    size_t *allocation = (size_t *)text - 1;
    size_t size = *allocation + sizeof(size_t);
    volatile unsigned char *bytes = (volatile unsigned char *)allocation;
    while (size--) *bytes++ = 0;
    free(allocation);
}

static inline void vmStringCleanup(char **text) {
    vmStringRelease(*text);
    *text = NULL;
}

static inline char *vmStringDecode(const uint8_t *cipher, uint32_t size,
                                    uint32_t id, uint32_t offset) {
    size_t allocationSize = sizeof(size_t) + (size_t)size;
    if (!cipher || !size || allocationSize < size) return NULL;
    size_t *allocation = (size_t *)malloc(allocationSize);
    if (!allocation) return NULL;
    *allocation = size;
    char *text = (char *)(allocation + 1);
    memcpy(text, cipher, size);
    if (!vmCodecTransformRange((uint8_t *)text, size, id, NMMP_VM_DOMAIN_STRING, offset)
            || text[size - 1] || memchr(text, 0, size - 1)) {
        vmStringRelease(text);
        return NULL;
    }
    return text;
}

// Generated resolvers are C, compiled by Clang. Cleanup covers every return and
// nested/reentrant JNI call owns a separate allocation (no shared scratch).
#define NMMP_TEMP_STRING __attribute__((cleanup(vmStringCleanup))) char *

#endif

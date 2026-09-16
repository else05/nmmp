#ifndef NMMP_PRIVATE_IMAGE_LAYOUT_H
#define NMMP_PRIVATE_IMAGE_LAYOUT_H
#include <stddef.h>
#include <stdint.h>

#define NMMP_PRIVATE_MAX_SEGMENTS 16u
#define NMMP_PRIVATE_MAX_IMPORT_SLOTS 64u
#define NMMP_PRIVATE_MAX_IMAGE_BYTES (64u * 1024u * 1024u)
#define NMMP_EXECUTABLE_SHARD_COUNT 3u
/* ELF PF_* bits, deliberately distinct from ProcessMaps permission bits. */
#define NMMP_IMAGE_EXEC 1u
#define NMMP_IMAGE_WRITE 2u
#define NMMP_IMAGE_READ 4u
typedef struct {
    uintptr_t start;
    size_t file_size;
    size_t memory_size;
    uint32_t flags;
    uint32_t reserved;
    uint8_t executable_digests[NMMP_EXECUTABLE_SHARD_COUNT][32];
} NmmpImageSegment;
typedef struct {
    uintptr_t address;
    uintptr_t expected;
    uint32_t symbol_id; /* 1=open, 2=open64, 3=openat, 4=openat64; outer: 5=mmap, 6=mprotect */
    uint32_t reserved;
} NmmpImportSlot;

static inline int nmmpExecutableShardRange(size_t size, uint32_t shard,
        size_t *offset, size_t *length) {
    if (shard >= NMMP_EXECUTABLE_SHARD_COUNT || !offset || !length) return 0;
    const size_t base = size / NMMP_EXECUTABLE_SHARD_COUNT;
    const size_t remainder = size % NMMP_EXECUTABLE_SHARD_COUNT;
    *offset = shard * base + (shard < remainder ? shard : remainder);
    *length = base + (shard < remainder ? 1u : 0u);
    return 1;
}

static inline int nmmpImageExecutable(const NmmpImageSegment *segments, size_t count, uintptr_t address) {
    if (!segments || !count || count > NMMP_PRIVATE_MAX_SEGMENTS) return 0;
    for (size_t i = 0; i < count; ++i) {
        const NmmpImageSegment *s = segments + i;
        if ((s->flags & NMMP_IMAGE_EXEC) && !(s->flags & NMMP_IMAGE_WRITE)
                && address >= s->start && address - s->start < s->file_size) return 1;
    }
    return 0;
}
#endif

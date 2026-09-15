#ifndef NMMP_PROCESS_MAPS_H
#define NMMP_PROCESS_MAPS_H

#include <stddef.h>
#include <stdint.h>

enum NmmpMapsStatus { NMMP_MAPS_COMPLETE, NMMP_MAPS_IO_ERROR, NMMP_MAPS_INVALID, NMMP_MAPS_LIMIT };
enum { NMMP_MAP_READ = 1, NMMP_MAP_WRITE = 2, NMMP_MAP_EXEC = 4, NMMP_MAP_PRIVATE = 8 };
struct NmmpMapEntry {
    uintptr_t start, end;
    uint64_t offset, inode;
    uint32_t deviceMajor, deviceMinor, permissions, nameOffset;
};
// Per-sample scratch, never a source of authority for dereferencing an address.
struct NmmpMapsSnapshot {
    NmmpMapsStatus status;
    size_t count, namesSize;
    NmmpMapEntry entries[4096];
    char names[256 * 1024];
};
typedef long (*NmmpMapsRead)(void *context, void *buffer, size_t size);
// Caller owns the snapshot. A partial/invalid snapshot must not be queried.
void nmmpParseMaps(NmmpMapsSnapshot *snapshot, NmmpMapsRead read, void *context);
void nmmpReadProcessMaps(NmmpMapsSnapshot *snapshot);
const NmmpMapEntry *nmmpFindMapping(const NmmpMapsSnapshot *snapshot, uintptr_t address);
const char *nmmpMappingName(const NmmpMapsSnapshot *snapshot, const NmmpMapEntry *entry);

#endif

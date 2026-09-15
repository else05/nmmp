#include "ProcessMaps.h"
#include "Arm64Syscall.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>

namespace {
bool number(const char *&p, unsigned base, uint64_t limit, uint64_t *value) {
    const char *first = p;
    uint64_t result = 0;
    for (;;) {
        const unsigned c = static_cast<unsigned char>(*p);
        const unsigned digit = c >= '0' && c <= '9' ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
        if (digit >= base) break;
        if (result > (limit - digit) / base) return false;
        result = result * base + digit;
        ++p;
    }
    *value = result;
    return p != first;
}
bool spaces(const char *&p) {
    if (*p != ' ' && *p != '\t') return false;
    do { ++p; } while (*p == ' ' || *p == '\t');
    return true;
}
NmmpMapsStatus append(NmmpMapsSnapshot *snapshot, const char *line) {
    if (snapshot->count == sizeof(snapshot->entries) / sizeof(snapshot->entries[0])) return NMMP_MAPS_LIMIT;
    const char *p = line;
    uint64_t start, end, offset, major, minor, inode;
    if (!number(p, 16, UINTPTR_MAX, &start) || *p++ != '-'
            || !number(p, 16, UINTPTR_MAX, &end) || start >= end || !spaces(p)) return NMMP_MAPS_INVALID;
    if (std::strlen(p) < 4 || (p[0] != 'r' && p[0] != '-') || (p[1] != 'w' && p[1] != '-')
            || (p[2] != 'x' && p[2] != '-') || (p[3] != 'p' && p[3] != 's')) return NMMP_MAPS_INVALID;
    const uint32_t permissions = (p[0] == 'r' ? NMMP_MAP_READ : 0) | (p[1] == 'w' ? NMMP_MAP_WRITE : 0)
            | (p[2] == 'x' ? NMMP_MAP_EXEC : 0) | (p[3] == 'p' ? NMMP_MAP_PRIVATE : 0);
    p += 4;
    if (!spaces(p) || !number(p, 16, UINT64_MAX, &offset) || !spaces(p)
            || !number(p, 16, UINT32_MAX, &major) || *p++ != ':'
            || !number(p, 16, UINT32_MAX, &minor) || !spaces(p)
            || !number(p, 10, UINT64_MAX, &inode)) return NMMP_MAPS_INVALID;
    if (*p && !spaces(p)) return NMMP_MAPS_INVALID;
    if (snapshot->count && start < snapshot->entries[snapshot->count - 1].end) return NMMP_MAPS_INVALID;
    const size_t length = std::strlen(p) + 1;
    if (length > sizeof(snapshot->names) - snapshot->namesSize) return NMMP_MAPS_LIMIT;
    NmmpMapEntry &entry = snapshot->entries[snapshot->count++];
    entry = {static_cast<uintptr_t>(start), static_cast<uintptr_t>(end), offset, inode,
             static_cast<uint32_t>(major), static_cast<uint32_t>(minor), permissions,
             static_cast<uint32_t>(snapshot->namesSize)};
    std::memcpy(snapshot->names + snapshot->namesSize, p, length);
    snapshot->namesSize += length;
    return NMMP_MAPS_COMPLETE;
}
long readFd(void *context, void *buffer, size_t size) {
    return nmmpRawRead(*static_cast<int *>(context), buffer, size);
}
}

void nmmpParseMaps(NmmpMapsSnapshot *snapshot, NmmpMapsRead read, void *context) {
    if (!snapshot) return;
    snapshot->count = snapshot->namesSize = 0;
    snapshot->status = NMMP_MAPS_INVALID;
    if (!read) return;
    char buffer[4096], line[4096];
    size_t used = 0, total = 0;
    unsigned interruptions = 0;
    for (;;) {
        const long count = read(context, buffer, sizeof(buffer));
        if (count == -EINTR && ++interruptions <= 64) continue;
        if (count < 0 || static_cast<size_t>(count) > sizeof(buffer)) {
            snapshot->status = NMMP_MAPS_IO_ERROR;
            return;
        }
        if (!count) {
            // proc maps emits complete newline-terminated records. A trailing
            // partial line is not a valid clean observation.
            snapshot->status = used || !snapshot->count ? NMMP_MAPS_INVALID : NMMP_MAPS_COMPLETE;
            return;
        }
        total += static_cast<size_t>(count);
        if (total > 2U * 1024U * 1024U) { snapshot->status = NMMP_MAPS_LIMIT; return; }
        for (long i = 0; i < count; ++i) {
            if (buffer[i] == '\n') {
                line[used] = 0;
                snapshot->status = append(snapshot, line);
                if (snapshot->status != NMMP_MAPS_COMPLETE) return;
                used = 0;
            } else {
                if (!buffer[i]) { snapshot->status = NMMP_MAPS_INVALID; return; }
                if (used == sizeof(line) - 1) { snapshot->status = NMMP_MAPS_LIMIT; return; }
                line[used++] = buffer[i];
            }
        }
    }
}

void nmmpReadProcessMaps(NmmpMapsSnapshot *snapshot) {
    if (!snapshot) return;
    const long descriptor = nmmpRawOpenAt(AT_FDCWD, "/proc/self/maps", O_RDONLY | O_CLOEXEC, 0);
    if (descriptor < 0) {
        snapshot->count = snapshot->namesSize = 0;
        snapshot->status = NMMP_MAPS_IO_ERROR;
        return;
    }
    int fd = static_cast<int>(descriptor);
    nmmpParseMaps(snapshot, readFd, &fd);
    nmmpRawClose(fd);
}

const NmmpMapEntry *nmmpFindMapping(const NmmpMapsSnapshot *snapshot, uintptr_t address) {
    if (!snapshot || snapshot->status != NMMP_MAPS_COMPLETE) return nullptr;
    size_t low = 0, high = snapshot->count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const NmmpMapEntry *entry = snapshot->entries + middle;
        if (address < entry->start) high = middle;
        else if (address >= entry->end) low = middle + 1;
        else return entry;
    }
    return nullptr;
}

const char *nmmpMappingName(const NmmpMapsSnapshot *snapshot, const NmmpMapEntry *entry) {
    if (!snapshot || !entry || snapshot->status != NMMP_MAPS_COMPLETE
            || entry->nameOffset >= snapshot->namesSize) return nullptr;
    return snapshot->names + entry->nameOffset;
}

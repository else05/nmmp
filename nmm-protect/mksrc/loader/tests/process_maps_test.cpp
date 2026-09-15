#include "ProcessMaps.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>
#include <cstdio>

long nmmpRawOpenAt(int, const char *, int, unsigned) { return -EACCES; }
long nmmpRawRead(int, void *, size_t) { return -EIO; }
long nmmpRawClose(int) { return 0; }
struct Input { std::string bytes; size_t offset = 0; size_t chunk = 13; unsigned interrupts = 0; };
static long readInput(void *opaque, void *buffer, size_t size) {
    auto *input = static_cast<Input *>(opaque);
    if (input->interrupts) { --input->interrupts; return -EINTR; }
    size_t count = input->bytes.size() - input->offset;
    if (count > size) count = size;
    if (count > input->chunk) count = input->chunk;
    std::memcpy(buffer, input->bytes.data() + input->offset, count);
    input->offset += count;
    return static_cast<long>(count);
}
static void check(bool value) { if (!value) std::abort(); }
int main() {
    auto *snapshot = static_cast<NmmpMapsSnapshot *>(std::malloc(sizeof(NmmpMapsSnapshot)));
    check(snapshot != nullptr);
    Input input;
    input.bytes = "1000-2000 r-xp 0000 08:01 42 /path with spaces/lib.so (deleted)\n"
                  "3000-4000 rw-s 1000 00:00 0\n";
    input.interrupts = 2;
    nmmpParseMaps(snapshot, readInput, &input);
    check(snapshot->status == NMMP_MAPS_COMPLETE && snapshot->count == 2);
    const NmmpMapEntry *entry = nmmpFindMapping(snapshot, 0x1fff);
    check(entry && entry->inode == 42 && entry->deviceMajor == 8 && entry->deviceMinor == 1);
    check(entry->permissions == (NMMP_MAP_READ | NMMP_MAP_EXEC | NMMP_MAP_PRIVATE));
    check(!std::strcmp(nmmpMappingName(snapshot, entry), "/path with spaces/lib.so (deleted)"));
    check(!nmmpFindMapping(snapshot, 0x2000) && !nmmpFindMapping(snapshot, 0x2fff));
    check(nmmpFindMapping(snapshot, 0x3000)->offset == 0x1000);
    check(!std::strcmp(nmmpMappingName(snapshot, snapshot->entries + 1), ""));
    const char *invalid[] = {"", "1000-2000 r-xp 0 00:00 0", "2000-1000 r-xp 0 00:00 0\n",
        "0-10000000000000000 r-xp 0 00:00 0\n", "1000-2000 rwxp 0 00:00 18446744073709551616\n",
        "1000-2000 rwxq 0 00:00 0\n", "1000-3000 r-xp 0 00:00 0\n2000-4000 r-xp 0 00:00 0\n"};
    for (const char *value : invalid) {
        input = Input(); input.bytes = value;
        nmmpParseMaps(snapshot, readInput, &input);
        check(snapshot->status == NMMP_MAPS_INVALID && !nmmpFindMapping(snapshot, 0x1000));
    }
    input = Input(); input.bytes = "1000-2000 r-xp 0 00:00 0 "; input.bytes.append(4096, 'a'); input.bytes += '\n';
    nmmpParseMaps(snapshot, readInput, &input);
    check(snapshot->status == NMMP_MAPS_LIMIT);
    input = Input(); input.bytes = "1000-2000 r-xp 0 00:00 0\n"; input.interrupts = 65;
    nmmpParseMaps(snapshot, readInput, &input);
    check(snapshot->status == NMMP_MAPS_IO_ERROR);
    input = Input(); input.chunk = 4096;
    for (unsigned i = 1; i <= 4097; ++i) {
        char line[100];
        std::snprintf(line, sizeof(line), "%x-%x r-xp 0 00:00 0\n", i * 4096, (i + 1) * 4096);
        input.bytes += line;
    }
    nmmpParseMaps(snapshot, readInput, &input);
    check(snapshot->status == NMMP_MAPS_LIMIT && snapshot->count == 4096);
    nmmpReadProcessMaps(snapshot);
    check(snapshot->status == NMMP_MAPS_IO_ERROR && snapshot->count == 0);
    uint32_t random = 0x52a81;
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        input = Input();
        input.bytes = "1000-2000 r-xp 0000 08:01 42 /path with spaces/lib.so\n";
        random = random * 1664525U + 1013904223U;
        input.bytes[random % input.bytes.size()] = static_cast<char>(random >> 24);
        nmmpParseMaps(snapshot, readInput, &input);
        if (snapshot->status == NMMP_MAPS_COMPLETE) {
            check(snapshot->count == 1);
            check(snapshot->entries[0].start < snapshot->entries[0].end);
        } else {
            check(!nmmpFindMapping(snapshot, 0x1000));
        }
    }
    std::free(snapshot);
}

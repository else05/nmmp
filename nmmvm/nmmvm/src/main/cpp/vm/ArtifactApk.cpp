#include "ArtifactApk.h"
#include "Arm64Syscall.h"
#include "Sha256.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

namespace {
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
bool read(int fd, uint64_t offset, void *buffer, size_t size) {
    uint8_t *p = static_cast<uint8_t *>(buffer);
    unsigned interrupted = 0;
    while (size) {
        long n = nmmpRawPread64(fd, p, size, offset);
        if (n == -EINTR && ++interrupted <= 64) continue;
        if (n <= 0 || static_cast<size_t>(n) > size) return false;
        p += n;
        size -= n;
        offset += n;
    }
    return true;
}

struct Directory { uint64_t start, size; uint16_t count; };
bool directory(int fd, uint64_t size, Directory *result) {
    if (size < 22 || size >= UINT32_MAX) return false;
    const size_t length = size < 65557 ? static_cast<size_t>(size) : 65557;
    auto *tail = static_cast<uint8_t *>(std::malloc(length));
    if (!tail) return false;
    if (!read(fd, size - length, tail, length)) { std::free(tail); return false; }
    unsigned found = 0;
    for (size_t i = length - 22;; --i) {
        const uint8_t *p = tail + i;
        if (u32(p) == 0x06054b50 && i + 22 + u16(p + 20) == length
                && !u16(p + 4) && !u16(p + 6) && u16(p + 8) == u16(p + 10)
                && u16(p + 10) != UINT16_MAX && u32(p + 12) != UINT32_MAX && u32(p + 16) != UINT32_MAX
                && uint64_t(u32(p + 12)) + u32(p + 16) == size - length + i) {
            result->start = u32(p + 16);
            result->size = u32(p + 12);
            result->count = u16(p + 10);
            ++found;
        }
        if (!i) break;
    }
    std::free(tail);
    return found == 1 && result->count && result->size <= 64u * 1024 * 1024;
}

bool extras(int fd, uint64_t offset, uint16_t size, bool localAlignment = false) {
    while (size) {
        uint8_t header[4];
        if (size < 4) {
            // zipflinger pads local headers with 1..3 zero bytes for align(4).
            // Accept only this bounded padding; central extras remain strict.
            if (!localAlignment || !read(fd, offset, header, size)) return false;
            for (uint16_t i = 0; i < size; ++i) if (header[i]) return false;
            return true;
        }
        if (!read(fd, offset, header, 4)) return false;
        const uint16_t length = u16(header + 2);
        if (u16(header) == 1 || length > size - 4) return false; // ZIP64 unsupported
        size -= 4 + length;
        offset += 4 + length;
    }
    return true;
}

bool dex(const uint8_t *name, size_t size) {
    if (size < 11 || std::memcmp(name, "classes", 7) || std::memcmp(name + size - 4, ".dex", 4)) return false;
    for (size_t i = 7; i < size - 4; ++i) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}
size_t lookup(const NmmpArtifactEntry *entries, size_t count, const uint8_t *name, size_t size) {
    size_t begin = 0, end = count;
    while (begin < end) {
        const size_t middle = begin + (end - begin) / 2;
        const NmmpArtifactEntry &entry = entries[middle];
        const size_t common = entry.name_size < size ? entry.name_size : size;
        int order = std::memcmp(entry.name, name, common);
        if (!order) order = entry.name_size < size ? -1 : entry.name_size > size ? 1 : 0;
        if (!order) return middle;
        if (order < 0) begin = middle + 1; else end = middle;
    }
    return count;
}

bool content(int fd, uint64_t offset, uint32_t compressed, uint16_t method,
             uint32_t expected_crc, const NmmpArtifactEntry &entry, uint8_t *copy = nullptr) {
    uint8_t input[16384], output[16384];
    NmmpSha256Context hash;
    nmmpSha256Init(&hash);
    uLong crc = crc32(0, Z_NULL, 0);
    uint64_t total = 0;
    z_stream stream = {};
    if (method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;
    bool ok = true, ended = method == 0;
    uint32_t remaining = compressed;
    while (remaining || (method == 8 && !ended)) {
        if (method == 0 || !stream.avail_in) {
            const size_t amount = remaining < sizeof(input) ? remaining : sizeof(input);
            if (amount && !read(fd, offset, input, amount)) { ok = false; break; }
            offset += amount;
            remaining -= amount;
            stream.next_in = input;
            stream.avail_in = static_cast<uInt>(amount);
        }
        size_t produced;
        const uint8_t *bytes;
        if (method == 0) {
            produced = stream.avail_in;
            bytes = input;
        } else {
            stream.next_out = output;
            stream.avail_out = sizeof(output);
            const uInt before = stream.avail_in;
            const int status = inflate(&stream, Z_NO_FLUSH);
            produced = sizeof(output) - stream.avail_out;
            bytes = output;
            ended = status == Z_STREAM_END;
            if ((status != Z_OK && !ended) || (!ended && !produced && before == stream.avail_in)) { ok = false; break; }
        }
        if (produced > entry.size - total) { ok = false; break; }
        if (copy) std::memcpy(copy + total, bytes, produced);
        total += produced;
        nmmpSha256Update(&hash, bytes, produced);
        crc = crc32(crc, bytes, static_cast<uInt>(produced));
        if (method == 8 && ended) { if (remaining || stream.avail_in) ok = false; break; }
    }
    if (method == 8) inflateEnd(&stream);
    if (!ok || !ended || total != entry.size || crc != expected_crc) return false;
    uint8_t digest[32], difference = 0;
    nmmpSha256Final(&hash, digest);
    for (size_t i = 0; i < 32; ++i) difference |= digest[i] ^ entry.sha256[i];
    return difference == 0;
}

struct Range { uint64_t start, end; };
bool verify(int fd, const Directory &directory, const NmmpArtifactEntry *entries, size_t count, bool requireDexSet = true, uint8_t *copy = nullptr) {
    if (copy && count != 1) return false;
    Range ranges[NMMP_ARTIFACT_MAX_ENTRIES] = {};
    uint8_t name[NMMP_ARTIFACT_MAX_NAME], local_name[NMMP_ARTIFACT_MAX_NAME];
    size_t found = 0;
    uint64_t position = directory.start, end = directory.start + directory.size;
    for (unsigned i = 0; i < directory.count; ++i) {
        uint8_t central[46];
        if (end - position < sizeof(central) || !read(fd, position, central, sizeof(central)) || u32(central) != 0x02014b50) return false;
        const uint16_t length = u16(central + 28), extra = u16(central + 30), comment = u16(central + 32);
        const uint64_t next = position + sizeof(central) + length + extra + comment;
        if (next > end || !length || u16(central + 34) || u32(central + 20) == UINT32_MAX
                || u32(central + 24) == UINT32_MAX || u32(central + 42) == UINT32_MAX
                || !extras(fd, position + sizeof(central) + length, extra)) return false;
        if (length > sizeof(name)) return false;
        if (!read(fd, position + sizeof(central), name, length) || std::memchr(name, 0, length)) return false;
        position = next;
        const size_t index = lookup(entries, count, name, length);
        if (index == count) { if (requireDexSet && dex(name, length)) return false; continue; }
        if (ranges[index].end) return false;
        const NmmpArtifactEntry &entry = entries[index];
        const uint16_t flags = u16(central + 8), method = u16(central + 10);
        const uint32_t crc = u32(central + 16), compressed = u32(central + 20), size = u32(central + 24);
        const uint64_t start = u32(central + 42);
        if ((flags & ~0x080eu) || (method != 0 && method != 8) || entry.size != size
                || compressed > NMMP_ARTIFACT_MAX_ENTRY_BYTES + 1024 * 1024
                || (method == 0 && compressed != size) || start > directory.start || directory.start - start < 30) return false;
        uint8_t local[30];
        if (!read(fd, start, local, sizeof(local)) || u32(local) != 0x04034b50
                || u16(local + 6) != flags || u16(local + 8) != method || u16(local + 26) != length) return false;
        const uint16_t local_extra = u16(local + 28);
        const uint64_t data = start + 30 + length + local_extra, data_end = data + compressed;
        if (data_end > directory.start || !read(fd, start + 30, local_name, length)
                || std::memcmp(name, local_name, length) || !extras(fd, start + 30 + length, local_extra, true)) return false;
        const uint32_t expected[] = {crc, compressed, size};
        for (size_t field = 0; field < 3; ++field) {
            uint32_t value = u32(local + 14 + field * 4);
            if (value != expected[field] && (!(flags & 8) || value != 0)) return false;
        }
        uint64_t range_end = data_end;
        if (flags & 8) {
            uint8_t descriptor[16];
            if (directory.start - data_end < 12 || !read(fd, data_end, descriptor, 12)) return false;
            const size_t skip = u32(descriptor) == 0x08074b50 ? 4 : 0;
            if (skip && (directory.start - data_end < 16 || !read(fd, data_end + 12, descriptor + 12, 4))) return false;
            for (size_t field = 0; field < 3; ++field) if (u32(descriptor + skip + field * 4) != expected[field]) return false;
            range_end += 12 + skip;
        }
        for (size_t previous = 0; previous < count; ++previous) {
            if (ranges[previous].end && start < ranges[previous].end && ranges[previous].start < range_end) return false;
        }
        ranges[index] = {start, range_end};
        if (!content(fd, data, compressed, method, crc, entry, copy)) return false;
        ++found;
    }
    return position == end && found == count;
}
}

bool nmmpVerifyArtifactApk(int fd, const NmmpArtifactEntry *entries, size_t count) {
    if (fd < 0 || !entries || !count || count > NMMP_ARTIFACT_MAX_ENTRIES) return false;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].name || !entries[i].name_size || entries[i].name_size > NMMP_ARTIFACT_MAX_NAME
                || !entries[i].sha256 || !entries[i].size || entries[i].size > NMMP_ARTIFACT_MAX_ENTRY_BYTES
                || entries[i].size > NMMP_ARTIFACT_MAX_TOTAL_BYTES - total) return false;
        total += entries[i].size;
    }
    struct stat before = {}, after = {};
    Directory central = {};
    if (nmmpRawFstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0
            || !directory(fd, static_cast<uint64_t>(before.st_size), &central)
            || !verify(fd, central, entries, count) || nmmpRawFstat(fd, &after)) return false;
    return before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
            && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
            && before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}

bool nmmpReadArtifactImage(int fd, const NmmpArtifactEntry *entry, uint8_t **image) {
    if (!image) return false;
    *image = nullptr;
    if (fd < 0 || !entry || !entry->name || !entry->name_size || entry->name_size > NMMP_ARTIFACT_MAX_NAME
            || !entry->sha256 || !entry->size || entry->size > 64u * 1024 * 1024) return false;
    struct stat status = {};
    Directory central = {};
    if (nmmpRawFstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_size < 0
            || !directory(fd, static_cast<uint64_t>(status.st_size), &central)) return false;
    auto *bytes = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(entry->size)));
    if (!bytes) return false;
    if (!verify(fd, central, entry, 1, false, bytes)) { std::free(bytes); return false; }
    *image = bytes;
    return true;
}

bool nmmpReadArtifactEnvelope(int fd, uint8_t **envelope, size_t *size) {
    if (!envelope || !size) return false;
    *envelope = nullptr;
    *size = 0;
    struct stat status = {};
    Directory central = {};
    if (fd < 0 || nmmpRawFstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_size < 0
            || !directory(fd, static_cast<uint64_t>(status.st_size), &central)) return false;
    const uint8_t name[] = NMMP_ARTIFACT_APK_ENTRY;
    uint64_t position = central.start, end = central.start + central.size;
    uint64_t data = 0;
    uint32_t length = 0;
    for (unsigned i = 0; i < central.count; ++i) {
        uint8_t header[46];
        if (end - position < sizeof(header) || !read(fd, position, header, sizeof(header)) || u32(header) != 0x02014b50) return false;
        const uint16_t name_size = u16(header + 28);
        const uint64_t next = position + 46 + name_size + u16(header + 30) + u16(header + 32);
        if (next > end) return false;
        uint8_t candidate[sizeof(name) - 1];
        if (name_size == sizeof(candidate)) {
            if (!read(fd, position + 46, candidate, sizeof(candidate))) return false;
            if (!std::memcmp(candidate, name, sizeof(candidate))) {
                if (length || u16(header + 10) != 0) return false;
                length = u32(header + 24);
                const uint64_t start = u32(header + 42);
                if (length < 80 || length > NMMP_ARTIFACT_MAX_BODY + 80u || u32(header + 20) != length
                        || start > central.start || central.start - start < 30) return false;
                uint8_t local[30];
                if (!read(fd, start, local, sizeof(local)) || u32(local) != 0x04034b50) return false;
                data = start + 30 + u16(local + 26) + u16(local + 28);
                if (data + length > central.start) return false;
            }
        }
        position = next;
    }
    if (position != end || !length) return false;
    auto *bytes = static_cast<uint8_t *>(std::malloc(length));
    if (!bytes) return false;
    uint8_t digest[32];
    bool valid = read(fd, data, bytes, length);
    if (valid) {
        nmmpSha256(bytes, length, digest);
        const NmmpArtifactEntry entry = {name, sizeof(name) - 1, length, digest};
        valid = verify(fd, central, &entry, 1, false);
    }
    if (!valid) { std::free(bytes); return false; }
    *envelope = bytes;
    *size = length;
    return true;
}

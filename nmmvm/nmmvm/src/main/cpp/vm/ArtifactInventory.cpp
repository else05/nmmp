#include "ArtifactInventory.h"
#include <cstring>

namespace {
struct Reader {
    const uint8_t *data;
    size_t remaining;
    bool bytes(size_t size, const uint8_t **value) {
        if (size > remaining) return false;
        *value = data;
        data += size;
        remaining -= size;
        return true;
    }
    bool integer(size_t size, uint64_t *value) {
        const uint8_t *bytes;
        if (!this->bytes(size, &bytes)) return false;
        *value = 0;
        for (size_t i = 0; i < size; ++i) *value = (*value << 8) | bytes[i];
        return true;
    }
};

bool alphaNumeric(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool dexName(const uint8_t *name, size_t size) {
    if (size < 11 || std::memcmp(name, "classes", 7) || std::memcmp(name + size - 4, ".dex", 4)) return false;
    const size_t digits = size - 11;
    if (!digits) return true;
    if (name[7] < (digits == 1 ? '2' : '1') || name[7] > '9') return false;
    for (size_t i = 8; i < size - 4; ++i) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

bool nativeName(const uint8_t *name, size_t size) {
    if (size < 10 || std::memcmp(name, "lib/", 4) || std::memcmp(name + size - 3, ".so", 3)) return false;
    size_t i = 4;
    for (; i < size && name[i] != '/'; ++i) {
        if (!alphaNumeric(name[i]) && name[i] != '_' && name[i] != '-') return false;
    }
    if (i == 4 || i >= size - 4) return false;
    for (++i; i < size; ++i) {
        if (!alphaNumeric(name[i]) && name[i] != '_' && name[i] != '-' && name[i] != '+' && name[i] != '.') return false;
    }
    return true;
}
}

bool nmmpParseArtifactInventory(const uint8_t *body, size_t body_size,
                               uint64_t expected_build_id,
                               const uint8_t *expected_package, size_t package_size,
                               NmmpArtifactEntry *entries, size_t capacity, size_t *count) {
    if (!count) return false;
    *count = 0;
    if (!body || body_size > NMMP_ARTIFACT_MAX_BODY || !expected_package || !package_size
            || package_size > NMMP_ARTIFACT_MAX_NAME || std::memchr(expected_package, 0, package_size)
            || !entries) return false;
    Reader reader = {body, body_size};
    const uint8_t *magic, *package;
    uint64_t version, build_id, name_size, entry_count;
    if (!reader.bytes(8, &magic) || std::memcmp(magic, "NMMPAINV", 8)
            || !reader.integer(4, &version) || version != 1
            || !reader.integer(8, &build_id) || build_id != expected_build_id
            || !reader.integer(4, &name_size) || name_size != package_size
            || !reader.bytes(package_size, &package) || std::memcmp(package, expected_package, package_size)
            || !reader.integer(4, &entry_count) || entry_count < 2
            || entry_count > NMMP_ARTIFACT_MAX_ENTRIES || entry_count > capacity) return false;
    uint64_t total = 0;
    bool main_dex = false, native = false;
    for (size_t i = 0; i < entry_count; ++i) {
        NmmpArtifactEntry entry = {};
        if (!reader.integer(4, &name_size) || !name_size || name_size > NMMP_ARTIFACT_MAX_NAME
                || !reader.bytes(static_cast<size_t>(name_size), &entry.name)
                || !reader.integer(8, &entry.size) || !entry.size || entry.size > NMMP_ARTIFACT_MAX_ENTRY_BYTES
                || entry.size > NMMP_ARTIFACT_MAX_TOTAL_BYTES - total
                || !reader.bytes(32, &entry.sha256)) return false;
        entry.name_size = static_cast<size_t>(name_size);
        if (dexName(entry.name, entry.name_size)) {
            if (entry.name_size == 11) main_dex = true;
        } else if (nativeName(entry.name, entry.name_size)) {
            native = true;
        } else return false;
        if (i) {
            const NmmpArtifactEntry &previous = entries[i - 1];
            const size_t common = previous.name_size < entry.name_size ? previous.name_size : entry.name_size;
            const int order = std::memcmp(previous.name, entry.name, common);
            if (order > 0 || (!order && previous.name_size >= entry.name_size)) return false;
        }
        total += entry.size;
        entries[i] = entry;
    }
    if (reader.remaining || !main_dex || !native) return false;
    *count = static_cast<size_t>(entry_count);
    return true;
}

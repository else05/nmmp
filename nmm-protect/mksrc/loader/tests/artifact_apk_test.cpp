#include "ArtifactApk.h"
#include "Arm64Syscall.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

#if !defined(__aarch64__)
long nmmpRawPread64(int fd, void *buffer, size_t size, uint64_t offset) {
    ssize_t result = pread(fd, buffer, size, static_cast<off_t>(offset));
    return result < 0 ? -errno : result;
}
long nmmpRawFstat(int fd, struct stat *status) { return fstat(fd, status) ? -errno : 0; }
#endif

static void check(bool value) { if (!value) std::abort(); }
static std::vector<uint8_t> load(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    check(input.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}
static uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static size_t locate(const std::vector<uint8_t> &data, uint32_t signature, size_t start = 0) {
    for (size_t i = start; i + 4 <= data.size(); ++i) if (u32(data.data() + i) == signature) return i;
    std::abort();
}
static bool verify(const std::string &path, const NmmpArtifactEntry *entries, size_t count) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    check(fd >= 0 && lseek(fd, 3, SEEK_SET) == 3);
    bool valid = nmmpVerifyArtifactApk(fd, entries, count);
    check(lseek(fd, 0, SEEK_CUR) == 3); // verifier must use pread, not disturb shared fd
    close(fd);
    return valid;
}
static void rejected(const std::vector<uint8_t> &bytes, const std::string &root,
                     const NmmpArtifactEntry *entries, size_t count) {
    std::string path = root + "/mutated-XXXXXX";
    std::vector<char> name(path.begin(), path.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    check(fd >= 0);
    size_t used = 0;
    while (used < bytes.size()) {
        ssize_t written = write(fd, bytes.data() + used, bytes.size() - used);
        check(written > 0);
        used += written;
    }
    const bool valid = nmmpVerifyArtifactApk(fd, entries, count);
    close(fd);
    unlink(name.data());
    check(!valid);
}
int main(int argc, char **argv) {
    check(argc == 2);
    std::string root = argv[1];
    const uint8_t package[] = "org.example.fixture";
    auto body = load(root + "/artifact-inventory.bin");
    std::vector<NmmpArtifactEntry> entries(NMMP_ARTIFACT_MAX_ENTRIES);
    size_t count = 0;
    check(nmmpParseArtifactInventory(body.data(), body.size(), 17, package, sizeof(package) - 1,
                                    entries.data(), entries.size(), &count));
    check(verify(root + "/stored.apk", entries.data(), count));
    check(verify(root + "/deflated.apk", entries.data(), count));
    check(verify(root + "/signed-aligned.apk", entries.data(), count));
    auto badPadding = load(root + "/signed-aligned.apk");
    check(badPadding[28] == 3 && badPadding[29] == 0 && badPadding[26] == 11);
    check(badPadding[41] == 0);
    badPadding[41] = 1;
    rejected(badPadding, root, entries.data(), count);
    for (const char *name : {"changed.apk", "extra.apk", "missing.apk"}) check(!verify(root + "/" + name, entries.data(), count));
    auto stored = load(root + "/stored.apk");
    const size_t central = locate(stored, 0x02014b50);
    const size_t second = locate(stored, 0x02014b50, central + 4);
    const size_t eocd = locate(stored, 0x06054b50);
    // Wrong local name, encryption, compression, CRC, size, local offset,
    // central count/disk/offset and ZIP64 sentinels must all fail.
    for (size_t offset : {size_t(30), size_t(6), size_t(8), size_t(14), size_t(18),
                          central + 16, central + 24, central + 42, eocd + 4, eocd + 10, eocd + 16}) {
        auto changed = stored;
        changed[offset] ^= 1;
        rejected(changed, root, entries.data(), count);
    }
    auto zip64 = stored;
    std::memset(zip64.data() + central + 20, 255, 4);
    rejected(zip64, root, entries.data(), count);
    auto duplicate = stored;
    // Insert a duplicate target record and fix EOCD count/size so rejection
    // exercises duplicate detection rather than a broken directory boundary.
    const size_t firstSize = second - central;
    duplicate.insert(duplicate.begin() + central, stored.begin() + central, stored.begin() + second);
    const size_t newEocd = eocd + firstSize;
    duplicate[newEocd + 8]++;
    duplicate[newEocd + 10]++;
    uint32_t directorySize = u32(duplicate.data() + newEocd + 12) + firstSize;
    for (unsigned i = 0; i < 4; ++i) duplicate[newEocd + 12 + i] = directorySize >> (8 * i);
    rejected(duplicate, root, entries.data(), count);
    for (size_t length = 0; length < stored.size(); ++length) {
        rejected(std::vector<uint8_t>(stored.begin(), stored.begin() + length), root, entries.data(), count);
    }
    auto deflated = load(root + "/deflated.apk");
    const size_t descriptor = locate(deflated, 0x08074b50);
    for (size_t offset : {size_t(30 + 11), descriptor + 4, descriptor + 8, descriptor + 12}) {
        auto changed = deflated;
        changed[offset] ^= 1;
        rejected(changed, root, entries.data(), count);
    }
    check(!nmmpVerifyArtifactApk(-1, entries.data(), count));
    auto large = load(root + "/large-inventory.bin");
    check(nmmpParseArtifactInventory(large.data(), large.size(), 17, package, sizeof(package) - 1,
                                    entries.data(), entries.size(), &count));
    check(verify(root + "/large.apk", entries.data(), count));
    std::puts("Native APK content, ZIP bounds, descriptors and mutations: PASS");
}

#include "ArtifactInventory.h"
#include "Sha256.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static void check(bool value) { if (!value) std::abort(); }
static const uint8_t package[] = "org.example.fixture";
static std::vector<NmmpArtifactEntry> entries(NMMP_ARTIFACT_MAX_ENTRIES);
static bool parse(const std::vector<uint8_t> &body, size_t *count, size_t capacity = NMMP_ARTIFACT_MAX_ENTRIES) {
    return nmmpParseArtifactInventory(body.data(), body.size(), 17, package, sizeof(package) - 1,
                                      entries.data(), capacity, count);
}
static void rejected(const std::vector<uint8_t> &body) {
    size_t count = 99;
    check(!parse(body, &count) && count == 0);
}
static void integer(std::vector<uint8_t> &body, uint64_t value, unsigned size) {
    for (unsigned i = size; i; --i) body.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}
static void replaceInteger(std::vector<uint8_t> &body, size_t offset, uint64_t value, unsigned size) {
    std::vector<uint8_t> bytes;
    integer(bytes, value, size);
    std::memcpy(body.data() + offset, bytes.data(), size);
}
static std::vector<uint8_t> make(const std::vector<std::string> &names, uint64_t size = 1) {
    std::vector<uint8_t> body;
    const char magic[] = "ARTINV01";
    body.insert(body.end(), magic, magic + 8);
    integer(body, 1, 4);
    integer(body, 17, 8);
    integer(body, sizeof(package) - 1, 4);
    body.insert(body.end(), package, package + sizeof(package) - 1);
    integer(body, names.size(), 4);
    for (const std::string &name : names) {
        integer(body, name.size(), 4);
        body.insert(body.end(), name.begin(), name.end());
        integer(body, size, 8);
        body.insert(body.end(), 32, 0);
    }
    return body;
}

int main(int argc, char **argv) {
    check(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    check(file.good());
    std::vector<uint8_t> body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t count = 0;
    check(parse(body, &count) && count == 3);
    const char *names[] = {"classes.dex", "classes2.dex", "lib/arm64-v8a/libfixture.so"};
    const char *contents[] = {"final-dex", "second-dex", "final-native"};
    for (size_t i = 0; i < count; ++i) {
        check(entries[i].name_size == std::strlen(names[i]));
        check(!std::memcmp(entries[i].name, names[i], entries[i].name_size));
        check(entries[i].size == std::strlen(contents[i]));
        uint8_t digest[32];
        NmmpSha256Context hash;
        nmmpSha256Init(&hash);
        nmmpSha256Update(&hash, reinterpret_cast<const uint8_t *>(contents[i]), std::strlen(contents[i]));
        nmmpSha256Final(&hash, digest);
        check(!std::memcmp(digest, entries[i].sha256, 32));
    }
    for (size_t size = 0; size < body.size(); ++size) {
        count = 99;
        check(!nmmpParseArtifactInventory(body.data(), size, 17, package, sizeof(package) - 1,
                                          entries.data(), entries.size(), &count) && !count);
    }
    check(!parse(body, &count, 2) && !count);
    check(!nmmpParseArtifactInventory(body.data(), body.size(), 18, package, sizeof(package) - 1,
                                      entries.data(), entries.size(), &count) && !count);
    check(!nmmpParseArtifactInventory(nullptr, body.size(), 17, package, sizeof(package) - 1,
                                      entries.data(), entries.size(), &count));
    for (size_t offset : {size_t(0), size_t(11), size_t(23), size_t(24)}) {
        auto changed = body;
        changed[offset] ^= 1;
        rejected(changed);
    }
    auto trailing = body;
    trailing.push_back(0);
    rejected(trailing);
    auto excessive = body;
    replaceInteger(excessive, 24 + sizeof(package) - 1, NMMP_ARTIFACT_MAX_ENTRIES + 1, 4);
    rejected(excessive);
    for (const std::string &name : {"classes0.dex", "classes1.dex", "classes01.dex", "classesx.dex",
                                    "../classes.dex", "lib//liba.so", "lib/arm64/../a.so", "lib/arm64/.so"}) {
        auto names = std::vector<std::string>{"classes.dex", name, "lib/arm64-v8a/libfixture.so"};
        rejected(make(names));
    }
    rejected(make({"classes.dex", "classes.dex", names[2]}));
    rejected(make({names[2], "classes.dex"}));
    rejected(make({"classes2.dex", names[2]}));
    rejected(make({"classes.dex", "classes2.dex"}));
    rejected(make({"classes.dex", names[2]}, 0));
    rejected(make({"classes.dex", names[2]}, NMMP_ARTIFACT_MAX_ENTRY_BYTES + 1));
    rejected(make({"classes.dex", "classes2.dex", "classes3.dex", "classes4.dex", names[2]}, NMMP_ARTIFACT_MAX_ENTRY_BYTES));
    uint32_t random = 17;
    for (size_t trial = 0; trial < 10000; ++trial) {
        auto changed = body;
        random = random * 1664525u + 1013904223u;
        const size_t offset = random % changed.size();
        random = random * 1664525u + 1013904223u;
        changed[offset] ^= static_cast<uint8_t>(random >> 24);
        count = 99;
        const bool valid = parse(changed, &count);
        check(valid ? count == 3 : count == 0);
        // A digest-byte mutation may parse successfully: parsing is not authentication.
    }
    std::puts("Java/native inventory contract and malformed inputs: PASS");
}

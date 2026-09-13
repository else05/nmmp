#include "ProtectionManifest.h"
#include "ProtectionPolicyTypes.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static void check(bool condition) { if (!condition) std::abort(); }

static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    check(input.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

extern "C" bool vmBindingMatchesExpectedIdentity(const char *package_name,
                                                   const uint8_t signer_digest[32],
                                                   bool signature_bound) {
    uint8_t difference = signature_bound || !package_name || package_name[0] != '\0';
    for (size_t i = 0; i < 32; ++i) difference |= signer_digest[i];
    return difference == 0;
}

int main(int argc, char **argv) {
    check(argc == 3);
    const std::string root = argv[1];
    std::vector<uint8_t> manifest = readFile(root + "/manifest.bin");
    std::vector<uint8_t> tag = readFile(root + "/manifest.tag");
    std::vector<uint8_t> key = readFile(root + "/manifest.keyxor");
    std::vector<uint8_t> id = readFile(root + "/manifest.id");
    check(manifest.size() >= 96 && tag.size() == 32 && key.size() == 32 && id.size() == 16);
    size_t manifest_size = manifest.size();
    const std::string scenario = argv[2];
    if (scenario.find("repeat-") == 0) {
        check(nmmpProtectionActivate(manifest.data(), manifest_size, tag.data(), key.data(), id.data()));
        const bool result = nmmpProtectionActivate(
                scenario == "repeat-null-body" ? nullptr : manifest.data(),
                scenario == "repeat-short" ? 0 : manifest_size,
                scenario == "repeat-null-tag" ? nullptr : tag.data(),
                scenario == "repeat-null-key" ? nullptr : key.data(),
                scenario == "repeat-null-id" ? nullptr : id.data());
        check(!result);
        check(nmmpProtectionActivate(manifest.data(), manifest_size, tag.data(), key.data(), id.data()));
        return 0;
    }
    const bool expected = std::strcmp(argv[2], "ok") == 0;
    if (std::strcmp(argv[2], "body") == 0) manifest[80] ^= 1;
    else if (std::strcmp(argv[2], "tag") == 0) tag[0] ^= 1;
    else if (std::strcmp(argv[2], "key") == 0) key[31] ^= 1;
    else if (std::strcmp(argv[2], "id") == 0) id[15] ^= 1;
    else if (std::strcmp(argv[2], "truncate") == 0) --manifest_size;
    else check(expected);

    const bool activated = nmmpProtectionActivate(
            manifest.data(), manifest_size, tag.data(), key.data(), id.data());
    check(activated == expected);
    if (expected) {
        NmmpManifestEntry entry = {};
        check(nmmpProtectionGetEntry(0, &entry));
        check(entry.moduleId == 0 && entry.moduleSize == 16 && entry.methodCount == 1);
        check(nmmpProtectionGetEntry(1, &entry));
        check(!nmmpProtectionGetEntry(2, &entry));
        check(nmmpProtectionPolicyFlags() == (NMMP_POLICY_CHECK_DEBUG | NMMP_POLICY_CHECK_MAPS));
        check(nmmpProtectionRecheckMillis() == 20000);
    }
    return 0;
}

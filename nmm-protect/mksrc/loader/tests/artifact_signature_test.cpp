#include "ArtifactSignature.h"
#include "ArtifactApk.h"
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#if !defined(__aarch64__)
#include "Arm64Syscall.h"
long nmmpRawPread64(int fd, void *buffer, size_t size, uint64_t offset) {
    ssize_t n = pread(fd, buffer, size, offset);
    return n < 0 ? -errno : n;
}
long nmmpRawFstat(int fd, struct stat *status) { return fstat(fd, status) ? -errno : 0; }
#endif
static void check(bool value) { if (!value) std::abort(); }
static std::vector<uint8_t> load(const std::string &name) {
    std::ifstream input(name, std::ios::binary);
    check(input.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}
int main(int argc, char **argv) {
    if (argc == 5) {
        auto key = load(argv[2]);
        char *end = nullptr;
        errno = 0;
        const uint64_t id = std::strtoull(argv[3], &end, 0);
        if (errno || !end || *end || argv[3][0] == '-' || key.size() != 32) return 2;
        int fd = open(argv[1], O_RDONLY);
        const bool valid = nmmpVerifySignedArtifactApk(fd, key.data(), key.size(), id,
                reinterpret_cast<const uint8_t *>(argv[4]), std::strlen(argv[4]));
        if (fd >= 0) close(fd);
        std::puts(valid ? "Signed APK artifact verification: PASS" : "Signed APK artifact verification: FAIL");
        return valid ? 0 : 1;
    }
    check(argc == 2);
    std::string root = argv[1];
    auto envelope = load(root + "/artifact-signed.bin"), key = load(root + "/artifact-public.bin");
    const uint8_t package[] = "org.example.fixture";
    std::vector<NmmpArtifactEntry> entries(NMMP_ARTIFACT_MAX_ENTRIES);
    size_t count = 0;
    auto authenticate = [&](const std::vector<uint8_t> &bytes, const std::vector<uint8_t> &public_key, uint64_t id = 17) {
        count = 99;
        bool result = nmmpAuthenticateArtifactInventory(bytes.data(), bytes.size(), public_key.data(), public_key.size(),
                id, package, sizeof(package) - 1, entries.data(), entries.size(), &count);
        check(result ? count == 3 : count == 0);
        return result;
    };
    check(authenticate(envelope, key));
    for (const char *name : {"signed-valid.apk", "signed-aligned.apk", "signed-changed.apk", "signed-invalid.apk", "signed-compressed.apk", "stored.apk"}) {
        int fd = open((root + "/" + name).c_str(), O_RDONLY);
        check(fd >= 0 && lseek(fd, 5, SEEK_SET) == 5);
        check(nmmpVerifySignedArtifactApk(fd, key.data(), key.size(), 17, package, sizeof(package) - 1)
                == (std::string(name) == "signed-valid.apk" || std::string(name) == "signed-aligned.apk"));
        check(!nmmpVerifySignedArtifactApk(fd, key.data(), key.size(), 18, package, sizeof(package) - 1));
        check(lseek(fd, 0, SEEK_CUR) == 5);
        close(fd);
    }
    for (const char *name : {"stored.apk", "deflated.apk", "changed.apk"}) {
        int fd = open((root + "/" + name).c_str(), O_RDONLY);
        check(fd >= 0);
        check(nmmpVerifyArtifactApk(fd, entries.data(), count) == (std::string(name) != "changed.apk"));
        close(fd);
    }
    check(!authenticate(envelope, key, 18));
    for (size_t i = 0; i < envelope.size(); ++i) {
        auto changed = envelope;
        changed[i] ^= 1;
        check(!authenticate(changed, key));
    }
    for (size_t i = 0; i < key.size(); ++i) {
        auto changed = key;
        changed[i] ^= 1;
        check(!authenticate(envelope, changed));
    }
    for (size_t size = 0; size < envelope.size(); ++size) {
        check(!authenticate(std::vector<uint8_t>(envelope.begin(), envelope.begin() + size), key));
    }
    key.pop_back();
    check(!authenticate(envelope, key));
    envelope.push_back(0);
    check(!authenticate(envelope, load(root + "/artifact-public.bin")));
    std::puts("Java Ed25519/native signature and authenticated APK content: PASS");
}

#include "ProtectionMarker.h"

#include "Sha256.h"
#include "VmCodecConfig.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

namespace {

static const size_t kPathCapacity = 768;
static const size_t kMaximumPackageLength = 255;
static const uint8_t kMask = UINT8_C(0xa5);

// Volatile encoded bytes prevent the compiler from folding storage paths back
// into searchable string literals.
static const volatile uint8_t kStorageRoot[] = {
        0x8a, 0xd6, 0xc1, 0xc6, 0xc4, 0xd7, 0xc1
};
static const volatile uint8_t kAndroidSegment[] = {
        0xe4, 0xcb, 0xc1, 0xd7, 0xca, 0xcc, 0xc1
};
static const volatile uint8_t kDataSegment[] = {
        0xc1, 0xc4, 0xd1, 0xc4
};
static const volatile uint8_t kFilesSegment[] = {
        0xc3, 0xcc, 0xc9, 0xc0, 0xd6
};
static const volatile uint8_t kDerivationDomain[] = {
        0xcb, 0xc8, 0xc8, 0xd5, 0x88, 0xc4, 0xc1, 0x88,
        0xc9, 0xc4, 0xd1, 0xc6, 0xcd, 0x88, 0x95, 0x94
};

struct MarkerPaths {
    char primary[kPathCapacity];
    char fallback[kPathCapacity];
};

class PathBuilder {
public:
    PathBuilder(char *buffer, size_t capacity) : buffer_(buffer), capacity_(capacity), size_(0) {
        if (capacity_) buffer_[0] = '\0';
    }

    bool append(const char *data, size_t size) {
        if (!data || size_ >= capacity_ || size > capacity_ - size_ - 1) return false;
        memcpy(buffer_ + size_, data, size);
        size_ += size;
        buffer_[size_] = '\0';
        return true;
    }

    bool append(char value) {
        return append(&value, 1);
    }

    bool appendDecoded(const volatile uint8_t *encoded, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            if (!append(static_cast<char>(encoded[i] ^ kMask))) return false;
        }
        return true;
    }

    bool appendHex(const uint8_t *bytes, size_t size) {
        static const char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < size; ++i) {
            if (!append(hex[bytes[i] >> 4]) || !append(hex[bytes[i] & 0x0f])) return false;
        }
        return true;
    }

private:
    char *buffer_;
    size_t capacity_;
    size_t size_;
};

static size_t boundedLength(const char *value, size_t maximum) {
    if (!value) return 0;
    size_t length = 0;
    while (length <= maximum && value[length]) ++length;
    return length;
}

static size_t normalizedRootLength(const char *root) {
    size_t length = boundedLength(root, kPathCapacity - 1);
    while (length > 1 && root[length - 1] == '/') --length;
    return length;
}

static bool validInputs(const char *root, const char *packageName,
                        size_t *rootLength, size_t *packageLength) {
    const size_t rootSize = normalizedRootLength(root);
    const size_t packageSize = boundedLength(packageName, kMaximumPackageLength);
    if (!rootSize || rootSize >= kPathCapacity || !packageSize
            || packageSize > kMaximumPackageLength) return false;
    for (size_t i = 0; i < packageSize; ++i) {
        if (packageName[i] == '/' || packageName[i] == '\\') return false;
    }
    *rootLength = rootSize;
    *packageLength = packageSize;
    return true;
}

static void deriveName(const char *packageName, size_t packageLength, uint8_t digest[32]) {
    uint8_t domain[sizeof(kDerivationDomain)];
    for (size_t i = 0; i < sizeof(domain); ++i) {
        domain[i] = static_cast<uint8_t>(kDerivationDomain[i] ^ kMask);
    }
    NmmpSha256Context hash;
    nmmpSha256Init(&hash);
    nmmpSha256Update(&hash, reinterpret_cast<const uint8_t *>(packageName), packageLength);
    nmmpSha256Update(&hash, domain, sizeof(domain));
    nmmpSha256Final(&hash, digest);
    memset(domain, 0, sizeof(domain));
}

static bool appendDerivedLeaf(PathBuilder *path, const uint8_t digest[32]) {
    return path->append('/') && path->append('.') && path->appendHex(digest, 8)
            && path->append('/') && path->append('.') && path->appendHex(digest + 8, 16);
}

static bool buildPaths(const char *root, const char *packageName, MarkerPaths *paths) {
    size_t rootLength;
    size_t packageLength;
    if (!paths || !validInputs(root, packageName, &rootLength, &packageLength)) return false;

    uint8_t digest[32];
    deriveName(packageName, packageLength, digest);

    PathBuilder primary(paths->primary, sizeof(paths->primary));
    PathBuilder fallback(paths->fallback, sizeof(paths->fallback));
    const bool ok = primary.append(root, rootLength) && appendDerivedLeaf(&primary, digest)
            && fallback.append(root, rootLength) && fallback.append('/')
            && fallback.appendDecoded(kAndroidSegment, sizeof(kAndroidSegment))
            && fallback.append('/') && fallback.appendDecoded(kDataSegment, sizeof(kDataSegment))
            && fallback.append('/') && fallback.append(packageName, packageLength)
            && fallback.append('/') && fallback.appendDecoded(kFilesSegment, sizeof(kFilesSegment))
            && appendDerivedLeaf(&fallback, digest);
    memset(digest, 0, sizeof(digest));
    return ok;
}

static bool buildProductionPaths(MarkerPaths *paths) {
    char root[sizeof(kStorageRoot) + 1];
    for (size_t i = 0; i < sizeof(kStorageRoot); ++i) {
        root[i] = static_cast<char>(kStorageRoot[i] ^ kMask);
    }
    root[sizeof(kStorageRoot)] = '\0';
    const bool result = buildPaths(root, NMMP_VM_PACKAGE_NAME, paths);
    memset(root, 0, sizeof(root));
    return result;
}

static bool regularFile(const char *path) {
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return false;
    struct stat status = {};
    const bool result = fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode);
    close(descriptor);
    return result;
}

static bool directory(const char *path) {
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0) return false;
    struct stat status = {};
    const bool result = fstat(descriptor, &status) == 0 && S_ISDIR(status.st_mode);
    close(descriptor);
    return result;
}

static bool createDirectory(const char *path) {
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST && directory(path);
}

static bool ensureParents(char *path, size_t rootLength) {
    const size_t length = boundedLength(path, kPathCapacity - 1);
    if (length <= rootLength || length >= kPathCapacity) return false;
    for (size_t i = rootLength + 1; i < length; ++i) {
        if (path[i] != '/') continue;
        path[i] = '\0';
        const bool created = createDirectory(path);
        path[i] = '/';
        if (!created) return false;
    }
    return true;
}

static bool writeAll(int descriptor, const uint8_t *data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const ssize_t result = write(descriptor, data + written, size - written);
        if (result > 0) {
            written += static_cast<size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool persistOne(char *path, size_t rootLength) {
    if (!ensureParents(path, rootLength)) return false;
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) return errno == EEXIST && regularFile(path);

    uint8_t content[32];
    nmmpSha256(reinterpret_cast<const uint8_t *>(path), strlen(path), content);
    const bool complete = writeAll(descriptor, content, sizeof(content));
    memset(content, 0, sizeof(content));
    if (complete) fsync(descriptor);
    close(descriptor);
    // File existence is the marker contract, including a short write caused by
    // storage exhaustion after the inode was created.
    return regularFile(path);
}

static bool present(const MarkerPaths &paths) {
    return regularFile(paths.primary) || regularFile(paths.fallback);
}

static bool persist(const char *root, const char *packageName) {
    MarkerPaths paths = {};
    size_t rootLength;
    size_t packageLength;
    if (!validInputs(root, packageName, &rootLength, &packageLength)
            || !buildPaths(root, packageName, &paths)) return false;
    if (persistOne(paths.primary, rootLength)) return true;
    return persistOne(paths.fallback, rootLength);
}

}  // namespace

bool nmmpProtectionMarkerPresent() {
    MarkerPaths paths = {};
    return buildProductionPaths(&paths) && present(paths);
}

bool nmmpProtectionMarkerPersist() {
    char root[sizeof(kStorageRoot) + 1];
    for (size_t i = 0; i < sizeof(kStorageRoot); ++i) {
        root[i] = static_cast<char>(kStorageRoot[i] ^ kMask);
    }
    root[sizeof(kStorageRoot)] = '\0';
    const bool result = persist(root, NMMP_VM_PACKAGE_NAME);
    memset(root, 0, sizeof(root));
    return result;
}

#if defined(NMMP_PROTECTION_MARKER_TEST)
bool nmmpProtectionMarkerBuildPathsForTest(const char *root,
                                           const char *packageName,
                                           char *primary,
                                           size_t primaryCapacity,
                                           char *fallback,
                                           size_t fallbackCapacity) {
    MarkerPaths paths = {};
    if (!primary || !fallback || !buildPaths(root, packageName, &paths)) return false;
    const size_t primaryLength = strlen(paths.primary);
    const size_t fallbackLength = strlen(paths.fallback);
    if (primaryLength + 1 > primaryCapacity || fallbackLength + 1 > fallbackCapacity) return false;
    memcpy(primary, paths.primary, primaryLength + 1);
    memcpy(fallback, paths.fallback, fallbackLength + 1);
    return true;
}

bool nmmpProtectionMarkerPresentForTest(const char *root, const char *packageName) {
    MarkerPaths paths = {};
    return buildPaths(root, packageName, &paths) && present(paths);
}

bool nmmpProtectionMarkerPersistForTest(const char *root, const char *packageName) {
    return persist(root, packageName);
}
#endif

#define _XOPEN_SOURCE 500

#include "ProtectionMarker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

static int removeEntry(const char *path, const struct stat *, int, struct FTW *) {
    return remove(path);
}

static bool exists(const char *path) {
    struct stat status = {};
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static bool blockPrimaryDirectory(const char *primary) {
    char parent[768];
    strcpy(parent, primary);
    char *separator = strrchr(parent, '/');
    if (!separator) return false;
    *separator = '\0';
    const int descriptor = open(parent, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) return false;
    close(descriptor);
    return true;
}

}  // namespace

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main() {
    char primary[768];
    char fallback[768];
    const char *packageName = "com.example.marker";

    char primaryRootTemplate[] = "/tmp/nmmp-marker-primary-XXXXXX";
    const char *primaryRoot = mkdtemp(primaryRootTemplate);
    CHECK(primaryRoot);
    CHECK(nmmpProtectionMarkerBuildPathsForTest(
            primaryRoot, packageName, primary, sizeof(primary), fallback, sizeof(fallback)));
    CHECK(nmmpProtectionMarkerPersistForTest(primaryRoot, packageName));
    CHECK(exists(primary));
    CHECK(!exists(fallback));
    CHECK(nmmpProtectionMarkerPresentForTest(primaryRoot, packageName));
    CHECK(nftw(primaryRoot, removeEntry, 16, FTW_DEPTH | FTW_PHYS) == 0);

    char fallbackRootTemplate[] = "/tmp/nmmp-marker-fallback-XXXXXX";
    const char *fallbackRoot = mkdtemp(fallbackRootTemplate);
    CHECK(fallbackRoot);
    CHECK(nmmpProtectionMarkerBuildPathsForTest(
            fallbackRoot, packageName, primary, sizeof(primary), fallback, sizeof(fallback)));
    CHECK(strncmp(primary, fallbackRoot, strlen(fallbackRoot)) == 0);
    CHECK(strstr(fallback, "/Android/data/com.example.marker/files/") != nullptr);
    CHECK(strcmp(primary, fallback) != 0);
    CHECK(!nmmpProtectionMarkerPresentForTest(fallbackRoot, packageName));

    CHECK(blockPrimaryDirectory(primary));
    CHECK(nmmpProtectionMarkerPersistForTest(fallbackRoot, packageName));
    CHECK(!exists(primary));
    CHECK(exists(fallback));
    CHECK(nmmpProtectionMarkerPresentForTest(fallbackRoot, packageName));
    CHECK(!nmmpProtectionMarkerPresentForTest(fallbackRoot, "com.example.other"));

    CHECK(nftw(fallbackRoot, removeEntry, 16, FTW_DEPTH | FTW_PHYS) == 0);
    return 0;
}

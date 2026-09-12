#include "Arm64Syscall.h"

#include <asm/unistd.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>

namespace {

#if defined(__aarch64__)

static long rawSyscall1(long number, long argument0) {
    register long x0 __asm__("x0") = argument0;
    register long x8 __asm__("x8") = number;
    __asm__ volatile(
            "svc #0"
            : "+r"(x0)
            : "r"(x8)
            : "memory", "cc");
    return x0;
}

static long rawSyscall2(long number, long argument0, long argument1) {
    register long x0 __asm__("x0") = argument0;
    register long x1 __asm__("x1") = argument1;
    register long x8 __asm__("x8") = number;
    __asm__ volatile(
            "svc #0"
            : "+r"(x0)
            : "r"(x1), "r"(x8)
            : "memory", "cc");
    return x0;
}

static long rawSyscall3(long number, long argument0, long argument1, long argument2) {
    register long x0 __asm__("x0") = argument0;
    register long x1 __asm__("x1") = argument1;
    register long x2 __asm__("x2") = argument2;
    register long x8 __asm__("x8") = number;
    __asm__ volatile(
            "svc #0"
            : "+r"(x0)
            : "r"(x1), "r"(x2), "r"(x8)
            : "memory", "cc");
    return x0;
}

static long rawSyscall4(long number,
                        long argument0,
                        long argument1,
                        long argument2,
                        long argument3) {
    register long x0 __asm__("x0") = argument0;
    register long x1 __asm__("x1") = argument1;
    register long x2 __asm__("x2") = argument2;
    register long x3 __asm__("x3") = argument3;
    register long x8 __asm__("x8") = number;
    __asm__ volatile(
            "svc #0"
            : "+r"(x0)
            : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
            : "memory", "cc");
    return x0;
}

#endif

static int inspectMapsLine(char *line,
                           size_t length,
                           const char *packageName,
                           char *path,
                           size_t pathSize,
                           bool *found) {
    line[length] = '\0';
    const char prefix[] = "/data/app/";
    const char suffix[] = "/base.apk";
    const char *candidate = std::strstr(line, prefix);
    if (candidate == nullptr) return 0;
    if (candidate != line && candidate[-1] != ' ' && candidate[-1] != '\t') return 0;

    const size_t prefixSize = sizeof(prefix) - 1;
    const size_t packageSize = std::strlen(packageName);
    const char *directory = candidate + prefixSize;
    if (std::strncmp(directory, packageName, packageSize) != 0
        || directory[packageSize] != '-') {
        return 0;
    }
    const char *pathSuffix = std::strchr(directory + packageSize + 1, '/');
    if (pathSuffix == nullptr
        || pathSuffix == directory + packageSize + 1
        || std::strcmp(pathSuffix, suffix) != 0) {
        return 0;
    }

    const size_t candidateSize = std::strlen(candidate);
    if (candidateSize + 1 > pathSize) return -1;
    if (!*found) {
        std::memcpy(path, candidate, candidateSize + 1);
        *found = true;
        return 1;
    }
    return std::strcmp(path, candidate) == 0 ? 1 : -1;
}

}  // namespace

long nmmpRawOpenAt(int dirFd, const char *path, int flags, unsigned int mode) {
#if defined(__aarch64__)
    return rawSyscall4(__NR_openat,
                       dirFd,
                       reinterpret_cast<long>(path),
                       flags,
                       mode);
#else
    (void) dirFd;
    (void) path;
    (void) flags;
    (void) mode;
    return -ENOSYS;
#endif
}

long nmmpRawRead(int fd, void *buffer, size_t size) {
#if defined(__aarch64__)
    return rawSyscall3(__NR_read, fd, reinterpret_cast<long>(buffer), size);
#else
    (void) fd;
    (void) buffer;
    (void) size;
    return -ENOSYS;
#endif
}

long nmmpRawPread64(int fd, void *buffer, size_t size, uint64_t offset) {
#if defined(__aarch64__)
    return rawSyscall4(__NR_pread64,
                       fd,
                       reinterpret_cast<long>(buffer),
                       size,
                       static_cast<long>(offset));
#else
    (void) fd;
    (void) buffer;
    (void) size;
    (void) offset;
    return -ENOSYS;
#endif
}

long nmmpRawFstat(int fd, struct stat *status) {
#if defined(__aarch64__)
    return rawSyscall2(__NR_fstat, fd, reinterpret_cast<long>(status));
#else
    (void) fd;
    (void) status;
    return -ENOSYS;
#endif
}

long nmmpRawClose(int fd) {
#if defined(__aarch64__)
    return rawSyscall1(__NR_close, fd);
#else
    (void) fd;
    return -ENOSYS;
#endif
}

bool nmmpFindMappedBaseApk(const char *packageName, char *path, size_t pathSize) {
    if (packageName == nullptr || packageName[0] == '\0' || path == nullptr || pathSize == 0) {
        return false;
    }

    const long descriptor = nmmpRawOpenAt(
            AT_FDCWD, "/proc/self/maps", O_RDONLY | O_CLOEXEC, 0);
    if (descriptor < 0) return false;

    char readBuffer[2048];
    char line[8192];
    size_t lineSize = 0;
    bool discardLine = false;
    bool found = false;
    bool valid = true;

    for (;;) {
        const long count = nmmpRawRead(
                static_cast<int>(descriptor), readBuffer, sizeof(readBuffer));
        if (count == -EINTR) continue;
        if (count < 0) {
            valid = false;
            break;
        }
        if (count == 0) break;

        for (long i = 0; i < count; ++i) {
            const char value = readBuffer[i];
            if (value == '\n') {
                if (!discardLine
                    && inspectMapsLine(
                            line, lineSize, packageName, path, pathSize, &found) < 0) {
                    valid = false;
                    break;
                }
                lineSize = 0;
                discardLine = false;
            } else if (!discardLine) {
                if (lineSize + 1 < sizeof(line)) {
                    line[lineSize++] = value;
                } else {
                    discardLine = true;
                }
            }
        }
        if (!valid) break;
    }

    if (valid && lineSize != 0 && !discardLine
        && inspectMapsLine(line, lineSize, packageName, path, pathSize, &found) < 0) {
        valid = false;
    }
    nmmpRawClose(static_cast<int>(descriptor));
    return valid && found;
}

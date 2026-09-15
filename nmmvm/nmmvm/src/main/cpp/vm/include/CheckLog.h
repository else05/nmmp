#pragma once

#if defined(NMMP_DIAGNOSTICS) && NMMP_DIAGNOSTICS
#include <android/log.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

// Diagnostic builds only. The process package owns /data/data/<package>.
static inline int nmmpDiagnosticLogPath(char *path, size_t capacity) {
    if (!path || capacity < 2) return 0;
    char package[256];
    const int cmdline = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (cmdline < 0) return 0;
    ssize_t length;
    do { length = read(cmdline, package, sizeof(package) - 1); } while (length < 0 && errno == EINTR);
    close(cmdline);
    if (length <= 0) return 0;
    size_t packageLength = 0;
    while (packageLength < (size_t)length && package[packageLength] && package[packageLength] != ':') {
        const char value = package[packageLength];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                || (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-')) {
            return 0;
        }
        ++packageLength;
    }
    if (!packageLength) return 0;
    package[packageLength] = '\0';
    const int written = snprintf(path, capacity, "/data/data/%s/check.log", package);
    return written > 0 && (size_t)written < capacity;
}

static inline void nmmpDiagnosticVLog(int priority, const char *tag, const char *format, va_list args) {
    const int savedErrno = errno;
    if (!tag) tag = "NMMP";
    char message[1536];
    vsnprintf(message, sizeof(message), format, args);
    __android_log_write(priority, tag, message);
    char line[1664];
    const int count = snprintf(line, sizeof(line), "%lld pid=%d level=%d %s %s\n",
                              (long long)time(NULL), getpid(), priority, tag, message);
    const size_t length = count < 0 ? 0 : (size_t)count < sizeof(line)
            ? (size_t)count : sizeof(line) - 1;
    char logPath[512];
    const int pathReady = nmmpDiagnosticLogPath(logPath, sizeof(logPath));
    const int fd = pathReady
            ? open(logPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600)
            : -1;
    int failure = fd < 0 ? (pathReady ? errno : ENOENT) : 0;
    if (fd >= 0) {
        ssize_t written;
        do { written = write(fd, line, length); } while (written < 0 && errno == EINTR);
        if (written < 0) failure = errno;
        else if ((size_t)written != length) failure = EIO;
        close(fd);
    }
    if (failure) __android_log_print(ANDROID_LOG_WARN, tag,
                                    "cannot append app diagnostic log: errno=%d", failure);
    errno = savedErrno;
}
static inline void nmmpDiagnosticLog(int priority, const char *tag, const char *format, ...) {
    const int savedErrno = errno;
    va_list args;
    va_start(args, format);
    nmmpDiagnosticVLog(priority, tag, format, args);
    va_end(args);
    errno = savedErrno;
}
#define nmmpCheckLog(tag, ...) nmmpDiagnosticLog(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#define NMMP_LOG(priority, tag, ...) nmmpDiagnosticLog(priority, tag, __VA_ARGS__)
#define NMMP_CHECK_LOG(...) nmmpCheckLog("NMMP_CHECK", __VA_ARGS__)
#else
#define NMMP_LOG(...) ((void)0)
#define NMMP_CHECK_LOG(...) ((void)0)
#endif

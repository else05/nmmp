#include "EnvironmentChecks.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
uint64_t milliseconds() {
    struct timespec value = {};
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0
            ? static_cast<uint64_t>(value.tv_sec) * 1000 + value.tv_nsec / 1000000 : 0;
}
bool threadId(const char *name) {
    size_t length = 0;
    while (name[length] >= '0' && name[length] <= '9' && length < 20) ++length;
    return length && length < 20 && !name[length];
}
}

NmmpCheckStatus nmmpCheckThreadNames() {
    const uint64_t started = milliseconds();
    if (!started) return NMMP_CHECK_UNKNOWN;
    DIR *directory = opendir("/proc/self/task");
    if (!directory) return NMMP_CHECK_UNKNOWN;
    NmmpCheckStatus result = NMMP_CHECK_PASS;
    unsigned count = 0;
    for (;;) {
        errno = 0;
        const struct dirent *entry = readdir(directory);
        if (!entry) { if (errno) result = NMMP_CHECK_UNKNOWN; break; }
        if (!threadId(entry->d_name)) continue;
        const uint64_t now = milliseconds();
        if (++count > 1024 || !now || now < started || now - started >= 20) {
            result = NMMP_CHECK_UNKNOWN;
            break;
        }
        char path[32];
        std::snprintf(path, sizeof(path), "%s/comm", entry->d_name);
        const int fd = openat(dirfd(directory), path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if (errno != ENOENT && errno != ESRCH) result = NMMP_CHECK_UNKNOWN;
            continue;
        }
        char name[64];
        const ssize_t size = read(fd, name, sizeof(name));
        const int error = errno;
        close(fd);
        if (size <= 0 || static_cast<size_t>(size) >= sizeof(name)) {
            if (size >= 0 || (error != ENOENT && error != ESRCH)) result = NMMP_CHECK_UNKNOWN;
            continue;
        }
        size_t length = static_cast<size_t>(size);
        if (name[length - 1] == '\n') --length;
        name[length] = 0;
        for (size_t i = 0; i < length; ++i) if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 'a' - 'A';
        if (!std::strcmp(name, "gum-js-loop") || !std::strcmp(name, "gmain") || !std::strcmp(name, "gdbus")) {
            result = NMMP_CHECK_SIGNAL;
            break;
        }
    }
    closedir(directory);
    return count ? result : NMMP_CHECK_UNKNOWN;
}

NmmpCheckStatus nmmpProbeLoopbackPort(uint16_t port) {
    if (!port) return NMMP_CHECK_NOT_APPLICABLE;
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return errno == EACCES || errno == EPERM ? NMMP_CHECK_NOT_APPLICABLE : NMMP_CHECK_UNKNOWN;
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int error = 0;
    if (connect(fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0) {
        error = errno;
        if (error == EINPROGRESS) {
            struct pollfd request = {fd, POLLOUT, 0};
            // An interruption or timeout is unknown, not evidence of absence.
            if (poll(&request, 1, 10) <= 0) { close(fd); return NMMP_CHECK_UNKNOWN; }
            socklen_t size = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || size != sizeof(error)) {
                close(fd);
                return NMMP_CHECK_UNKNOWN;
            }
        }
    }
    close(fd);
    if (!error) return NMMP_CHECK_SIGNAL;
    if (error == ECONNREFUSED) return NMMP_CHECK_PASS;
    if (error == EACCES || error == EPERM) return NMMP_CHECK_NOT_APPLICABLE;
    return NMMP_CHECK_UNKNOWN;
}

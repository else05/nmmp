#ifndef NMMP_ARM64_SYSCALL_H
#define NMMP_ARM64_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

long nmmpRawOpenAt(int dirFd, const char *path, int flags, unsigned int mode);
long nmmpRawRead(int fd, void *buffer, size_t size);
long nmmpRawPread64(int fd, void *buffer, size_t size, uint64_t offset);
long nmmpRawFstat(int fd, struct stat *status);
long nmmpRawClose(int fd);

bool nmmpFindMappedBaseApk(const char *packageName, char *path, size_t pathSize);

#endif

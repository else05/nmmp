#ifndef NMMP_PROCESS_MEMORY_H
#define NMMP_PROCESS_MEMORY_H

#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

static inline ssize_t nmmpReadSelfMemory(struct iovec *local, struct iovec *remote) {
    return (ssize_t)syscall(SYS_process_vm_readv, getpid(), local, 1, remote, 1, 0);
}

#endif

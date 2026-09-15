#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
static unsigned clockCalls;
static time_t seconds = 10;
static int clockFails;
static int guardClock(clockid_t id, struct timespec *value) {
    assert(id == CLOCK_MONOTONIC);
    __atomic_add_fetch(&clockCalls, 1, __ATOMIC_RELAXED);
    value->tv_sec = seconds;
    value->tv_nsec = 0;
    return clockFails ? -1 : 0;
}
#define clock_gettime guardClock
#include "ProtectionEntryGuard.h"
NmmpProtectionEntryGuard nmmpProtectionEntryGuard;
static unsigned char code[NMMP_ENTRY_GUARD_COUNT][NMMP_ENTRY_GUARD_BYTES];
static pthread_barrier_t barrier;
static void *caller(void *unused) {
    (void)unused;
    pthread_barrier_wait(&barrier);
    int allowed;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(allowed);
    return 0;
}
int main(void) {
    int allowed;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(!allowed);
    for (unsigned i = 0; i < NMMP_ENTRY_GUARD_COUNT; ++i) {
        memset(code[i], i + 1, sizeof(code[i]));
        nmmpProtectionEntryGuard.addresses[i] = (uintptr_t)code[i];
        memcpy(nmmpProtectionEntryGuard.expected[i], code[i], sizeof(code[i]));
    }
    nmmpProtectionEntryGuard.ready = 1;
    pthread_t threads[32];
    pthread_barrier_init(&barrier, 0, 32);
    for (unsigned i = 0; i < 32; ++i) assert(!pthread_create(&threads[i], 0, caller, 0));
    for (unsigned i = 0; i < 32; ++i) pthread_join(threads[i], 0);
    pthread_barrier_destroy(&barrier);
    // One initial clock per caller, and one completion clock for the sole scan.
    assert(clockCalls == 33 && nmmpProtectionEntryGuard.completed == UINT64_C(10000000000));
    code[2][0] ^= 1;
    seconds = 14;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(allowed && !nmmpProtectionEntryGuard.failed);
    seconds = 15;
    nmmpProtectionEntryGuard.busy = 1;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(allowed && !nmmpProtectionEntryGuard.failed);
    nmmpProtectionEntryGuard.busy = 0;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(!allowed && nmmpProtectionEntryGuard.failed);
    code[2][0] ^= 1;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(!allowed); // Restoring code cannot clear a confirmed failure.
    nmmpProtectionEntryGuard.failed = 0;
    clockFails = 1;
    NMMP_CHECK_PROTECTION_ENTRIES(allowed);
    assert(!allowed && !nmmpProtectionEntryGuard.failed && !nmmpProtectionEntryGuard.busy);
    return 0;
}

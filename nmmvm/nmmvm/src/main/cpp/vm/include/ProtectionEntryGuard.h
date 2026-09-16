#ifndef NMMP_PROTECTION_ENTRY_GUARD_H
#define NMMP_PROTECTION_ENTRY_GUARD_H
#include <stdint.h>
#include <time.h>
#include "CheckLog.h"
#define NMMP_ENTRY_GUARD_COUNT 9
#define NMMP_ENTRY_GUARD_BYTES 32
typedef struct {
    uintptr_t addresses[NMMP_ENTRY_GUARD_COUNT];
    uint8_t expected[NMMP_ENTRY_GUARD_COUNT][NMMP_ENTRY_GUARD_BYTES];
} NmmpProtectionEntryBaseline;
typedef struct {
    const NmmpProtectionEntryBaseline *baseline;
    uint64_t completed;
    uint32_t ready, busy, failed;
} NmmpProtectionEntryGuard;
#ifdef __cplusplus
extern "C" {
#endif
extern NmmpProtectionEntryGuard nmmpProtectionEntryGuard;
#ifdef __cplusplus
}
#endif
// Expanded in each VMP wrapper, not a callable checking function.
// Concurrent losers proceed; confirmed failure is terminal.
#define NMMP_CHECK_PROTECTION_ENTRIES(allowed) do { \
    NmmpProtectionEntryGuard *nmmpGuard = &nmmpProtectionEntryGuard; \
    const NmmpProtectionEntryBaseline *nmmpBaseline = \
            __atomic_load_n(&nmmpGuard->baseline, __ATOMIC_ACQUIRE); \
    (allowed) = __atomic_load_n(&nmmpGuard->ready, __ATOMIC_ACQUIRE) \
            && nmmpBaseline \
            && !__atomic_load_n(&nmmpGuard->failed, __ATOMIC_ACQUIRE); \
    if (allowed) { \
        struct timespec nmmpTime = {0, 0}; \
        if (clock_gettime(CLOCK_MONOTONIC, &nmmpTime) == 0) { \
            uint64_t nmmpNow = (uint64_t)nmmpTime.tv_sec * UINT64_C(1000000000) + nmmpTime.tv_nsec; \
            uint64_t nmmpLast = __atomic_load_n(&nmmpGuard->completed, __ATOMIC_ACQUIRE); \
            uint32_t nmmpIdle = 0; \
            if ((!nmmpLast || nmmpNow < nmmpLast || nmmpNow - nmmpLast >= UINT64_C(5000000000)) \
                    && __atomic_compare_exchange_n(&nmmpGuard->busy, &nmmpIdle, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { \
                nmmpLast = __atomic_load_n(&nmmpGuard->completed, __ATOMIC_ACQUIRE); \
                if (!nmmpLast || nmmpNow < nmmpLast || nmmpNow - nmmpLast >= UINT64_C(5000000000)) { \
                    unsigned nmmpChanged = 0; \
                    for (unsigned nmmpI = 0; nmmpI < NMMP_ENTRY_GUARD_COUNT; ++nmmpI) { \
                        const volatile uint8_t *nmmpCode = (const volatile uint8_t *)nmmpBaseline->addresses[nmmpI]; \
                        unsigned nmmpDifference = 0; \
                        for (unsigned nmmpB = 0; nmmpB < NMMP_ENTRY_GUARD_BYTES; ++nmmpB) \
                            nmmpDifference |= nmmpCode[nmmpB] ^ nmmpBaseline->expected[nmmpI][nmmpB]; \
                        nmmpChanged |= nmmpDifference; \
                        NMMP_CHECK_LOG("entry_guard point=%u status=%s", nmmpI, nmmpDifference ? "MODIFIED" : "PASS"); \
                    } \
                    if (nmmpChanged) __atomic_store_n(&nmmpGuard->failed, 1, __ATOMIC_RELEASE); \
                    if (clock_gettime(CLOCK_MONOTONIC, &nmmpTime) == 0) \
                        nmmpNow = (uint64_t)nmmpTime.tv_sec * UINT64_C(1000000000) + nmmpTime.tv_nsec; \
                    __atomic_store_n(&nmmpGuard->completed, nmmpNow, __ATOMIC_RELEASE); \
                } \
                __atomic_store_n(&nmmpGuard->busy, 0, __ATOMIC_RELEASE); \
            } \
        } else (allowed) = 0; \
        if (__atomic_load_n(&nmmpGuard->failed, __ATOMIC_ACQUIRE)) (allowed) = 0; \
    } \
} while (0)
#endif

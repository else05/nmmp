#ifndef NMMP_VM_DECODED_STATE_H
#define NMMP_VM_DECODED_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

// Explicit decoded storage only. VM values, JNI references and return values
// retain their existing ownership. No per-instruction allocation or cache.
struct VmDecodedState {
    uint64_t tmp64;
    int64_t arrayData, switchData;
    uint32_t ref, tmp32, arg5, count;
    int32_t tmpSigned, offset;
    int branchOffset, catchRelPc;
    uint16_t inst, vdst, vsrc1, vsrc2, regs, srcRegs, litInfo, arrayInfo;
    bool methodCallRange;
};
static_assert(std::is_pod<VmDecodedState>::value, "Decoded state must remain POD");

static inline void nmmpWipeDecoded(void *memory, size_t size) {
    volatile unsigned char *p = static_cast<volatile unsigned char *>(memory);
    while (size--) *p++ = 0;
}

enum { NMMP_DECODE_ENTER, NMMP_DECODE_BEFORE_WIPE, NMMP_DECODE_AFTER_WIPE };
enum { NMMP_DECODE_RETURN, NMMP_DECODE_UNCAUGHT, NMMP_DECODE_READER_FAILED };
#if defined(NMMP_TEST_DECODE_WIPE)
extern "C" void nmmpObserveDecodedState(const VmDecodedState *, unsigned phase, unsigned reason);
extern "C" void nmmpObserveDecodedWord(const uint16_t *);
#endif

static inline uint16_t nmmpTakeDecodedWord(uint16_t *word) {
    // The necessary return-value copy is not a promise to erase CPU/spill copies.
    const uint16_t result = *word;
    nmmpWipeDecoded(word, sizeof(*word));
#if defined(NMMP_TEST_DECODE_WIPE)
    nmmpObserveDecodedWord(word);
#endif
    return result;
}

#endif

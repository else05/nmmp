#ifdef NDEBUG
#undef NDEBUG
#endif
#include "VmCodec.h"
#include <assert.h>
#include <thread>
#include <vector>
#include <atomic>
#include <stdio.h>

int main() {
    const uint64_t mask = UINT64_C(0xfedcba9876543210);
    assert(!vmCodecIsActivated());
    uint64_t untouched = 123;
    assert(!vmCodecGetSeed(&untouched) && untouched == 123);
    std::atomic<unsigned> passed{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 16; ++i) threads.emplace_back([&] {
        for (unsigned n = 0; n < 100; ++n) {
            bool activated = vmCodecActivate(mask);
#if defined(NMMP_EXPECT_NATIVE_INIT_FAILURE)
            assert(!activated && !vmCodecIsActivated());
#else
            uint64_t seed = 0;
            assert(activated && vmCodecGetSeed(&seed));
            // Reference formula exists only in this test, never the codec3 production branch.
            assert(seed == (NMMP_VM_SEED_DATA ^ mask));
#endif
            ++passed;
        }
    });
    for (auto &thread : threads) thread.join();
    assert(passed == 1600);
#if defined(NMMP_EXPECT_NATIVE_INIT_FAILURE)
    assert(!vmCodecActivate(mask) && !vmCodecGetSeed(&untouched));
    puts("PASS: invalid native program produces permanent initialization failure for all waiters");
#else
    assert(!vmCodecActivate(mask ^ 1));
    assert(vmCodecActivate(mask));
    puts("PASS: native VM recovers and publishes root once across 1600 concurrent activations");
#endif
}

#include "Stage0.h"
#include <stddef.h>

static void wipe(void *data, size_t size) {
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size--) *p++ = 0;
}
bool nmmpRecoverStage0(const NmmpNativeProgram programs[4], const uint8_t build_id[16], uint8_t key[32]) {
    if (!key) return false;
    wipe(key, 32);
    if (!programs || !build_id) return false;
    uint64_t inputs[2] = {0}, value = 0;
    for (unsigned i = 0; i < 16; ++i) inputs[i / 8] |= (uint64_t)build_id[i] << ((i & 7) * 8);
    bool success = false;
    for (unsigned word = 0; word < 4; ++word) {
        if (!nmmpNativeRun(&programs[word], inputs, 2, &value)) goto cleanup;
        for (unsigned b = 0; b < 8; ++b) key[word * 8 + b] = (uint8_t)(value >> (b * 8));
        wipe(&value, sizeof(value));
    }
    success = true;
cleanup:
    wipe(inputs, sizeof(inputs)); wipe(&value, sizeof(value));
    if (!success) wipe(key, 32);
    return success;
}

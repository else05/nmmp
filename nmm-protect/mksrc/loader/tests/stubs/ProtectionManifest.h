#ifndef NMMP_TEST_PROTECTION_MANIFEST_H
#define NMMP_TEST_PROTECTION_MANIFEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool nmmpProtectionActivate(const uint8_t *manifest, size_t manifest_size,
                            const uint8_t tag[32], const uint8_t key_xor[32],
                            const uint8_t expected_id[16]);
#endif

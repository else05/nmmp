#ifndef NMMP_TEST_MANIFEST_VM_BINDING_H
#define NMMP_TEST_MANIFEST_VM_BINDING_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool vmBindingMatchesExpectedIdentity(const char *package_name,
                                      const uint8_t signer_digest[32],
                                      bool signature_bound);
#ifdef __cplusplus
}
#endif
#endif

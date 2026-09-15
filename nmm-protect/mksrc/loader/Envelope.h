#ifndef NMMP_PRIVATE_ENVELOPE_H
#define NMMP_PRIVATE_ENVELOPE_H
#include <stddef.h>
#include <stdint.h>
#include "PrivateImageLayout.h"

#define NMMP_LOADER_LIMIT NMMP_PRIVATE_MAX_IMAGE_BYTES

uint32_t nmmp_u32(const uint8_t *p);
uint64_t nmmp_u64(const uint8_t *p);
/* Success transfers decoded ownership to the caller; failure leaves output NULL. */
int nmmp_open_payload(const uint8_t *payload, size_t size, const uint8_t key[32],
                      const uint8_t build_id[16], uint8_t **decoded, size_t *decoded_size);
void nmmp_free_secret(void *data, size_t size);
#endif

#ifndef NMMP_SHA256_H
#define NMMP_SHA256_H

#include <stddef.h>
#include <stdint.h>

void nmmpSha256(const uint8_t *data, size_t size, uint8_t digest[32]);

#endif

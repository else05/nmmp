#include "Envelope.h"
#include "vendor/monocypher/monocypher.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

uint32_t nmmp_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint64_t nmmp_u64(const uint8_t *p) {
    return (uint64_t)nmmp_u32(p) | (uint64_t)nmmp_u32(p + 4) << 32;
}

void nmmp_free_secret(void *data, size_t size) {
    if (data) {
        crypto_wipe(data, size);
        free(data);
    }
}

int nmmp_open_payload(const uint8_t *p, size_t size, const uint8_t key[32],
                      const uint8_t build_id[16], uint8_t **out, size_t *out_size) {
    *out = NULL;
    *out_size = 0;
    if (!p || size < 96 || memcmp(p, "NMMPPL01", 8) || nmmp_u32(p + 8) != 1 ||
        nmmp_u32(p + 12) != 1 || nmmp_u32(p + 16) != 183 || nmmp_u32(p + 20) != 1 ||
        nmmp_u32(p + 24) != 1 || nmmp_u32(p + 28) != 80 || nmmp_u32(p + 76) ||
        crypto_verify16(p + 32, build_id)) return -1;
    uint64_t cipher_size = nmmp_u64(p + 48), decoded_size = nmmp_u64(p + 56);
    if (!cipher_size || cipher_size > NMMP_LOADER_LIMIT || cipher_size != size - 96 ||
        decoded_size < 64 || decoded_size > NMMP_LOADER_LIMIT) return -1;
    uint8_t *compressed = malloc((size_t)cipher_size);
    if (!compressed) return -2;
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, p + 64);
    int status = crypto_aead_read(&ctx, compressed, p + 80 + cipher_size,
                                 p, 80, p + 80, (size_t)cipher_size);
    crypto_wipe(&ctx, sizeof(ctx));
    if (status) {
        nmmp_free_secret(compressed, (size_t)cipher_size);
        return -3;
    }
    /* Neither the allocator for decoded data nor zlib sees unauthenticated bytes. */
    uint8_t *decoded = malloc((size_t)decoded_size);
    if (!decoded) {
        nmmp_free_secret(compressed, (size_t)cipher_size);
        return -2;
    }
    z_stream stream = {0};
    stream.next_in = compressed;
    stream.avail_in = (uInt)cipher_size;
    stream.next_out = decoded;
    stream.avail_out = (uInt)decoded_size;
    int initialized = inflateInit(&stream) == Z_OK;
    status = initialized ? inflate(&stream, Z_FINISH) : Z_MEM_ERROR;
    int valid = status == Z_STREAM_END && stream.total_in == cipher_size &&
                stream.total_out == decoded_size;
    if (initialized) inflateEnd(&stream);
    nmmp_free_secret(compressed, (size_t)cipher_size);
    if (!valid) {
        nmmp_free_secret(decoded, (size_t)decoded_size);
        return -4;
    }
    *out = decoded;
    *out_size = (size_t)decoded_size;
    return 0;
}

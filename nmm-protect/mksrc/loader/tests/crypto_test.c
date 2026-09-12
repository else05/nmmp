#include "Envelope.h"
#include "vendor/monocypher/monocypher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static size_t hex(uint8_t *out, const char *s) {
    size_t n = strlen(s) / 2;
    for (size_t i = 0; i < n; ++i) { unsigned v; sscanf(s + i * 2, "%2x", &v); out[i] = v; }
    return n;
}
int main(int argc, char **argv) {
    /* RFC 8439 section 2.8.2; independent of the build-side implementation. */
    uint8_t key[32], nonce[12], aad[12], expected[114], tag[16], result[114], actual_tag[16];
    hex(key, "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    hex(nonce, "070000004041424344454647");
    hex(aad, "50515253c0c1c2c3c4c5c6c7");
    hex(tag, "1ae10b594f09e26a7e902ecbd0600691");
    hex(expected, "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
                  "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
                  "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3"
                  "ff4def08e4b7a9de576d26586cec64b6116");
    const uint8_t plain[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    check(sizeof(plain) - 1 == sizeof(expected), "vector length");
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, result, actual_tag, aad, sizeof(aad), plain, sizeof(expected));
    check(!memcmp(result, expected, sizeof(expected)) && !memcmp(actual_tag, tag, 16), "RFC 8439 encrypt");
    crypto_aead_init_ietf(&ctx, key, nonce);
    check(!crypto_aead_read(&ctx, result, tag, aad, sizeof(aad), expected, sizeof(expected)) &&
          !memcmp(result, plain, sizeof(expected)), "RFC 8439 decrypt");
    tag[0] ^= 1;
    memset(result, 0xa5, sizeof(result));
    crypto_aead_init_ietf(&ctx, key, nonce);
    check(crypto_aead_read(&ctx, result, tag, aad, sizeof(aad), expected, sizeof(expected)) != 0, "bad tag");
    for (size_t i = 0; i < sizeof(result); ++i) check(result[i] == 0xa5, "plaintext before authentication");
    crypto_wipe(&ctx, sizeof(ctx));
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        check(file != NULL, "open golden");
        check(!fseek(file, 0, SEEK_END), "seek");
        long length = ftell(file);
        check(length >= 96 && length < 1024 * 1024, "golden size");
        rewind(file);
        uint8_t *payload = malloc((size_t)length + 1), *decoded = NULL;
        size_t decoded_size = 0;
        check(payload && fread(payload, 1, length, file) == (size_t)length, "read golden");
        fclose(file);
        for (int i = 0; i < 32; ++i) key[i] = (uint8_t)i;
        uint8_t build_id[16];
        for (int i = 0; i < 16; ++i) build_id[i] = (uint8_t)i;
        check(!nmmp_open_payload(payload, length, key, build_id, &decoded, &decoded_size), "golden decrypt");
        check(decoded_size == 320, "golden decoded length");
        for (size_t i = 0; i < decoded_size; ++i) check(decoded[i] == (uint8_t)i, "golden bytes");
        nmmp_free_secret(decoded, decoded_size);
        for (long i = 0; i < length; ++i) {
            payload[i] ^= 1;
            check(nmmp_open_payload(payload, length, key, build_id, &decoded, &decoded_size) != 0 &&
                  decoded == NULL && decoded_size == 0, "mutated byte accepted");
            payload[i] ^= 1;
        }
        for (long i = 0; i < length; ++i)
            check(nmmp_open_payload(payload, i, key, build_id, &decoded, &decoded_size) != 0, "truncation accepted");
        payload[length] = 0;
        check(nmmp_open_payload(payload, length + 1, key, build_id, &decoded, &decoded_size) != 0, "trailing byte accepted");
        key[0] ^= 1;
        check(nmmp_open_payload(payload, length, key, build_id, &decoded, &decoded_size) != 0, "wrong key accepted");
        free(payload);
    }
    puts("RFC 8439 and envelope tests passed");
    return 0;
}

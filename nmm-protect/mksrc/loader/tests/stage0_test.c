#include "Stage0.h"
#include "Envelope.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const NmmpNativeProgram nmmp_stage0_programs[4];
static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static void cleared(const uint8_t *key) {
    for (unsigned i = 0; i < 32; ++i) check(!key[i], "partial stage0 key not erased");
}
int main(int argc, char **argv) {
    check(argc == 2, "expected golden envelope path");
    uint8_t build[16], key[32];
    for (unsigned i = 0; i < 16; ++i) build[i] = i;
    check(nmmpRecoverStage0(nmmp_stage0_programs, build, key), "stage0 recovery");
    for (unsigned i = 0; i < 32; ++i) check(key[i] == i, "stage0 key mismatch");
    for (unsigned pos = 0; pos < 16; ++pos) {
        build[pos] ^= 0x80; memset(key, 0xa5, sizeof(key));
        check(!nmmpRecoverStage0(nmmp_stage0_programs, build, key), "wrong full build ID accepted");
        cleared(key); build[pos] ^= 0x80;
    }
    for (unsigned word = 0; word < 4; ++word) {
        NmmpNativeProgram programs[4]; memcpy(programs, nmmp_stage0_programs, sizeof(programs));
        programs[word].hash ^= 1; memset(key, 0xa5, sizeof(key));
        check(!nmmpRecoverStage0(programs, build, key), "bad stage0 program accepted"); cleared(key);
        programs[word] = nmmp_stage0_programs[word]; programs[word].opcodes[1] = programs[word].opcodes[0];
        check(!nmmpRecoverStage0(programs, build, key), "duplicate stage0 opcode accepted"); cleared(key);
        programs[word] = nmmp_stage0_programs[word]; programs[word].size -= 1;
        check(!nmmpRecoverStage0(programs, build, key), "bad stage0 size accepted"); cleared(key);
        programs[word] = nmmp_stage0_programs[word]; programs[word].key ^= 1;
        check(!nmmpRecoverStage0(programs, build, key), "wrong stage0 bootstrap key accepted"); cleared(key);
    }
    check(!nmmpRecoverStage0(NULL, build, key), "null program"); cleared(key);
    check(!nmmpRecoverStage0(nmmp_stage0_programs, NULL, key), "null build"); cleared(key);
    check(!nmmpRecoverStage0(nmmp_stage0_programs, build, NULL), "null output");
    FILE *file = fopen(argv[1], "rb"); check(file != NULL, "open golden");
    check(!fseek(file, 0, SEEK_END), "seek"); long length = ftell(file);
    check(length >= 96 && length < 1024 * 1024, "golden size"); rewind(file);
    uint8_t *payload = malloc((size_t)length), *decoded = NULL; size_t decoded_size = 0;
    check(payload && fread(payload, 1, length, file) == (size_t)length, "read golden"); fclose(file);
    check(nmmpRecoverStage0(nmmp_stage0_programs, build, key), "repeat test recovery");
    check(!nmmp_open_payload(payload, length, key, build, &decoded, &decoded_size), "stage0 key cannot authenticate envelope");
    check(decoded_size == 320, "golden decoded length");
    for (size_t i = 0; i < decoded_size; ++i) check(decoded[i] == (uint8_t)i, "golden decoded byte");
    nmmp_free_secret(decoded, decoded_size); decoded = NULL; decoded_size = 0;
    key[8] ^= 1;
    check(nmmp_open_payload(payload, length, key, build, &decoded, &decoded_size) != 0 && !decoded && !decoded_size,
          "wrong recovered key bypassed AEAD");
    free(payload);
    puts("PASS: stage0 recovery, 128-bit identity, four partial-failure cleanups, authenticated envelope");
}

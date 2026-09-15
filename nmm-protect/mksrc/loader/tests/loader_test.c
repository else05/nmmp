#include "Loader.h"
#include "Envelope.h"
#include "NativeIntegrity.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int nmmp_test_failure, nmmp_test_fail_at, nmmp_test_calls, nmmp_test_maps, nmmp_test_handles;

static void check(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static unsigned char *copy(const unsigned char *p, size_t n) {
    unsigned char *r = malloc(n);
    check(r != NULL, "allocation");
    memcpy(r, p, n);
    return r;
}
static void put64(unsigned char *p, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static void permissions(uintptr_t address, const char *expected) {
    FILE *maps = fopen("/proc/self/maps", "r");
    check(maps != NULL, "maps open");
    char line[1024], flags[5];
    unsigned long begin, end;
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%lx-%lx %4s", &begin, &end, flags) == 3 && address >= begin && address < end) {
            check(!strncmp(flags, expected, 3), "page permissions");
            check(!(flags[1] == 'w' && flags[2] == 'x'), "W+X mapping");
            found = 1;
            break;
        }
    }
    fclose(maps);
    check(found, "mapped address missing");
}
int main(int argc, char **argv) {
    check(argc == 2, "content vector argument");
    FILE *file = fopen(argv[1], "rb");
    check(file != NULL, "content open");
    check(!fseek(file, 0, SEEK_END), "content seek");
    long length = ftell(file);
    check(length > 64 && length < 1024 * 1024, "content size");
    rewind(file);
    unsigned char *original = malloc(length);
    check(original && fread(original, 1, length, file) == (size_t)length, "content read");
    fclose(file);
    unsigned char *data = copy(original, length);
    NmmpModule *module = NULL;
    check(!nmmp_map_image(data, length, &module), "map golden");
    uintptr_t bias = nmmp_image_bias(module), entry = (uintptr_t)nmmp_bootstrap_address(module);
    check(nmmp_image_size(module) == 0x4000 && entry == bias + 0x1000, "mapping bias/entry");
    size_t segment_count;
    const NmmpImageSegment *segments = nmmp_image_segments(module, &segment_count);
    check(segments && segment_count > 0 && segment_count <= NMMP_PRIVATE_MAX_SEGMENTS, "segment registry");
    check(nmmpImageExecutable(segments, segment_count, entry), "entry executable registration");
    check(!nmmpImageExecutable(segments, segment_count, bias + 0x2000), "image hole accepted as code");
    check(!nmmpImageExecutable(segments, segment_count, bias + 0x3000), "RELRO accepted as code");
    check(!nmmpImageExecutable(segments, segment_count, bias + 0x3400), "BSS accepted as code");
    check(nmmpVerifyExecutableSegments(segments, segment_count) == NMMP_NATIVE_MATCH, "authenticated code baseline");
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *code_page = (void *)(entry & ~(uintptr_t)(page - 1));
    check(!mprotect(code_page, page, PROT_READ | PROT_WRITE), "code test write permission");
    *(unsigned char *)entry ^= 1;
    check(nmmpVerifyExecutableSegments(segments, segment_count) == NMMP_NATIVE_MISMATCH, "code mutation missed");
    *(unsigned char *)entry ^= 1;
    check(!mprotect(code_page, page, PROT_READ | PROT_EXEC), "restore code permission");
    check(nmmpVerifyExecutableSegments(segments, segment_count) == NMMP_NATIVE_MATCH, "restored code baseline");
    for (int i = 0; i < 4; ++i) check(*(uintptr_t *)(bias + 0x3000 + i * 8) == entry, "RELA result");
    for (size_t i = 0x3400; i < 0x4000; ++i) check(*(unsigned char *)(bias + i) == 0, "BSS zero");
    permissions(bias, "r-x");
    permissions(bias + 0x2000, "---");
    permissions(bias + 0x3000, "r--");
#if defined(__aarch64__)
    check(((int (*)(void))entry)() == 0, "ARM64 machine code execution");
#endif
    nmmp_discard_image(module);
    free(data);
    check(nmmp_test_maps == 0 && nmmp_test_handles == 0, "normal cleanup");
    data = copy(original, length);
    uint64_t blocks = nmmp_u64(data + 48), phdr = nmmp_u64(data + blocks + 24);
    put64(data + phdr + 4 * 56 + 16, 0x10000);
    check(nmmp_map_image(data, length, &module) != 0 && !module, "out-of-image RELRO accepted");
    free(data);
    data = copy(original, length);
    uint64_t dynamic = nmmp_u64(data + blocks + 32 + 24);
    put64(data + dynamic + 11 * 16 + 8, 0x3201);
    check(nmmp_map_image(data, length, &module) != 0 && !module, "unaligned INIT_ARRAY accepted");
    free(data);
    for (int kind = 1; kind <= 3; ++kind) {
        int points = kind == 1 ? 3 : kind == 2 ? 1 : 7;
        for (int point = 1; point <= points; ++point) {
            data = copy(original, length);
            nmmp_test_failure = kind; nmmp_test_fail_at = point; nmmp_test_calls = 0;
            check(nmmp_map_image(data, length, &module) != 0 && !module, "injected failure accepted");
            check(nmmp_test_maps == 0 && nmmp_test_handles == 0, "failure leaked owned resource");
            nmmp_test_failure = 0;
            free(data);
        }
    }
    for (size_t n = 0; n < 64; ++n) {
        data = copy(original, length);
        check(nmmp_map_image(data, n, &module) != 0 && !module, "short header accepted");
        free(data);
    }
    /* Deterministic malformed corpus: accepted code/data mutations are allowed,
       but every path must stay bounded and release pre-constructor resources. */
    unsigned state = 0x31415926;
    for (unsigned i = 0; i < 2000; ++i) {
        data = copy(original, length);
        state = state * 1664525U + 1013904223U;
        size_t index = state % (size_t)length;
        data[index] ^= (unsigned char)((state >> 24) | 1);
        if (!nmmp_map_image(data, length, &module)) nmmp_discard_image(module);
        free(data);
    }
    free(original);
    check(nmmp_test_maps == 0 && nmmp_test_handles == 0, "corpus leaked mapping");
    puts("mapping, all RELA types, BSS, RX/RELRO/holes and malformed corpus passed");
    return 0;
}

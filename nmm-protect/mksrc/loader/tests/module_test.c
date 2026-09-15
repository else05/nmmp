#include "Loader.h"
#include "Envelope.h"
#include "NativeIntegrity.h"
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int nmmp_test_failure, nmmp_test_fail_at, nmmp_test_calls, nmmp_test_maps, nmmp_test_handles;
extern const char *nmmp_test_missing_name;
static void check(int ok, const char *message) { if (!ok) { fprintf(stderr, "%s\n", message); exit(1); } }
static int mapping_protection(uintptr_t address) {
    FILE *file = fopen("/proc/self/maps", "r");
    check(file != NULL, "maps open");
    char line[1024], flags[5];
    unsigned long start, end;
    int result = -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, flags) == 3 && address >= start && address < end) {
            result = (flags[0] == 'r' ? PROT_READ : 0) | (flags[1] == 'w' ? PROT_WRITE : 0)
                     | (flags[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
    }
    fclose(file);
    check(result >= 0, "mapping found");
    return result;
}
static unsigned char *virtual_at(unsigned char *data, uint64_t address) {
    for (unsigned i = 0; i < nmmp_u32(data + 20); ++i) {
        unsigned char *s = data + 64 + i * 56;
        uint64_t addr = nmmp_u64(s), size = nmmp_u64(s + 8);
        if (address >= addr && address - addr < size) return data + nmmp_u64(s + 40) + address - addr;
    }
    check(0, "fixture virtual address"); return NULL;
}
static uint64_t dynamic_value(unsigned char *data, uint64_t tag) {
    unsigned char *block = data + nmmp_u64(data + 48) + 32;
    unsigned char *dynamic = data + nmmp_u64(block + 24);
    for (uint64_t i = 0; i < nmmp_u64(block + 16); i += 16)
        if (nmmp_u64(dynamic + i) == tag) return nmmp_u64(dynamic + i + 8);
    return 0;
}
int main(int argc, char **argv) {
    check(argc == 3, "module and content arguments");
    void *normal = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    check(normal != NULL, "system load");
    int (*reference)(void) = dlsym(normal, "nmmp_inner_bootstrap_v1");
    check(reference && reference() == 12345, "system constructors and function");
    check(!dlclose(normal), "system close");
    FILE *f = fopen(argv[2], "rb");
    check(f && !fseek(f, 0, SEEK_END), "content open/seek");
    long size = ftell(f);
    check(size > 64 && size < 1024 * 1024, "content length");
    rewind(f);
    unsigned char *original = malloc(size), *data = malloc(size);
    check(original && data && fread(original, 1, size, f) == (size_t)size, "read content");
    fclose(f);
    for (int kind = 4; kind <= 7; ++kind) {
        memcpy(data, original, size);
        nmmp_test_failure = kind; nmmp_test_fail_at = 1; nmmp_test_calls = 0;
        NmmpModule *m = NULL;
        check(nmmp_map_image(data, size, &m) != 0 && !m, "dependency/symbol/version failure accepted");
        check(!nmmp_test_maps && !nmmp_test_handles, "dependency failure cleanup");
    }
    /* Simulate a missing symbol on both default and explicit-version queries.
       Strong fails; changing the same manifest/dynsym binding to WEAK gives S=0. */
    for (unsigned weak = 0; weak < 2; ++weak) {
        memcpy(data, original, size);
        unsigned char *import = data + nmmp_u64(data + 56);
        unsigned sym_index = nmmp_u32(import);
        unsigned char *symbol = virtual_at(data, dynamic_value(data, 6) + sym_index * 24);
        nmmp_test_missing_name = (const char *)virtual_at(data, dynamic_value(data, 5) + nmmp_u32(symbol));
        if (weak) { symbol[4] = (symbol[4] & 15) | 32; import[12] = 2; }
        nmmp_test_failure = 8;
        NmmpModule *test = NULL;
        int status = nmmp_map_image(data, size, &test);
        check(weak ? status == 0 : status != 0, "strong/weak unresolved behavior");
        if (test) {
            unsigned found = 0;
            uint64_t rela = dynamic_value(original, 23), bytes = dynamic_value(original, 2);
            for (uint64_t i = 0; i < bytes; i += 24) {
                unsigned char *r = virtual_at(data, rela + i);
                if (nmmp_u64(r + 8) >> 32 == sym_index) {
                    check(nmmp_u64(r + 16) == 0 && *(uintptr_t *)(nmmp_image_bias(test) + nmmp_u64(r)) == 0,
                          "unresolved weak S+A result");
                    found = 1;
                }
            }
            check(found, "weak relocation exercised");
            nmmp_discard_image(test);
        }
        check(!nmmp_test_maps && !nmmp_test_handles, "strong/weak failure cleanup");
    }
    nmmp_test_missing_name = NULL;
    nmmp_test_failure = 0;
    memcpy(data, original, size);
    NmmpModule *m = NULL;
    check(!nmmp_map_image(data, size, &m), "private map");
    size_t slot_count = 0;
    const NmmpImportSlot *slots = nmmp_import_slots(m, &slot_count);
    check(slots && slot_count > 0 && slot_count <= NMMP_PRIVATE_MAX_IMPORT_SLOTS, "critical import recorded");
    check(nmmpVerifyImportSlots(slots, slot_count) == NMMP_NATIVE_MATCH, "critical import baseline");
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *slot_page = (void *)(slots[0].address & ~(uintptr_t)(page - 1));
    const int original_protection = mapping_protection(slots[0].address);
    check(!(original_protection & PROT_EXEC), "import slot in code");
    check(!mprotect(slot_page, page, PROT_READ | PROT_WRITE), "test slot writable");
    *(uintptr_t *)slots[0].address = slots[0].expected ^ (uintptr_t)1;
    check(nmmpVerifyImportSlots(slots, slot_count) == NMMP_NATIVE_MISMATCH, "changed GOT entry missed");
    *(uintptr_t *)slots[0].address = slots[0].expected;
    check(!mprotect(slot_page, page, original_protection), "restore slot permissions");
    check(nmmpVerifyImportSlots(slots, slot_count) == NMMP_NATIVE_MATCH, "restored GOT entry");
    check(!nmmp_run_constructors(m), "private constructors");
    check(nmmp_run_constructors(m) != 0, "constructors executed twice");
    int (*function)(void) = nmmp_bootstrap_address(m);
    check(function() == 12345, "private function differs from system");
    int maps = nmmp_test_maps, handles = nmmp_test_handles;
    nmmp_discard_image(m);
    check(nmmp_test_maps == maps && nmmp_test_handles == handles && function() == 12345,
          "unsafe unload after constructors");
    free(original); free(data);
    puts("system/private constructors, BSS, imports/default versions, floating result and failure lifecycle passed");
    return 0;
}

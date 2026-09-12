#ifdef NDEBUG
#undef NDEBUG
#endif
#include "NativeVm.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <string>

static uint32_t read32(FILE *f) {
    unsigned char b[4]; assert(fread(b, 1, 4, f) == 4);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
static uint64_t read64(FILE *f) { uint64_t x = read32(f); return x | uint64_t(read32(f)) << 32; }
int main(int argc, char **argv) {
    uint64_t output = 999;
    assert(!nmmpNativeRun(nullptr, nullptr, 0, &output) && !output);
    assert(!nmmpNativeRun(nullptr, nullptr, 0, nullptr));
    if (argc != 2) { puts("NativeVmTest requires generated vector binary"); return 2; }
    FILE *f = fopen(argv[1], "rb"); assert(f);
    uint32_t count = read32(f);
    for (uint32_t n = 0; n < count; ++n) {
        std::string name(read32(f), '\0'); assert(fread(&name[0], 1, name.size(), f) == name.size());
        NmmpNativeProgram program = {};
        std::vector<uint8_t> code(read32(f)); assert(fread(code.data(), 1, code.size(), f) == code.size());
        program.code = code.data(); program.size = code.size(); program.key = read64(f);
        assert(fread(program.opcodes, 1, NMMP_NATIVE_OP_COUNT, f) == NMMP_NATIVE_OP_COUNT);
        program.hash = read32(f);
        std::vector<uint64_t> inputs(read32(f)); for (auto &x : inputs) x = read64(f);
        bool expectedSuccess = read32(f) != 0; uint64_t expectedOutput = read64(f);
        output = 999;
        bool result = nmmpNativeRun(&program, inputs.data(), inputs.size(), &output);
        if (result != expectedSuccess || output != expectedOutput) {
            fprintf(stderr, "Native VM vector mismatch: %s (%u)\n", name.c_str(), n); return 1;
        }
    }
    assert(fgetc(f) == EOF); fclose(f);
    printf("PASS: %u independent Java/native program vectors\n", count);
}

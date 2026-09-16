#include "VmReader.h"
#include <cstdlib>
#include <cstdio>
#include "DemandVector.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
int main() {
    {
        VmReader encoded(vectorCode, sizeof(vectorCode), vectorTries, sizeof(vectorTries),
                         vectorBoundaries, vectorSeed);
        // Reverse order and repeated domain changes exercise random access and cache replacement.
        for (int pass = 0; pass < 3; ++pass) for (int pc = 79; pc >= 0; --pc) {
            unsigned domain = (vectorBoundaries[pc / 2] >> ((pc & 1) * 4)) & 15;
            uint16_t value;
            if (domain == NMMP_READER_FETCH) value = encoded.instruction(pc);
            else if (domain == NMMP_READER_PAYLOAD) value = encoded.payload16(pc);
            else {
                int start = pc;
                while (((vectorBoundaries[start / 2] >> ((start & 1) * 4)) & 15) != NMMP_READER_FETCH) --start;
                value = encoded.operand(start, pc - start);
            }
            CHECK(value == (vectorPlain[pc * 2] | (uint16_t(vectorPlain[pc * 2 + 1]) << 8)));
            CHECK(encoded.try16(pc * 2) == value);
        }
        CHECK(!encoded.failed());
        CHECK(!encoded.target(1)); // operand is not an executable instruction
        VmReader bad(vectorCode, sizeof(vectorCode), nullptr, 0, vectorBoundaries, vectorSeed);
        bad.operand(0, 2); CHECK(bad.failed()); // crosses into the next instruction

        VmReader cached(vectorCode, sizeof(vectorCode), vectorTries, sizeof(vectorTries),
                        vectorBoundaries, vectorSeed);
        for (unsigned pass = 0; pass < 64; ++pass) {
            CHECK(cached.instruction(0) == (vectorPlain[0] | (uint16_t(vectorPlain[1]) << 8)));
            CHECK(cached.operand(0, 1) == (vectorPlain[2] | (uint16_t(vectorPlain[3]) << 8)));
        }
        CHECK(cached.cacheMisses() == 2); // FETCH and OPERAND retain independent cache lines.
    }
    const uint8_t bytes[] = {0x12,0xab,0xff,0xff,0x01,0x80,0x23,0x45};
    VmReader r(bytes, sizeof(bytes));
    CHECK(r.instruction(0) == 0xab12);
    CHECK(r.operand(0, 1) == 0xffff);
    CHECK(r.payload32(2) == 0x45238001);
    CHECK(r.target(3) && !r.failed());
    CHECK(!r.target(-1) && r.failed());
    for (uint32_t n = 0; n < sizeof(bytes); ++n) {
        VmReader cut(bytes, n);
        cut.payload32(2);
        CHECK(cut.failed());
    }
    const uint8_t leb[] = {0, 0x7f, 0xe5, 0x8e, 0x26, 0x9b, 0xf1, 0x59,
                           0xff,0xff,0xff,0xff,0x0f, 0x80,0x80,0x80,0x80,0x78};
    VmReader t(nullptr, 0, leb, sizeof(leb));
    uint32_t p = 0;
    CHECK(t.uleb(p) == 0); CHECK(t.sleb(p) == -1);
    CHECK(t.uleb(p) == 624485); CHECK(t.sleb(p) == -624485);
    CHECK(t.uleb(p) == UINT32_MAX); CHECK(t.sleb(p) == INT32_MIN);
    CHECK(!t.failed() && p == sizeof(leb));
    for (uint32_t n = 0; n < 5; ++n) {
        VmReader cut(nullptr, 0, leb + 8, n); p = 0;
        cut.uleb(p); CHECK(cut.failed());
    }
    const uint8_t invalid[][5] = {{0x80,0x80,0x80,0x80,0x80},
        {0x80,0x80,0x80,0x80,0x10}, {0x80,0x80,0x80,0x80,0x08}};
    for (unsigned i = 0; i < 3; ++i) {
        VmReader bad(nullptr, 0, invalid[i], 5); p = 0;
        if (i == 2) bad.sleb(p); else bad.uleb(p);
        CHECK(bad.failed());
    }
    std::puts("reader scalar, bounds and LEB128 vectors passed");
}

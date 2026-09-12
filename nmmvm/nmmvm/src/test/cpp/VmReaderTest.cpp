#include "VmReader.h"
#include <cstdlib>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
int main() {
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

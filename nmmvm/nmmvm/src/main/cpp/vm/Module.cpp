#include "vm.h"
#include "VmCodec.h"
#include "VmReader.h"
#include "Exception.h"
#include "PrivateLoaderState.h"
#include <stdlib.h>
#include <sys/mman.h>
#if defined(__ANDROID__)
#include <android/api-level.h>
#endif

namespace {
constexpr uint32_t HEADER = 64, RECORD = 56, MAP_BYTES = 1024;
struct Context {
    uint64_t root;
    const uint8_t *blob;
    uint32_t size, count, records, map, boundaryBytes, data;
    uint8_t rows[MAP_BYTES];
    // Decoded boundary nibbles follow this fixed context. No instruction bytes.
};
struct Record {
    uint32_t id, regs, ins, code, codeBytes, tries, triesBytes, boundary, boundaryBytes, row;
    uint64_t tag, seed;
};
static uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
static uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static void wipe(void *p, size_t size) {
    volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
    while (size--) *v++ = 0;
}
static void decode(uint8_t *out, const uint8_t *in, uint32_t size,
                   uint64_t root, uint32_t id, uint32_t domain) {
    const uint64_t base = root ^ (uint64_t(id) * UINT64_C(0x9e3779b97f4a7c15))
            ^ (uint64_t(domain) * UINT64_C(0xd6e8feb86659fd93));
    for (uint32_t pos = 0; pos < size;) {
        uint64_t key = mix(base ^ (pos / 8));
        for (unsigned b = 0; b < 8 && pos < size; ++b, ++pos)
            out[pos] = in[pos] ^ uint8_t(key >> (8 * b));
        wipe(&key, sizeof(key));
    }
}
static bool range(uint32_t offset, uint32_t length, uint32_t first, uint32_t end) {
    return offset >= first && offset <= end && length <= end - offset;
}
static bool record(const Context *c, uint32_t token, uint32_t offset, Record *r) {
    if (!range(offset, RECORD, c->records, c->map) || (offset - c->records) % RECORD) return false;
    uint8_t bytes[RECORD];
    decode(bytes, c->blob + offset, RECORD, c->root, token, NMMP_READER_RECORD);
    bool valid = u32(bytes + 48) == 3 && vmCodecHash(bytes, 52) == u32(bytes + 52);
    if (valid) {
        r->id = u32(bytes); r->tag = u64(bytes + 4);
        r->regs = u32(bytes + 12); r->ins = u32(bytes + 16);
        r->code = u32(bytes + 20); r->codeBytes = u32(bytes + 24);
        r->tries = u32(bytes + 28); r->triesBytes = u32(bytes + 32);
        r->boundary = u32(bytes + 36); r->boundaryBytes = u32(bytes + 40); r->row = u32(bytes + 44);
        r->seed = mix(c->root ^ (uint64_t(r->id) * UINT64_C(0x9e3779b97f4a7c15))
                ^ r->tag ^ (uint64_t(r->regs) << 32) ^ r->ins);
        valid = r->regs <= 65535 && r->ins <= r->regs && r->row == (r->seed & 3)
                && r->codeBytes && !(r->codeBytes & 1)
                && r->boundaryBytes == (uint64_t(r->codeBytes) + 3) / 4
                && range(r->code, r->codeBytes, c->data, c->size)
                && range(r->tries, r->triesBytes, c->data, c->size)
                && range(r->boundary, r->boundaryBytes, 0, c->boundaryBytes);
    }
    wipe(bytes, sizeof(bytes));
    return valid;
}
struct Span { uint32_t offset, size; };
static int compareSpan(const void *a, const void *b) {
    uint32_t x = static_cast<const Span *>(a)->offset, y = static_cast<const Span *>(b)->offset;
    return (x > y) - (x < y);
}
static int compareId(const void *a, const void *b) {
    uint32_t x = *static_cast<const uint32_t *>(a), y = *static_cast<const uint32_t *>(b);
    return (x > y) - (x < y);
}
static bool partition(Span *spans, uint32_t count, uint32_t first, uint32_t end) {
    qsort(spans, count, sizeof(Span), compareSpan);
    uint64_t next = first;
    for (uint32_t i = 0; i < count; ++i) {
        if (spans[i].offset != next) return false;
        next += spans[i].size;
    }
    return next == end;
}
static bool initialize(void *argument) {
    auto *m = static_cast<vmDemandModule *>(argument);
    uint64_t root = 0;
    if (NMMP_VM_CODEC_VERSION != 3 || !vmCodecGetSeed(&root)) return false;
#if defined(__ANDROID__)
    const int api = android_get_device_api_level();
    if (api != 26 && api != 27) { wipe(&root, sizeof(root)); return false; }
#if !defined(__aarch64__)
    wipe(&root, sizeof(root)); return false;
#endif
#endif
    const uint8_t *b = m->blob;
    Context *c = nullptr;
    void *scratch = nullptr;
    size_t allocation = 0;
    bool ok = false;
    do {
        if (!b || m->size < HEADER || memcmp(b, "VMOD0003", 8)
                || u32(b + 8) != 3 || u32(b + 12) != 4 || u32(b + 16) != m->moduleId
                || u32(b + 24) != HEADER || u32(b + 44) != m->size || u64(b + 48) != m->buildId
                || u32(b + 56) || u32(b + 60) || vmCodecHash(b, m->size) != m->hash) break;
        uint32_t count = u32(b + 20), map = u32(b + 28), boundary = u32(b + 32);
        uint32_t boundaryBytes = u32(b + 36), data = u32(b + 40);
        if (uint64_t(HEADER) + uint64_t(count) * (8 + RECORD) != map
                || uint64_t(map) + MAP_BYTES != boundary
                || uint64_t(boundary) + boundaryBytes != data || data > m->size) break;
        allocation = sizeof(Context) + size_t(boundaryBytes);
        void *memory = mmap(nullptr, allocation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) break;
        c = static_cast<Context *>(memory);
        c->root = root; c->blob = b; c->size = m->size; c->count = count;
        c->records = HEADER + count * 8; c->map = map; c->boundaryBytes = boundaryBytes; c->data = data;
        decode(c->rows, b + map, MAP_BYTES, root, m->moduleId, NMMP_READER_MAP);
        auto *bounds = reinterpret_cast<uint8_t *>(c + 1);
        decode(bounds, b + boundary, boundaryBytes, root, m->moduleId, NMMP_READER_DIRECTORY);
        bool valid = true;
        for (unsigned row = 0; row < 4; ++row) {
            bool seen[256] = {};
            if (c->rows[row * 256]) valid = false;
            for (unsigned i = 0; i < 256; ++i) {
                uint8_t op = c->rows[row * 256 + i];
                if (seen[op]) valid = false;
                seen[op] = true;
            }
        }
        if (!valid) break;
        // Only initialization allocates metadata proportional to the method count.
        scratch = calloc(count ? count : 1, sizeof(Span) * 3 + sizeof(uint32_t) + 1);
        if (!scratch) break;
        auto *codeSpans = static_cast<Span *>(scratch);
        auto *boundSpans = codeSpans + count * 2;
        auto *ids = reinterpret_cast<uint32_t *>(boundSpans + count);
        auto *seen = reinterpret_cast<uint8_t *>(ids + count);
        uint32_t codeCount = 0, previous = 0;
        for (uint32_t i = 0; i < count && valid; ++i) {
            const uint8_t *entry = b + HEADER + i * 8;
            uint32_t token = u32(entry), offset = u32(entry + 4);
            Record r = {};
            valid = (!i || token > previous) && record(c, token, offset, &r);
            previous = token;
            if (valid) {
                uint32_t slot = (offset - c->records) / RECORD;
                valid = !seen[slot]; seen[slot] = 1;
                ids[i] = r.id;
                codeSpans[codeCount++] = {r.code, r.codeBytes};
                if (r.triesBytes) codeSpans[codeCount++] = {r.tries, r.triesBytes};
                boundSpans[i] = {r.boundary, r.boundaryBytes};
                const uint8_t *kinds = bounds + r.boundary;
                if ((kinds[0] & 15) != NMMP_READER_FETCH) valid = false;
                for (uint32_t pc = 0; pc < r.codeBytes / 2; ++pc)
                    if (((kinds[pc / 2] >> ((pc & 1) * 4)) & 15) > NMMP_READER_PAYLOAD) valid = false;
                if ((r.codeBytes / 2 & 1) && (kinds[r.boundaryBytes - 1] & 0xf0)) valid = false;
            }
            wipe(&r, sizeof(r));
        }
        if (!valid || !partition(codeSpans, codeCount, data, m->size)
                || !partition(boundSpans, count, 0, boundaryBytes)) break;
        qsort(ids, count, sizeof(uint32_t), compareId);
        for (uint32_t i = 1; i < count; ++i) if (ids[i] == ids[i - 1]) valid = false;
        if (!valid || __atomic_load_n(&m->init.state, __ATOMIC_ACQUIRE) != 1) break;
        if (mprotect(c, allocation, PROT_READ)) break;
        m->context = c;
        ok = true;
    } while (false);
    free(scratch);
    if (!ok && c) { wipe(c, allocation); munmap(c, allocation); }
    wipe(&root, sizeof(root));
    return ok;
}
static void error(JNIEnv *env) {
    if (!env->ExceptionCheck()) dvmThrowInternalError(env, "Invalid on-demand module or token");
}
}

bool vmPrepareDemandModule(JNIEnv *env, vmDemandModule *module) {
    if (module && vmInitRun(&module->init, initialize, module)) return true;
    error(env); return false;
}

jvalue vmExecuteToken(JNIEnv *env, const vmDemandModule *m, u4 token,
                      regptr_t *regs, u1 *regFlags, u4 registerCapacity, const vmResolver *resolver) {
#if defined(NMMP_PRIVATE_LINKER)
    if (nmmpPrivateLoaderFailed()) { error(env); return {}; }
#endif
    if (!m || !regs || !regFlags || !resolver || __atomic_load_n(&m->init.state, __ATOMIC_ACQUIRE) != 2) {
        error(env); return {};
    }
    const auto *c = static_cast<const Context *>(m->context);
    uint32_t low = 0, high = c->count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t candidate = u32(c->blob + HEADER + mid * 8);
        if (candidate < token) low = mid + 1; else high = mid;
    }
    Record r = {};
    if (low == c->count || u32(c->blob + HEADER + low * 8) != token
            || !record(c, token, u32(c->blob + HEADER + low * 8 + 4), &r)
            || r.regs != registerCapacity) {
        wipe(&r, sizeof(r)); error(env); return {};
    }
    VmReader reader(c->blob + r.code, r.codeBytes, c->blob + r.tries, r.triesBytes,
            reinterpret_cast<const uint8_t *>(c + 1) + r.boundary, r.seed, c->rows + r.row * 256);
    const vmCode frame = {nullptr, r.codeBytes / 2, regs, regFlags, nullptr, r.triesBytes, &reader, registerCapacity};
    wipe(&r, sizeof(r));
    return vmInterpretReader(env, &frame, resolver, &reader);
}

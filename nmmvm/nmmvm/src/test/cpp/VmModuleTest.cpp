// Host tests use the actual module parser/reader with a minimal JNI exception boundary.
#ifdef NDEBUG
#undef NDEBUG
#endif
#define _LIBS_CUTILS_LOG_H
#define NMMP_VM_CODEC_CONFIG_H
#define NMMP_VM_CODEC_VERSION 3
#define _64_BIT
#include "../../main/cpp/vm/Module.cpp"
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cassert>

static thread_local bool pending;
static uint64_t ROOT = UINT64_C(0x0123456789abcdef);
static std::vector<uint8_t> expectedCode, expectedTries, expectedBounds;
static jboolean JNICALL checkException(JNIEnv *) { return pending; }
void dvmThrowInternalError(JNIEnv *, const char *) { pending = true; }
extern "C" bool vmCodecGetSeed(uint64_t *seed) { *seed = ROOT; return true; }
extern "C" uint32_t vmCodecHash(const uint8_t *bytes, uint32_t size) {
    uint32_t hash = 0x811c9dc5;
    for (uint32_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 0x01000193; }
    return hash;
}
extern "C" jvalue vmInterpretReader(JNIEnv *, const vmCode *, const vmResolver *, VmReader *reader) {
    jvalue value = {};
    if (!expectedCode.empty()) {
        // Reverse traversal forces domain/window changes and backward random access.
        for (uint32_t pc = expectedCode.size() / 2; pc-- > 0;) {
            auto kind = [&](uint32_t p) { return (expectedBounds[p / 2] >> ((p & 1) * 4)) & 15; };
            uint16_t word = expectedCode[pc * 2] | uint16_t(expectedCode[pc * 2 + 1]) << 8;
            switch (kind(pc)) {
                case 0: assert(!word); break; // Padding is deliberately not executable/readable.
                case 1: assert(reader->instruction(pc) == word); break;
                case 2: case 3: {
                    uint32_t start = pc;
                    while (start && kind(start) != 1) --start;
                    assert(reader->operand(start, pc - start) == word); break;
                }
                case 4: assert(reader->payload16(pc) == word); break;
                default: assert(false);
            }
            assert(!reader->failed());
        }
        for (uint32_t p = expectedTries.size(); p-- > 0;) assert(reader->tryByte(p) == expectedTries[p]);
        assert(!reader->failed());
        value.i = 42; return value;
    }
    assert(reader->instruction(0) == 0x1234);
    assert(reader->operand(0, 1) == 0xabcd);
    assert(!reader->failed());
    value.i = 42; return value;
}
static void put32(std::vector<uint8_t> &b, size_t p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = x >> (8 * i);
}
static void put64(std::vector<uint8_t> &b, size_t p, uint64_t x) {
    put32(b, p, uint32_t(x)); put32(b, p + 4, uint32_t(x >> 32));
}
static void transform(std::vector<uint8_t> &b, uint64_t seed, uint32_t id, uint32_t domain) {
    // Scalar byte oracle, independent of the runtime block decoder.
    for (size_t i = 0; i < b.size(); ++i) {
        uint64_t x = seed ^ uint64_t(id) * UINT64_C(0x9e3779b97f4a7c15)
                ^ uint64_t(domain) * UINT64_C(0xd6e8feb86659fd93) ^ (i / 8);
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        x ^= x >> 31;
        b[i] ^= uint8_t(x >> ((i & 7) * 8));
    }
}
static std::vector<uint8_t> fixture() {
    std::vector<uint8_t> blob(1181), rows(1024), bounds = {0x21}, r(56);
    memcpy(blob.data(), "NMMPOD03", 8);
    put32(blob, 8, 3); put32(blob, 12, 4); put32(blob, 16, 9); put32(blob, 20, 1);
    put32(blob, 24, 64); put32(blob, 28, 128); put32(blob, 32, 1152);
    put32(blob, 36, 1); put32(blob, 40, 1153); put32(blob, 44, 1157);
    blob.resize(1157);
    put64(blob, 48, 77); put32(blob, 64, 0xfedcba98); put32(blob, 68, 72);
    uint64_t seed = mix(ROOT ^ UINT64_C(0x9e3779b97f4a7c15) ^ (UINT64_C(2) << 32));
    unsigned row = seed & 3;
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 1; x < 256; ++x)
        rows[y * 256 + x] = (x - 1 + 17 * (y + 1)) % 255 + 1;
    uint8_t stored = 0;
    while (rows[row * 256 + stored] != 0x34) ++stored;
    std::vector<uint8_t> code = {stored, 0x12, 0xcd, 0xab};
    auto fetch = code, operand = code;
    transform(fetch, seed, 0, 1); transform(operand, seed, 0, 2);
    code[0] = fetch[0]; code[1] = fetch[1]; code[2] = operand[2]; code[3] = operand[3];
    put32(r, 0, 1); put32(r, 12, 2); put32(r, 20, 1153); put32(r, 24, 4);
    put32(r, 28, 1157); put32(r, 40, 1); put32(r, 44, row); put32(r, 48, 3);
    put32(r, 52, vmCodecHash(r.data(), 52));
    transform(r, ROOT, 0xfedcba98, 6); transform(rows, ROOT, 9, 7); transform(bounds, ROOT, 9, 8);
    std::copy(r.begin(), r.end(), blob.begin() + 72);
    std::copy(rows.begin(), rows.end(), blob.begin() + 128);
    blob[1152] = bounds[0]; std::copy(code.begin(), code.end(), blob.begin() + 1153);
    return blob;
}
static void destroy(vmDemandModule &m) {
    if (m.context) {
        size_t size = sizeof(Context) + static_cast<const Context *>(m.context)->boundaryBytes;
        void *p = const_cast<void *>(m.context);
        assert(!mprotect(p, size, PROT_READ | PROT_WRITE)); wipe(p, size); munmap(p, size);
    }
    pthread_cond_destroy(&m.init.condition); pthread_mutex_destroy(&m.init.mutex);
}
static uint32_t read32(FILE *f) {
    uint8_t bytes[4]; assert(fread(bytes, 1, 4, f) == 4); return u32(bytes);
}
static void readBytes(FILE *f, std::vector<uint8_t> &b, uint32_t n) {
    b.resize(n); assert(fread(b.data(), 1, n, f) == n);
}
int main(int argc, char **argv) {
    JNINativeInterface_ functions = {}; functions.ExceptionCheck = checkException;
    JNIEnv env = {&functions};
    if (argc > 1) {
        unsigned methods = 0;
        for (int file = 1; file < argc; ++file) {
            FILE *f = fopen(argv[file], "rb"); assert(f);
            ROOT = read32(f); ROOT |= uint64_t(read32(f)) << 32;
            uint32_t id = read32(f); uint64_t build = read32(f); build |= uint64_t(read32(f)) << 32;
            std::vector<uint8_t> b; readBytes(f, b, read32(f));
            vmDemandModule m = NMMP_DEMAND_MODULE_INIT(b.data(), uint32_t(b.size()), vmCodecHash(b.data(), b.size()), id, build);
            assert(vmPrepareDemandModule(&env, &m) && !pending);
            uint32_t count = read32(f);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t token = read32(f), capacity = read32(f), code = read32(f), tries = read32(f), bounds = read32(f);
                readBytes(f, expectedCode, code); readBytes(f, expectedTries, tries); readBytes(f, expectedBounds, bounds);
                std::vector<regptr_t> regs(capacity ? capacity : 1); std::vector<uint8_t> flags(regs.size());
                vmResolver resolver = {};
                assert(vmExecuteToken(&env, &m, token, regs.data(), flags.data(), capacity, &resolver).i == 42 && !pending);
                ++methods;
            }
            assert(fgetc(f) == EOF); fclose(f); destroy(m);
        }
        printf("PASS: %u Java-generated methods through native token/record/map and reverse-order reader\n", methods);
        return 0;
    }
    auto blob = fixture();
    vmDemandModule module = NMMP_DEMAND_MODULE_INIT(blob.data(), uint32_t(blob.size()), vmCodecHash(blob.data(), blob.size()), 9, 77);
    std::atomic<unsigned> passed{0}; std::vector<std::thread> threads;
    for (unsigned i = 0; i < 16; ++i) threads.emplace_back([&] {
        for (unsigned n = 0; n < 100; ++n) if (vmPrepareDemandModule(&env, &module)) ++passed;
    });
    for (auto &thread : threads) thread.join();
    assert(passed == 1600 && module.init.state == 2);
    regptr_t regs[2] = {}; uint8_t flags[2] = {}; vmResolver resolver = {};
    assert(vmExecuteToken(&env, &module, 0xfedcba98, regs, flags, 2, &resolver).i == 42 && !pending);
    vmExecuteToken(&env, &module, 0xfedcba99, regs, flags, 2, &resolver); assert(pending); pending = false;
    vmExecuteToken(&env, &module, 0xfedcba98, regs, flags, 1, &resolver); assert(pending); pending = false;
    blob[72] ^= 1;
    vmExecuteToken(&env, &module, 0xfedcba98, regs, flags, 2, &resolver); assert(pending); pending = false;
    blob[72] ^= 1; destroy(module);
    // Each corrupted byte must be rejected by the trusted whole-blob checksum.
    for (size_t i = 0; i < blob.size(); ++i) {
        auto bad = blob; bad[i] ^= 1;
        vmDemandModule m = NMMP_DEMAND_MODULE_INIT(bad.data(), uint32_t(bad.size()), vmCodecHash(blob.data(), blob.size()), 9, 77);
        assert(!vmPrepareDemandModule(&env, &m) && pending && m.init.state == 3); pending = false;
        m.blob = blob.data(); assert(!vmPrepareDemandModule(&env, &m)); pending = false; destroy(m);
    }
    // Recomputed outer checksum cannot legitimize bad header fields or encoded records.
    for (unsigned p : {8u,12u,16u,20u,24u,28u,32u,36u,40u,44u,48u,56u,60u,68u,72u,128u,1152u}) {
        auto bad = blob; bad[p] ^= 0x80;
        vmDemandModule m = NMMP_DEMAND_MODULE_INIT(bad.data(), uint32_t(bad.size()), vmCodecHash(bad.data(), bad.size()), 9, 77);
        assert(!vmPrepareDemandModule(&env, &m) && pending); pending = false; destroy(m);
    }
    // Record bounds/row checks are independent of both consistency hashes.
    for (unsigned p : {12u,16u,20u,24u,28u,32u,36u,40u,44u,48u}) {
        auto bad = blob; std::vector<uint8_t> r(bad.begin() + 72, bad.begin() + 128);
        transform(r, ROOT, 0xfedcba98, 6); put32(r, p, 0xffffffff); put32(r, 52, vmCodecHash(r.data(), 52));
        transform(r, ROOT, 0xfedcba98, 6); std::copy(r.begin(), r.end(), bad.begin() + 72);
        vmDemandModule m = NMMP_DEMAND_MODULE_INIT(bad.data(), uint32_t(bad.size()), vmCodecHash(bad.data(), bad.size()), 9, 77);
        assert(!vmPrepareDemandModule(&env, &m) && pending); pending = false; destroy(m);
    }
    VmInit reentry = NMMP_VM_INIT;
    auto nested = [](void *p) { return vmInitRun(static_cast<VmInit *>(p), [](void *) { return true; }, nullptr); };
    assert(!vmInitRun(&reentry, nested, &reentry) && reentry.state == 3);
    assert(!vmInitRun(&reentry, [](void *) { return true; }, nullptr));
    pthread_cond_destroy(&reentry.condition); pthread_mutex_destroy(&reentry.mutex);
    VmInit businessReentry = NMMP_VM_INIT;
    assert(!vmInitRun(&businessReentry, [](void *p) {
        auto *c = static_cast<VmInit *>(p);
        assert(!pthread_mutex_trylock(&c->mutex)); pthread_mutex_unlock(&c->mutex);
        assert(!vmInitRequireReady(c)); return true;
    }, &businessReentry));
    assert(businessReentry.state == 3);
    pthread_cond_destroy(&businessReentry.condition); pthread_mutex_destroy(&businessReentry.mutex);
    struct Failing { VmInit init = NMMP_VM_INIT; std::atomic<unsigned> calls{0}, entered{0}; std::atomic<bool> release{false}; } failing;
    std::atomic<unsigned> failures{0}; threads.clear();
    for (unsigned i = 0; i < 16; ++i) threads.emplace_back([&] {
        ++failing.entered;
        bool ready = vmInitRun(&failing.init, [](void *p) {
            auto *state = static_cast<Failing *>(p); ++state->calls;
            while (!state->release.load()) std::this_thread::yield();
            return false;
        }, &failing);
        if (!ready) ++failures;
    });
    while (failing.entered != 16) std::this_thread::yield();
    failing.release = true;
    for (auto &thread : threads) thread.join();
    assert(failures == 16 && failing.calls == 1 && failing.init.state == 3);
    assert(!vmInitRun(&failing.init, [](void *) { assert(false); return true; }, nullptr));
    pthread_cond_destroy(&failing.init.condition); pthread_mutex_destroy(&failing.init.mutex);
    puts("module: concurrent publication, unsigned token, mapped reader, corruptions, bounds and reentry PASS");
}

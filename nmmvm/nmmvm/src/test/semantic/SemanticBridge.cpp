#include "vm.h"
#include "GlobalCache.h"
#include "DexOpcodes.h"
#include <vector>
#include <cstring>
#if defined(NMMP_TEST_DEMAND)
#include "VmCodec.h"
#include "VmReader.h"
static uint64_t testMix(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
#endif

static jclass resolveClass(JNIEnv *env, u4 idx) {
    return env->FindClass(idx == 0 ? "java/lang/StringBuilder" : "java/lang/String");
}
static const vmMethod *resolveMethod(JNIEnv *env, u4 idx, bool) {
    // Test-only: each scenario runs serially on the caller's Java thread.
    static vmMethod method;
    jclass clazz = resolveClass(env, idx);
    method.classIdx = idx;
    method.shorty = idx == 0 ? "L" : "I";
    method.methodId = env->GetMethodID(clazz, idx == 0 ? "toString" : "length",
                                     idx == 0 ? "()Ljava/lang/String;" : "()I");
    env->DeleteLocalRef(clazz);
    return &method;
}
static const vmResolver resolver = {nullptr, resolveMethod, nullptr, resolveClass, nullptr, nullptr};

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
#if defined(NMMP_TEST_DEMAND)
    if (!vmCodecActivate(0)) return JNI_ERR;
#endif
    cacheInitial(env);
    return env->ExceptionCheck() ? JNI_ERR : JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_nmmedit_semantic_SemanticMain_eval(JNIEnv *env, jclass, jint scenario, jlong input, jobject object) {
    regptr_t regs[8] = {};
    u1 flags[8] = {};
    regs[0] = input;
    std::vector<u2> words;
    std::vector<u1> tries;
    switch (scenario) {
        case 0: case 20: case 21: case 22: case 23: case 24: case 25: case 26:
        case 27: case 28: case 29: case 30: case 31:
            words = {u2(OP_ADD_INT_LIT8 | 0x0100), 0x0700, u2(OP_RETURN | 0x0100)}; break;
        case 1: words = {u2(OP_CONST_4 | 0x0100), u2(OP_CONST_4 | 0x0200),
            u2(OP_IF_GE | 0x0000 | 0x0200), 7, // v2 >= v0 -> return at 9
            u2(OP_ADD_INT_2ADDR | 0x2100),
            u2(OP_ADD_INT_LIT8 | 0x0200), 0x0102,
            u2(OP_GOTO_16), u2(-5), u2(OP_RETURN | 0x0100)}; break;
        case 2: words = {u2(OP_CONST_WIDE | 0x0200), 0xcdef,0x89ab,0x4567,0x0123,
            u2(OP_XOR_LONG_2ADDR | 0x2000), u2(OP_RETURN_WIDE)}; break;
        case 3: words = {u2(OP_PACKED_SWITCH), 12,0, u2(OP_CONST_16 | 0x0100),33,u2(OP_RETURN | 0x0100),
            u2(OP_CONST_16 | 0x0100),11,u2(OP_RETURN | 0x0100),
            u2(OP_CONST_16 | 0x0100),22,u2(OP_RETURN | 0x0100),
            0x0100,2,1,0,6,0,9,0}; break;
        case 4: words = {u2(OP_SPARSE_SWITCH),12,0,u2(OP_CONST_16 | 0x0100),33,u2(OP_RETURN | 0x0100),
            u2(OP_CONST_16 | 0x0100),11,u2(OP_RETURN | 0x0100),
            u2(OP_CONST_16 | 0x0100),22,u2(OP_RETURN | 0x0100),
            0x0200,2,u2(-10000),0xffff,10000,0,6,0,9,0}; break;
        case 5: case 12: {
            const unsigned width = unsigned(input);
            regs[0] = reinterpret_cast<regptr_t>(object); flags[0] = 1;
            words = {u2(OP_FILL_ARRAY_DATA),6,0,u2(OP_CONST_16 | 0x0100),259,u2(OP_RETURN | 0x0100),
                     0x0300,u2(width),259,0};
            words.resize(10 + (259 * width + 1) / 2, 0);
            for (unsigned i = 0; i < 259; ++i)
                for (unsigned b = 0; b < width; ++b) {
                    unsigned pos = 20 + i * width + b;
                    words[pos / 2] |= u2((uint64_t(i) >> (b * 8)) & 255) << ((pos & 1) * 8);
                }
            break;
        }
        case 6:
            words = {u2(OP_CONST_4),u2(OP_THROW),u2(OP_MOVE_EXCEPTION | 0x0100),
                     u2(OP_CONST_16 | 0x0200),77,u2(OP_RETURN | 0x0200)};
            // One try [0,2), handler list count=1, catch-all at pc=2.
            tries = {1,0,0,0, 0,0,0,0,2,0,1,0, 1,0,2}; break;
        case 7: words = {u2(OP_CONST_4),u2(OP_THROW)}; break;
        case 8: case 9:
            regs[0] = reinterpret_cast<regptr_t>(object); flags[0] = 1;
            words = {u2(OP_INVOKE_VIRTUAL | 0x1000),0,0};
            if (scenario == 8) words.insert(words.end(), {u2(OP_MOVE_RESULT_OBJECT | 0x0100),
                u2(OP_INVOKE_VIRTUAL | 0x1000),1,1,u2(OP_MOVE_RESULT | 0x0200),u2(OP_RETURN | 0x0200)});
            else words.insert(words.end(), {u2(OP_CONST_16 | 0x0100),41,u2(OP_RETURN | 0x0100)});
            break;
        case 10: words = {u2(OP_CONST_WIDE),0}; break;
        case 11: words = {u2(OP_GOTO_16),u2(-1)}; break;
        case 32: words = {u2(OP_RETURN | 0xff00)}; break;
        default: env->ThrowNew(env->FindClass("java/lang/AssertionError"), "unknown scenario"); return 0;
    }
#if defined(NMMP_TEST_DEMAND)
    // Test-only encoder for fixed hand-authored Dalvik fixtures. Production uses Java DemandCodec.
    std::vector<u1> encoded(words.size() * 2), boundaries((words.size() + 1) / 2, 0);
    auto putKind = [&](unsigned pc, unsigned kind) { boundaries[pc / 2] |= kind << ((pc & 1) * 4); };
    for (unsigned pc = 0; pc < words.size();) {
        u2 op = words[pc] & 255;
        unsigned width = 1, domain = NMMP_READER_OPERAND;
        if (words[pc] == 0x0100 || words[pc] == 0x0200 || words[pc] == 0x0300) {
            for (; pc < words.size(); ++pc) putKind(pc, NMMP_READER_PAYLOAD);
            break;
        }
        switch (op) {
            case OP_CONST_WIDE: width = 5; domain = NMMP_READER_WIDE; break;
            case OP_ADD_INT_LIT8: case OP_CONST_16: case OP_IF_GE: case OP_GOTO_16: width = 2; break;
            case OP_PACKED_SWITCH: case OP_SPARSE_SWITCH: case OP_FILL_ARRAY_DATA: case OP_INVOKE_VIRTUAL: width = 3; break;
        }
        putKind(pc, NMMP_READER_FETCH);
        for (unsigned i = 1; i < width && pc + i < words.size(); ++i) putKind(pc + i, domain);
        pc += width;
    }
    uint64_t root = 0; vmCodecGetSeed(&root);
    const uint64_t seed = testMix(root ^ (uint64_t(scenario) * UINT64_C(0x9e3779b97f4a7c15)) ^ (UINT64_C(8) << 32));
    auto key = [&](unsigned pos, unsigned domain) {
        return u1(testMix(seed ^ (uint64_t(domain) * UINT64_C(0xd6e8feb86659fd93)) ^ (pos / 8)) >> ((pos & 7) * 8));
    };
    for (unsigned pos = 0; pos < encoded.size(); ++pos) {
        unsigned domain = (boundaries[pos / 4] >> (((pos / 2) & 1) * 4)) & 15;
        encoded[pos] = u1(words[pos / 2] >> ((pos & 1) * 8)) ^ key(pos, domain);
    }
    for (unsigned pos = 0; pos < tries.size(); ++pos) tries[pos] ^= key(pos, NMMP_READER_TRIES);
    vmDemandCode demand = {encoded.data(), u4(encoded.size()), tries.data(), u4(tries.size()),
        boundaries.data(), u4(boundaries.size()), u4(scenario), 0, 8, 0,
        vmCodecHash(encoded.data(), encoded.size()), vmCodecHash(tries.data(), tries.size()),
        vmCodecHash(boundaries.data(), boundaries.size()), 0, 0};
    const uint32_t fields[] = {3, demand.methodId, 0, 0, 8, 0, demand.codeBytes,
        demand.triesBytes, demand.boundariesBytes, demand.codeHash, demand.triesHash,
        demand.boundariesHash, uint32_t(seed), uint32_t(seed >> 32)};
    demand.contextHash = UINT32_C(0x811c9dc5);
    for (uint32_t field : fields) for (unsigned b = 0; b < 4; ++b) {
        demand.contextHash ^= uint8_t(field >> (b * 8)); demand.contextHash *= UINT32_C(0x01000193);
    }
    switch (scenario) {
        case 20: demand.descriptorTag ^= 1; break;
        case 21: demand.methodId ^= 1; break;
        case 22: demand.registersSize ^= 1; break;
        case 23: demand.insSize ^= 1; break;
        case 24: demand.codeBytes ^= 1; break;
        case 25: demand.triesBytes ^= 1; break;
        case 26: demand.boundariesBytes ^= 1; break;
        case 27: demand.codeHash ^= 1; break;
        case 28: demand.triesHash ^= 1; break;
        case 29: demand.boundariesHash ^= 1; break;
        case 30: demand.contextHash ^= 1; break;
    }
    if (!vmPrepareDemandCode(env, &demand)) return 0;
    if (scenario == 31) demand.descriptorTag ^= 1; // corruption after READY
    jvalue value = vmExecuteDemand(env, &demand, regs, flags, 8, &resolver);
#else
    const vmCode code = {words.data(), u4(words.size()), regs, flags,
                        tries.empty() ? nullptr : tries.data()
#ifndef NMMP_BASELINE
                        , u4(tries.size()), nullptr
#endif
    };
    jvalue value = vmInterpret(env, &code, &resolver);
#endif
    return scenario == 2 ? value.j : value.i;
}

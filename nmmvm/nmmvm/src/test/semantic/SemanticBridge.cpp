#include "vm.h"
#include "GlobalCache.h"
#include "DexOpcodes.h"
#include <vector>
#include <cstring>

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
        case 0: words = {u2(OP_ADD_INT_LIT8 | 0x0100), 0x0700, u2(OP_RETURN | 0x0100)}; break;
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
        case 5: {
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
        default: env->ThrowNew(env->FindClass("java/lang/AssertionError"), "unknown scenario"); return 0;
    }
    const vmCode code = {words.data(), u4(words.size()), regs, flags,
                        tries.empty() ? nullptr : tries.data()
#ifndef NMMP_BASELINE
                        , u4(tries.size()), nullptr
#endif
    };
    jvalue value = vmInterpret(env, &code, &resolver);
    return scenario == 2 ? value.j : value.i;
}

#include "vm.h"
#include "VmReader.h"
#include "DexOpcodes.h"
#include <vector>
#include <cstddef>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

static jclass callbackClass(JNIEnv *env, u4) { return env->FindClass("InvariantMain"); }
static const vmMethod *callbackMethod(JNIEnv *env, u4 index, bool) {
    static thread_local vmMethod method;
    jclass cls = callbackClass(env, 0);
    method.classIdx = 0;
    method.shorty = index ? "IIIIIII" : "I";
    method.methodId = env->GetStaticMethodID(cls, index ? "sum6" : "callback",
                                            index ? "(IIIIII)I" : "()I");
    env->DeleteLocalRef(cls);
    return &method;
}
static const vmResolver callbacks = [] {
    vmResolver r = {};
    r.dvmResolveClass = callbackClass;
    r.dvmResolveMethod = callbackMethod;
    return r;
}();

extern "C" JNIEXPORT jlong JNICALL
Java_InvariantMain_eval(JNIEnv *env, jclass, jint scenario, jboolean checked, jint capacity) {
    regptr_t regs[8] = {};
    u1 flags[8] = {};
    regs[0] = 5;
    std::vector<u2> words;
    switch (scenario) {
        case 0: case 6: words = {u2(OP_ADD_INT_LIT8 | 0x0100), 0x0700, u2(OP_RETURN | 0x0100)}; break;
        case 1: words = {u2(OP_CONST_WIDE | 0x0600), 0xcdef, 0x89ab, 0x4567, 0x0123,
                         u2(OP_RETURN_WIDE | 0x0600)}; break;
        case 2: regs[0] = 1; words = {u2(OP_MOVE_OBJECT | 0x0100), u2(OP_RETURN_VOID)}; break;
        case 3: words = {u2(OP_INVOKE_STATIC_RANGE | 0x0600), 1, 3, u2(OP_RETURN_VOID)}; break;
        case 4: words = {u2(OP_INVOKE_STATIC), 0, 0, u2(OP_MOVE_RESULT),
                         u2(OP_ADD_INT_LIT8 | 0x0100), 0x0300, u2(OP_RETURN | 0x0100)}; break;
        case 5: words = {u2(OP_RETURN | 0xff00)}; break;
        default: env->FatalError("Unknown invariant fixture"); return 0;
    }
    VmReader reader(reinterpret_cast<const uint8_t *>(words.data()), words.size() * 2);
    if (scenario == 6) {
        // Stronger than a poisoned value: legacy capacity is on an unreadable page.
        // The complete object is first legally constructed, then only that page is protected.
        size_t page = size_t(sysconf(_SC_PAGESIZE));
        void *memory = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) env->FatalError("Guard fixture mmap failed");
        void *position = static_cast<char *>(memory) + page - offsetof(vmCode, registerCapacity);
        const vmCode *guarded = new (position) const vmCode {words.data(), u4(words.size()), regs, flags,
                                                           nullptr, 0, nullptr, 0};
        if (mprotect(static_cast<char *>(memory) + page, page, PROT_NONE)) env->FatalError("Guard fixture mprotect failed");
        jvalue value = vmInterpret(env, guarded, &callbacks);
        if (mprotect(static_cast<char *>(memory) + page, page, PROT_READ | PROT_WRITE)) env->FatalError("Guard fixture restore failed");
        guarded->~vmCode();
        munmap(memory, page * 2);
        return value.i;
    }
    const vmCode code = {words.data(), u4(words.size()), regs, flags, nullptr, 0,
                         checked ? &reader : nullptr, u4(capacity)};
    jvalue value = vmInterpret(env, &code, &callbacks);
    return scenario == 1 ? value.j : value.i;
}

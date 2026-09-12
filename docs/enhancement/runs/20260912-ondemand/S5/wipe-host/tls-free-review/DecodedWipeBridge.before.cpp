#include "vm.h"
#include "VmReader.h"
#include "VmDecodedState.h"
#include "DexOpcodes.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#ifndef NMMP_TEST_DECODE_WIPE
#error This bridge is test-only
#endif

namespace {
std::mutex lock;
std::set<const VmDecodedState *> active;
// errors, enter, before, after, normal, uncaught, readerFailed, words, nested, dirty
std::atomic<long> counts[10];
struct Frame { const VmDecodedState *state; VmDecodedState parent; bool before; };
thread_local std::vector<Frame> frames;
bool zero(const void *p, size_t n) {
    const unsigned char *b = static_cast<const unsigned char *>(p);
    while (n--) if (*b++) return false;
    return true;
}
// Aggregate initialization initializes members; do not require entry padding.
bool membersZero(const VmDecodedState &s) {
    return s.tmp64 == 0 && s.arrayData == 0 && s.switchData == 0 && s.ref == 0 &&
           s.tmp32 == 0 && s.arg5 == 0 && s.count == 0 && s.tmpSigned == 0 &&
           s.offset == 0 && s.branchOffset == 0 && s.catchRelPc == 0 && s.inst == 0 &&
           s.vdst == 0 && s.vsrc1 == 0 && s.vsrc2 == 0 && s.regs == 0 &&
           s.srcRegs == 0 && s.litInfo == 0 && s.arrayInfo == 0 && !s.methodCallRange;
}
void check(bool ok) { if (!ok) ++counts[0]; }
jclass callbackClass(JNIEnv *env, u4) { return env->FindClass("com/nmmedit/semantic/DecodedWipeMain"); }
const vmMethod *callbackMethod(JNIEnv *env, u4, bool) {
    static thread_local vmMethod method;
    jclass cls = callbackClass(env, 0);
    method.classIdx = 0;
    method.shorty = "I";
    method.methodId = env->GetStaticMethodID(cls, "callback", "()I");
    env->DeleteLocalRef(cls);
    return &method;
}
const vmResolver resolver = [] {
    vmResolver r = {};
    r.dvmResolveClass = callbackClass;
    r.dvmResolveMethod = callbackMethod;
    return r;
}();
}

extern "C" void nmmpObserveDecodedWord(const uint16_t *word) {
    check(*word == 0);
    ++counts[7];
}
extern "C" void nmmpObserveDecodedState(const VmDecodedState *state, unsigned phase, unsigned reason) {
    if (phase == NMMP_DECODE_ENTER) {
        check(membersZero(*state));
        Frame frame = {state, {}, false};
        if (!frames.empty()) {
            std::memcpy(&frame.parent, frames.back().state, sizeof(frame.parent));
            ++counts[8];
        }
        { std::lock_guard<std::mutex> guard(lock); check(active.insert(state).second); }
        frames.push_back(frame);
        ++counts[1];
        return;
    }
    check(!frames.empty() && frames.back().state == state);
    if (frames.empty()) return;
    if (phase == NMMP_DECODE_BEFORE_WIPE) {
        check(!frames.back().before);
        frames.back().before = true;
        ++counts[2];
        if (!membersZero(*state)) ++counts[9];
        check(reason <= NMMP_DECODE_READER_FAILED);
        if (reason <= NMMP_DECODE_READER_FAILED) ++counts[4 + reason];
    } else {
        check(phase == NMMP_DECODE_AFTER_WIPE && frames.back().before);
        check(zero(state, sizeof(*state)));
        if (frames.size() > 1)
            check(std::memcmp(&frames.back().parent, frames[frames.size() - 2].state, sizeof(*state)) == 0);
        { std::lock_guard<std::mutex> guard(lock); check(active.erase(state) == 1); }
        frames.pop_back();
        ++counts[3];
    }
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_nmmedit_semantic_DecodedWipeMain_snapshot(JNIEnv *env, jclass) {
    jlong result[11];
    for (unsigned i = 0; i < 10; ++i) result[i] = counts[i].load();
    { std::lock_guard<std::mutex> guard(lock); result[10] = active.size(); }
    jlongArray out = env->NewLongArray(11);
    if (out) env->SetLongArrayRegion(out, 0, 11, result);
    return out;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nmmedit_semantic_DecodedWipeMain_eval(JNIEnv *env, jclass, jint scenario, jthrowable pending) {
    regptr_t regs[8] = {};
    u1 flags[8] = {};
    regs[0] = 5;
    std::vector<u2> words;
    std::vector<u1> tries;
    switch (scenario) {
        case 0: words = {u2(OP_ADD_INT_LIT8 | 0x0100), 0x0700, u2(OP_RETURN | 0x0100)}; break;
        case 1: words = {OP_CONST_4, OP_THROW}; break;
        case 2: case 5: words = {OP_CONST_WIDE, 0}; break;
        case 3:
            words = {OP_CONST_4, OP_THROW, u2(OP_MOVE_EXCEPTION | 0x0100),
                     u2(OP_CONST_16 | 0x0200), 77, u2(OP_RETURN | 0x0200)};
            tries = {1,0,0,0, 0,0,0,0,2,0,1,0, 1,0,2};
            break;
        case 4:
            words = {OP_INVOKE_STATIC, 0, 0, OP_MOVE_RESULT,
                     u2(OP_ADD_INT_LIT8 | 0x0100), 0x0300, u2(OP_RETURN | 0x0100)};
            break;
        default: env->FatalError("Unknown wipe fixture"); return 0;
    }
    VmReader reader(reinterpret_cast<const uint8_t *>(words.data()), words.size() * 2,
                    tries.data(), tries.size());
    const vmCode code = {words.data(), u4(words.size()), regs, flags, tries.data(), u4(tries.size()), &reader, 8};
    if (pending) env->Throw(pending);
    jvalue value = vmInterpret(env, &code, &resolver);
    return value.i;
}

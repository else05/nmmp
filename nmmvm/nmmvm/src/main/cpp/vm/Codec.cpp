#include <cstdlib>

#include "Exception.h"
#include "VmCodec.h"
#include "vm.h"

extern "C"
jvalue vmExecute(JNIEnv *env,
                 const vmEncodedCode *code,
                 const vmResolver *dvmResolver) {
    jvalue result = {};
    u1 *decodedInsns = NULL;
    u1 *decodedTries = NULL;

    do {
        if (code == NULL) {
            dvmThrowInternalError(env, "VM 编码结构为空");
            break;
        }
        if (code->codecVersion != NMMP_VM_CODEC_VERSION) {
            dvmThrowInternalError(env, "VM codec 版本不匹配");
            break;
        }
        if (code->encodedInsns == NULL
                || code->encodedInsnsByteSize == 0
                || (code->encodedInsnsByteSize & 1U) != 0) {
            dvmThrowInternalError(env, "VM 指令数据无效");
            break;
        }
        if ((code->encodedTries == NULL) != (code->encodedTriesByteSize == 0)) {
            dvmThrowInternalError(env, "VM 异常表数据无效");
            break;
        }
        if (dvmResolver == NULL) {
            dvmThrowInternalError(env, "VM 解析器为空");
            break;
        }

        decodedInsns = static_cast<u1 *>(malloc(code->encodedInsnsByteSize));
        if (decodedInsns == NULL) {
            dvmThrowInternalError(env, "VM 指令内存分配失败");
            break;
        }
        memcpy(decodedInsns, code->encodedInsns, code->encodedInsnsByteSize);
        vmCodecTransform(decodedInsns,
                         code->encodedInsnsByteSize,
                         code->methodId,
                         NMMP_VM_DOMAIN_CODE);
        if (vmCodecHash(decodedInsns, code->encodedInsnsByteSize)
                != code->plainCodeHash) {
            dvmThrowInternalError(env, "VM 指令解码校验失败");
            break;
        }

        if (code->encodedTriesByteSize != 0) {
            decodedTries = static_cast<u1 *>(malloc(code->encodedTriesByteSize));
            if (decodedTries == NULL) {
                dvmThrowInternalError(env, "VM 异常表内存分配失败");
                break;
            }
            memcpy(decodedTries, code->encodedTries, code->encodedTriesByteSize);
            vmCodecTransform(decodedTries,
                             code->encodedTriesByteSize,
                             code->methodId,
                             NMMP_VM_DOMAIN_TRIES);
        }
        if (vmCodecHash(decodedTries, code->encodedTriesByteSize)
                != code->plainTriesHash) {
            dvmThrowInternalError(env, "VM 异常表解码校验失败");
            break;
        }

        const vmCode runtimeCode = {
                reinterpret_cast<const u2 *>(decodedInsns),
                code->encodedInsnsByteSize / 2U,
                code->regs,
                code->reg_flags,
                decodedTries
        };
        result = vmInterpret(env, &runtimeCode, dvmResolver);
    } while (false);

    free(decodedTries);
    free(decodedInsns);
    return result;
}

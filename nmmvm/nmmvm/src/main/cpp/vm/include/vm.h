//
// Created by mao on 20-8-11.
//

#ifndef DEX_EDITOR_VM_H
#define DEX_EDITOR_VM_H

#include <jni.h>
#include "Common.h"
#include "VmInit.h"

#ifdef __cplusplus
class VmReader;
extern "C" {
#else
typedef struct VmReader VmReader;
#endif

typedef struct {
    u2 classIdx;
    char type;
    jfieldID fieldId;
} vmField;

typedef struct {
    u2 classIdx;
    const char *shorty;  //包含返回类型及参数类型,用于确定使用什么样的方法调用
    jmethodID methodId;
} vmMethod;

typedef struct {
    const u2 *insns;             //指令
    const u4 insnsSize;          //指令大小
    regptr_t *regs;                    //寄存器
    u1 *reg_flags;               //寄存器数据类型标记,主要标记是否为对象
    const u1 *triesHandlers;     //异常表
    u4 triesByteSize;
    VmReader *reader;
    u4 registerCapacity;
} vmCode;

typedef struct {
    const u1 *encodedInsns;
    u4 encodedInsnsByteSize;
    regptr_t *regs;
    u1 *reg_flags;
    const u1 *encodedTries;
    u4 encodedTriesByteSize;
    u4 methodId;
    u4 plainCodeHash;
    u4 plainTriesHash;
    u2 codecVersion;
} vmEncodedCode;

// S2 transition record. S3 replaces wrapper references with a token directory.
typedef struct {
    const u1 *code;
    u4 codeBytes;
    const u1 *tries;
    u4 triesBytes;
    const u1 *boundaries;
    u4 boundariesBytes;
    u4 methodId;
    u8 descriptorTag;
    u4 registersSize;
    u4 insSize;
    u4 codeHash;
    u4 triesHash;
    u4 boundariesHash;
    u4 contextHash;
    int state;
} vmDemandCode;

typedef struct {
    const u1 *blob;
    u4 size;
    u4 hash;
    u4 moduleId;
    u8 buildId;
    VmInit init;
    const void *context;
} vmDemandModule;
#define NMMP_DEMAND_MODULE_INIT(blob, size, hash, module, build) \
    {blob, size, hash, module, build, NMMP_VM_INIT, NULL}

typedef struct {

    const vmField *(*dvmResolveField)(JNIEnv *env, u4 idx, bool isStatic);

    const vmMethod *(*dvmResolveMethod)(JNIEnv *env, u4 idx, bool isStatic);

    //从类型常量池取得类型名
    const char *(*dvmResolveTypeUtf)(JNIEnv *env, u4 idx);

    //直接返回jclass对象,本地引用需要释放引用
    jclass (*dvmResolveClass)(JNIEnv *env, u4 idx);

    //根据类型名得到class
    jclass (*dvmFindClass)(JNIEnv *env, const char *type);

    //const_string指令加载的字符串对象
    jstring (*dvmConstantString)(JNIEnv *env, u4 idx);

} vmResolver;


jvalue vmInterpret(
        JNIEnv *env,
        const vmCode *code,
        const vmResolver *dvmResolver
);

jvalue vmExecute(
        JNIEnv *env,
        const vmEncodedCode *code,
        const vmResolver *dvmResolver
);

bool vmPrepareDemandCode(JNIEnv *env, vmDemandCode *code);
bool vmPrepareDemandModule(JNIEnv *env, vmDemandModule *module);
jvalue vmExecuteToken(JNIEnv *env, const vmDemandModule *module, u4 token,
                      regptr_t *regs, u1 *regFlags, u4 registerCapacity, const vmResolver *resolver);
jvalue vmInterpretReader(JNIEnv *env, const vmCode *code, const vmResolver *resolver, VmReader *reader);
jvalue vmExecuteDemand(JNIEnv *env, const vmDemandCode *code, regptr_t *regs,
                       u1 *regFlags, u4 registerCapacity, const vmResolver *resolver);

#ifdef __cplusplus
}
#endif

#endif //DEX_EDITOR_VM_H

package com.nmmedit.apkprotect.dex2c.converter;

import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.reference.FieldReference;
import com.android.tools.smali.dexlib2.iface.reference.MethodReference;
import com.android.tools.smali.dexlib2.util.MethodUtil;
import com.nmmedit.apkprotect.dex2c.MethodCodec;
import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.util.ModifiedUtf8;

import javax.annotation.Nonnull;
import java.io.IOException;
import java.io.UTFDataFormatException;
import java.io.Writer;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * 根据dex生成符号解析代码,比如字符串常量池,类型常量池这些
 */

public class ResolverCodeGenerator {


    private final References references;
    private final ProtectionContext protectionContext;
    private final long dexId;
    private byte[] manifestEncodedStringPool;
    private long[] manifestStringOffsets;

    public ResolverCodeGenerator(DexBackedDexFile dexFile,
                                 @Nonnull ClassAnalyzer analyzer,
                                 @Nonnull ProtectionContext protectionContext,
                                 long dexId
    ) {

        references = new References(dexFile, analyzer);
        this.protectionContext = protectionContext;
        this.dexId = dexId;
    }

    public References getReferences() {
        return references;
    }

    public void generate(Writer writer) throws IOException {
        writer.write("#include \"GlobalCache.h\"\n");
        writer.write("#include \"ConstantPool.h\"\n");
        writer.write("#include \"VmCodec.h\"\n");
        writer.write("#include \"VmCodecConfig.h\"\n\n");
        writer.write("#include \"Sha256.h\"\n");
        writer.write("#include \"ProtectionManifest.h\"\n\n");
        writer.write("#include <pthread.h>\n");
        writer.write("#include <string.h>\n\n");

        generateStringPool(writer);
        generateTypePool(writer);

        //额外添加的,方便生成结构体
        generateClassNamePool(writer);
        generateSignaturePool(writer);

        generateFieldPool(writer);

        generateMethodPool(writer);

        generateStringConstants(writer);

        //生成初始化函数及符号解析器结构体
        generateDemandValidation(writer);
        generateResolver(writer);
    }

    //产生const-string*指令对应的缓存
    private void generateStringConstants(Writer writer) throws IOException {
        final References references = this.references;
        final List<String> constantStringPool = references.getConstantStringPool();

        final int[] constStringIds = new int[constantStringPool.size()];
        for (int i = 0; i < constantStringPool.size(); i++) {
            //把得到字符串索引
            constStringIds[i] = references.getStringItemIndex(constantStringPool.get(i));
        }

        //
        writer.write(
                "\n//字符串常量索引缓存,const-string指令索引被重写，直接根据索引得到字符串索引，然后创建jstring\n" +
                        "typedef struct {\n" +
                        "    u4 idx;\n" +
                        "} ConstStringId;\n"
        );

        writer.write(String.format(
                "static const ConstStringId gStringConstantIds[%d] = {\n",
                Math.max(1, constStringIds.length)));
        for (int offset : constStringIds) {
            writer.write(String.format("    {.idx=0x%04x},\n", offset));
        }
        writer.write("};\n");

        writer.write(String.format(
                "static jstring gStringConstants[%d];\n",
                Math.max(1, constStringIds.length)));
        writer.write(String.format(
                "static u1 gStringReady[%d];\n\n",
                Math.max(1, constStringIds.length)));
    }

    private void generateDemandValidation(Writer writer) throws IOException {
        String[] names = {"gStringIds", "gTypeIds", "gClassIds", "gSignatureIds", "gFieldIds", "gMethodIds", "gStringConstantIds"};
        int[] counts = {references.getStringPool().size(), references.getTypePool().size(),
                references.getClassNamePool().size(), references.getSignaturePool().size(),
                references.getFieldPool().size(), references.getMethodPool().size(), references.getConstantStringPool().size()};
        for (int i = 0; i < names.length; ++i) writer.write("#define " + names[i] + "_COUNT " + counts[i] + "u\n");
        writer.write("static bool nmmp_validate_resolver(void) {\n"
                + "    for (u4 i=0; i<gStringIds_COUNT; ++i) { u4 off=gStringIds[i].off; if (off>=gStringPoolByteSize || !memchr(gBaseStrPtr+off, 0, gStringPoolByteSize-off)) return false; }\n");
        for (String name : new String[]{"gTypeIds", "gClassIds", "gSignatureIds", "gStringConstantIds"})
            writer.write("    for (u4 i=0; i<" + name + "_COUNT; ++i) if (" + name + "[i].idx>=gStringIds_COUNT) return false;\n");
        writer.write("    if (gTypeIds_COUNT != gClassIds_COUNT) return false;\n"
                + "    for (u4 i=0; i<gFieldIds_COUNT; ++i) { FieldId f=gFieldIds[i]; if(f.classIdx>=gClassIds_COUNT || f.typeIdx>=gTypeIds_COUNT || f.nameIdx>=gStringIds_COUNT) return false; }\n"
                + "    for (u4 i=0; i<gMethodIds_COUNT; ++i) { MethodId m=gMethodIds[i]; if(m.classIdx>=gClassIds_COUNT || m.sigIdx>=gSignatureIds_COUNT || m.nameIdx>=gStringIds_COUNT || m.shortyIdx>=gStringIds_COUNT) return false;\n"
                + "        const char *s=(const char *)gBaseStrPtr+gStringIds[m.shortyIdx].off; if(!*s || !strchr(\"VZBCSIJFDL\", *s)) return false;\n"
                + "        for (++s; *s; ++s) if(!strchr(\"ZBCSIJFDL\", *s)) return false;\n"
                + "    }\n    return true;\n}\n");
        writer.write("static bool nmmp_verify_resolver_manifest(const uint8_t expected[32]) {\n"
                + "    NmmpSha256Context hash; uint8_t digest[32];\n"
                + "    nmmpSha256Init(&hash);\n"
                + "    nmmpSha256Update(&hash, (const uint8_t *)\"NMMP-RSL1\", 9);\n"
                + "    nmmpSha256UpdateU32(&hash, gStringPoolDexId);\n"
                + "    nmmpSha256UpdateU32(&hash, gStringPoolByteSize);\n"
                + "    nmmpSha256Update(&hash, gBaseStrPtr, gStringPoolByteSize);\n"
                + "    nmmpSha256UpdateU32(&hash, gStringIds_COUNT);\n"
                + "    for (u4 i=0; i<gStringIds_COUNT; ++i) nmmpSha256UpdateU32(&hash, gStringIds[i].off);\n"
                + "    nmmpSha256UpdateU32(&hash, gTypeIds_COUNT);\n"
                + "    for (u4 i=0; i<gTypeIds_COUNT; ++i) nmmpSha256UpdateU32(&hash, gTypeIds[i].idx);\n"
                + "    nmmpSha256UpdateU32(&hash, gClassIds_COUNT);\n"
                + "    for (u4 i=0; i<gClassIds_COUNT; ++i) nmmpSha256UpdateU32(&hash, gClassIds[i].idx);\n"
                + "    nmmpSha256UpdateU32(&hash, gSignatureIds_COUNT);\n"
                + "    for (u4 i=0; i<gSignatureIds_COUNT; ++i) nmmpSha256UpdateU32(&hash, gSignatureIds[i].idx);\n"
                + "    nmmpSha256UpdateU32(&hash, gFieldIds_COUNT);\n"
                + "    for (u4 i=0; i<gFieldIds_COUNT; ++i) { FieldId v=gFieldIds[i]; nmmpSha256UpdateU32(&hash,v.classIdx); nmmpSha256UpdateU32(&hash,v.nameIdx); nmmpSha256UpdateU32(&hash,v.typeIdx); }\n"
                + "    nmmpSha256UpdateU32(&hash, gMethodIds_COUNT);\n"
                + "    for (u4 i=0; i<gMethodIds_COUNT; ++i) { MethodId v=gMethodIds[i]; nmmpSha256UpdateU32(&hash,v.classIdx); nmmpSha256UpdateU32(&hash,v.nameIdx); nmmpSha256UpdateU32(&hash,v.shortyIdx); nmmpSha256UpdateU32(&hash,v.sigIdx); }\n"
                + "    nmmpSha256UpdateU32(&hash, gStringConstantIds_COUNT);\n"
                + "    for (u4 i=0; i<gStringConstantIds_COUNT; ++i) nmmpSha256UpdateU32(&hash, gStringConstantIds[i].idx);\n"
                + "    nmmpSha256Final(&hash, digest);\n"
                + "    return nmmpProtectionDigestEqual(digest, expected);\n"
                + "}\n");
    }

    private void generateResolver(Writer writer) throws IOException {
        writer.write("static pthread_mutex_t gResolverPublishMutex = PTHREAD_MUTEX_INITIALIZER;\n" +
                "\n" +
                "static void decodeStringPool(void) {\n" +
                "    if (gStringPoolCodecVersion != NMMP_VM_CODEC_VERSION) return;\n" +
                "    vmCodecTransform(gBaseStrPtr,\n" +
                "                     gStringPoolByteSize,\n" +
                "                     gStringPoolDexId,\n" +
                "                     NMMP_VM_DOMAIN_STRING);\n" +
                "    if (vmCodecHash(gBaseStrPtr, gStringPoolByteSize) != gStringPoolHash) return;\n" +
                "    if (!nmmp_validate_resolver()) return;\n" +
                "    gStringPoolReady = true;\n" +
                "}\n" +
                "\n" +
                "static bool resolver_init(JNIEnv *env) {\n" +
                "    int result = pthread_once(&gStringPoolOnce, decodeStringPool);\n" +
                "    if (result != 0 || !gStringPoolReady) {\n" +
                "        (*env)->ThrowNew(env, gVm.exInternalError, \"string pool decode failed\");\n" +
                "        return false;\n" +
                "    }\n" +
                "    return true;\n" +
                "}\n" +
                "\n" +
                "#define NMMP_INDEX(_idx, _array) do { if ((u4)(_idx) >= _array##_COUNT) { if (!(*env)->ExceptionCheck(env)) (*env)->ThrowNew(env, gVm.exInternalError, \"Invalid VM reference index\"); return NULL; } } while (0)\n" +
                "#define STRING_BY_ID(_idx) ((const char *) (gBaseStrPtr + gStringIds[_idx].off))\n" +
                "\n" +
                "#define STRING_BY_TYPE_ID(_idx) (STRING_BY_ID(gTypeIds[_idx].idx))\n" +
                "\n" +
                "#define STRING_BY_CLASS_ID(_idx) (STRING_BY_ID(gClassIds[_idx].idx))\n" +
                "\n" +
                "#define STRING_BY_SIGNATURE_ID(_idx) (STRING_BY_ID(gSignatureIds[_idx].idx))\n" +
                "\n" +
                "#define FIND_CLASS_BY_NAME(_className)                          \\\n" +
                "    clazz = (*env)->FindClass(env, _className);                 \\\n" +
                "    if (clazz == NULL) {                                        \\\n" +
                "        if (!(*env)->ExceptionCheck(env)) {                     \\\n" +
                "            vmThrowNoClassDefFoundError(env, _className);       \\\n" +
                "        }                                                       \\\n" +
                "        return NULL;                                            \\\n" +
                "    }\n" +
                "\n" +
                "\n" +
                "static void vmThrowNoClassDefFoundError(JNIEnv *env, const char *msg) {\n" +
                "    (*env)->ThrowNew(env, gVm.exNoClassDefFoundError, msg);\n" +
                "}\n" +
                "\n" +
                "static void vmThrowNoSuchFieldError(JNIEnv *env, const char *msg) {\n" +
                "    (*env)->ThrowNew(env, gVm.exNoSuchFieldError, msg);\n" +
                "}\n" +
                "\n" +
                "static void vmThrowNoSuchMethodError(JNIEnv *env, const char *msg) {\n" +
                "    (*env)->ThrowNew(env, gVm.exNoSuchMethodError, msg);\n" +
                "}\n" +
                "\n" +
                "static const vmField *dvmResolveField(JNIEnv *env, u4 idx, bool isStatic) {\n" +
                "    NMMP_INDEX(idx, gFieldIds);\n" +
                "    vmField *field = &gFields[idx];\n" +
                "    if (__atomic_load_n(&gFieldReady[idx], __ATOMIC_ACQUIRE)) return field;\n" +
                "\n" +
                "    FieldId fieldId = gFieldIds[idx];\n" +
                "    NMMP_INDEX(fieldId.classIdx, gClassIds);\n" +
                "    NMMP_INDEX(fieldId.typeIdx, gTypeIds);\n" +
                "    NMMP_INDEX(fieldId.nameIdx, gStringIds);\n" +
                "    NMMP_INDEX(gClassIds[fieldId.classIdx].idx, gStringIds);\n" +
                "    NMMP_INDEX(gTypeIds[fieldId.typeIdx].idx, gStringIds);\n" +
                "    jclass clazz;\n" +
                "    FIND_CLASS_BY_NAME(STRING_BY_CLASS_ID(fieldId.classIdx));\n" +
                "\n" +
                "    const char *type = STRING_BY_TYPE_ID(fieldId.typeIdx);\n" +
                "    const char *name = STRING_BY_ID(fieldId.nameIdx);\n" +
                "    vmField candidate = {\n" +
                "            .classIdx = fieldId.classIdx,\n" +
                "            .type = (*type == '[') ? 'L' : *type,\n" +
                "            .fieldId = NULL\n" +
                "    };\n" +
                "    if (isStatic) {\n" +
                "        candidate.fieldId = (*env)->GetStaticFieldID(env, clazz, name, type);\n" +
                "    } else {\n" +
                "        candidate.fieldId = (*env)->GetFieldID(env, clazz, name, type);\n" +
                "    }\n" +
                "    if (candidate.fieldId == NULL) {\n" +
                "        (*env)->DeleteLocalRef(env, clazz);\n" +
                "        if (!(*env)->ExceptionCheck(env)) {\n" +
                "            vmThrowNoSuchFieldError(env, name);\n" +
                "        }\n" +
                "        return NULL;\n" +
                "    }\n" +
                "    (*env)->DeleteLocalRef(env, clazz);\n" +
                "\n" +
                "    pthread_mutex_lock(&gResolverPublishMutex);\n" +
                "    if (!__atomic_load_n(&gFieldReady[idx], __ATOMIC_RELAXED)) {\n" +
                "        *field = candidate;\n" +
                "        __atomic_store_n(&gFieldReady[idx], 1, __ATOMIC_RELEASE);\n" +
                "    }\n" +
                "    pthread_mutex_unlock(&gResolverPublishMutex);\n" +
                "    return field;\n" +
                "}\n" +
                "\n" +
                "static const vmMethod *dvmResolveMethod(JNIEnv *env, u4 idx, bool isStatic) {\n" +
                "    NMMP_INDEX(idx, gMethodIds);\n" +
                "    vmMethod *method = &gMethods[idx];\n" +
                "    if (__atomic_load_n(&gMethodReady[idx], __ATOMIC_ACQUIRE)) return method;\n" +
                "\n" +
                "    MethodId methodId = gMethodIds[idx];\n" +
                "    NMMP_INDEX(methodId.classIdx, gClassIds);\n" +
                "    NMMP_INDEX(methodId.sigIdx, gSignatureIds);\n" +
                "    NMMP_INDEX(methodId.nameIdx, gStringIds);\n" +
                "    NMMP_INDEX(methodId.shortyIdx, gStringIds);\n" +
                "    NMMP_INDEX(gClassIds[methodId.classIdx].idx, gStringIds);\n" +
                "    NMMP_INDEX(gSignatureIds[methodId.sigIdx].idx, gStringIds);\n" +
                "    jclass clazz;\n" +
                "    FIND_CLASS_BY_NAME(STRING_BY_CLASS_ID(methodId.classIdx));\n" +
                "\n" +
                "    const char *name = STRING_BY_ID(methodId.nameIdx);\n" +
                "    const char *sig = STRING_BY_SIGNATURE_ID(methodId.sigIdx);\n" +
                "    vmMethod candidate = {\n" +
                "            .classIdx = methodId.classIdx,\n" +
                "            .shorty = STRING_BY_ID(methodId.shortyIdx),\n" +
                "            .methodId = NULL\n" +
                "    };\n" +
                "    if (isStatic) {\n" +
                "        candidate.methodId = (*env)->GetStaticMethodID(env, clazz, name, sig);\n" +
                "    } else {\n" +
                "        candidate.methodId = (*env)->GetMethodID(env, clazz, name, sig);\n" +
                "    }\n" +
                "    if (candidate.methodId == NULL) {\n" +
                "        (*env)->DeleteLocalRef(env, clazz);\n" +
                "        if (!(*env)->ExceptionCheck(env)) {\n" +
                "            vmThrowNoSuchMethodError(env, name);\n" +
                "        }\n" +
                "        return NULL;\n" +
                "    }\n" +
                "    (*env)->DeleteLocalRef(env, clazz);\n" +
                "\n" +
                "    pthread_mutex_lock(&gResolverPublishMutex);\n" +
                "    if (!__atomic_load_n(&gMethodReady[idx], __ATOMIC_RELAXED)) {\n" +
                "        *method = candidate;\n" +
                "        __atomic_store_n(&gMethodReady[idx], 1, __ATOMIC_RELEASE);\n" +
                "    }\n" +
                "    pthread_mutex_unlock(&gResolverPublishMutex);\n" +
                "    return method;\n" +
                "}\n" +
                "\n" +
                "static jstring dvmConstantString(JNIEnv *env, u4 idx) {\n" +
                "    NMMP_INDEX(idx, gStringConstantIds);\n" +
                "    NMMP_INDEX(gStringConstantIds[idx].idx, gStringIds);\n" +
                "    if (__atomic_load_n(&gStringReady[idx], __ATOMIC_ACQUIRE)) {\n" +
                "        return (jstring) (*env)->NewLocalRef(env, gStringConstants[idx]);\n" +
                "    }\n" +
                "\n" +
                "    jstring local = (*env)->NewStringUTF(env, STRING_BY_ID(gStringConstantIds[idx].idx));\n" +
                "    if (local == NULL) return NULL;\n" +
                "    jstring candidate = (jstring) (*env)->NewGlobalRef(env, local);\n" +
                "    if (candidate == NULL) {\n" +
                "        (*env)->DeleteLocalRef(env, local);\n" +
                "        return NULL;\n" +
                "    }\n" +
                "\n" +
                "    jstring published;\n" +
                "    pthread_mutex_lock(&gResolverPublishMutex);\n" +
                "    if (!__atomic_load_n(&gStringReady[idx], __ATOMIC_RELAXED)) {\n" +
                "        gStringConstants[idx] = candidate;\n" +
                "        candidate = NULL;\n" +
                "        __atomic_store_n(&gStringReady[idx], 1, __ATOMIC_RELEASE);\n" +
                "    }\n" +
                "    published = gStringConstants[idx];\n" +
                "    pthread_mutex_unlock(&gResolverPublishMutex);\n" +
                "\n" +
                "    if (candidate != NULL) (*env)->DeleteGlobalRef(env, candidate);\n" +
                "    (*env)->DeleteLocalRef(env, local);\n" +
                "    return (jstring) (*env)->NewLocalRef(env, published);\n" +
                "}\n" +
                "\n" +
                "\n" +
                "static const char *dvmResolveTypeUtf(JNIEnv *env, u4 idx) {\n" +
                "    NMMP_INDEX(idx, gTypeIds);\n" +
                "    NMMP_INDEX(gTypeIds[idx].idx, gStringIds);\n" +
                "    return STRING_BY_TYPE_ID(idx);\n" +
                "}\n" +
                "\n" +
                "static jclass dvmResolveClass(JNIEnv *env, u4 idx) {\n" +
                "    NMMP_INDEX(idx, gTypeIds);\n" +
                "    NMMP_INDEX(idx, gClassIds);\n" +
                "    NMMP_INDEX(gTypeIds[idx].idx, gStringIds);\n" +
                "    NMMP_INDEX(gClassIds[idx].idx, gStringIds);\n" +
                "    jclass clazz = getCacheClass(env, STRING_BY_TYPE_ID(idx));\n" +
                "    if (clazz != NULL) {\n" +
                "        return (jclass) (*env)->NewLocalRef(env, clazz);\n" +
                "    }\n" +
                "\n" +
                "    FIND_CLASS_BY_NAME(STRING_BY_CLASS_ID(idx));\n" +
                "\n" +
                "    return clazz;\n" +
                "}\n\n");

        //因为类型需要去掉开头的'L'和结尾的';',所以最大最大class名不需要再加1表示字符串结尾
        writer.write(String.format(
                "static jclass dvmFindClass(JNIEnv *env, const char *type) {\n" +
                        "    jclass clazz = getCacheClass(env, type);\n" +
                        "    if (clazz != NULL) {\n" +
                        "        return (jclass) (*env)->NewLocalRef(env, clazz);\n" +
                        "    }\n" +
                        "    if (*type == 'L') {\n" +
                        "        char clazzName[%d];\n" +
                        "        size_t len = strlen(type);\n" +
                        "        strncpy(clazzName, type + 1, len - 2);\n" +
                        "        clazzName[len - 2] = 0;\n" +
                        "\n" +
                        "        FIND_CLASS_BY_NAME(clazzName);\n" +
                        "\n" +
                        "        return clazz;\n" +
                        "    }\n" +
                        "\n" +
                        "    FIND_CLASS_BY_NAME(type);\n" +
                        "\n" +
                        "    return clazz;\n" +
                        "}\n\n", references.getMaxTypeLen()));
        writer.write(
                "static const vmResolver dvmResolver = {\n" +
                        "        .dvmResolveField = dvmResolveField,\n" +
                        "        .dvmResolveMethod = dvmResolveMethod,\n" +
                        "        .dvmResolveTypeUtf = dvmResolveTypeUtf,\n" +
                        "        .dvmResolveClass = dvmResolveClass,\n" +
                        "        .dvmFindClass = dvmFindClass,\n" +
                        "        .dvmConstantString = dvmConstantString,\n" +
                        "};\n" +
                        "\n");
    }

    private void generateMethodPool(Writer writer) throws IOException {
        final References references = this.references;
        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u2 classIdx;\n" +
                        "    u4 nameIdx;\n" +
                        "    u4 shortyIdx;\n" +
                        "    u4 sigIdx;\n" +
                        "} MethodId;\n\n");
        final List<MethodReference> methodPool = references.getMethodPool();
        writer.write(String.format(
                "static const MethodId gMethodIds[%d] = {\n",
                Math.max(1, methodPool.size())));
        for (MethodReference methodReference : methodPool) {
            String definingClass = methodReference.getDefiningClass();
            String className;
            if (definingClass.charAt(0) == 'L') {
                className = definingClass.substring(1, definingClass.length() - 1);
            } else {
                className = definingClass;
            }
            int classNameIdx = references.getClassNameItemIndex(className);
            if (classNameIdx < 0) {
                throw new RuntimeException("unknown class name" + definingClass);
            }
            String name = methodReference.getName();
            int nameIdx = references.getStringItemIndex(name);
            if (nameIdx < 0) {
                throw new RuntimeException("unknown method name");
            }
            int shortyIdx = references.getStringItemIndex(MethodUtil.getShorty(methodReference.getParameterTypes(), methodReference.getReturnType()));
            if (shortyIdx < 0) {
                throw new RuntimeException("unknown method shorty");
            }
            String signature = MyMethodUtil.getMethodSignature(methodReference.getParameterTypes(), methodReference.getReturnType());
            int sigIdx = references.getSignatureItemIndex(signature);
            if (sigIdx < 0) {
                throw new RuntimeException("unknown method signature");
            }

            writer.write(String.format(
                    "    {.classIdx=%d, .nameIdx=%d, .shortyIdx=%d, .sigIdx=%d},\n",
                    classNameIdx, nameIdx, shortyIdx, sigIdx));
        }
        writer.write("};\n");
        writer.write("//ends method data\n\n");
        writer.write(String.format(
                "static vmMethod gMethods[%d];\n",
                Math.max(1, methodPool.size())));
        writer.write(String.format(
                "static u1 gMethodReady[%d];\n",
                Math.max(1, methodPool.size())));
        writer.write("\n");
    }

    private void generateFieldPool(Writer writer) throws IOException {
        final References references = this.references;
        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u2 classIdx;\n" +
                        "    u4 nameIdx;\n" +
                        "    u2 typeIdx;\n" +
                        "} FieldId;\n\n");
        final List<FieldReference> fieldPool = references.getFieldPool();
        writer.write(String.format(
                "static const FieldId gFieldIds[%d] = {\n",
                Math.max(1, fieldPool.size())));
        for (FieldReference reference : fieldPool) {
            String definingClass = reference.getDefiningClass();
            String className;
            if (definingClass.charAt(0) == 'L') {
                className = definingClass.substring(1, definingClass.length() - 1);
            } else {
                className = definingClass;
            }
            int classNameIdx = references.getClassNameItemIndex(className);
            if (classNameIdx < 0) {
                throw new RuntimeException("unknown class name");
            }
            int nameIdx = references.getStringItemIndex(reference.getName());
            if (nameIdx < 0) {
                throw new RuntimeException("unknown field name");
            }
            int typeIdx = references.getTypeItemIndex(reference.getType());
            if (typeIdx < 0) {
                throw new RuntimeException("unknown field type");
            }

            writer.write(String.format(
                    "    {.classIdx=%d, .nameIdx=%d, .typeIdx=%d},\n",
                    classNameIdx, nameIdx, typeIdx));
        }
        writer.write("};\n");
        writer.write("//ends field id\n\n");
        writer.write(String.format(
                "static vmField gFields[%d];\n",
                Math.max(1, fieldPool.size())));
        writer.write(String.format(
                "static u1 gFieldReady[%d];\n",
                Math.max(1, fieldPool.size())));
    }


    private void generateStringPool(Writer writer) throws IOException {
        final ArrayList<Long> strOffsets = new ArrayList<>();
        final ByteArrayOutputStream plainPool = new ByteArrayOutputStream();
        final List<String> stringPool = references.getStringPool();
        for (String string : stringPool) {
            final byte[] bytes = ModifiedUtf8.encode(string);
            strOffsets.add((long) plainPool.size());
            plainPool.write(bytes, 0, bytes.length);
            plainPool.write(0);
        }
        final byte[] plainBytes = plainPool.toByteArray();
        final byte[] encodedBytes = protectionContext.getMethodCodec().transform(
                plainBytes,
                dexId,
                MethodCodec.DOMAIN_STRING);
        manifestEncodedStringPool = encodedBytes.clone();
        manifestStringOffsets = new long[strOffsets.size()];
        for (int i = 0; i < strOffsets.size(); ++i) manifestStringOffsets[i] = strOffsets.get(i);

        writer.write(String.format(
                "static u1 gBaseStrPtr[%d] = {\n",
                Math.max(1, encodedBytes.length)));
        if (encodedBytes.length == 0) {
            writer.write("    0x00,\n");
        } else {
            for (int i = 0; i < encodedBytes.length; i++) {
                if (i % 12 == 0) {
                    writer.write("    ");
                }
                writer.write(String.format("0x%02x,", encodedBytes[i] & 0xff));
                if (i % 12 == 11 || i == encodedBytes.length - 1) {
                    writer.write("\n");
                }
            }
        }
        writer.write("};\n");
        writer.write(String.format(
                "static const u4 gStringPoolByteSize = %d;\n",
                plainBytes.length));
        writer.write(String.format(
                "static const u4 gStringPoolDexId = 0x%08x;\n",
                dexId));
        writer.write(String.format(
                "static const u4 gStringPoolHash = 0x%08x;\n",
                MethodCodec.hash(plainBytes)));
        writer.write(String.format(
                "static const u2 gStringPoolCodecVersion = %d;\n",
                protectionContext.getCodecVersion()));
        writer.write("static pthread_once_t gStringPoolOnce = PTHREAD_ONCE_INIT;\n");
        writer.write("static bool gStringPoolReady;\n\n");

        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u4 off;\n" +
                        "} StringId;\n");

        writer.write(String.format(
                "static const StringId gStringIds[%d] = {\n",
                Math.max(1, strOffsets.size())));
        for (Long offset : strOffsets) {
            if (offset > 0xFFFFFFFFL) {
                throw new RuntimeException("string offset too long");
            }
            writer.write(String.format("    {.off=0x%04x},\n", offset));
        }
        writer.write("};\n");
        writer.write("//ends string ids\n\n");

        writer.flush();
    }

    public int getManifestItemCount() {
        long count = references.getStringPool().size() + references.getTypePool().size()
                + references.getClassNamePool().size() + references.getSignaturePool().size()
                + references.getFieldPool().size() + references.getMethodPool().size()
                + references.getConstantStringPool().size();
        if (count > Integer.MAX_VALUE) throw new IllegalStateException("Resolver item count overflow");
        return (int) count;
    }

    public byte[] getManifestDigest() {
        if (manifestEncodedStringPool == null || manifestStringOffsets == null) {
            throw new IllegalStateException("Resolver manifest requested before generation");
        }
        ProtectionManifest.CanonicalDigest digest = new ProtectionManifest.CanonicalDigest("NMMP-RSL1");
        digest.putU32(dexId).putU32(manifestEncodedStringPool.length).putBytes(manifestEncodedStringPool);
        digest.putU32(manifestStringOffsets.length);
        for (long offset : manifestStringOffsets) digest.putU32(offset);
        digest.putU32(references.getTypePool().size());
        for (String value : references.getTypePool()) digest.putU32(references.getStringItemIndex(value));
        digest.putU32(references.getClassNamePool().size());
        for (String value : references.getClassNamePool()) digest.putU32(references.getStringItemIndex(value));
        digest.putU32(references.getSignaturePool().size());
        for (String value : references.getSignaturePool()) digest.putU32(references.getStringItemIndex(value));
        digest.putU32(references.getFieldPool().size());
        for (FieldReference value : references.getFieldPool()) {
            digest.putU32(references.getClassNameItemIndex(className(value.getDefiningClass())))
                    .putU32(references.getStringItemIndex(value.getName()))
                    .putU32(references.getTypeItemIndex(value.getType()));
        }
        digest.putU32(references.getMethodPool().size());
        for (MethodReference value : references.getMethodPool()) {
            digest.putU32(references.getClassNameItemIndex(className(value.getDefiningClass())))
                    .putU32(references.getStringItemIndex(value.getName()))
                    .putU32(references.getStringItemIndex(
                            MethodUtil.getShorty(value.getParameterTypes(), value.getReturnType())))
                    .putU32(references.getSignatureItemIndex(
                            MyMethodUtil.getMethodSignature(value.getParameterTypes(), value.getReturnType())));
        }
        digest.putU32(references.getConstantStringPool().size());
        for (String value : references.getConstantStringPool()) {
            digest.putU32(references.getStringItemIndex(value));
        }
        return digest.finish();
    }

    private static String className(String definingClass) {
        return definingClass.charAt(0) == 'L'
                ? definingClass.substring(1, definingClass.length() - 1)
                : definingClass;
    }

    static String stringEsc(String str) throws UTFDataFormatException {
        byte[] bytes = ModifiedUtf8.encode(str);
        StringBuilder sb = new StringBuilder(4 * bytes.length);
        for (byte b : bytes) {
            sb.append(String.format("\\x%02x", b & 0xFF));
        }
        return sb.toString();
    }

    private void generateTypePool(Writer writer) throws IOException {

        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u4 idx;\n" +
                        "} TypeId;\n");

        final References references = this.references;
        writer.write(String.format(
                "static const TypeId gTypeIds[%d] = {\n",
                Math.max(1, references.getTypePool().size())));
        for (String type : references.getTypePool()) {
            writer.write(String.format("    {.idx=%d},\n", references.getStringItemIndex(type)));
        }
        writer.write("};\n");
        writer.write("//ends type ids\n\n");
        writer.flush();
    }

    //根据类型池,去掉L开头和;得到class name,其他则不变
    private void generateClassNamePool(Writer writer) throws IOException {
        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u4 idx;\n" +
                        "} ClassId;\n");


        final References references = this.references;
        writer.write(String.format(
                "static const ClassId gClassIds[%d] = {\n",
                Math.max(1, references.getClassNamePool().size())));
        for (String className : references.getClassNamePool()) {
            int classNameIdx = references.getStringItemIndex(className);
            if (classNameIdx < 0) {
                throw new RuntimeException("string not contain");
            }
            writer.write(String.format("    {.idx=%d},\n", classNameIdx));

        }
        writer.write("};\n");
        writer.write("//ends class name ids\n\n");
    }

    private void generateSignaturePool(Writer writer) throws IOException {
        writer.write(
                "typedef struct {\n" +
                        "    u4 idx;\n" +
                        "} SignatureId;\n");

        final References references = this.references;
        writer.write(String.format(
                "static const SignatureId gSignatureIds[%d] = {\n",
                Math.max(1, references.getSignaturePool().size())));
        for (String sig : references.getSignaturePool()) {
            int sigIdx = references.getStringItemIndex(sig);
            if (sigIdx < 0) {
                throw new RuntimeException("string not contain");
            }
            writer.write(String.format("    {.idx=%d},\n", sigIdx));
        }
        writer.write("};\n");
        writer.write("//ends method signature pool\n\n");
    }
}

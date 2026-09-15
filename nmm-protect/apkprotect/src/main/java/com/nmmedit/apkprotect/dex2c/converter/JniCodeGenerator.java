package com.nmmedit.apkprotect.dex2c.converter;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedClassDef;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedMethod;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.MethodImplementation;
import com.android.tools.smali.dexlib2.util.MethodUtil;
import com.google.common.collect.HashMultimap;
import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.DemandCodec;
import com.nmmedit.apkprotect.dex2c.DemandModule;
import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.dex2c.converter.structs.RegisterNativesUtilClassDef;

import javax.annotation.Nonnull;
import java.io.IOException;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.*;


/**
 * 根据dex把所有方法的字节码和异常表提取出来生成jni函数
 */

public class JniCodeGenerator {
    // 不能再设为false,不然无法正确加载本地库,而且按标准的jni函数名可能会有命名冲突问题
    // 所以只保留注册本地函数这种方式
    // todo 再改改jni函数名生成方式应该可以把bridge这类方法也native化
    private final boolean isRegisterNative = true;

    private final HashMultimap<String, MyMethod> handledNativeMethods = HashMultimap.create();

    private final Map<String, Integer> nativeMethodOffsets = new HashMap<>();
    private final ResolverCodeGenerator resolverCodeGenerator;
    private final InstructionRewriter instructionRewriter;
    private final DexBackedDexFile dexFile;
    private final ProtectionContext protectionContext;
    private final DemandModule demandModule;

    public JniCodeGenerator(@Nonnull DexBackedDexFile dexFile,
                            @Nonnull ClassAnalyzer analyzer,
                            @Nonnull InstructionRewriter instructionRewriter,
                            @Nonnull ProtectionContext protectionContext,
                            long dexId) {
        this.dexFile = dexFile;
        this.protectionContext = protectionContext;
        this.demandModule = new DemandModule(protectionContext.getBuildSeed(), dexId, protectionContext.getBuildId());

//      根据dex里字符串常量,类型常量等生成符号解析代码,给vm提供符号信息
        resolverCodeGenerator = new ResolverCodeGenerator(
                dexFile,
                analyzer,
                protectionContext,
                dexId);

        this.instructionRewriter = instructionRewriter;

        instructionRewriter.loadReferences(resolverCodeGenerator.getReferences(), analyzer);

    }

    public void addMethod(Method method, Writer writer) throws IOException {
        final MethodImplementation implementation = method.getImplementation();
        if (implementation == null) {
            return;
        }
        final long methodId = protectionContext.nextMethodId();

        final boolean isStatic = AccessFlags.STATIC.isSet(method.getAccessFlags());

        final String classType = method.getDefiningClass();

        final String methodName = method.getName();
        final int registerCount = implementation.getRegisterCount();
        final List<? extends CharSequence> parameterTypes = method.getParameterTypes();
        final int parameterRegisterCount = MethodUtil.getParameterRegisterCount(parameterTypes, isStatic);
        final String returnType = method.getReturnType();


        String clazzName = classType.substring(1, classType.length() - 1);

        final boolean sensitive = protectionContext.bindSensitiveMethod(method);
        handledNativeMethods.put(clazzName, new MyMethod(clazzName, methodName, parameterTypes, returnType, sensitive, isStatic));

        final byte[] plain = instructionRewriter.rewriteInstructions(method);
        final byte[] tries = instructionRewriter.handleTries(implementation);
        final DemandCodec.Encoded encoded = DemandCodec.encode(method, plain, tries,
                protectionContext.getBuildSeed(), methodId, parameterRegisterCount);
        final long demandToken = demandModule.add(methodId, registerCount, parameterRegisterCount, encoded);


        writer.write(String.format("%s %s %s(JNIEnv *env, %s ",
                isRegisterNative ? "static" : "JNIEXPORT",
                getJNIType(returnType),
                MyMethodUtil.getJniFunctionName(clazzName, methodName, parameterTypes, returnType),
                isStatic ? "jclass jcls" : "jobject thiz")
        );


//        --------jni函数定义及参数赋值-------

        //如果寄存器数量比较小直接使用栈上内存,不自己分配和释放
        boolean useStack = registerCount <= 8;

        //寄存器初始化
        StringBuilder regsAssign;
        StringBuilder regFlagsAssign;
        if (useStack) {
            regsAssign = new StringBuilder(String.format(
                    "    regptr_t regs[%d];\n", registerCount));
            //直接赋值数组元素值为0,初始化寄存器及其状态,不调用memset
            //好处是和后面赋值参数及参数类型时,编译器可以优化无用赋值

            for (int i = 0; i < registerCount; i++) {
                regsAssign.append(String.format("    regs[%d] = 0;\n", i));
            }
            regFlagsAssign = new StringBuilder(String.format(
                    "    u1 reg_flags[%d];\n", registerCount));

            for (int i = 0; i < registerCount; i++) {
                regFlagsAssign.append(String.format("    reg_flags[%d] = 0;\n", i));
            }
        } else {
            //一次同时分配寄存器及它的状态所需内存
            regsAssign = new StringBuilder(String.format(
                    "    regptr_t *regs = (regptr_t *) calloc(%d, sizeof(regptr_t) + sizeof(u1));\n",
                    registerCount));

            regsAssign.append("    if (regs == NULL) {\n"
                    + "        if (!(*env)->ExceptionCheck(env)) {\n"
                    + "            jclass oom = (*env)->FindClass(env, \"java/lang/OutOfMemoryError\");\n"
                    + "            if (oom != NULL) { (*env)->ThrowNew(env, oom, \"VM register allocation\"); (*env)->DeleteLocalRef(env, oom); }\n"
                    + "        }\n"
                    + (returnType.equals("V") ? "        return;\n" : "        return 0;\n")
                    + "    }\n");

            //寄存器后面部分是寄存器状态数组,和寄存器数量一一对应
            regFlagsAssign = new StringBuilder(
                    String.format("    u1 *reg_flags = ((u1 *) regs) + (%d * sizeof(regptr_t));\n", registerCount));
        }
        int paramRegStart = registerCount - parameterRegisterCount;
        if (!isStatic) {
            regsAssign.append(String.format("    regs[%d] = (regptr_t) thiz;\n", paramRegStart));
            //对象寄存器需要标识出来
            regFlagsAssign.append(String.format("    reg_flags[%d] = 1;\n", paramRegStart));

            paramRegStart++;
        }
        StringBuilder params = new StringBuilder();
        for (int i = 0, size = parameterTypes.size(); i < size; i++) {
            String type = parameterTypes.get(i).toString();
            String jniType = getJNIType(type);
            final int argNum = isStatic ? i : i + 1;
            params.append(jniType)
                    .append(" p")
                    .append(argNum);
            if (type.startsWith("[") || type.startsWith("L")) {//对象类型
                regsAssign.append(String.format("    regs[%d] = (regptr_t) p%d;\n", paramRegStart, argNum));

                regFlagsAssign.append(String.format("    reg_flags[%d] = 1;\n", paramRegStart));

                paramRegStart++;
            } else if (type.equals("F")) {
                regsAssign.append(String.format("    SET_REGISTER_FLOAT(%d, p%d);\n", paramRegStart++, argNum));
            } else if (type.equals("D")) {
                regsAssign.append(String.format("    SET_REGISTER_DOUBLE(%d, p%d);\n", paramRegStart++, argNum));
                paramRegStart++;
            } else if (type.equals("J")) {
                regsAssign.append(String.format("    SET_REGISTER_WIDE(%d, p%d);\n", paramRegStart++, argNum));
                paramRegStart++;
            } else {
                regsAssign.append(String.format("    regs[%d] = p%d;\n", paramRegStart++, argNum));
            }
            if (i < size - 1) {//最后不用加,
                params.append(", ");
            }
        }
        if (params.length() > 0) {
            writer.append(", ").append(params.toString());
        }
        writer.append(") {\n");
        writer.write("    if (!nmmp_require_ready(env)) return" + (returnType.equals("V") ? ";\n" : " 0;\n"));
        if (sensitive) {
            writer.write("    if (!nmmpProtectionVerifySensitiveCall(env)) {\n"
                    + "        if (!(*env)->ExceptionCheck(env)) {\n"
                    + "            jclass error = (*env)->FindClass(env, \"java/lang/InternalError\");\n"
                    + "            if (error) { (*env)->ThrowNew(env, error, \"Sensitive operation unavailable\"); (*env)->DeleteLocalRef(env, error); }\n"
                    + "        }\n"
                    + (returnType.equals("V") ? "        return;\n" : "        return 0;\n")
                    + "    }\n");
        }

        writer.append(regsAssign);
        writer.append("\n");

        writer.append(regFlagsAssign);
        writer.append("\n");
//        -----------结束----------------

        final boolean hasReturnValue = !returnType.equals("V");
        writer.write((hasReturnValue ? "    volatile jvalue value = " : "    ")
                + String.format(Locale.ROOT,
                        "vmExecuteToken(env, &nmmpModule, UINT32_C(0x%08x), regs, reg_flags, %d, &dvmResolver);\n",
                        demandToken, registerCount));


        //不使用栈需要释放内存
        if (!useStack) {
            writer.write("    free(regs);\n");
        }

        //根据返回类型处理jvalue
        if (hasReturnValue) {
            char typeCh = returnType.charAt(0);
            writer.append(
                    String.format("    return value.%s;\n", Character.toLowerCase(typeCh == '[' ? 'L' : typeCh))
            );
        }
        writer.append("}\n\n");
    }

    private static void writeByteArray(Writer writer,
                                       String name,
                                       byte[] data) throws IOException {
        writer.write(String.format("    static const u1 %s[] = {", name));
        for (int i = 0; i < data.length; i++) {
            if (i % 12 == 0) {
                writer.write("\n");
            }
            writer.write(String.format("0x%02x, ", data[i] & 0xff));
        }
        writer.write("\n    };\n");
    }

    public Set<String> getHandledNativeClasses() {
        return handledNativeMethods.keySet();
    }

    //必须在产生代码后调用才有效果
    public Map<String, Integer> getNativeMethodOffsets() {
        return nativeMethodOffsets;
    }

    public void generate(DexConfig config, Writer resolverWriter, Writer codeWriter) throws IOException {
        resolverCodeGenerator.generate(resolverWriter);

        codeWriter.write(String.format("\n" +
                        "#include <stdio.h>\n" +
                        "#include <string.h>\n" +
                        "#include <malloc.h>\n" +
                        "#include <jni.h>\n" +
                        "#include <stdbool.h>\n" +
                        "#include \"vm.h\"\n" +
                        "#include \"ProtectionManifest.h\"\n" +
                        "#include \"ProtectionPolicy.h\"\n" +
                        "#include \"ArtMethodChecks.h\"\n" +
                        "#include \"Sha256.h\"\n" +
                        "#include \"%s\"\n" +
                        "\n" +
                        "#ifdef __cplusplus\n" +
                        "extern \"C\" {\n" +
                        "#endif\n" +
                        "\n" +
                        "\n" +
                        "#define SET_REGISTER_FLOAT(_idx, _val)      (*((float*) &regs[(_idx)]) = (_val))\n" +
                        "\n" +
                        "\n" +
                        "#define SET_REGISTER_WIDE(_idx, _val)       (regs[(_idx)] =(s8) (_val));\n" +
                        "\n" +
                        "#define SET_REGISTER_DOUBLE(_idx, _val)     (*((double*) &regs[(_idx)]) = (_val));\n" +
                        "\n" +
                        "\n"
                , config.getResolverFile().getName()));

        codeWriter.write(
                "extern bool nmmp_vm_require_ready(void);\n"
                        + "extern void nmmp_vm_fail(void);\n"
                        + "static bool nmmp_require_ready(JNIEnv *env) {\n"
                        + "    if (nmmp_vm_require_ready() && nmmpProtectionAllowCall(env)) return true;\n"
                        + "    nmmp_vm_fail();\n"
                        + "    if (!(*env)->ExceptionCheck(env)) {\n"
                        + "        jclass error = (*env)->FindClass(env, \"java/lang/InternalError\");\n"
                        + "        if (error) { (*env)->ThrowNew(env, error, \"Protected execution unavailable\"); (*env)->DeleteLocalRef(env, error); }\n"
                        + "    }\n    return false;\n}\n");
        codeWriter.write("static vmDemandModule nmmpModule;\n\n");
        for (DexBackedClassDef classDef : dexFile.getClasses()) {
            for (DexBackedMethod method : classDef.getMethods()) {
                addMethod(method, codeWriter);
            }
        }

        DemandModule.Built module = demandModule.build();
        writeByteArray(codeWriter, "nmmpModuleBlob", module.blob);
        codeWriter.write(String.format(Locale.ROOT,
                "static vmDemandModule nmmpModule = NMMP_DEMAND_MODULE_INIT(nmmpModuleBlob, %d, "
                        + "UINT32_C(0x%08x), UINT32_C(0x%08x), UINT64_C(0x%016x));\n",
                module.blob.length, module.hash, module.moduleId, module.buildId));
        codeWriter.write("static bool nmmp_prepare_demand(JNIEnv *env) {\n");
        codeWriter.write("    return vmPrepareDemandModule(env, &nmmpModule);\n}\n");
        generateNativeMethodCode(config, codeWriter, module);
        protectionContext.addManifestEntry(new ProtectionManifest.Entry(
                module.moduleId,
                module.blob.length,
                handledNativeMethods.size(),
                resolverCodeGenerator.getManifestItemCount(),
                manifestRegisterCount,
                ProtectionManifest.sha256(module.blob),
                resolverCodeGenerator.getManifestDigest(),
                manifestRegisterDigest));

        codeWriter.write(String.format("void %s(JNIEnv *env) {\n", config.getHeaderFileAndSetupFunc().setupFunctionName));
        codeWriter.write("    if ((*env)->ExceptionCheck(env)) { nmmp_vm_fail(); return; }\n");

        codeWriter.write("\n    //符号解析器初始化\n");
        if (!protectionContext.isSignatureBound()) {
            codeWriter.write("    if (!nmmp_verify_protected_data()) { nmmpProtectionMarkIntegrityFailure(); return; }\n");
            codeWriter.write("    if (!resolver_init(env)) return;\n\n");
            codeWriter.write("    if (!nmmp_prepare_demand(env)) return;\n");
        }

        if (isRegisterNative) {
            codeWriter.write("    //注册\n");

            final String funName = MyMethodUtil.getJniFunctionName(config.getRegisterNativesClassName(),
                    config.getRegisterNativesMethodName(), Collections.singletonList("I"), "V");
            codeWriter.write(String.format(
                    "    jclass clazz = (*env)->FindClass(env, \"%s\");\n" +
                            "    if (clazz == NULL) { nmmp_vm_fail(); nmmp_require_ready(env); return; }\n" +
                            "    static const JNINativeMethod nativeMethod = {\n" +
                            "        .name=\"%s\",\n" +
                            "        .signature=\"(I)V\",\n" +
                            "        .fnPtr=%s\n" +
                            "    };\n" +
                            "   const jint result = (*env)->RegisterNatives(env, clazz, &nativeMethod, 1);\n" +
                            (handledNativeMethods.values().stream().anyMatch(value -> value.sensitive)
                                    ? "   if (result == JNI_OK && !(*env)->ExceptionCheck(env)) nmmpArtCalibrate(env, clazz, &nativeMethod);\n" : "") +
                            "\n" +
                            "   (*env)->DeleteLocalRef(env, clazz);\n" +
                            "   if (result != JNI_OK || (*env)->ExceptionCheck(env)) { nmmp_vm_fail(); nmmp_require_ready(env); }\n" +
                            "\n"
                    , config.getRegisterNativesClassName(),
                    config.getRegisterNativesMethodName(), funName));
        }
        codeWriter.write("}\n");

        codeWriter.write(
                "\n\n#ifdef __cplusplus\n" +
                        "}\n" +
                        "#endif\n\n");
    }

    //生成本地方法注册代码,同时返回类名和方法数组索引等
    private byte[] manifestRegisterDigest;
    private int manifestRegisterCount;

    private void generateNativeMethodCode(DexConfig config,
                                          Writer writer,
                                          DemandModule.Built module) throws IOException {
        if (!isRegisterNative) {
            return;
        }
        //记录类名之下所有方法起始位置及数量用于生成注册代码
        HashMap<String, Ranger> methodRanger = new HashMap<>();

        writer.write("\n" +
                "typedef struct{\n" +
                "    u4 nameIdx;\n" +
                "    u4 sigIdx;\n" +
                "    u4 entryId;\n" +
                "    void *fnPtr;\n" +
                "} MyNativeMethod;\n");

        int methodIdx = 0;
        StringBuilder sensitiveCases = new StringBuilder();
        final ProtectionManifest.CanonicalDigest registerDigest =
                new ProtectionManifest.CanonicalDigest("NMMP-REG1");
        registerDigest.putU32(module.moduleId).putU32(handledNativeMethods.size());
        writer.write("static const MyNativeMethod gNativeMethods[] = {\n");
        final References references = resolverCodeGenerator.getReferences();
        final List<String> classes = new ArrayList<>(handledNativeMethods.keySet());
        Collections.sort(classes);
        for (String clazz : classes) {

            int startIdx = methodIdx;
            List<MyMethod> methods = new ArrayList<>(handledNativeMethods.get(clazz));
            methods.sort(Comparator.comparing(JniCodeGenerator::methodIdentity));
            for (MyMethod method : methods) {
                int nameIdx = references.getStringItemIndex(method.name);
                int sigIdx = references.getStringItemIndex(MyMethodUtil.getMethodSignature(method.parameterTypes, method.returnType));
                int entryId = methodEntryId(method);
                writer.write(String.format(
                        "    {%d, %d, UINT32_C(0x%08x), (void *) %s},\n",
                        nameIdx, sigIdx, entryId,
                        MyMethodUtil.getJniFunctionName(method.className, method.name, method.parameterTypes, method.returnType)
                ));
                registerDigest.putU32(nameIdx).putU32(sigIdx).putU32(Integer.toUnsignedLong(entryId));
                if (method.sensitive) {
                    sensitiveCases.append("            case ").append(methodIdx)
                            .append(": nmmpArtRegister(env, clazz, methods + i, ")
                            .append(method.isStatic ? "true" : "false").append("); break;\n");
                }
                methodIdx++;
            }
            methodRanger.put(clazz, new Ranger(startIdx, methodIdx - startIdx));
        }

        writer.write("};\n");
        writer.write("//ends native method\n");
        writer.write("static bool nmmp_register_sensitive(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, u4 offset, u4 count) {\n"
                + "    for (u4 i = 0; i < count; ++i) {\n"
                + "        switch (offset + i) {\n" + sensitiveCases
                + "            default: continue;\n        }\n"
                + "        if ((*env)->ExceptionCheck(env)) return false;\n"
                + "    }\n    return true;\n}\n");

        //根据索引生成注册需要的结构体
        writer.write(
                "\n" +
                        "typedef struct {\n" +
                        "    u4 classIdx;\n" +
                        "    u4 offset;\n" +
                        "    u4 count;\n" +
                        "} NativeMethodData;\n");

        writer.write("static const NativeMethodData gNativeRegisterData[] = {\n");
        int dataOff = 0;
        registerDigest.putU32(classes.size());
        for (String className : classes) {
            Ranger ranger = methodRanger.get(className);
            int classIdx = references.getClassNameItemIndex(className);
            writer.write(String.format("    {.classIdx = %d, .offset = %d, .count = %d},\n", classIdx, ranger.start, ranger.count));
            registerDigest.putU32(classIdx).putU32(ranger.start).putU32(ranger.count);
            nativeMethodOffsets.put(
                    className,
                    dataOff++ + (protectionContext.isSignatureBound() ? 1 : 0));
        }
        writer.write("};\n\n");
        manifestRegisterCount = dataOff;
        manifestRegisterDigest = registerDigest.finish();
        writer.write(String.format(Locale.ROOT,
                "#define NMMP_PROTECTED_METHOD_COUNT %du\n#define NMMP_REGISTER_DATA_COUNT %du\n",
                methodIdx, dataOff));
        writer.write(String.format(Locale.ROOT,
                "static bool nmmp_verify_registration_manifest(const uint8_t expected[32]) {\n"
                        + "    NmmpSha256Context hash; uint8_t digest[32];\n"
                        + "    nmmpSha256Init(&hash); nmmpSha256Update(&hash, (const uint8_t *)\"NMMP-REG1\", 9);\n"
                        + "    nmmpSha256UpdateU32(&hash, UINT32_C(0x%08x));\n"
                        + "    nmmpSha256UpdateU32(&hash, NMMP_PROTECTED_METHOD_COUNT);\n"
                        + "    for (u4 i=0; i<NMMP_PROTECTED_METHOD_COUNT; ++i) {\n"
                        + "        MyNativeMethod v=gNativeMethods[i];\n"
                        + "        if (!v.fnPtr || !nmmpProtectionPointerInImage(v.fnPtr)) return false;\n"
                        + "        nmmpSha256UpdateU32(&hash,v.nameIdx); nmmpSha256UpdateU32(&hash,v.sigIdx); nmmpSha256UpdateU32(&hash,v.entryId);\n"
                        + "    }\n"
                        + "    nmmpSha256UpdateU32(&hash, NMMP_REGISTER_DATA_COUNT);\n"
                        + "    for (u4 i=0; i<NMMP_REGISTER_DATA_COUNT; ++i) { NativeMethodData v=gNativeRegisterData[i];\n"
                        + "        if (v.offset>NMMP_PROTECTED_METHOD_COUNT || v.count>NMMP_PROTECTED_METHOD_COUNT-v.offset) return false;\n"
                        + "        nmmpSha256UpdateU32(&hash,v.classIdx); nmmpSha256UpdateU32(&hash,v.offset); nmmpSha256UpdateU32(&hash,v.count);\n"
                        + "    }\n"
                        + "    nmmpSha256Final(&hash,digest); return nmmpProtectionDigestEqual(digest,expected);\n"
                        + "}\n"
                        + "static bool nmmp_verify_protected_data(void) {\n"
                        + "    NmmpManifestEntry entry; uint8_t digest[32];\n"
                        + "    if (!nmmpProtectionGetEntry(UINT32_C(0x%08x), &entry)"
                        + " || entry.moduleSize != sizeof(nmmpModuleBlob)"
                        + " || entry.methodCount != NMMP_PROTECTED_METHOD_COUNT"
                        + " || entry.resolverItemCount != %du"
                        + " || entry.registerCount != NMMP_REGISTER_DATA_COUNT) return false;\n"
                        + "    nmmpSha256(nmmpModuleBlob, sizeof(nmmpModuleBlob), digest);\n"
                        + "    if (!nmmpProtectionDigestEqual(digest,entry.moduleDigest)"
                        + " || !nmmp_verify_resolver_manifest(entry.resolverDigest)"
                        + " || !nmmp_verify_registration_manifest(entry.registerDigest)) return false;\n"
                        + "    return true;\n"
                        + "}\n\n",
                module.moduleId, module.moduleId, resolverCodeGenerator.getManifestItemCount()));

        //当前dex下所有处理过的class对应的本地方法注册
        final String funName = MyMethodUtil.getJniFunctionName(config.getRegisterNativesClassName(),
                config.getRegisterNativesMethodName(), Collections.singletonList("I"), "V");
        if (protectionContext.isSignatureBound()) {
            generateBoundRegisterCode(config, writer, funName, dataOff);
            return;
        }
        writer.write(String.format(
                "static void %s(JNIEnv *env, jclass jcls, jint dataIdx){\n" +
                        "    if ((*env)->ExceptionCheck(env)) { nmmp_vm_fail(); return; }\n" +
                        "    if ((u4) dataIdx >= NMMP_REGISTER_DATA_COUNT) { nmmp_vm_fail(); nmmp_require_ready(env); return; }\n" +
                        "#define MAX_METHOD 8\n" +
                        "    JNINativeMethod methodBuf[MAX_METHOD];\n" +
                        "\n" +
                        "    JNINativeMethod *methods;\n" +
                        "    const NativeMethodData data = gNativeRegisterData[(u4) dataIdx];\n" +
                        "    if (data.count > MAX_METHOD) {\n" +
                        "        methods = (JNINativeMethod *) malloc(sizeof(JNINativeMethod) * data.count);\n" +
                        "        if (methods == NULL) { nmmp_vm_fail(); nmmp_require_ready(env); return; }\n" +
                        "    } else {\n" +
                        "        //方法数比较小直接使用栈内存,减少内存分配和释放\n" +
                        "        methods = methodBuf;\n" +
                        "    }\n" +
                        "\n" +
                        "    jclass clazz = (*env)->FindClass(env, STRING_BY_CLASS_ID(data.classIdx));\n" +
                        "    if (clazz == NULL) {\n" +
                        "        if (methods != methodBuf) free(methods);\n" +
                        "        nmmp_vm_fail(); nmmp_require_ready(env);\n" +
                        "        return;\n" +
                        "    }\n" +
                        "    for (int midx = 0; midx < data.count; ++midx) {\n" +
                        "        MyNativeMethod myNativeMethod = gNativeMethods[data.offset + midx];\n" +
                        "\n" +
                        "        JNINativeMethod *method = methods + midx;\n" +
                        "        method->name = STRING_BY_ID(myNativeMethod.nameIdx);\n" +
                        "        method->signature = STRING_BY_ID(myNativeMethod.sigIdx);\n" +
                        "        method->fnPtr = myNativeMethod.fnPtr;\n" +
                        "    }\n" +
                        "\n" +
                        "    const jint result = (*env)->RegisterNatives(env, clazz, methods, data.count);\n" +
                        "    const bool artValid = result == JNI_OK && !(*env)->ExceptionCheck(env)\n" +
                        "            && nmmp_register_sensitive(env, clazz, methods, data.offset, data.count);\n" +
                        "\n" +
                        "    (*env)->DeleteLocalRef(env, clazz);\n" +
                        "\n" +
                        "    //不相等表示使用malloc申请的内存需要释放\n" +
                        "    if (methods != methodBuf)free(methods);\n" +
                        "    if (!artValid || (*env)->ExceptionCheck(env)) { nmmp_vm_fail(); nmmp_require_ready(env); }\n" +
                        "}\n\n"
                , funName)
        );
    }

    private void generateBoundRegisterCode(DexConfig config,
                                           Writer writer,
                                           String funName,
                                           int registerCount) throws IOException {
        final String activateFunction =
                config.getHeaderFileAndSetupFunc().setupFunctionName + "_activate";
        writer.write(String.format(
                "#define NMMP_REGISTER_COUNT %d\n"
                        + "static u1 gNmmpPending[NMMP_REGISTER_COUNT > 0 ? NMMP_REGISTER_COUNT : 1];\n"
                        + "static pthread_mutex_t gNmmpPendingLock = PTHREAD_MUTEX_INITIALIZER;\n"
                        + "extern bool nmmp_vm_is_ready(void);\n"
                        + "extern bool nmmp_vm_activate(JNIEnv *env, jobject context);\n"
                        + "extern void nmmp_vm_fail(void);\nextern bool nmmp_vm_failed(void);\n\n",
                registerCount));
        writer.write(
                "static bool nmmp_register_class(JNIEnv *env, u4 dataIdx) {\n"
                        + "#define MAX_METHOD 8\n"
                        + "    JNINativeMethod methodBuf[MAX_METHOD];\n"
                        + "    JNINativeMethod *methods;\n"
                        + "    const NativeMethodData data = gNativeRegisterData[dataIdx];\n");
        writer.write(
                "    if (data.count > MAX_METHOD) {\n"
                        + "        methods = (JNINativeMethod *) malloc(sizeof(JNINativeMethod) * data.count);\n"
                        + "        if (methods == NULL) return false;\n"
                        + "    } else {\n"
                        + "        methods = methodBuf;\n"
                        + "    }\n"
                        + "    jclass clazz = (*env)->FindClass(env, STRING_BY_CLASS_ID(data.classIdx));\n"
                        + "    if (clazz == NULL) {\n"
                        + "        if (methods != methodBuf) free(methods);\n"
                        + "        return false;\n"
                        + "    }\n"
                        + "    for (int midx = 0; midx < data.count; ++midx) {\n"
                        + "        MyNativeMethod value = gNativeMethods[data.offset + midx];\n"
                        + "        methods[midx].name = STRING_BY_ID(value.nameIdx);\n"
                        + "        methods[midx].signature = STRING_BY_ID(value.sigIdx);\n"
                        + "        methods[midx].fnPtr = value.fnPtr;\n"
                        + "    }\n");
        writer.write(
                "    const jint result = (*env)->RegisterNatives(env, clazz, methods, data.count);\n"
                        + "    const bool artValid = result == JNI_OK && !(*env)->ExceptionCheck(env)\n"
                        + "            && nmmp_register_sensitive(env, clazz, methods, data.offset, data.count);\n"
                        + "    (*env)->DeleteLocalRef(env, clazz);\n"
                        + "    if (methods != methodBuf) free(methods);\n"
                        + "    return artValid && !(*env)->ExceptionCheck(env);\n"
                        + "}\n\n");
        writer.write(String.format(
                "static void %s(JNIEnv *env, jclass jcls, jint dataIdx) {\n"
                        + "    if ((*env)->ExceptionCheck(env)) { nmmp_vm_fail(); return; }\n"
                        + "    if (dataIdx == 0) {\n"
                        + "        jfieldID field = (*env)->GetStaticFieldID(env, jcls, \"%s\", \"%s\");\n"
                        + "        if (field == NULL) {\n"
                        + "            nmmp_vm_fail(); nmmp_require_ready(env);\n"
                        + "            return;\n"
                        + "        }\n"
                        + "        jobject context = (*env)->GetStaticObjectField(env, jcls, field);\n"
                        + "        if ((*env)->ExceptionCheck(env)) {\n"
                        + "            if (context != NULL) (*env)->DeleteLocalRef(env, context);\n"
                        + "            nmmp_vm_fail(); return;\n"
                        + "        }\n"
                        + "        if (context == NULL) return;\n"
                        + "        if (!nmmp_vm_activate(env, context)) { nmmp_vm_fail(); nmmp_require_ready(env); }\n"
                        + "        (*env)->DeleteLocalRef(env, context);\n"
                        + "        return;\n"
                        + "    }\n",
                funName,
                RegisterNativesUtilClassDef.CONTEXT_FIELD_NAME,
                RegisterNativesUtilClassDef.CONTEXT_TYPE));
        writer.write(
                "    if ((u4) dataIdx > NMMP_REGISTER_COUNT) return;\n"
                        + "    const u4 registerIdx = (u4) dataIdx - 1;\n"
                        + "    pthread_mutex_lock(&gNmmpPendingLock);\n"
                        + "    bool ready = nmmp_vm_is_ready();\n"
                        + "    if (!ready) gNmmpPending[registerIdx] = 1;\n"
                        + "    pthread_mutex_unlock(&gNmmpPendingLock);\n"
                        + "    if ((ready && !nmmp_register_class(env, registerIdx)) || nmmp_vm_failed()) {\n"
                        + "        nmmp_vm_fail(); nmmp_require_ready(env);\n"
                        + "    }\n"
                        + "}\n\n");
        writer.write("bool " + config.getHeaderFileAndSetupFunc().setupFunctionName + "_finish(JNIEnv *env) {\n"
                + "    for (u4 i = 0; i < NMMP_REGISTER_COUNT; ++i) {\n"
                + "        pthread_mutex_lock(&gNmmpPendingLock);\n"
                + "        bool pending = gNmmpPending[i] != 0;\n"
                + "        gNmmpPending[i] = 0;\n"
                + "        pthread_mutex_unlock(&gNmmpPendingLock);\n"
                + "        if (pending && !nmmp_register_class(env, i)) return false;\n"
                + "    }\n    return true;\n}\n\n");
        writer.write(String.format(
                "bool %s(JNIEnv *env) {\n"
                        + "    if (!nmmp_verify_protected_data()) { nmmpProtectionMarkIntegrityFailure(); return false; }\n"
                        + "    if (!resolver_init(env)) return false;\n"
                        + "    if (!nmmp_prepare_demand(env)) return false;\n"
                        + "    return true;\n"
                        + "}\n\n",
                activateFunction));
    }

    private static String methodIdentity(MyMethod method) {
        return method.className + "->" + method.name
                + MyMethodUtil.getMethodSignature(method.parameterTypes, method.returnType);
    }

    private static int methodEntryId(MyMethod method) {
        byte[] digest = ProtectionManifest.sha256(methodIdentity(method).getBytes(StandardCharsets.UTF_8));
        return (digest[0] & 0xff) | (digest[1] & 0xff) << 8
                | (digest[2] & 0xff) << 16 | (digest[3] & 0xff) << 24;
    }

    public static String getJNIType(String type) {
        switch (type) {
            case "Z":
                return "jboolean";
            case "B":
                return "jbyte";
            case "S":
                return "jshort";
            case "C":
                return "jchar";
            case "I":
                return "jint";
            case "F":
                return "jfloat";
            case "J":
                return "jlong";
            case "D":
                return "jdouble";
//            case "Ljava/lang/String;":
//                return "jstring";
            case "V":
                return "void";
            default:
                return "jobject";
        }
    }

    private static class MyMethod {
        final String className;
        final String name;
        final List<? extends CharSequence> parameterTypes;

        final String returnType;
        final boolean sensitive;
        final boolean isStatic;

        MyMethod(String className, String name, List<? extends CharSequence> parameterTypes, String returnType,
                 boolean sensitive, boolean isStatic) {
            this.className = className;
            this.name = name;
            this.parameterTypes = parameterTypes;
            this.returnType = returnType;
            this.sensitive = sensitive;
            this.isStatic = isStatic;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;

            MyMethod myMethod = (MyMethod) o;

            if (!className.equals(myMethod.className)) return false;
            if (!name.equals(myMethod.name)) return false;
            if (!parameterTypes.equals(myMethod.parameterTypes)) return false;
            return returnType.equals(myMethod.returnType);
        }

        @Override
        public int hashCode() {
            int result = className.hashCode();
            result = 31 * result + name.hashCode();
            result = 31 * result + parameterTypes.hashCode();
            result = 31 * result + returnType.hashCode();
            return result;
        }
    }

    private static class Ranger {
        final int start;
        final int count;

        Ranger(int start, int count) {
            this.start = start;
            this.count = count;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;

            Ranger ranger = (Ranger) o;

            if (start != ranger.start) return false;
            return count == ranger.count;
        }

        @Override
        public int hashCode() {
            int result = start;
            result = 31 * result + count;
            return result;
        }
    }
}

package com.nmmedit.apkprotect.dex2c;

import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.MethodImplementation;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.dexlib2.iface.instruction.ReferenceInstruction;
import com.android.tools.smali.dexlib2.iface.reference.MethodReference;
import com.android.tools.smali.dexlib2.util.MethodUtil;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import com.google.common.collect.HashMultimap;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.JniCodeGenerator;
import com.nmmedit.apkprotect.dex2c.converter.MyMethodUtil;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.dex2c.converter.structs.MethodConverter;
import com.nmmedit.apkprotect.dex2c.converter.structs.MyClassDef;
import com.nmmedit.apkprotect.dex2c.converter.structs.RegisterNativesCallerClassDef;
import com.nmmedit.apkprotect.dex2c.filters.ClassAndMethodFilter;
import com.nmmedit.apkprotect.util.Pair;

import javax.annotation.Nonnull;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

public class Dex2c {

    public static final String LANDROID_APP_APPLICATION = "Landroid/app/Application;";

    private Dex2c() {
    }

    /**
     * 处理多个dex文件
     *
     * @param dexFiles dex文件列表
     * @param outDir   生成c文件等输出目录
     * @return 输出结果配置
     * @throws IOException
     */
    public static GlobalDexConfig handleAllDex(@Nonnull List<File> dexFiles,
                                               @Nonnull ClassAndMethodFilter filter,
                                               @Nonnull InstructionRewriter instructionRewriter,
                                               @Nonnull ClassAnalyzer classAnalyzer,
                                               @Nonnull File outDir,
                                               @Nonnull ProtectionContext protectionContext) throws IOException {
        if (!outDir.exists()) outDir.mkdirs();
        final GlobalDexConfig globalConfig = new GlobalDexConfig(outDir);
        int matchedClassCount = 0;
        int matchedMethodCount = 0;
        int skippedEmptyMethodCount = 0;
        int convertedClassCount = 0;
        int convertedMethodCount = 0;

        for (File file : dexFiles) {
            final DexConfig config = handleDex(
                    file,
                    filter,
                    classAnalyzer,
                    instructionRewriter,
                    outDir,
                    protectionContext);

            matchedClassCount += config.getMatchedClassCount();
            matchedMethodCount += config.getMatchedMethodCount();
            skippedEmptyMethodCount += config.getSkippedEmptyMethodCount();
            convertedClassCount += config.getShellMethods().keySet().size();
            convertedMethodCount += config.getShellMethods().size();

            //不需要给外部
            config.setShellMethods(null);

            globalConfig.addDexConfig(config);
        }
        printConversionStats(matchedClassCount, matchedMethodCount, skippedEmptyMethodCount,
                convertedClassCount, convertedMethodCount);
        globalConfig.generateJniInitCode();
        return globalConfig;
    }

    private static void printConversionStats(int matchedClassCount,
                                             int matchedMethodCount,
                                             int skippedEmptyMethodCount,
                                             int convertedClassCount,
                                             int convertedMethodCount) {
        System.out.printf("[nmmp] Matched:       classes=%d, methods=%d%n",
                matchedClassCount, matchedMethodCount);
        System.out.printf("[nmmp] Skipped empty: methods=%d%n", skippedEmptyMethodCount);
        System.out.printf("[nmmp] Converted:     classes=%d, methods=%d%n",
                convertedClassCount, convertedMethodCount);
    }

    /**
     * 处理单个dex文件
     */
    public static DexConfig handleDex(@Nonnull File dexFile,
                                      @Nonnull ClassAndMethodFilter filter,
                                      @Nonnull ClassAnalyzer classAnalyzer,
                                      @Nonnull InstructionRewriter instructionRewriter,
                                      @Nonnull File outDir,
                                      @Nonnull ProtectionContext protectionContext) throws IOException {
        return handleDex(new BufferedInputStream(new FileInputStream(dexFile)),
                dexFile.getName(),
                filter,
                classAnalyzer,
                instructionRewriter,
                outDir,
                protectionContext);
    }

    public static DexConfig handleModuleDex(@Nonnull File dexFile,
                                            @Nonnull ClassAndMethodFilter filter,
                                            @Nonnull ClassAnalyzer classAnalyzer,
                                            @Nonnull InstructionRewriter instructionRewriter,
                                            @Nonnull File outDir,
                                            @Nonnull ProtectionContext protectionContext) throws IOException {
        final GlobalDexConfig globalDexConfig = new GlobalDexConfig(outDir);
        final DexConfig dexConfig = handleDex(
                dexFile,
                filter,
                classAnalyzer,
                instructionRewriter,
                outDir,
                protectionContext);
        globalDexConfig.addDexConfig(dexConfig);
        printConversionStats(dexConfig.getMatchedClassCount(), dexConfig.getMatchedMethodCount(),
                dexConfig.getSkippedEmptyMethodCount(),
                dexConfig.getShellMethods().keySet().size(), dexConfig.getShellMethods().size());

        globalDexConfig.generateJniInitCode();
        return dexConfig;
    }

    /**
     * 处理单个dex流
     */
    public static DexConfig handleDex(@Nonnull InputStream dex,
                                      @Nonnull String dexFileName,
                                      @Nonnull ClassAndMethodFilter filter,
                                      @Nonnull ClassAnalyzer classAnalyzer,
                                      @Nonnull InstructionRewriter instructionRewriter,
                                      @Nonnull File outDir,
                                      @Nonnull ProtectionContext protectionContext) throws IOException {
        if (!outDir.exists()) outDir.mkdirs();
        DexConfig config = splitDex(dex, dexFileName, filter, classAnalyzer, outDir);


        final DexBackedDexFile nativeImplDexFile = DexBackedDexFile.fromInputStream(null,
                new BufferedInputStream(new FileInputStream(config.getImplDexFile())));

        //根据符号dex生成c代码
        try (Writer nativeCodeWriter = new OutputStreamWriter(
                new FileOutputStream(config.getNativeFunctionsFile()), StandardCharsets.UTF_8);
             Writer resolverWriter = new OutputStreamWriter(
                     new FileOutputStream(config.getResolverFile()), StandardCharsets.UTF_8);
        ) {
            final long dexId = protectionContext.nextDexId();
            JniCodeGenerator codeGenerator = new JniCodeGenerator(nativeImplDexFile,
                    classAnalyzer,
                    instructionRewriter,
                    protectionContext,
                    dexId);

            codeGenerator.generate(
                    config,
                    resolverWriter,
                    nativeCodeWriter
            );
            config.setResult(codeGenerator);
        }

        return config;
    }

    //分割dex产生两个dex,一个为壳dex,一个为实现dex,壳dex将会打包进apk,实现dex会被转换为c代码
    @Nonnull
    private static DexConfig splitDex(@Nonnull InputStream dex,
                                      @Nonnull String dexFileName,
                                      @Nonnull ClassAndMethodFilter filter,
                                      @Nonnull ClassAnalyzer classAnalyzer,
                                      @Nonnull File outDir) throws IOException {
        DexBackedDexFile originDexFile = DexBackedDexFile.fromInputStream(
                null,
                dex);

        //把方法变为本地方法,用它替换掉原本的dex
        DexPool shellDexPool = new DexPool(originDexFile.getOpcodes());

        DexPool nativeImplDexPool = new DexPool(originDexFile.getOpcodes());

        final MethodConverter methodConverter = new MethodConverter(classAnalyzer);

        HashMultimap<String, List<? extends Method>> shellMethods = HashMultimap.create();
        int matchedClassCount = 0;
        int matchedMethodCount = 0;
        int skippedEmptyMethodCount = 0;

        for (final ClassDef classDef : originDexFile.getClasses()) {
            if (filter.acceptClass(classDef)) {
                matchedClassCount++;
                final ArrayList<Method> shellDirectMethods = new ArrayList<>();
                final ArrayList<Method> shellVirtualMethods = new ArrayList<>();

                final ArrayList<Method> implDirectMethods = new ArrayList<>();
                final ArrayList<Method> implVirtualMethods = new ArrayList<>();

                // 处理所有需要转换的方法
                for (Method method : classDef.getMethods()) {
                    final boolean accepted = filter.acceptMethod(method);
                    if (accepted) {
                        matchedMethodCount++;
                    }
                    final boolean emptyVoidMethod = accepted
                            && MyMethodUtil.isEmptyVoidMethod(method);
                    final Opcode unsupportedOpcode = accepted && !emptyVoidMethod
                            ? findUnsupportedOpcode(method, classAnalyzer)
                            : null;
                    if (accepted && !emptyVoidMethod && unsupportedOpcode == null
                        // 有直接调用jna方法的指令,则不能进行native化
                        // 感觉很少会发生,默认就把这个判断注释掉了,谁需要再去掉注释
//                            && !classAnalyzer.hasCallJnaMethod(method)
                    ) {
                        final Pair<List<? extends Method>, Method> pair = methodConverter.convert(method);
                        // 转换后可能出现变为多个方法
                        addMethods(shellDirectMethods, shellVirtualMethods, pair.first);

                        //记录当前类，所有需要被修改的方法
                        shellMethods.put(classDef.getType(), pair.first);

                        //只有一个具体实现
                        addMethod(implDirectMethods, implVirtualMethods, pair.second);
                    } else {
                        if (emptyVoidMethod) {
                            skippedEmptyMethodCount++;
                            System.out.printf(
                                    "[nmmp] 跳过空方法（保留 DEX）: %s->%s%s%n",
                                    method.getDefiningClass(),
                                    method.getName(),
                                    MyMethodUtil.getMethodSignature(
                                            method.getParameterTypes(),
                                            method.getReturnType()));
                        } else if (unsupportedOpcode != null) {
                            System.err.printf(
                                    "跳过不支持的 DEX 指令: %s->%s%s，opcode=%s%n",
                                    method.getDefiningClass(),
                                    method.getName(),
                                    MyMethodUtil.getMethodSignature(
                                            method.getParameterTypes(),
                                            method.getReturnType()),
                                    unsupportedOpcode.name());
                        }
                        //不需要进行处理
                        addMethod(shellDirectMethods, shellVirtualMethods, method);
                    }
                }

                //把需要转换的方法设为native
                shellDexPool.internClass(new MyClassDef(classDef, shellDirectMethods, shellVirtualMethods));
                //收集所有需要转换的方法生成新dex
                nativeImplDexPool.internClass(new MyClassDef(classDef, implDirectMethods, implVirtualMethods));
            } else {
                //不需要处理的class,直接复制
                shellDexPool.internClass(classDef);
            }
        }
        DexConfig config = new DexConfig(outDir, dexFileName);

        config.setShellMethods(shellMethods);
        config.setMatchedStats(matchedClassCount, matchedMethodCount);
        config.setSkippedEmptyMethodCount(skippedEmptyMethodCount);

        //写入需要运行的dex
        shellDexPool.writeTo(new FileDataStore(config.getShellDexFile()));
        //写入符号dex
        nativeImplDexPool.writeTo(new FileDataStore(config.getImplDexFile()));
        return config;
    }

    private static Opcode findUnsupportedOpcode(Method method, ClassAnalyzer classAnalyzer) {
        final MethodImplementation implementation = method.getImplementation();
        if (implementation == null) {
            return null;
        }
        for (Instruction instruction : implementation.getInstructions()) {
            switch (instruction.getOpcode()) {
                case INVOKE_SUPER:
                case INVOKE_SUPER_RANGE: {
                    final MethodReference reference = (MethodReference)
                            ((ReferenceInstruction) instruction).getReference();
                    if (classAnalyzer.isInterfaceInvokeSuper(method.getDefiningClass(), reference)) {
                        return instruction.getOpcode();
                    }
                    break;
                }
                case INVOKE_POLYMORPHIC:
                case INVOKE_POLYMORPHIC_RANGE:
                case INVOKE_CUSTOM:
                case INVOKE_CUSTOM_RANGE:
                case CONST_METHOD_HANDLE:
                case CONST_METHOD_TYPE:
                    return instruction.getOpcode();
                default:
                    break;
            }
        }
        return null;
    }

    private static void addMethods(List<Method> directMethods,
                                   List<Method> virtualMethods,
                                   List<? extends Method> methods) {
        for (Method method : methods) {
            addMethod(directMethods, virtualMethods, method);
        }
    }

    private static void addMethod(List<Method> directMethods,
                                  List<Method> virtualMethods,
                                  Method method) {
        if (MethodUtil.isDirect(method)) {
            directMethods.add(method);
        } else {
            virtualMethods.add(method);
        }
    }

    //在处理过的class的static{}块最前面添加注册本地方法代码,如果不存在static{}块则新增<clinit>方法
    public static List<DexPool> injectCallRegisterNativeInsns(DexConfig config,
                                                              DexPool lastDexPool,
                                                              Set<String> mainClassSet,
                                                              int maxPoolSize) throws IOException {

        DexBackedDexFile dexNativeFile = DexBackedDexFile.fromInputStream(
                null,
                new BufferedInputStream(new FileInputStream(config.getShellDexFile())));

        List<DexPool> dexPools = new ArrayList<>();
        dexPools.add(lastDexPool);


        for (ClassDef classDef : dexNativeFile.getClasses()) {
            if (mainClassSet.contains(classDef.getType())) {//提前处理过的class,不用再处理
                continue;
            }
            internClass(config, lastDexPool, classDef);

            if (lastDexPool.hasOverflowed(maxPoolSize)) {
                lastDexPool = new DexPool(dexNativeFile.getOpcodes());
                dexPools.add(lastDexPool);
            }
        }
        return dexPools;
    }

    private static void internClass(DexConfig config, DexPool dexPool, ClassDef classDef) {
        final Set<String> classes = config.getHandledNativeClasses();
        final String type = classDef.getType();
        final String className = type.substring(1, type.length() - 1);
        if (classes.contains(className)) {
            final RegisterNativesCallerClassDef nativeClassDef = new RegisterNativesCallerClassDef(
                    classDef,
                    config.getOffsetFromClassName(className),
                    "L" + config.getRegisterNativesClassName() + ";",
                    config.getRegisterNativesMethodName());
            dexPool.internClass(nativeClassDef);
        } else {
            dexPool.internClass(classDef);
        }
    }

}

package com.nmmedit.dex2c;

import com.nmmedit.apkprotect.dex2c.Dex2c;
import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.GlobalDexConfig;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.MyMethodUtil;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.NoneInstructionRewriter;
import com.nmmedit.apkprotect.dex2c.converter.testbuild.ClassMethodImplCollection;
import com.nmmedit.apkprotect.dex2c.filters.ClassAndMethodFilter;
import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.reference.MethodReference;
import com.android.tools.smali.dexlib2.immutable.ImmutableClassDef;
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodReference;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import org.junit.Test;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Collections;
import java.util.Iterator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class Dex2cTest {

    @Test
    public void testParseDex() throws IOException {
        parseDex(this.getClass().getResourceAsStream("/classes2.dex"));
    }

    @Test
    public void testDexSplit() throws IOException {
        File outdir = new File("/tmp", "outdir");
        if (!outdir.exists()) outdir.mkdirs();
        final InstructionRewriter instructionRewriter = new NoneInstructionRewriter();
        final ClassAnalyzer classAnalyzer = new ClassAnalyzer();
        final DexBackedDexFile dexFile = DexBackedDexFile.fromInputStream(null, new BufferedInputStream(this.getClass().getResourceAsStream("/classes2.dex")));
        classAnalyzer.loadDexFile(dexFile);

        final DexConfig config = Dex2c.handleDex(this.getClass().getResourceAsStream("/classes2.dex"),
                "classes.dex",
                testFilter,
                classAnalyzer,
                instructionRewriter,
                outdir,
                new ProtectionContext(0x0123456789abcdefL));

        assertTrue(config.getMatchedClassCount() >= config.getShellMethods().keySet().size());
        assertTrue(config.getMatchedMethodCount() >= config.getShellMethods().size());

        final String resolverSource = StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(
                        Files.readAllBytes(new File(outdir, "classes_resolver.c").toPath())))
                .toString();
        assertFalse(resolverSource.contains("Exception.h"));
        assertTrue(resolverSource.contains("ThrowNew(env, gVm.exInternalError"));
        assertFalse(resolverSource.contains("ExceptionClear(env)"));
        assertTrue(resolverSource.contains("if (!(*env)->ExceptionCheck(env))"));
    }

    @Test
    public void testSignatureBoundRegistrationUsesInitSlotZero() throws IOException {
        final File outDir = Files.createTempDirectory("nmmp-bound").toFile();
        final ClassAnalyzer analyzer = new ClassAnalyzer();
        try (InputStream input = getClass().getResourceAsStream("/classes2.dex")) {
            analyzer.loadDexFile(DexBackedDexFile.fromInputStream(null, input));
        }
        final ProtectionContext context = ProtectionContext.createBound(
                "com.example.app", new byte[]{1, 2, 3});
        final DexConfig config;
        try (InputStream input = getClass().getResourceAsStream("/classes2.dex")) {
            config = Dex2c.handleDex(input, "classes.dex", testFilter, analyzer,
                    new NoneInstructionRewriter(), outDir, context);
        }
        final Iterator<String> classes = config.getHandledNativeClasses().iterator();
        assertTrue(classes.hasNext());
        assertTrue(config.getOffsetFromClassName(classes.next()) >= 1);

        final String nativeSource = new String(
                Files.readAllBytes(config.getNativeFunctionsFile().toPath()),
                StandardCharsets.UTF_8);
        assertTrue(nativeSource.contains("if (dataIdx == 0)"));
        assertTrue(nativeSource.contains("gNmmpPending[registerIdx] = 1"));
        assertTrue(nativeSource.contains("bool classes_setup_activate(JNIEnv *env)"));

        final GlobalDexConfig global = new GlobalDexConfig(outDir, true);
        global.addDexConfig(config);
        global.generateJniInitCode();
        final String initSource = new String(
                Files.readAllBytes(global.getInitCodeFile().toPath()),
                StandardCharsets.UTF_8);
        assertTrue(initSource.contains("vmBindingActivate(env, context)"));
        assertTrue(initSource.contains("classes_setup_activate(env)"));
    }

    @Test
    public void testClassInvokeSuperUsesDirectSuperclass() {
        final ImmutableClassDef callerClass = new ImmutableClassDef(
                "Ltests/Child;",
                AccessFlags.PUBLIC.getValue(),
                "Ljava/util/LinkedHashSet;",
                Collections.<String>emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Collections.emptyList());
        final MethodReference originalReference = new ImmutableMethodReference(
                "Ljava/util/AbstractCollection;",
                "add",
                Collections.singletonList("Ljava/lang/Object;"),
                "Z");

        final MethodReference rewrittenReference = ClassAnalyzer.createClassInvokeSuperReference(
                callerClass,
                originalReference);

        assertEquals("Ljava/util/LinkedHashSet;", rewrittenReference.getDefiningClass());
        assertEquals(originalReference.getName(), rewrittenReference.getName());
        assertEquals(originalReference.getParameterTypes(), rewrittenReference.getParameterTypes());
        assertEquals(originalReference.getReturnType(), rewrittenReference.getReturnType());
    }

    public static ClassAndMethodFilter testFilter = new ClassAndMethodFilter() {

        @Override
        public boolean acceptClass(ClassDef classDef) {
            return classDef.getType().startsWith("Ltests/");
        }

        @Override
        public boolean acceptMethod(Method method) {
            return !MyMethodUtil.isConstructorOrAbstract(method) && !AccessFlags.BRIDGE.isSet(method.getAccessFlags());
        }
    };

    public static void parseDex(InputStream dexStream) throws IOException {
        DexBackedDexFile dexFile = DexBackedDexFile.fromInputStream(Opcodes.forApi(21), dexStream);
        DexPool dexPool = new DexPool(Opcodes.forApi(21));
        DexPool dexPoolMethodIml = new DexPool(Opcodes.forApi(21));


        StringBuilder sb = new StringBuilder();

        for (final ClassDef classDef : dexFile.getClasses()) {
            if (testFilter.acceptClass(classDef)) {
                dexPool.internClass(new ClassMethodToNative(classDef, testFilter));
                dexPoolMethodIml.internClass(new ClassMethodImplCollection(classDef, sb));
            } else {
                dexPool.internClass(classDef);
            }
        }

        //需要看输出文件可以自己制定目录
//        File outdir = File.createTempFile("mytest", "dex2c-dir");
//        if(!outdir.exists()) outdir.mkdirs();
//        dexPool.writeTo(new FileDataStore(new File(outdir,"classes2.dex")));
//        dexPoolMethodIml.writeTo(new FileDataStore(new File(outdir,"sym.dat")));

    }

    @Test
    public void testDexConvert() throws IOException {
//        File dexdir = new File("/home/mao/estest/");
//        File outdir = new File(dexdir, "dex2c");
//        if (!outdir.exists()) outdir.mkdirs();
//        ArrayList<File> dexes = new ArrayList<>();
//        for (File file : dexdir.listFiles()) {
//            if (file.getName().endsWith(".dex")) {
//                dexes.add(file);
//            }
//        }
//
//        final InstructionRewriter instructionRewriter = new NoneInstructionRewriter();
//        final GlobalDexConfig globalConfig = Dex2c.handleDexes(dexes, new ClassAndMethodFilter() {
//                    @Override
//                    public boolean acceptClass(ClassDef classDef) {
//                        if (
//                                classDef.getType().startsWith("Landroid/") ||
//                                        classDef.getType().startsWith("Landroidx/")
//                        ) {
//                            return false;
//                        }
//                        return true;
//                    }
//
//                    @Override
//                    public boolean acceptMethod(Method method) {
//                        return !MyMethodUtil.isConstructorOrAbstract(method) &&
//                                !MyMethodUtil.isBridgeOrSynthetic(method);
//                    }
//                },
//                instructionRewriter,
//                outdir);
//
    }
}

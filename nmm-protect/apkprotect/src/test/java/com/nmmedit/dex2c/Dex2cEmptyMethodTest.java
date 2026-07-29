package com.nmmedit.dex2c;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.dexlib2.immutable.ImmutableClassDef;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import com.nmmedit.apkprotect.dex2c.Dex2c;
import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.filters.ClassAndMethodFilter;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.NoneInstructionRewriter;
import org.junit.Test;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class Dex2cEmptyMethodTest {

    @Test
    public void testRegisterNativeNames() {
        final DexConfig mainDexConfig = new DexConfig(new File("."), "classes.dex");
        final DexConfig secondaryDexConfig = new DexConfig(new File("."), "classes2.dex");

        assertEquals("com/google/libc/ReactNative",
                mainDexConfig.getRegisterNativesClassName());
        assertEquals("js", mainDexConfig.getRegisterNativesMethodName());
        assertEquals("js2", secondaryDexConfig.getRegisterNativesMethodName());
    }

    @Test
    public void testEmptyMethodRemainsInDex() throws Exception {
        final File outDir = Files.createTempDirectory("nmmp-empty-method").toFile();
        final File inputDex = new File(outDir, "input.dex");
        writeInputDex(inputDex);

        final ClassAnalyzer classAnalyzer = new ClassAnalyzer();
        try (InputStream input = new BufferedInputStream(new FileInputStream(inputDex))) {
            classAnalyzer.loadDexFile(DexBackedDexFile.fromInputStream(null, input));
        }

        final DexConfig config;
        try (InputStream input = new BufferedInputStream(new FileInputStream(inputDex))) {
            config = Dex2c.handleDex(
                    input,
                    "classes.dex",
                    ACCEPT_ALL,
                    classAnalyzer,
                    new NoneInstructionRewriter(),
                    outDir,
                    new ProtectionContext(0x0123456789abcdefL));
        }

        assertEquals(1, config.getMatchedClassCount());
        assertEquals(2, config.getMatchedMethodCount());
        assertEquals(1, config.getSkippedEmptyMethodCount());
        assertEquals(1, config.getShellMethods().size());

        final DexBackedDexFile shellDex;
        try (InputStream input = new BufferedInputStream(
                new FileInputStream(config.getShellDexFile()))) {
            shellDex = DexBackedDexFile.fromInputStream(null, input);
        }
        final ClassDef classDef = shellDex.getClasses().iterator().next();
        final Method emptyMethod = findMethod(classDef, "empty");
        final Method nonEmptyMethod = findMethod(classDef, "nonEmpty");

        assertFalse(AccessFlags.NATIVE.isSet(emptyMethod.getAccessFlags()));
        assertNotNull(emptyMethod.getImplementation());
        assertTrue(AccessFlags.NATIVE.isSet(nonEmptyMethod.getAccessFlags()));
        assertNull(nonEmptyMethod.getImplementation());
    }

    private static void writeInputDex(File inputDex) throws Exception {
        final ImmutableMethod emptyMethod = createMethod(
                "empty",
                Collections.singletonList(new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        final ImmutableMethod nonEmptyMethod = createMethod(
                "nonEmpty",
                Arrays.asList(
                        new ImmutableInstruction10x(Opcode.NOP),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        final ImmutableClassDef classDef = new ImmutableClassDef(
                "Ltests/EmptyMethods;",
                AccessFlags.PUBLIC.getValue(),
                "Ljava/lang/Object;",
                Collections.emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Arrays.asList(emptyMethod, nonEmptyMethod));

        final DexPool dexPool = new DexPool(Opcodes.forApi(21));
        dexPool.internClass(classDef);
        dexPool.writeTo(new FileDataStore(inputDex));
    }

    private static ImmutableMethod createMethod(
            String name,
            List<? extends Instruction> instructions) {
        return new ImmutableMethod(
                "Ltests/EmptyMethods;",
                name,
                Collections.emptyList(),
                "V",
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        0,
                        instructions,
                        Collections.emptyList(),
                        Collections.emptyList()));
    }

    private static Method findMethod(ClassDef classDef, String name) {
        for (Method method : classDef.getMethods()) {
            if (name.equals(method.getName())) {
                return method;
            }
        }
        throw new AssertionError("找不到方法: " + name);
    }

    private static final ClassAndMethodFilter ACCEPT_ALL = new ClassAndMethodFilter() {
        @Override
        public boolean acceptClass(ClassDef classDef) {
            return true;
        }

        @Override
        public boolean acceptMethod(Method method) {
            return true;
        }
    };
}

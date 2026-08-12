package com.nmmedit.apkprotect.dex2c.converter.structs;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.dexlib2.iface.instruction.ReferenceInstruction;
import com.android.tools.smali.dexlib2.iface.reference.MethodReference;
import com.android.tools.smali.dexlib2.immutable.ImmutableClassDef;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodParameter;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import org.junit.Test;

import java.io.File;
import java.io.BufferedInputStream;
import java.nio.file.Files;
import java.util.Collections;

import static org.junit.Assert.*;

public class ApplicationInitClassDefTest {
    private static final String UTIL = "Lcom/google/libc/ReactNative;";

    @Test
    public void generatedClassesCanBeWrittenAndRead() throws Exception {
        final DexPool pool = new DexPool(Opcodes.forApi(21));
        pool.internClass(new RegisterNativesUtilClassDef(
                UTIL, Collections.singletonList("js"), "nmmp"));
        pool.internClass(new ApplicationInitClassDef("Ltest/GeneratedApp;", UTIL, "js"));
        pool.internClass(new ApplicationInitClassDef(existingApplication(), UTIL, "js"));

        final File dexFile = Files.createTempFile("nmmp-application-init", ".dex").toFile();
        pool.writeTo(new FileDataStore(dexFile));
        final DexBackedDexFile dex = DexBackedDexFile.fromInputStream(
                null, new BufferedInputStream(Files.newInputStream(dexFile.toPath())));

        assertInitCall(findClass(dex, "Ltest/GeneratedApp;"));
        assertInitCall(findClass(dex, "Ltest/ExistingApp;"));
        assertUtilityClass(findClass(dex, UTIL));
    }

    private static ImmutableClassDef existingApplication() {
        final ImmutableMethod attach = new ImmutableMethod(
                "Ltest/ExistingApp;",
                "attachBaseContext",
                Collections.singletonList(new ImmutableMethodParameter(
                        RegisterNativesUtilClassDef.CONTEXT_TYPE,
                        Collections.emptySet(),
                        null)),
                "V",
                AccessFlags.PROTECTED.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        2,
                        Collections.singletonList(new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                        Collections.emptyList(),
                        Collections.emptyList()));
        return new ImmutableClassDef(
                "Ltest/ExistingApp;",
                AccessFlags.PUBLIC.getValue(),
                "Landroid/app/Application;",
                Collections.emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Collections.singletonList(attach));
    }

    private static void assertInitCall(ClassDef classDef) {
        final Method attach = findMethod(
                classDef, "attachBaseContext", RegisterNativesUtilClassDef.CONTEXT_TYPE);
        assertNotNull(attach.getImplementation());
        final Instruction first = attach.getImplementation().getInstructions().iterator().next();
        assertEquals(Opcode.INVOKE_STATIC_RANGE, first.getOpcode());
        final MethodReference reference = (MethodReference)
                ((ReferenceInstruction) first).getReference();
        assertEquals(UTIL, reference.getDefiningClass());
        assertEquals("js", reference.getName());
        assertEquals(Collections.singletonList(RegisterNativesUtilClassDef.CONTEXT_TYPE),
                reference.getParameterTypes());
    }

    private static void assertUtilityClass(ClassDef classDef) {
        assertEquals(RegisterNativesUtilClassDef.CONTEXT_TYPE,
                classDef.getStaticFields().iterator().next().getType());
        final Method nativeMethod = findMethod(classDef, "js", "I");
        final Method helperMethod = findMethod(
                classDef, "js", RegisterNativesUtilClassDef.CONTEXT_TYPE);
        assertTrue(AccessFlags.NATIVE.isSet(nativeMethod.getAccessFlags()));
        assertNull(nativeMethod.getImplementation());
        assertFalse(AccessFlags.NATIVE.isSet(helperMethod.getAccessFlags()));
        assertNotNull(helperMethod.getImplementation());
    }

    private static ClassDef findClass(DexBackedDexFile dex, String type) {
        for (ClassDef classDef : dex.getClasses()) {
            if (type.equals(classDef.getType())) return classDef;
        }
        throw new AssertionError("Class not found: " + type);
    }

    private static Method findMethod(ClassDef classDef, String name, String parameterType) {
        for (Method method : classDef.getMethods()) {
            if (name.equals(method.getName())
                    && method.getParameterTypes().equals(Collections.singletonList(parameterType))) {
                return method;
            }
        }
        throw new AssertionError("Method not found: " + name + parameterType);
    }
}

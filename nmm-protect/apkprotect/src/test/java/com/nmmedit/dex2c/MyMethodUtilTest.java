package com.nmmedit.dex2c;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.nmmedit.apkprotect.dex2c.converter.MyMethodUtil;
import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class MyMethodUtilTest {

    @Test
    public void testOnlyReturnVoidIsEmpty() {
        assertTrue(MyMethodUtil.isEmptyVoidMethod(
                createMethod("V", Collections.singletonList(
                        new ImmutableInstruction10x(Opcode.RETURN_VOID)))));
    }

    @Test
    public void testMethodWithoutImplementationIsNotEmpty() {
        assertFalse(MyMethodUtil.isEmptyVoidMethod(new ImmutableMethod(
                "Ltests/Empty;",
                "empty",
                Collections.emptyList(),
                "V",
                AccessFlags.PUBLIC.getValue() | AccessFlags.ABSTRACT.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                null)));
    }

    @Test
    public void testNonVoidMethodIsNotEmpty() {
        assertFalse(MyMethodUtil.isEmptyVoidMethod(
                createMethod("I", Collections.singletonList(
                        new ImmutableInstruction10x(Opcode.RETURN_VOID)))));
    }

    @Test
    public void testAdditionalInstructionIsNotEmpty() {
        assertFalse(MyMethodUtil.isEmptyVoidMethod(
                createMethod("V", Arrays.asList(
                        new ImmutableInstruction10x(Opcode.NOP),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID)))));
    }

    private static ImmutableMethod createMethod(
            String returnType,
            List<ImmutableInstruction10x> instructions) {
        return new ImmutableMethod(
                "Ltests/Empty;",
                "empty",
                Collections.emptyList(),
                returnType,
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        0,
                        instructions,
                        Collections.emptyList(),
                        Collections.emptyList()));
    }
}

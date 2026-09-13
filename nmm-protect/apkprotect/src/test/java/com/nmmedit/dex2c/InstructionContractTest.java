package com.nmmedit.dex2c;

import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.dexlib2.immutable.*;
import com.android.tools.smali.dexlib2.immutable.instruction.*;
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodReference;
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodProtoReference;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.NoneInstructionRewriter;
import org.junit.Test;
import java.util.*;
import static org.junit.Assert.*;

public class InstructionContractTest {
    private static ImmutableMethod method(List<? extends Instruction> instructions) {
        return new ImmutableMethod("LTest;", "run", Collections.emptyList(), "V", 8,
                Collections.emptySet(), Collections.emptySet(), new ImmutableMethodImplementation(4,
                instructions, Collections.emptyList(), Collections.emptyList()));
    }

    @Test public void polymorphicInstructionsMustNotDisappear() {
        ImmutableMethodReference target = new ImmutableMethodReference("Ljava/lang/invoke/MethodHandle;",
                "invokeExact", Collections.emptyList(), "V");
        ImmutableMethodProtoReference proto = new ImmutableMethodProtoReference(Collections.emptyList(), "V");
        List<Instruction> unsupported = Arrays.asList(
                new ImmutableInstruction45cc(Opcode.INVOKE_POLYMORPHIC, 1, 0, 0, 0, 0, 0, target, proto),
                new ImmutableInstruction4rcc(Opcode.INVOKE_POLYMORPHIC_RANGE, 0, 1, target, proto));
        for (Instruction instruction : unsupported) {
            try {
                new NoneInstructionRewriter().rewriteInstructions(method(Arrays.asList(
                        instruction, new ImmutableInstruction10x(Opcode.RETURN_VOID))));
                fail("silently discarded " + instruction.getOpcode());
            } catch (IllegalArgumentException expected) {
                assertTrue(expected.getMessage().contains("LTest;->run"));
                assertTrue(expected.getMessage().contains(instruction.getOpcode().name));
            }
        }
    }

    @Test public void supportedInstructionsPreserveCodeUnits() {
        assertArrayEquals(new byte[]{0, 0, 0x0e, 0}, new NoneInstructionRewriter().rewriteInstructions(method(
                Arrays.asList(new ImmutableInstruction10x(Opcode.NOP), new ImmutableInstruction10x(Opcode.RETURN_VOID)))));
    }

    private static ImmutableMethodImplementation withTry(int start, int count, int target) {
        return new ImmutableMethodImplementation(1,
                Arrays.asList(new ImmutableInstruction10x(Opcode.NOP), new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                Collections.singletonList(new ImmutableTryBlock(start, count,
                        Collections.singletonList(new ImmutableExceptionHandler(null, target)))), Collections.emptyList());
    }

    @Test public void exceptionRangesMustStayInsideMethod() throws Exception {
        for (int[] invalid : new int[][]{{0, 3, 1}, {2, 1, 1}, {0, 1, 2}, {0, 0, 1}}) {
            try {
                new NoneInstructionRewriter().handleTries(withTry(invalid[0], invalid[1], invalid[2]));
                fail("out-of-range exception metadata accepted: " + Arrays.toString(invalid));
            } catch (IllegalArgumentException expected) {
                assertTrue(expected.getMessage().contains("exception"));
            }
        }
    }

    @Test public void validCatchAllStillSerializes() throws Exception {
        assertTrue(new NoneInstructionRewriter().handleTries(withTry(0, 1, 1)).length > 12);
    }
}

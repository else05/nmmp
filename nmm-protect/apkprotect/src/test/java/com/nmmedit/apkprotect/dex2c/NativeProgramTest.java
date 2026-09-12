package com.nmmedit.apkprotect.dex2c;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import org.junit.Test;

import java.io.StringWriter;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.Set;

import static com.nmmedit.apkprotect.dex2c.NativeFormats.*;
import static org.junit.Assert.*;

public class NativeProgramTest {
    private static final long KEY = 0x0123456789abcdefL;
    private static NativeProgram.Instruction i(int op, int dst, int a, int b, long imm, int target) {
        return new NativeProgram.Instruction(op, dst, a, b, imm, target);
    }
    private static NativeProgram.Instruction constant(int dst, long value) { return i(CONST, dst, 0, 0, value, 0); }
    private static NativeProgram.Instruction ret() { return i(RETURN, 0, 0, 0, 0, 0); }
    private static byte[] identity() {
        byte[] ops = new byte[OP_COUNT];
        for (int op = 0; op < OP_COUNT; ++op) ops[op] = (byte)op;
        return ops;
    }
    private static NativeProgram.Program program(NativeProgram.Instruction... instructions) {
        return NativeProgram.assemble(Arrays.asList(instructions), KEY, identity());
    }
    private static final class Vector {
        final String name;
        final NativeProgram.Program program;
        final long[] inputs;
        final boolean success;
        final long output;
        Vector(String name, NativeProgram.Program program, long[] inputs, boolean success, long output) {
            this.name = name; this.program = program; this.inputs = inputs;
            this.success = success; this.output = output;
        }
        void check() {
            NativeProgram.Result result = NativeProgram.run(program, inputs);
            assertEquals(name, success, result.success);
            assertEquals(name, output, result.value);
        }
    }

    private static NativeProgram.Program changed(NativeProgram.Program program, int offset, int value) {
        byte[] plain = new MethodCodec(program.key).transform(program.code, 0, DOMAIN);
        plain[offset] = (byte)value;
        byte[] code = new MethodCodec(program.key).transform(plain, 0, DOMAIN);
        return new NativeProgram.Program(code, program.key, program.opcodes, MethodCodec.hash(code));
    }
    private static void add(List<Vector> vectors, String name, NativeProgram.Program program,
                            boolean success, long output, long... inputs) {
        vectors.add(new Vector(name, program, inputs, success, output));
    }

    private static List<Vector> vectors() {
        List<Vector> vectors = new ArrayList<>();
        int[] binary = {XOR, AND, OR, ADD, SUB, MUL, SHL, SHR};
        long[] left = {-1, 0x55aa, 0x5500, -1, 0, Long.MAX_VALUE, 1, Long.MIN_VALUE};
        long[] right = {0x1234, 0x0ff0, 0x00aa, 1, 1, 2, 63, 63};
        long[] outputs = {~0x1234L, 0x05a0, 0x55aa, 0, -1, -2, Long.MIN_VALUE, 1};
        for (int index = 0; index < binary.length; ++index) {
            NativeProgram.Program p = program(i(LOAD_INPUT, 14, 0, 0, 0, 0), i(LOAD_INPUT, 15, 1, 0, 0, 0),
                    i(binary[index], 0, 14, 15, 0, 0), constant(1, 1), ret());
            add(vectors, "binary-" + binary[index], p, true, outputs[index], left[index], right[index]);
            if (binary[index] == SHL || binary[index] == SHR) {
                add(vectors, "shift64-" + binary[index], p, false, 0, 1, 64);
                add(vectors, "shiftUnsignedMax-" + binary[index], p, false, 0, 1, -1);
                add(vectors, "shift0-" + binary[index], p, true, Long.MIN_VALUE, Long.MIN_VALUE, 0);
            }
        }
        NativeProgram.Program branch = program(i(LOAD_INPUT, 2, 0, 0, 0, 0), i(LOAD_INPUT, 3, 1, 0, 0, 0),
                constant(1, 1), i(JULT, 0, 2, 3, 0, 6), constant(0, 7), i(JMP, 0, 0, 0, 0, 7),
                constant(0, 9), ret());
        add(vectors, "jult-unsigned-not-taken", branch, true, 7, -1, 0);
        add(vectors, "jult-unsigned-taken", branch, true, 9, 0, -1);
        add(vectors, "jult-equal", branch, true, 7, -1, -1);
        long[] sixteen = new long[16]; sixteen[15] = 0x8877665544332211L;
        add(vectors, "load-last-input-mov", program(i(LOAD_INPUT, 15, 15, 0, 0, 0), i(MOV, 0, 15, 0, 0, 0),
                constant(1, 1), ret()), true, sixteen[15], sixteen);
        add(vectors, "return-status0", program(constant(0, 123), ret()), false, 0);
        add(vectors, "return-status2", program(constant(0, 123), constant(1, 2), ret()), false, 0);
        add(vectors, "return-statusUnsignedMax", program(constant(0, 123), constant(1, -1), ret()), false, 0);
        NativeProgram.Program valid = program(constant(1, 1), ret());
        add(vectors, "empty-inputs-success", valid, true, 0);
        add(vectors, "inputCount17", valid, false, 0, new long[17]);
        add(vectors, "missing-input", program(i(LOAD_INPUT, 0, 0, 0, 0, 0), constant(1, 1), ret()), false, 0);
        add(vectors, "bad-opcode", changed(valid, 0, 255), false, 0);
        add(vectors, "bad-dst-register", changed(valid, 1, 16), false, 0);
        add(vectors, "bad-a-register", program(i(MOV, 0, 16, 0, 0, 0), ret()), false, 0);
        add(vectors, "bad-b-register", program(i(ADD, 0, 0, 16, 0, 0), ret()), false, 0);
        add(vectors, "bad-input-index", program(i(LOAD_INPUT, 0, 16, 0, 0, 0), ret()), false, 0, sixteen);
        add(vectors, "bad-jump-target", program(i(JMP, 0, 0, 0, 0, 2), ret()), false, 0);
        add(vectors, "bad-jult-target", program(i(JULT, 0, 0, 0, 0, 2), ret()), false, 0);
        add(vectors, "bad-reserved14", changed(valid, 14, 1), false, 0);
        add(vectors, "bad-reserved15", changed(valid, 15, 1), false, 0);
        NativeProgram.Program unreachable = program(constant(1, 1), ret(), ret());
        add(vectors, "invalid-unreachable", changed(unreachable, 32, 255), false, 0);
        add(vectors, "bad-hash", new NativeProgram.Program(valid.code, valid.key, valid.opcodes, valid.hash ^ 1), false, 0);
        add(vectors, "bad-key", new NativeProgram.Program(valid.code, valid.key ^ 1, valid.opcodes, valid.hash), false, 0);
        byte[] duplicate = identity(); duplicate[1] = duplicate[0];
        add(vectors, "duplicate-opcodes", new NativeProgram.Program(valid.code, valid.key, duplicate, valid.hash), false, 0);
        for (int length : new int[]{0, 1, 15, 17, 4097, 4112}) {
            byte[] code = new byte[length];
            add(vectors, "bad-size-" + length, new NativeProgram.Program(code, KEY, identity(), MethodCodec.hash(code)), false, 0);
        }
        add(vectors, "falls-off-end", program(constant(1, 1)), false, 0);
        add(vectors, "infinite-jump-budget", program(i(JMP, 0, 0, 0, 0, 0)), false, 0);
        for (int loops : new int[]{510, 511}) {
            NativeProgram.Program budget = program(constant(1, 1), constant(3, loops), i(ADD, 2, 2, 1, 0, 0),
                    i(JULT, 0, 2, 3, 0, 2), i(MOV, 0, 2, 0, 0, 0), ret());
            add(vectors, "steps-" + (4 + 2 * loops), budget, loops == 510, loops == 510 ? 510 : 0);
        }
        List<NativeProgram.Instruction> maximum = new ArrayList<>(Collections.nCopies(254, i(MOV, 0, 0, 0, 0, 0)));
        maximum.add(constant(1, 1)); maximum.add(ret());
        add(vectors, "maximum-program-256", NativeProgram.assemble(maximum, KEY, identity()), true, 0);
        int[] offsets = {1, 2, 3, 4, 12};
        int[] bits = {USES_DST, USES_A, USES_B, USES_IMM, USES_TARGET};
        for (int op = 0; op < OP_COUNT; ++op) {
            NativeProgram.Program p = program(i(op, 0, 0, 0, 0, 0), ret());
            for (int field = 0; field < offsets.length; ++field)
                if ((NativeFormats.usedFields(op) & bits[field]) == 0)
                    add(vectors, "unused-" + op + "-offset" + offsets[field], changed(p, offsets[field], 1), false, 0, 0);
        }
        for (int seed = 0; seed < 3; ++seed) {
            long build = seed == 0 ? 0 : seed == 1 ? Long.MIN_VALUE : -1;
            NativeProgram.Program root = NativeProgram.root(build, new Random(123 + seed));
            add(vectors, "root-valid-" + seed, root, true, 0x1111111111111111L,
                    1, 0x0123456789abcdefL, 0x1032547698badcfeL, build);
            for (long verified : new long[]{0, 2, Long.MIN_VALUE, -1})
                add(vectors, "root-bad-status-" + seed + "-" + Long.toUnsignedString(verified), root, false, 0,
                        verified, 7, 9, build);
            add(vectors, "root-bad-build-" + seed, root, false, 0, 1, 7, 9, build ^ 1);
            add(vectors, "root-missing-build-" + seed, root, false, 0, 1, 7, 9);
        }
        return vectors;
    }

    @Test public void frozenVectorsCoverEveryOpcodeAndFailure() {
        assertEquals(0x4e564d31, DOMAIN);
        for (Vector vector : vectors()) vector.check();
    }

    @Test public void everyByteCorruptionFailsTrustedHash() {
        NativeProgram.Program root = NativeProgram.root(77, new Random(44));
        for (int i = 0; i < root.code.length; ++i) {
            byte[] bytes = root.code.clone(); bytes[i] ^= 1;
            NativeProgram.Result result = NativeProgram.run(new NativeProgram.Program(bytes, root.key, root.opcodes, root.hash),
                    new long[]{1, 2, 3, 77});
            assertFalse(result.success); assertEquals(0, result.value);
        }
    }

    @Test public void seededArithmeticProgramsAgreeWithBigIntegerOracle() {
        Random random = new Random(0x456789);
        BigInteger mask = BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE);
        for (int test = 0; test < 80; ++test) {
            long initial = random.nextLong();
            BigInteger expected = new BigInteger(Long.toUnsignedString(initial));
            List<NativeProgram.Instruction> code = new ArrayList<>();
            code.add(constant(0, initial));
            for (int n = 0; n < 30; ++n) {
                long operand = random.nextLong();
                BigInteger value = new BigInteger(Long.toUnsignedString(operand));
                int op = new int[]{ADD, SUB, MUL, XOR, AND, OR}[random.nextInt(6)];
                code.add(constant(15, operand)); code.add(i(op, 0, 0, 15, 0, 0));
                switch (op) {
                    case ADD: expected = expected.add(value); break;
                    case SUB: expected = expected.subtract(value); break;
                    case MUL: expected = expected.multiply(value); break;
                    case XOR: expected = expected.xor(value); break;
                    case AND: expected = expected.and(value); break;
                    case OR: expected = expected.or(value); break;
                    default: fail();
                }
                expected = expected.and(mask);
            }
            code.add(constant(1, 1)); code.add(ret());
            NativeProgram.Program program = NativeProgram.assemble(code, random.nextLong(), NativeProgram.randomOpcodes(random));
            NativeProgram.Result result = NativeProgram.run(program, new long[0]);
            assertTrue(result.success); assertEquals(expected.longValue(), result.value);
        }
    }

    @Test public void rootHeaderAndRandomTableAreStableOnlyUnderExplicitTestSeed() throws Exception {
        NativeProgram.Program a = NativeProgram.root(77, new Random(19));
        NativeProgram.Program b = NativeProgram.root(77, new Random(19));
        NativeProgram.Program c = NativeProgram.root(77, new Random(20));
        assertArrayEquals(a.code, b.code); assertArrayEquals(a.opcodes, b.opcodes);
        assertFalse(Arrays.equals(a.opcodes, c.opcodes)); assertNotEquals(a.key, c.key);
        Set<Byte> unique = new HashSet<>(); for (byte op : a.opcodes) unique.add(op);
        assertEquals(OP_COUNT, unique.size());
        StringWriter header = new StringWriter(); a.writeHeader(header);
        assertTrue(header.toString().contains("#include \"NativeVm.h\""));
        assertTrue(header.toString().contains("static const NmmpNativeProgram NMMP_ROOT_PROGRAM"));
        assertTrue(header.toString().contains("NMMP_ROOT_CODE, 224,"));
        for (long data : new long[]{0, 1, Long.MIN_VALUE, -1}) for (long binding : new long[]{0, 1, Long.MAX_VALUE, -1}) {
            NativeProgram.Result result = NativeProgram.run(a, new long[]{1, data, binding, 77});
            assertTrue(result.success); assertEquals(data ^ binding, result.value);
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder text = new StringBuilder();
        for (byte value : bytes) text.append(String.format(Locale.ROOT, "%02x", value & 255));
        return text.toString();
    }

    /** Explicit test-vector export; normal JUnit runs do not write files. */
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected vector output directory");
        Path directory = Paths.get(args[0]); Files.createDirectories(directory);
        JsonObject manifest = new JsonObject();
        manifest.addProperty("format", "nmmp-native-program-v1");
        manifest.addProperty("instructionBytes", INSTRUCTION_BYTES);
        manifest.addProperty("domainHex", "4e564d31");
        JsonArray cases = new JsonArray();
        for (Vector vector : vectors()) {
            vector.check();
            JsonObject object = new JsonObject();
            object.addProperty("name", vector.name);
            object.addProperty("codeHex", hex(vector.program.code));
            object.addProperty("keyHex", String.format(Locale.ROOT, "%016x", vector.program.key));
            object.addProperty("opcodesHex", hex(vector.program.opcodes));
            object.addProperty("hashHex", String.format(Locale.ROOT, "%08x", vector.program.hash));
            JsonArray inputs = new JsonArray();
            for (long value : vector.inputs) inputs.add(String.format(Locale.ROOT, "%016x", value));
            object.add("inputHex", inputs);
            object.addProperty("success", vector.success);
            object.addProperty("outputHex", String.format(Locale.ROOT, "%016x", vector.output));
            cases.add(object);
        }
        manifest.add("vectors", cases);
        Files.write(directory.resolve("native-program-vectors.json"),
                (new GsonBuilder().setPrettyPrinting().create().toJson(manifest) + "\n").getBytes(StandardCharsets.UTF_8));
        try (java.io.Writer writer = Files.newBufferedWriter(directory.resolve("NativeProgramConfig.h"), StandardCharsets.UTF_8)) {
            NativeProgram.root(77, new Random(19)).writeHeader(writer);
        }
        System.out.println("Exported " + cases.size() + " native VM vectors to " + directory.toAbsolutePath());
    }
}

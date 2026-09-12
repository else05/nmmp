package com.nmmedit.apkprotect.dex2c;

import java.io.IOException;
import java.io.Writer;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.Random;

import static com.nmmedit.apkprotect.dex2c.NativeFormats.*;

/** Build-time generator and Java reference interpreter for the restricted native VM. */
public final class NativeProgram {
    private NativeProgram() {}

    public static final class Program {
        public final byte[] code, opcodes;
        public final long key;
        public final int hash;

        Program(byte[] code, long key, byte[] opcodes, int hash) {
            this.code = code.clone(); this.key = key;
            this.opcodes = opcodes.clone(); this.hash = hash;
        }

        public void writeHeader(Writer writer) throws IOException {
            writer.write("#ifndef NMMP_NATIVE_PROGRAM_CONFIG_H\n#define NMMP_NATIVE_PROGRAM_CONFIG_H\n"
                    + "#include <stdint.h>\n#include \"NativeVm.h\"\n\n"
                    + "static const uint8_t NMMP_ROOT_CODE[] = {\n");
            writer.write(bytes(code));
            writer.write("\n};\nstatic const NmmpNativeProgram NMMP_ROOT_PROGRAM = {\n");
            writer.write(String.format(Locale.ROOT, "    NMMP_ROOT_CODE, %d, UINT64_C(0x%016x),\n    {%s}, UINT32_C(0x%08x)\n};\n#endif\n",
                    code.length, key, bytes(opcodes), hash));
        }
    }

    public static final class Result {
        public final boolean success;
        public final long value;
        private Result(boolean success, long value) { this.success = success; this.value = success ? value : 0; }
    }

    public static final class Instruction {
        final int op, dst, a, b, target;
        final long immediate;
        public Instruction(int op, int dst, int a, int b, long immediate, int target) {
            this.op = op; this.dst = dst; this.a = a; this.b = b;
            this.immediate = immediate; this.target = target;
        }
    }

    public static Program root(long buildId) { return root(buildId, new SecureRandom()); }

    static Program root(long buildId, Random random) {
        byte[] opcodes = randomOpcodes(random);
        Program program = assemble(Arrays.asList(
                new Instruction(LOAD_INPUT, 2, 0, 0, 0, 0),
                new Instruction(CONST, 3, 0, 0, 1, 0),
                new Instruction(JULT, 0, 2, 3, 0, 13),
                new Instruction(JULT, 0, 3, 2, 0, 13),
                new Instruction(LOAD_INPUT, 2, 3, 0, 0, 0),
                new Instruction(CONST, 3, 0, 0, buildId, 0),
                new Instruction(JULT, 0, 2, 3, 0, 13),
                new Instruction(JULT, 0, 3, 2, 0, 13),
                new Instruction(LOAD_INPUT, 2, 1, 0, 0, 0),
                new Instruction(LOAD_INPUT, 3, 2, 0, 0, 0),
                new Instruction(XOR, 0, 2, 3, 0, 0),
                new Instruction(CONST, 1, 0, 0, 1, 0),
                new Instruction(RETURN, 0, 0, 0, 0, 0),
                new Instruction(RETURN, 0, 0, 0, 0, 0)), random.nextLong(), opcodes);
        Result check = run(program, new long[]{1, 0x0123456789abcdefL, 0xfedcba9876543210L, buildId});
        if (!check.success || check.value != -1L
                || run(program, new long[]{0, 0, 0, buildId}).success
                || run(program, new long[]{1, 0, 0, buildId ^ 1}).success)
            throw new IllegalStateException("Native root program failed reference verification");
        return program;
    }

    static byte[] randomOpcodes(Random random) {
        byte[] bytes = new byte[256];
        for (int i = 0; i < bytes.length; ++i) bytes[i] = (byte)i;
        for (int i = bytes.length - 1; i > 0; --i) {
            int j = random.nextInt(i + 1);
            byte value = bytes[i]; bytes[i] = bytes[j]; bytes[j] = value;
        }
        return Arrays.copyOf(bytes, OP_COUNT);
    }

    public static Program assemble(List<Instruction> instructions, long key, byte[] opcodes) {
        if (instructions.isEmpty() || instructions.size() > MAX_INSTRUCTIONS || opcodes.length != OP_COUNT)
            throw new IllegalArgumentException("Invalid native program dimensions");
        ByteBuffer plain = ByteBuffer.allocate(instructions.size() * INSTRUCTION_BYTES).order(ByteOrder.LITTLE_ENDIAN);
        for (Instruction i : instructions) {
            if (i.op < 0 || i.op >= OP_COUNT || i.dst < 0 || i.dst > 255 || i.a < 0 || i.a > 255
                    || i.b < 0 || i.b > 255 || i.target < 0 || i.target > 65535)
                throw new IllegalArgumentException("Native instruction field does not fit format");
            plain.put(opcodes[i.op]).put((byte)i.dst).put((byte)i.a).put((byte)i.b);
            plain.putLong(i.immediate).putShort((short)i.target).putShort((short)0);
        }
        byte[] encoded = new MethodCodec(key).transform(plain.array(), 0, DOMAIN);
        Arrays.fill(plain.array(), (byte)0);
        return new Program(encoded, key, opcodes, MethodCodec.hash(encoded));
    }

    public static Result run(Program program, long[] inputs) {
        Result failure = new Result(false, 0);
        if (program == null || inputs == null || inputs.length > REGISTERS
                || program.code.length == 0 || program.code.length % INSTRUCTION_BYTES != 0
                || program.code.length / INSTRUCTION_BYTES > MAX_INSTRUCTIONS
                || program.opcodes.length != OP_COUNT || MethodCodec.hash(program.code) != program.hash) return failure;
        int[] semantics = new int[256];
        Arrays.fill(semantics, -1);
        for (int i = 0; i < OP_COUNT; ++i) {
            int stored = program.opcodes[i] & 255;
            if (semantics[stored] != -1) return failure;
            semantics[stored] = i;
        }
        byte[] instruction = new byte[INSTRUCTION_BYTES];
        long[] registers = new long[REGISTERS];
        int count = program.code.length / INSTRUCTION_BYTES;
        try {
            // Validate even unreachable instructions, without retaining decoded instructions.
            for (int pc = 0; pc < count; ++pc) {
                decode(program, pc, instruction);
                if (!valid(instruction, semantics[instruction[OFFSET_OP] & 255], count, inputs.length)) return failure;
            }
            int pc = 0;
            for (int steps = 0; steps < MAX_STEPS; ++steps) {
                if (pc < 0 || pc >= count) return failure;
                decode(program, pc, instruction);
                int op = semantics[instruction[OFFSET_OP] & 255];
                int dst = instruction[OFFSET_DST] & 255, a = instruction[OFFSET_A] & 255, b = instruction[OFFSET_B] & 255;
                int next = pc + 1;
                switch (op) {
                    case LOAD_INPUT: registers[dst] = inputs[a]; break;
                    case CONST: registers[dst] = immediate(instruction); break;
                    case MOV: registers[dst] = registers[a]; break;
                    case XOR: registers[dst] = registers[a] ^ registers[b]; break;
                    case AND: registers[dst] = registers[a] & registers[b]; break;
                    case OR: registers[dst] = registers[a] | registers[b]; break;
                    case ADD: registers[dst] = registers[a] + registers[b]; break;
                    case SUB: registers[dst] = registers[a] - registers[b]; break;
                    case MUL: registers[dst] = registers[a] * registers[b]; break;
                    case SHL:
                    case SHR:
                        if (Long.compareUnsigned(registers[b], 63) > 0) return failure;
                        registers[dst] = op == SHL ? registers[a] << registers[b] : registers[a] >>> registers[b];
                        break;
                    case JULT: if (Long.compareUnsigned(registers[a], registers[b]) < 0) next = target(instruction); break;
                    case JMP: next = target(instruction); break;
                    case RETURN: return new Result(registers[STATUS_REGISTER] == SUCCESS_STATUS, registers[VALUE_REGISTER]);
                    default: return failure;
                }
                pc = next;
            }
            return failure;
        } finally {
            Arrays.fill(instruction, (byte)0);
            Arrays.fill(registers, 0);
        }
    }

    private static boolean valid(byte[] i, int op, int count, int inputs) {
        int mask = NativeFormats.usedFields(op);
        if (mask < 0 || i[OFFSET_RESERVED] != 0 || i[OFFSET_RESERVED + 1] != 0) return false;
        int dst = i[OFFSET_DST] & 255, a = i[OFFSET_A] & 255, b = i[OFFSET_B] & 255;
        if ((mask & USES_DST) == 0 ? dst != 0 : dst >= REGISTERS) return false;
        if ((mask & USES_A) == 0 ? a != 0 : a >= (op == LOAD_INPUT ? inputs : REGISTERS)) return false;
        if ((mask & USES_B) == 0 ? b != 0 : b >= REGISTERS) return false;
        if ((mask & USES_IMM) == 0 && immediate(i) != 0) return false;
        return (mask & USES_TARGET) == 0 ? target(i) == 0 : target(i) < count;
    }

    private static long immediate(byte[] bytes) {
        long value = 0;
        for (int b = 0; b < 8; ++b) value |= (bytes[OFFSET_IMM + b] & 255L) << (8 * b);
        return value;
    }

    private static int target(byte[] bytes) {
        return (bytes[OFFSET_TARGET] & 255) | ((bytes[OFFSET_TARGET + 1] & 255) << 8);
    }

    private static void decode(Program program, int pc, byte[] out) {
        for (int byteIndex = 0; byteIndex < INSTRUCTION_BYTES; ++byteIndex) {
            int position = pc * INSTRUCTION_BYTES + byteIndex;
            long key = program.key ^ (long)DOMAIN * 0xd6e8feb86659fd93L ^ (position / 8);
            key = (key ^ (key >>> 30)) * 0xbf58476d1ce4e5b9L;
            key = (key ^ (key >>> 27)) * 0x94d049bb133111ebL;
            key ^= key >>> 31;
            out[byteIndex] = (byte)(program.code[position] ^ (byte)(key >>> ((position & 7) * 8)));
        }
    }

    private static String bytes(byte[] data) {
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < data.length; ++i) {
            if (i != 0) result.append(", ");
            if (i != 0 && i % INSTRUCTION_BYTES == 0) result.append('\n');
            result.append(String.format(Locale.ROOT, "0x%02x", data[i] & 255));
        }
        return result.toString();
    }
}

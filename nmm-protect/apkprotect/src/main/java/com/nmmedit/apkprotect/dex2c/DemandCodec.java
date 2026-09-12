package com.nmmedit.apkprotect.dex2c;

import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.dexlib2.iface.instruction.OffsetInstruction;
import com.android.tools.smali.dexlib2.iface.instruction.SwitchPayload;
import com.android.tools.smali.dexlib2.iface.instruction.SwitchElement;
import com.android.tools.smali.dexlib2.iface.TryBlock;
import com.android.tools.smali.dexlib2.iface.ExceptionHandler;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;

/** Codec 3: all complete decoding in this class is offline build verification. */
public final class DemandCodec {
    public static final long ID_MIX = 0x9e3779b97f4a7c15L;
    private DemandCodec() {}
    public static long mix(long x) {
        x = (x ^ (x >>> 30)) * 0xbf58476d1ce4e5b9L;
        x = (x ^ (x >>> 27)) * 0x94d049bb133111ebL;
        return x ^ (x >>> 31);
    }
    public static String descriptor(Method method) {
        StringBuilder s = new StringBuilder(method.getDefiningClass()).append("->")
                .append(method.getName()).append('(');
        for (CharSequence p : method.getParameterTypes()) s.append(p);
        return s.append(')').append(method.getReturnType()).toString();
    }
    public static long tag(String descriptor) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(descriptor.getBytes(StandardCharsets.UTF_8));
            long result = 0;
            for (int i = 0; i < 8; ++i) result |= (digest[i] & 255L) << (8 * i);
            return result;
        } catch (NoSuchAlgorithmException e) { throw new AssertionError(e); }
    }
    public static long seed(long root, long id, long tag, int registers, int ins) {
        return mix(root ^ id * ID_MIX ^ tag ^ ((long) registers << 32) ^ Integer.toUnsignedLong(ins));
    }
    public static int contextHash(long root, long id, long tag, int registers, int ins,
                                  byte[] code, byte[] tries, byte[] boundaries) {
        long seed = seed(root, id, tag, registers, ins);
        int[] fields = {3, (int)id, (int)tag, (int)(tag >>> 32), registers, ins,
                code.length, tries.length, boundaries.length, MethodCodec.hash(code),
                MethodCodec.hash(tries), MethodCodec.hash(boundaries), (int)seed, (int)(seed >>> 32)};
        int hash = 0x811c9dc5;
        for (int field : fields) for (int b = 0; b < 4; ++b) {
            hash ^= (field >>> (b * 8)) & 255; hash *= 0x01000193;
        }
        return hash;
    }
    public static final class Encoded {
        public final byte[] code, tries, boundaries;
        public final long descriptorTag;
        Encoded(byte[] code, byte[] tries, byte[] boundaries, long tag) {
            this.code = code; this.tries = tries; this.boundaries = boundaries; this.descriptorTag = tag;
        }
    }
    public static int kind(byte[] boundaries, int pc) {
        return (boundaries[pc >>> 1] >>> ((pc & 1) * 4)) & 15;
    }
    private static void putKind(byte[] boundaries, int pc, int kind) {
        boundaries[pc >>> 1] |= (byte)(kind << ((pc & 1) * 4));
    }
    private static void require(boolean condition, String descriptor, String error) {
        if (!condition) throw new IllegalArgumentException(descriptor + ": " + error);
    }
    private static boolean target(byte[] boundaries, int units, long pc) {
        return pc >= 0 && pc < units && kind(boundaries, (int)pc) == ReaderFormats.FETCH;
    }
    public static Encoded encode(Method method, byte[] plain, byte[] tries, long root, long id, int ins) {
        String descriptor = descriptor(method);
        require(plain.length > 0 && (plain.length & 1) == 0, descriptor, "invalid code length");
        require(method.getImplementation().getRegisterCount() >= ins && method.getImplementation().getRegisterCount() <= 65535, descriptor, "invalid register layout");
        int units = plain.length / 2;
        byte[] boundaries = new byte[(units + 1) / 2];
        List<Instruction> instructions = new ArrayList<>();
        for (Instruction i : method.getImplementation().getInstructions()) instructions.add(i);
        Map<Integer, Instruction> starts = new HashMap<>();
        int pc = 0;
        for (int index = 0; index < instructions.size(); ++index) {
            Instruction i = instructions.get(index);
            require(!ReaderFormats.unsupported(i.getOpcode().name()), descriptor, "Unsupported opcode: " + i.getOpcode().name());
            int[] recipe;
            try { recipe = ReaderFormats.recipe(i.getOpcode().format.name()); }
            catch (IllegalArgumentException e) { throw new IllegalArgumentException(descriptor + ": " + e.getMessage(), e); }
            int width = i.getCodeUnits();
            require(width > 0 && width <= units - pc, descriptor, "rewriter length mismatch");
            require(recipe[0] == 0 || recipe[0] == width, descriptor, "format width mismatch");
            boolean payload = recipe[0] == 0;
            boolean padding = i.getOpcode() == Opcode.NOP && (pc & 1) != 0 && index + 1 < instructions.size()
                    && instructions.get(index + 1).getOpcode().format.isPayloadFormat;
            if (payload) require((pc & 1) == 0, descriptor, "unaligned payload");
            starts.put(pc, i);
            for (int word = 0; word < width; ++word) {
                int domain = padding ? 0 : payload ? ReaderFormats.PAYLOAD : word == 0 ? ReaderFormats.FETCH
                        : ReaderFormats.wide(i.getOpcode().name()) ? ReaderFormats.WIDE : ReaderFormats.OPERAND;
                putKind(boundaries, pc + word, domain);
            }
            pc += width;
        }
        require(pc == units && target(boundaries, units, 0), descriptor, "incomplete method layout");
        for (Map.Entry<Integer, Instruction> entry : starts.entrySet()) {
            Instruction i = entry.getValue(); pc = entry.getKey();
            if (i instanceof OffsetInstruction) {
                long dest = (long)pc + ((OffsetInstruction)i).getCodeOffset();
                if (i.getOpcode().format.name().equals("Format31t")) {
                    require(dest >= 0 && dest < units, descriptor, "payload offset out of range");
                    Instruction payload = starts.get((int)dest);
                    String expected = i.getOpcode() == Opcode.PACKED_SWITCH ? "PackedSwitchPayload"
                            : i.getOpcode() == Opcode.SPARSE_SWITCH ? "SparseSwitchPayload" : "ArrayPayload";
                    require(payload != null && payload.getOpcode().format.name().equals(expected), descriptor, "payload type mismatch");
                    if (payload instanceof SwitchPayload)
                        for (SwitchElement element : ((SwitchPayload)payload).getSwitchElements())
                            require(target(boundaries, units, (long)pc + element.getOffset()), descriptor, "switch target is not an instruction");
                } else require(target(boundaries, units, dest), descriptor, "branch target is not an instruction");
            }
        }
        for (TryBlock<? extends ExceptionHandler> block : method.getImplementation().getTryBlocks()) {
            long end = (long)block.getStartCodeAddress() + block.getCodeUnitCount();
            require(target(boundaries, units, block.getStartCodeAddress()) && end <= units,
                    descriptor, "try interval out of range");
            for (ExceptionHandler handler : block.getExceptionHandlers())
                require(target(boundaries, units, handler.getHandlerCodeAddress()), descriptor, "catch target is not an instruction");
        }
        long tag = tag(descriptor);
        long methodSeed = seed(root, id, tag, method.getImplementation().getRegisterCount(), ins);
        byte[] encoded = transformCode(plain, methodSeed, boundaries);
        MethodCodec codec = new MethodCodec(methodSeed);
        byte[] encodedTries = codec.transform(tries, 0, ReaderFormats.TRIES);
        require(Arrays.equals(plain, transformCode(encoded, methodSeed, boundaries)), descriptor, "code roundtrip mismatch");
        require(Arrays.equals(tries, codec.transform(encodedTries, 0, ReaderFormats.TRIES)), descriptor, "tries roundtrip mismatch");
        return new Encoded(encoded, encodedTries, boundaries, tag);
    }
    public static byte[] transformCode(byte[] source, long seed, byte[] boundaries) {
        byte[] output = source.clone();
        // Build-time only: independently materialize each domain, then select the matching byte.
        MethodCodec codec = new MethodCodec(seed);
        for (int domain = ReaderFormats.FETCH; domain <= ReaderFormats.PAYLOAD; ++domain) {
            byte[] transformed = codec.transform(source, 0, domain);
            for (int pos = 0; pos < source.length; ++pos) {
                int kind = kind(boundaries, pos / 2);
                if (kind == 0) kind = ReaderFormats.PAYLOAD;
                if (kind == domain) output[pos] = transformed[pos];
            }
        }
        return output;
    }
}

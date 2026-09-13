package com.nmmedit.apkprotect.dex2c;

import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.immutable.ImmutableClassDef;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.android.tools.smali.dexlib2.writer.io.MemoryDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.JniCodeGenerator;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.NoneInstructionRewriter;
import org.junit.Test;

import java.io.File;
import java.io.StringWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Locale;
import java.util.Random;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.Assert.*;

public class DemandModuleTest {
    private static final long ROOT = 0xfedcba9876543210L;
    private static final long MODULE = 0x80000001L;
    private static final long BUILD = 0x8877665544332211L;
    private static ByteBuffer le(byte[] bytes) { return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN); }

    private static byte[] plain() {
        byte[] bytes = new byte[144];
        for (int i = 0; i < bytes.length; ++i) bytes[i] = (byte)(i * 37 + 17);
        bytes[0] = 0; // NOP is fixed; FETCH high bytes and payload signatures must remain untouched.
        return bytes;
    }

    private static byte[] boundaries() {
        byte[] bytes = new byte[36];
        int[] domains = {1, 3, 3, 3, 3, 1, 2, 2, 0, 4, 4, 4};
        for (int pc = 0; pc < 72; ++pc) bytes[pc / 2] |= domains[pc % domains.length] << ((pc & 1) * 4);
        return bytes;
    }

    private static DemandCodec.Encoded fixture(long id) {
        long tag = DemandCodec.tag("LTest;->m" + id + "(JI)V");
        long seed = DemandCodec.seed(ROOT, id, tag, 9, 3);
        return new DemandCodec.Encoded(DemandCodec.transformCode(plain(), seed, boundaries()),
                new MethodCodec(seed).transform(new byte[]{0, 1, (byte)0x80, (byte)0xff, 3}, 0, ReaderFormats.TRIES),
                boundaries(), tag);
    }

    // Independent byte-at-a-time decoder; does not call DemandCodec.transformCode or MethodCodec.transform.
    private static byte[] decode(byte[] source, long seed, long id, int domain) {
        byte[] result = source.clone();
        for (int i = 0; i < result.length; ++i) {
            long x = seed ^ id * 0x9e3779b97f4a7c15L ^ (long)domain * 0xd6e8feb86659fd93L ^ (i / 8);
            x = (x ^ (x >>> 30)) * 0xbf58476d1ce4e5b9L;
            x = (x ^ (x >>> 27)) * 0x94d049bb133111ebL;
            x ^= x >>> 31;
            result[i] ^= (byte)(x >>> ((i & 7) * 8));
        }
        return result;
    }

    private static int fnv(byte[] bytes, int size) {
        int hash = 0x811c9dc5;
        for (int i = 0; i < size; ++i) hash = (hash ^ (bytes[i] & 255)) * 0x01000193;
        return hash;
    }

    @Test public void frozenLayoutUnsignedDirectoryAndEveryMethodRoundtrip() {
        DemandModule module = new DemandModule(ROOT, MODULE, BUILD, new Random(1234));
        Map<Long, Long> ids = new HashMap<>();
        for (long id = 0x80000000L; id < 0x80000020L; ++id) ids.put(module.add(id, 9, 3, fixture(id)), id);
        DemandModule.Built built = module.build();
        byte[] blob = built.blob;
        ByteBuffer h = le(blob);
        assertEquals("NMMPOD03", new String(blob, 0, 8, StandardCharsets.US_ASCII));
        assertEquals(3, h.getInt(8)); assertEquals(4, h.getInt(12));
        assertEquals(MODULE, Integer.toUnsignedLong(h.getInt(16)));
        assertEquals(32, h.getInt(20)); assertEquals(64, h.getInt(24));
        assertEquals(64 + 32 * 64, h.getInt(28));
        assertEquals(h.getInt(28) + 1024, h.getInt(32));
        assertEquals(32 * 36, h.getInt(36));
        assertEquals(h.getInt(32) + h.getInt(36), h.getInt(40));
        assertEquals(blob.length, h.getInt(44)); assertEquals(BUILD, h.getLong(48));
        assertEquals(0, h.getInt(56)); assertEquals(0, h.getInt(60));
        assertEquals(fnv(blob, blob.length), built.hash);
        assertEquals(MODULE, built.moduleId); assertEquals(BUILD, built.buildId);
        byte[] maps = decode(Arrays.copyOfRange(blob, h.getInt(28), h.getInt(32)), ROOT, MODULE, 7);
        byte[] bounds = decode(Arrays.copyOfRange(blob, h.getInt(32), h.getInt(40)), ROOT, MODULE, 8);
        for (int row = 0; row < 4; ++row) {
            Set<Byte> values = new HashSet<>();
            for (int i = 0; i < 256; ++i) values.add(maps[row * 256 + i]);
            assertEquals(256, values.size()); assertEquals(0, maps[row * 256]);
            for (int previous = 0; previous < row; ++previous)
                assertFalse(Arrays.equals(Arrays.copyOfRange(maps, previous * 256, (previous + 1) * 256),
                        Arrays.copyOfRange(maps, row * 256, (row + 1) * 256)));
        }
        Set<Integer> recordOffsets = new HashSet<>(), rows = new HashSet<>();
        Map<Integer, Integer> dataRegions = new TreeMap<>(), boundaryRegions = new TreeMap<>();
        long previous = -1;
        boolean reordered = false, highToken = false;
        for (int i = 0; i < 32; ++i) {
            long token = Integer.toUnsignedLong(h.getInt(64 + i * 8));
            assertTrue(token > previous); previous = token;
            highToken |= token >= 0x80000000L;
            int offset = h.getInt(68 + i * 8);
            assertTrue(recordOffsets.add(offset));
            assertTrue(offset >= 64 + 32 * 8 && offset + 56 <= h.getInt(28));
            assertEquals(0, (offset - 64 - 32 * 8) % 56);
            reordered |= offset != 64 + 32 * 8 + i * 56;
            byte[] record = decode(Arrays.copyOfRange(blob, offset, offset + 56), ROOT, token, 6);
            ByteBuffer r = le(record);
            long id = Integer.toUnsignedLong(r.getInt(0));
            assertEquals(ids.get(token).longValue(), id);
            assertEquals(fixture(id).descriptorTag, r.getLong(4));
            assertEquals(9, r.getInt(12)); assertEquals(3, r.getInt(16));
            assertEquals(3, r.getInt(48)); assertEquals(fnv(record, 52), r.getInt(52));
            long seed = DemandCodec.seed(ROOT, id, r.getLong(4), 9, 3);
            int row = r.getInt(44); rows.add(row); assertEquals(seed & 3, row);
            byte[] bound = Arrays.copyOfRange(bounds, r.getInt(36), r.getInt(36) + r.getInt(40));
            assertArrayEquals(boundaries(), bound);
            assertEquals(144, r.getInt(24)); assertEquals(5, r.getInt(32));
            assertNull(dataRegions.put(r.getInt(20), r.getInt(24)));
            assertNull(dataRegions.put(r.getInt(28), r.getInt(32)));
            assertNull(boundaryRegions.put(r.getInt(36), r.getInt(40)));
            byte[] code = Arrays.copyOfRange(blob, r.getInt(20), r.getInt(20) + r.getInt(24));
            byte[][] domains = new byte[5][];
            for (int domain = 1; domain <= 4; ++domain) domains[domain] = decode(code, seed, 0, domain);
            for (int pos = 0; pos < code.length; ++pos) {
                int kind = (bound[pos / 4] >>> (((pos / 2) & 1) * 4)) & 15;
                if (kind == 0) kind = 4;
                code[pos] = domains[kind][pos];
                if (kind == 1 && (pos & 1) == 0) code[pos] = maps[row * 256 + (code[pos] & 255)];
            }
            assertArrayEquals(plain(), code);
            assertArrayEquals(new byte[]{0, 1, (byte)0x80, (byte)0xff, 3}, decode(
                    Arrays.copyOfRange(blob, r.getInt(28), r.getInt(28) + r.getInt(32)), seed, 0, 5));
        }
        assertEquals(4, rows.size()); assertTrue(reordered); assertTrue(highToken);
        int cursor = h.getInt(40);
        for (Map.Entry<Integer, Integer> region : dataRegions.entrySet()) {
            assertEquals(cursor, region.getKey().intValue()); cursor += region.getValue();
        }
        assertEquals(blob.length, cursor);
        cursor = 0;
        for (Map.Entry<Integer, Integer> region : boundaryRegions.entrySet()) {
            assertEquals(cursor, region.getKey().intValue()); cursor += region.getValue();
        }
        assertEquals(h.getInt(36), cursor);
    }

    @Test public void tokenCollisionRetriesAndPreservesEntireUnsignedRange() {
        Random scripted = new Random(91) {
            private final int[] tokens = {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MAX_VALUE, -1, 0};
            private int cursor;
            @Override public int nextInt() { return tokens[cursor++]; }
        };
        DemandModule module = new DemandModule(ROOT, 0, 0, scripted);
        assertEquals(0x80000000L, module.add(0, 9, 3, fixture(0)));
        assertEquals(0x7fffffffL, module.add(1, 9, 3, fixture(1)));
        assertEquals(0xffffffffL, module.add(2, 9, 3, fixture(2)));
        assertEquals(0, module.add(3, 9, 3, fixture(3)));
        ByteBuffer blob = le(module.build().blob);
        long[] expected = {0, 0x7fffffffL, 0x80000000L, 0xffffffffL};
        for (int i = 0; i < 4; ++i) assertEquals(expected[i], Integer.toUnsignedLong(blob.getInt(64 + i * 8)));
    }

    @Test public void randomizationChangesStructureAndInputsAreOwned() {
        DemandCodec.Encoded input = fixture(0);
        DemandModule first = new DemandModule(ROOT, MODULE, BUILD, new Random(1));
        DemandModule same = new DemandModule(ROOT, MODULE, BUILD, new Random(1));
        DemandModule different = new DemandModule(ROOT, MODULE, BUILD, new Random(2));
        first.add(0, 9, 3, input); same.add(0, 9, 3, input); different.add(0, 9, 3, input);
        input.code[0] ^= 1; input.tries[0] ^= 1; input.boundaries[0] ^= 1;
        byte[] blob = first.build().blob;
        assertArrayEquals(blob, same.build().blob);
        assertFalse(Arrays.equals(blob, different.build().blob));
        assertThrows(IllegalArgumentException.class, () -> first.add(0, 9, 3, fixture(0)));
        assertThrows(IllegalArgumentException.class, () -> new DemandModule(ROOT, 0x100000000L, BUILD));
    }

    @Test public void emptyModuleHasValidFixedSections() {
        ByteBuffer blob = le(new DemandModule(ROOT, MODULE, BUILD, new Random(1)).build().blob);
        assertEquals(0, blob.getInt(20)); assertEquals(64, blob.getInt(28));
        assertEquals(1088, blob.getInt(32)); assertEquals(0, blob.getInt(36));
        assertEquals(1088, blob.getInt(40)); assertEquals(1088, blob.getInt(44));
    }

    private static DemandModule.Built fixedVector() {
        long root = 0x0123456789abcdefL;
        long tag = DemandCodec.tag("LModuleVector;->run()V");
        long seed = DemandCodec.seed(root, 1, tag, 2, 0);
        DemandModule module = new DemandModule(root, 9, 77, new Random(0x5343) {
            @Override public int nextInt() { return 0x89abcdef; }
        });
        module.add(1, 2, 0, new DemandCodec.Encoded(
                DemandCodec.transformCode(new byte[]{0x34, 0x12, (byte)0xcd, (byte)0xab}, seed, new byte[]{0x21}),
                new byte[0], new byte[]{0x21}, tag));
        return module.build();
    }

    @Test public void fixedNativeReplayVector() {
        byte[] blob = fixedVector().blob;
        assertArrayEquals(blob, fixedVector().blob);
        ByteBuffer h = le(blob);
        assertEquals(0x89abcdefL, Integer.toUnsignedLong(h.getInt(64)));
        ByteBuffer record = le(decode(Arrays.copyOfRange(blob, h.getInt(68), h.getInt(68) + 56),
                0x0123456789abcdefL, 0x89abcdefL, 6));
        assertEquals(1, record.getInt(0)); assertEquals(2, record.getInt(12));
        assertEquals(0, record.getInt(16)); assertEquals(4, record.getInt(24));
        assertEquals(0, record.getInt(32)); assertEquals(1, record.getInt(40));
    }

    /** Explicit export for the native replay test; ordinary JUnit runs do not write files. */
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected vector output directory");
        DemandModule.Built built = fixedVector();
        long tag = DemandCodec.tag("LModuleVector;->run()V");
        long seed = DemandCodec.seed(0x0123456789abcdefL, 1, tag, 2, 0);
        JsonObject json = new JsonObject();
        json.addProperty("format", "NMMPOD03");
        json.addProperty("blob", "module-vector.bin");
        json.addProperty("rootHex", "0123456789abcdef");
        json.addProperty("moduleId", 9);
        json.addProperty("buildIdHex", "000000000000004d");
        json.addProperty("tokenHex", "89abcdef");
        json.addProperty("methodId", 1);
        json.addProperty("descriptor", "LModuleVector;->run()V");
        json.addProperty("descriptorTagHex", String.format(Locale.ROOT, "%016x", tag));
        json.addProperty("methodSeedHex", String.format(Locale.ROOT, "%016x", seed));
        json.addProperty("registers", 2);
        json.addProperty("ins", 0);
        json.addProperty("row", seed & 3);
        json.addProperty("plainCodeHex", "3412cdab");
        json.addProperty("plainTriesHex", "");
        json.addProperty("boundariesHex", "21");
        json.addProperty("blobSize", built.blob.length);
        json.addProperty("blobFnv1aHex", String.format(Locale.ROOT, "%08x", built.hash));
        Path directory = Paths.get(args[0]);
        Files.createDirectories(directory);
        Files.write(directory.resolve("module-vector.bin"), built.blob);
        Files.write(directory.resolve("module-vector.json"),
                (new GsonBuilder().setPrettyPrinting().create().toJson(json) + "\n").getBytes(StandardCharsets.UTF_8));
        System.out.println(directory.resolve("module-vector.json").toAbsolutePath());
    }

    @Test public void generatorOnlyUsesTokenModule() throws Exception {
        DexPool pool = new DexPool(Opcodes.forApi(26));
        ImmutableMethod method = new ImmutableMethod("LTest;", "run", Collections.emptyList(), "V", 9,
                Collections.emptySet(), Collections.emptySet(), new ImmutableMethodImplementation(0,
                Collections.singletonList(new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                Collections.emptyList(), Collections.emptyList()));
        pool.internClass(new ImmutableClassDef("LTest;", 1, "Ljava/lang/Object;", Collections.emptyList(),
                null, Collections.emptyList(), Collections.emptyList(), Collections.singletonList(method)));
        MemoryDataStore store = new MemoryDataStore();
        pool.writeTo(store);
        DexBackedDexFile dex = new DexBackedDexFile(Opcodes.forApi(26), store.getData());
        ClassAnalyzer analyzer = new ClassAnalyzer(); analyzer.loadDexFile(dex);
        StringWriter source = new StringWriter();
        JniCodeGenerator generator = new JniCodeGenerator(dex, analyzer, new NoneInstructionRewriter(),
                new ProtectionContext(ROOT), MODULE);
        generator.generate(new DexConfig(new File("."), "classes.dex"), new StringWriter(), source);
        String code = source.toString();
        assertFalse(code.contains("vmExecute(env,"));
        assertFalse(code.contains("vmEncodedCode"));
        assertTrue(code.indexOf("static vmDemandModule nmmpModule;") < code.indexOf("vmExecuteToken("));
        assertTrue(code.contains("vmPrepareDemandModule(env, &nmmpModule)"));
        assertTrue(code.contains("NMMP_DEMAND_MODULE_INIT(nmmpModuleBlob,"));
        assertTrue(code.contains("UINT32_C(0x80000001)"));
        assertFalse(code.contains("vmExecuteDemand(")); assertFalse(code.contains("static vmDemandCode"));
        assertFalse(code.contains("encodedInsns")); assertFalse(code.contains("nmmpDemand_"));
        Matcher token = Pattern.compile("vmExecuteToken\\(env, &nmmpModule, UINT32_C\\(0x([0-9a-f]{8})\\)").matcher(code);
        assertTrue(token.find());
        long wrapperToken = Long.parseLong(token.group(1), 16);
        String array = code.substring(code.indexOf("static const u1 nmmpModuleBlob[] = {"));
        array = array.substring(0, array.indexOf("};"));
        Matcher bytes = Pattern.compile("0x([0-9a-f]{2}),").matcher(array);
        java.io.ByteArrayOutputStream blob = new java.io.ByteArrayOutputStream();
        while (bytes.find()) blob.write(Integer.parseInt(bytes.group(1), 16));
        byte[] moduleBlob = blob.toByteArray();
        ByteBuffer header = le(moduleBlob);
        assertEquals(wrapperToken, Integer.toUnsignedLong(header.getInt(64)));
        int recordOffset = header.getInt(68);
        ByteBuffer record = le(decode(Arrays.copyOfRange(moduleBlob, recordOffset, recordOffset + 56), ROOT, wrapperToken, 6));
        assertEquals(header.getInt(40), record.getInt(20));
        assertEquals(0, record.getInt(32));
        assertEquals(moduleBlob.length, record.getInt(28));
        assertEquals(record.getInt(20) + record.getInt(24), record.getInt(28));
        assertTrue(code.contains(String.format("UINT32_C(0x%08x)", fnv(moduleBlob, moduleBlob.length))));
    }
}

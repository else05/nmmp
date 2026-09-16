package com.nmmedit.apkprotect.dex2c;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;

/** Builds and verifies the VMOD0003 module. All complete decoding is build-time only. */
public final class DemandModule {
    public static final int HEADER_SIZE = 64;
    public static final int RECORD_SIZE = 56;
    public static final int MAP_SIZE = 1024;
    private final long root, moduleId, buildId;
    private final Random random;
    private final byte[] rows = new byte[MAP_SIZE];
    private final List<Entry> entries = new ArrayList<>();
    private final Set<Long> tokens = new HashSet<>();
    private final Set<Long> methodIds = new HashSet<>();

    public DemandModule(long root, long moduleId, long buildId) {
        this(root, moduleId, buildId, GeneratorRandom.create("demand", moduleId));
    }

    // Deterministic randomness is only exposed to tests in this package.
    DemandModule(long root, long moduleId, long buildId, Random random) {
        requireU32(moduleId);
        this.root = root;
        this.moduleId = moduleId;
        this.buildId = buildId;
        this.random = random;
        for (int row = 0; row < 4; ++row) {
            int base = row * 256;
            for (int i = 0; i < 256; ++i) rows[base + i] = (byte)i;
            for (int i = 255; i > 1; --i) {
                int j = 1 + random.nextInt(i);
                byte value = rows[base + i];
                rows[base + i] = rows[base + j];
                rows[base + j] = value;
            }
        }
    }

    public long add(long methodId, int registers, int ins, DemandCodec.Encoded encoded) {
        requireU32(methodId);
        if (registers < 0 || registers > 65535 || ins < 0 || ins > registers
                || encoded.code.length == 0 || (encoded.code.length & 1) != 0
                || encoded.boundaries.length != (encoded.code.length + 3L) / 4)
            throw new IllegalArgumentException("Invalid demand method layout: " + methodId);
        if (!methodIds.add(methodId)) throw new IllegalArgumentException("Duplicate method ID: " + methodId);
        long token;
        do { token = Integer.toUnsignedLong(random.nextInt()); } while (!tokens.add(token));
        entries.add(new Entry(methodId, token, registers, ins, encoded));
        return token;
    }

    public static final class Built {
        public final byte[] blob;
        public final int hash;
        public final long moduleId, buildId;

        private Built(byte[] blob, long moduleId, long buildId) {
            this.blob = blob;
            this.hash = MethodCodec.hash(blob);
            this.moduleId = moduleId;
            this.buildId = buildId;
        }
    }

    public Built build() {
        int mapOff = size(HEADER_SIZE + (8L + RECORD_SIZE) * entries.size());
        int boundaryOff = size((long)mapOff + MAP_SIZE);
        long boundaryBytes = 0, dataBytes = 0;
        for (Entry entry : entries) {
            boundaryBytes += entry.encoded.boundaries.length;
            dataBytes += (long)entry.encoded.code.length + entry.encoded.tries.length;
        }
        int dataOff = size(boundaryOff + boundaryBytes);
        byte[] blob = new byte[size(dataOff + dataBytes)];
        ByteBuffer out = ByteBuffer.wrap(blob).order(ByteOrder.LITTLE_ENDIAN);
        out.put("VMOD0003".getBytes(StandardCharsets.US_ASCII));
        out.putInt(3).putInt(4).putInt((int)moduleId).putInt(entries.size());
        out.putInt(HEADER_SIZE).putInt(mapOff).putInt(boundaryOff).putInt(size(boundaryBytes));
        out.putInt(dataOff).putInt(blob.length).putLong(buildId).putInt(0).putInt(0);

        MethodCodec rootCodec = new MethodCodec(root);
        List<Entry> ordered = new ArrayList<>(entries);
        Collections.shuffle(ordered, random);
        byte[] boundaries = new byte[size(boundaryBytes)];
        int recordOff = HEADER_SIZE + entries.size() * 8;
        int boundCursor = 0, dataCursor = dataOff;
        for (Entry entry : ordered) {
            entry.recordOff = recordOff;
            entry.boundOff = boundCursor;
            entry.codeOff = dataCursor;
            byte[] mapped = mapCode(entry, rows);
            System.arraycopy(mapped, 0, blob, dataCursor, mapped.length);
            dataCursor += mapped.length;
            entry.triesOff = dataCursor;
            System.arraycopy(entry.encoded.tries, 0, blob, dataCursor, entry.encoded.tries.length);
            dataCursor += entry.encoded.tries.length;
            System.arraycopy(entry.encoded.boundaries, 0, boundaries, boundCursor, entry.encoded.boundaries.length);
            boundCursor += entry.encoded.boundaries.length;
            byte[] record = record(entry);
            out.position(recordOff);
            out.put(rootCodec.transform(record, entry.token, ReaderFormats.RECORD));
            recordOff += RECORD_SIZE;
        }
        List<Entry> directory = new ArrayList<>(entries);
        // Tokens are stored as nonnegative u32 values in a Java long.
        directory.sort(Comparator.comparingLong(entry -> entry.token));
        out.position(HEADER_SIZE);
        for (Entry entry : directory) out.putInt((int)entry.token).putInt(entry.recordOff);
        out.position(mapOff);
        out.put(rootCodec.transform(rows, moduleId, ReaderFormats.MAP));
        out.position(boundaryOff);
        out.put(rootCodec.transform(boundaries, moduleId, ReaderFormats.DIRECTORY));
        verify(blob, directory, mapOff, boundaryOff, size(boundaryBytes));
        return new Built(blob, moduleId, buildId);
    }

    private long seed(Entry entry) {
        return DemandCodec.seed(root, entry.id, entry.encoded.descriptorTag, entry.registers, entry.ins);
    }

    private byte[] mapCode(Entry entry, byte[] mapping) {
        byte[] plain = DemandCodec.transformCode(entry.encoded.code, seed(entry), entry.encoded.boundaries);
        byte[] inverse = new byte[256];
        int base = (int)(seed(entry) & 3) * 256;
        for (int stored = 0; stored < 256; ++stored) inverse[mapping[base + stored] & 255] = (byte)stored;
        for (int pc = 0; pc < plain.length / 2; ++pc)
            if (DemandCodec.kind(entry.encoded.boundaries, pc) == ReaderFormats.FETCH)
                plain[pc * 2] = inverse[plain[pc * 2] & 255];
        return DemandCodec.transformCode(plain, seed(entry), entry.encoded.boundaries);
    }

    private byte[] record(Entry entry) {
        ByteBuffer record = ByteBuffer.allocate(RECORD_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        record.putInt((int)entry.id).putLong(entry.encoded.descriptorTag);
        record.putInt(entry.registers).putInt(entry.ins);
        record.putInt(entry.codeOff).putInt(entry.encoded.code.length);
        record.putInt(entry.triesOff).putInt(entry.encoded.tries.length);
        record.putInt(entry.boundOff).putInt(entry.encoded.boundaries.length);
        record.putInt((int)(seed(entry) & 3)).putInt(3);
        record.putInt(MethodCodec.hash(Arrays.copyOf(record.array(), 52)));
        return record.array();
    }

    private void verify(byte[] blob, List<Entry> directory, int mapOff, int boundaryOff, int boundaryBytes) {
        MethodCodec codec = new MethodCodec(root);
        byte[] mapping = codec.transform(Arrays.copyOfRange(blob, mapOff, mapOff + MAP_SIZE), moduleId, ReaderFormats.MAP);
        byte[] boundaries = codec.transform(Arrays.copyOfRange(blob, boundaryOff, boundaryOff + boundaryBytes), moduleId, ReaderFormats.DIRECTORY);
        if (!Arrays.equals(mapping, rows)) throw new IllegalStateException("Demand map roundtrip mismatch");
        ByteBuffer input = ByteBuffer.wrap(blob).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < directory.size(); ++i) {
            Entry entry = directory.get(i);
            long token = Integer.toUnsignedLong(input.getInt(HEADER_SIZE + i * 8));
            int recordOff = input.getInt(HEADER_SIZE + i * 8 + 4);
            byte[] record = codec.transform(Arrays.copyOfRange(blob, recordOff, recordOff + RECORD_SIZE), token, ReaderFormats.RECORD);
            if (token != entry.token || !Arrays.equals(record, record(entry)))
                throw new IllegalStateException("Demand record roundtrip mismatch: " + entry.id);
            ByteBuffer r = ByteBuffer.wrap(record).order(ByteOrder.LITTLE_ENDIAN);
            byte[] bound = Arrays.copyOfRange(boundaries, r.getInt(36), r.getInt(36) + r.getInt(40));
            byte[] code = Arrays.copyOfRange(blob, r.getInt(20), r.getInt(20) + r.getInt(24));
            byte[] plain = DemandCodec.transformCode(code, seed(entry), bound);
            for (int pc = 0; pc < plain.length / 2; ++pc)
                if (DemandCodec.kind(bound, pc) == ReaderFormats.FETCH)
                    plain[pc * 2] = mapping[r.getInt(44) * 256 + (plain[pc * 2] & 255)];
            byte[] expected = DemandCodec.transformCode(entry.encoded.code, seed(entry), entry.encoded.boundaries);
            byte[] tries = Arrays.copyOfRange(blob, r.getInt(28), r.getInt(28) + r.getInt(32));
            MethodCodec methodCodec = new MethodCodec(seed(entry));
            if (!Arrays.equals(bound, entry.encoded.boundaries) || !Arrays.equals(plain, expected)
                    || !Arrays.equals(methodCodec.transform(tries, 0, ReaderFormats.TRIES),
                            methodCodec.transform(entry.encoded.tries, 0, ReaderFormats.TRIES)))
                throw new IllegalStateException("Demand code/tries roundtrip mismatch: " + entry.id);
        }
    }

    private static int size(long value) {
        if (value < 0 || value > Integer.MAX_VALUE)
            throw new IllegalArgumentException("Demand module exceeds Java array size: " + value);
        return (int)value;
    }

    private static void requireU32(long value) {
        if (value < 0 || value > 0xffffffffL) throw new IllegalArgumentException("Not a u32: " + value);
    }

    private static final class Entry {
        final long id, token;
        final int registers, ins;
        final DemandCodec.Encoded encoded;
        int recordOff, boundOff, codeOff, triesOff;

        Entry(long id, long token, int registers, int ins, DemandCodec.Encoded encoded) {
            this.id = id; this.token = token; this.registers = registers; this.ins = ins;
            this.encoded = new DemandCodec.Encoded(encoded.code.clone(), encoded.tries.clone(),
                    encoded.boundaries.clone(), encoded.descriptorTag);
        }
    }
}

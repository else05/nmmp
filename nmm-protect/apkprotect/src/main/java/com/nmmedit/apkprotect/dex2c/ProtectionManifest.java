package com.nmmedit.apkprotect.dex2c;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/** Deterministic, authenticated contract shared by generated VM components. */
public final class ProtectionManifest {
    public static final int VERSION = 1;
    public static final int POLICY_VERSION = 1;
    public static final int POLICY_ENFORCE = 1;
    public static final int POLICY_CHECK_DEBUG = 1 << 1;
    public static final int POLICY_CHECK_MAPS = 1 << 2;
    public static final int RECHECK_MILLIS = 5000;
    public static final int DIGEST_SIZE = 32;
    private static final int HEADER_SIZE = 96;
    private static final int ENTRY_SIZE = 120;
    private static final byte[] MAGIC = "NMMPMF01".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] KEY_XOR_MASK = {
            (byte) 0x91, 0x37, (byte) 0xe4, 0x2b, 0x6d, (byte) 0xa8, 0x53, (byte) 0xc1,
            0x0f, 0x72, (byte) 0xb9, 0x44, (byte) 0xde, 0x18, (byte) 0x85, 0x6a,
            0x3c, (byte) 0xf0, 0x27, (byte) 0x9d, 0x51, (byte) 0xcb, 0x04, 0x7e,
            (byte) 0xa3, 0x68, (byte) 0xd5, 0x12, (byte) 0xbc, 0x49, (byte) 0xf7, 0x20
    };

    public static final class Entry {
        public final long moduleId;
        public final int moduleSize;
        public final int methodCount;
        public final int resolverItemCount;
        public final int registerCount;
        public final byte[] moduleDigest;
        public final byte[] resolverDigest;
        public final byte[] registerDigest;

        public Entry(long moduleId,
                     int moduleSize,
                     int methodCount,
                     int resolverItemCount,
                     int registerCount,
                     byte[] moduleDigest,
                     byte[] resolverDigest,
                     byte[] registerDigest) {
            if (moduleId < 0 || moduleId > 0xffffffffL || moduleSize <= 0
                    || methodCount < 0 || resolverItemCount < 0 || registerCount < 0) {
                throw new IllegalArgumentException("Invalid protection manifest entry dimensions");
            }
            this.moduleId = moduleId;
            this.moduleSize = moduleSize;
            this.methodCount = methodCount;
            this.resolverItemCount = resolverItemCount;
            this.registerCount = registerCount;
            this.moduleDigest = digest(moduleDigest);
            this.resolverDigest = digest(resolverDigest);
            this.registerDigest = digest(registerDigest);
        }

        private static byte[] digest(byte[] value) {
            if (value == null || value.length != DIGEST_SIZE) {
                throw new IllegalArgumentException("Manifest digest must contain 32 bytes");
            }
            return value.clone();
        }
    }

    public static final class Built {
        private final byte[] bytes;
        private final byte[] tag;
        private final byte[] id;
        private final byte[] keyXor;
        private final int policyFlags;

        private Built(byte[] bytes, byte[] tag, byte[] id, byte[] keyXor, int policyFlags) {
            this.bytes = bytes;
            this.tag = tag;
            this.id = id;
            this.keyXor = keyXor;
            this.policyFlags = policyFlags;
        }

        public byte[] getBytes() { return bytes.clone(); }
        public byte[] getTag() { return tag.clone(); }
        public byte[] getId() { return id.clone(); }
        public byte[] getKeyXor() { return keyXor.clone(); }
        public int getPolicyFlags() { return policyFlags; }
    }

    /** Canonical SHA-256 writer used by build-time producers. */
    public static final class CanonicalDigest {
        private final MessageDigest digest;

        public CanonicalDigest(String domain) {
            try {
                digest = MessageDigest.getInstance("SHA-256");
            } catch (NoSuchAlgorithmException e) {
                throw new IllegalStateException("SHA-256 unavailable", e);
            }
            putBytes(domain.getBytes(StandardCharsets.US_ASCII));
        }

        public CanonicalDigest putU32(long value) {
            if (value < 0 || value > 0xffffffffL) {
                throw new IllegalArgumentException("Canonical u32 out of range: " + value);
            }
            digest.update((byte) value);
            digest.update((byte) (value >>> 8));
            digest.update((byte) (value >>> 16));
            digest.update((byte) (value >>> 24));
            return this;
        }

        public CanonicalDigest putBytes(byte[] value) {
            digest.update(value);
            return this;
        }

        public byte[] finish() { return digest.digest(); }
    }

    private final long buildId;
    private final int codecVersion;
    private final int templateVersion;
    private final String packageName;
    private final boolean signatureBound;
    private final byte[] signerDigest;
    private final byte[] key;
    private final int policyFlags;
    private final List<Entry> entries = new ArrayList<>();
    private Built built;

    public ProtectionManifest(long buildId,
                              int codecVersion,
                              int templateVersion,
                              String packageName,
                              boolean signatureBound,
                              byte[] signerDigest,
                              byte[] key,
                              int policyFlags) {
        if (packageName == null || packageName.getBytes(StandardCharsets.UTF_8).length > 1024
                || signerDigest == null || signerDigest.length != DIGEST_SIZE
                || key == null || key.length != DIGEST_SIZE
                || (policyFlags & ~(POLICY_ENFORCE | POLICY_CHECK_DEBUG | POLICY_CHECK_MAPS)) != 0) {
            throw new IllegalArgumentException("Invalid protection manifest identity or policy");
        }
        this.buildId = buildId;
        this.codecVersion = codecVersion;
        this.templateVersion = templateVersion;
        this.packageName = packageName;
        this.signatureBound = signatureBound;
        this.signerDigest = signerDigest.clone();
        this.key = key.clone();
        this.policyFlags = policyFlags;
    }

    public synchronized void add(Entry entry) {
        if (built != null) throw new IllegalStateException("Protection manifest is already frozen");
        for (Entry current : entries) {
            if (current.moduleId == entry.moduleId) {
                throw new IllegalArgumentException("Duplicate manifest module ID: " + entry.moduleId);
            }
        }
        entries.add(entry);
    }

    public synchronized Built build() {
        if (built != null) return built;
        final byte[] packageBytes = packageName.getBytes(StandardCharsets.UTF_8);
        final List<Entry> ordered = new ArrayList<>(entries);
        ordered.sort(Comparator.comparingLong(entry -> entry.moduleId));
        final long total = (long) HEADER_SIZE + packageBytes.length + (long) ENTRY_SIZE * ordered.size();
        if (ordered.isEmpty() || ordered.size() > 256 || total > 64 * 1024) {
            throw new IllegalStateException("Protection manifest dimensions are invalid");
        }
        final ByteBuffer out = ByteBuffer.allocate((int) total).order(ByteOrder.LITTLE_ENDIAN);
        out.put(MAGIC).putInt(VERSION).putInt((int) total).putLong(buildId)
                .putInt(codecVersion).putInt(templateVersion)
                .putInt(POLICY_VERSION).putInt(policyFlags).putInt(RECHECK_MILLIS)
                .putInt(ordered.size()).putInt(packageBytes.length).putInt(signatureBound ? 1 : 0)
                .putInt(0).putInt(0).put(signerDigest).put(packageBytes);
        for (Entry entry : ordered) {
            out.putInt((int) entry.moduleId).putInt(entry.moduleSize).putInt(entry.methodCount)
                    .putInt(entry.resolverItemCount).putInt(entry.registerCount).putInt(0)
                    .put(entry.moduleDigest).put(entry.resolverDigest).put(entry.registerDigest);
        }
        final byte[] bytes = out.array();
        final byte[] tag = hmac(key, bytes);
        final byte[] id = Arrays.copyOf(sha256(bytes), 16);
        final byte[] keyXor = new byte[DIGEST_SIZE];
        for (int i = 0; i < keyXor.length; ++i) keyXor[i] = (byte) (key[i] ^ KEY_XOR_MASK[i]);
        Arrays.fill(key, (byte) 0);
        built = new Built(bytes, tag, id, keyXor, policyFlags);
        return built;
    }

    public static byte[] sha256(byte[] value) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(value);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 unavailable", e);
        }
    }

    static boolean verify(Built value) {
        return verify(value.bytes, value.tag, value.id, value.keyXor);
    }

    static boolean verify(byte[] bytes, byte[] tag, byte[] id, byte[] keyXor) {
        if (bytes == null || tag == null || tag.length != DIGEST_SIZE || id == null || id.length != 16
                || keyXor == null || keyXor.length != DIGEST_SIZE) return false;
        byte[] key = new byte[DIGEST_SIZE];
        for (int i = 0; i < key.length; ++i) key[i] = (byte) (keyXor[i] ^ KEY_XOR_MASK[i]);
        byte[] actual = hmac(key, bytes);
        Arrays.fill(key, (byte) 0);
        return MessageDigest.isEqual(actual, tag)
                && MessageDigest.isEqual(Arrays.copyOf(sha256(bytes), 16), id);
    }

    private static byte[] hmac(byte[] key, byte[] value) {
        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key, "HmacSHA256"));
            return mac.doFinal(value);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException("HmacSHA256 unavailable", e);
        }
    }
}

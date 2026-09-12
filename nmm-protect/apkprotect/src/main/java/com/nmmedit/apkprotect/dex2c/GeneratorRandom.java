package com.nmmedit.apkprotect.dex2c;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Random;

/** Explicit test-only reproducibility; production randomness remains independent and secure. */
public final class GeneratorRandom {
    private GeneratorRandom() {}

    public static Long configuredTestSeed() {
        String value = System.getProperty("nmmp.testSeed");
        if (value == null) value = System.getenv("NMMP_TEST_SEED");
        if (value == null) return null;
        if (!value.matches("[0-9a-fA-F]{16}"))
            throw new IllegalArgumentException("nmmp.testSeed/NMMP_TEST_SEED must be exactly 16 hexadecimal digits");
        return Long.parseUnsignedLong(value, 16);
    }

    public static Random create(String purpose) { return create(purpose, 0); }

    public static Random create(String purpose, long identity) {
        Long seed = configuredTestSeed();
        if (seed == null) return new SecureRandom();
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            digest.update("nmmp-test-random-v1".getBytes(StandardCharsets.UTF_8));
            digest.update(ByteBuffer.allocate(16).putLong(seed).putLong(identity).array());
            byte[] hash = digest.digest(purpose.getBytes(StandardCharsets.UTF_8));
            return new Random(ByteBuffer.wrap(hash).getLong());
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 unavailable", e);
        }
    }
}

package com.nmmedit.apkprotect.dex2c;

import javax.annotation.Nonnull;

public final class MethodCodec {

    public static final int DOMAIN_CODE = 0x434f4445;
    public static final int DOMAIN_TRIES = 0x54524945;
    public static final int DOMAIN_STRING = 0x53545247;

    private static final long ID_MIX = 0x9e3779b97f4a7c15L;
    private static final long DOMAIN_MIX = 0xd6e8feb86659fd93L;
    private static final long MIX_1 = 0xbf58476d1ce4e5b9L;
    private static final long MIX_2 = 0x94d049bb133111ebL;

    private final long buildSeed;

    public MethodCodec(long buildSeed) {
        this.buildSeed = buildSeed;
    }

    public long getBuildSeed() {
        return buildSeed;
    }

    @Nonnull
    public byte[] transform(@Nonnull byte[] input, long id, int domain) {
        checkUnsignedInt(id, "id");
        final byte[] output = new byte[input.length];
        for (int i = 0; i < input.length; i++) {
            output[i] = (byte) ((input[i] & 0xff) ^ keyByte(id, domain, i));
        }
        return output;
    }

    public static int hash(@Nonnull byte[] input) {
        int hash = 0x811c9dc5;
        for (byte value : input) {
            hash ^= value & 0xff;
            hash *= 0x01000193;
        }
        return hash;
    }

    private int keyByte(long id, int domain, long byteIndex) {
        final long blockIndex = byteIndex >>> 3;
        long value = buildSeed
                ^ (id * ID_MIX)
                ^ (Integer.toUnsignedLong(domain) * DOMAIN_MIX)
                ^ blockIndex;
        value = (value ^ (value >>> 30)) * MIX_1;
        value = (value ^ (value >>> 27)) * MIX_2;
        value ^= value >>> 31;
        return (int) ((value >>> ((byteIndex & 7) * 8)) & 0xff);
    }

    private static void checkUnsignedInt(long value, String name) {
        if (value < 0 || value > 0xffffffffL) {
            throw new IllegalArgumentException(name + " 超出 u4 范围: " + value);
        }
    }
}

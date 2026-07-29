package com.nmmedit.apkprotect.dex2c;

import java.security.SecureRandom;

public final class ProtectionContext {

    public static final int CODEC_VERSION = 1;
    public static final int TEMPLATE_VERSION = 1;

    private final long buildSeed;
    private final MethodCodec methodCodec;
    private long methodId;
    private long dexId;

    public static ProtectionContext create() {
        return new ProtectionContext(new SecureRandom().nextLong());
    }

    public ProtectionContext(long buildSeed) {
        this.buildSeed = buildSeed;
        this.methodCodec = new MethodCodec(buildSeed);
    }

    public long getBuildSeed() {
        return buildSeed;
    }

    public MethodCodec getMethodCodec() {
        return methodCodec;
    }

    public long nextMethodId() {
        return takeNextId(methodId++, "methodId");
    }

    public long nextDexId() {
        return takeNextId(dexId++, "dexId");
    }

    private static long takeNextId(long value, String name) {
        if (value < 0 || value > 0xffffffffL) {
            throw new IllegalStateException(name + " 已耗尽");
        }
        return value;
    }
}

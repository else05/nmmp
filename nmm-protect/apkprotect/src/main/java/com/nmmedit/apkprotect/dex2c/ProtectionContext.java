package com.nmmedit.apkprotect.dex2c;

import com.nmmedit.apkprotect.sign.SignatureBinding;

import java.security.SecureRandom;

public final class ProtectionContext {

    public static final int CODEC_VERSION = 2;
    public static final int TEMPLATE_VERSION = 2;

    private final long buildSeed;
    private final long seedData;
    private final long buildId;
    private final String packageName;
    private final boolean signatureBound;
    private final MethodCodec methodCodec;
    private long methodId;
    private long dexId;

    public static ProtectionContext create() {
        return new ProtectionContext(new SecureRandom().nextLong());
    }

    public static ProtectionContext createBound(String packageName, byte[] signerCertificate) {
        final SecureRandom random = new SecureRandom();
        return new ProtectionContext(
                random.nextLong(),
                random.nextLong(),
                packageName,
                signerCertificate);
    }

    public ProtectionContext(long buildSeed) {
        this(buildSeed, 0, "", null);
    }

    ProtectionContext(long buildSeed,
                      long buildId,
                      String packageName,
                      byte[] signerCertificate) {
        this.buildSeed = buildSeed;
        this.buildId = buildId;
        this.packageName = packageName;
        signatureBound = signerCertificate != null;
        seedData = signatureBound
                ? buildSeed ^ SignatureBinding.deriveMask(packageName, signerCertificate, buildId)
                : buildSeed;
        this.methodCodec = new MethodCodec(buildSeed);
    }

    public long getBuildSeed() {
        return buildSeed;
    }

    public long getSeedData() {
        return seedData;
    }

    public long getBuildId() {
        return buildId;
    }

    public String getPackageName() {
        return packageName;
    }

    public boolean isSignatureBound() {
        return signatureBound;
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

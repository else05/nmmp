package com.nmmedit.apkprotect.dex2c;

import com.nmmedit.apkprotect.sign.SignatureBinding;

import java.util.Random;

public final class ProtectionContext {

    public static final int CODEC_VERSION = 3;
    public static final int TEMPLATE_VERSION = 5;

    public static void validateDecodeMode() {
        String mode = System.getProperty("vmDecodeMode", System.getenv("NMMP_VM_DECODE_MODE"));
        if (mode != null && !mode.equals("on-demand-v1"))
            throw new IllegalArgumentException("Only on-demand-v1 is supported; legacy has been removed");
    }
    public int getCodecVersion() { return CODEC_VERSION; }
    public String getDecodeMode() { return "on-demand-v1"; }

    private final long buildSeed;
    private final long seedData;
    private final long buildId;
    private final String packageName;
    private final boolean signatureBound;
    private final byte[] expectedSignerSha256;
    private final MethodCodec methodCodec;
    private long methodId;
    private long dexId;

    public static ProtectionContext create() {
        return new ProtectionContext(GeneratorRandom.create("context").nextLong(), 0, "", null);
    }

    public static ProtectionContext createBound(String packageName, byte[] signerCertificate) {
        final Random random = GeneratorRandom.create("context");
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
        validateDecodeMode();
        this.buildSeed = buildSeed;
        this.buildId = buildId;
        this.packageName = packageName;
        signatureBound = signerCertificate != null;
        expectedSignerSha256 = signatureBound
                ? SignatureBinding.certificateSha256(signerCertificate)
                : new byte[SignatureBinding.SIGNER_DIGEST_SIZE];
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

    public byte[] getExpectedSignerSha256() {
        return expectedSignerSha256.clone();
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

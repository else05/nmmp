package com.nmmedit.apkprotect.dex2c;

import com.nmmedit.apkprotect.sign.SignatureBinding;

import java.util.Arrays;
import java.util.Random;
import java.util.LinkedHashSet;
import java.util.Set;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.io.IOException;
import com.android.tools.smali.dexlib2.iface.Method;

public final class ProtectionContext {

    public static final int CODEC_VERSION = 3;
    public static final int TEMPLATE_VERSION = 7;

    public int getCodecVersion() { return CODEC_VERSION; }
    public String getDecodeMode() { return "on-demand-v1"; }

    private final long buildSeed;
    private final long seedData;
    private final long buildId;
    private final String packageName;
    private final boolean signatureBound;
    private final byte[] expectedSignerSha256;
    private final MethodCodec methodCodec;
    private final ProtectionManifest protectionManifest;
    private final Set<String> sensitiveMethods = readSensitiveMethods();
    private final Set<String> matchedSensitiveMethods = new LinkedHashSet<>();
    private long methodId;
    private long dexId;

    public static ProtectionContext create() {
        final Random random = GeneratorRandom.create("context");
        return new ProtectionContext(random.nextLong(), random.nextLong(), "", null);
    }

    public static ProtectionContext create(String packageName) {
        final Random random = GeneratorRandom.create("context");
        return new ProtectionContext(random.nextLong(), random.nextLong(), packageName, null);
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
        final byte[] manifestKey = new byte[ProtectionManifest.DIGEST_SIZE];
        GeneratorRandom.create("manifest-key", buildId).nextBytes(manifestKey);
        protectionManifest = new ProtectionManifest(
                buildId,
                CODEC_VERSION,
                TEMPLATE_VERSION,
                packageName,
                signatureBound,
                expectedSignerSha256,
                manifestKey,
                protectionPolicyFlags());
        Arrays.fill(manifestKey, (byte) 0);
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

    public void addManifestEntry(ProtectionManifest.Entry entry) {
        protectionManifest.add(entry);
    }

    public ProtectionManifest.Built buildManifest() {
        Set<String> missing = new LinkedHashSet<>(sensitiveMethods);
        missing.removeAll(matchedSensitiveMethods);
        if (!missing.isEmpty()) {
            throw new IllegalArgumentException("Sensitive methods were not converted: " + missing);
        }
        return protectionManifest.build();
    }

    private static Set<String> readSensitiveMethods() {
        Set<String> methods = new LinkedHashSet<>();
        String path = System.getProperty("nmmp.sensitiveMethodsFile");
        if (path == null) return methods;
        try {
            if (Files.size(Paths.get(path)) > 32 * 1024) {
                throw new IllegalArgumentException("Sensitive methods file exceeds 32 KiB");
            }
            for (String line : Files.readAllLines(Paths.get(path), StandardCharsets.UTF_8)) {
                String method = line.trim();
                if (method.isEmpty() || method.startsWith("#")) continue;
                if (method.length() > 1024 || !method.startsWith("L") || !method.contains(";->")
                        || !method.contains("(") || !method.contains(")") || !methods.add(method)) {
                    throw new IllegalArgumentException("Invalid or duplicate sensitive method: " + method);
                }
                if (methods.size() > 20) throw new IllegalArgumentException("At most 20 sensitive methods are supported");
            }
        } catch (IOException e) {
            throw new IllegalArgumentException("Cannot read sensitive methods file: " + path, e);
        }
        if (methods.isEmpty()) throw new IllegalArgumentException("Sensitive methods file is empty");
        return methods;
    }

    public synchronized boolean bindSensitiveMethod(Method method) {
        StringBuilder identity = new StringBuilder(method.getDefiningClass())
                .append("->").append(method.getName()).append('(');
        for (CharSequence parameter : method.getParameterTypes()) identity.append(parameter);
        String descriptor = identity.append(')').append(method.getReturnType()).toString();
        if (!sensitiveMethods.contains(descriptor)) return false;
        if (!matchedSensitiveMethods.add(descriptor)) {
            throw new IllegalArgumentException("Sensitive method converted more than once: " + descriptor);
        }
        return true;
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

    private static int protectionPolicyFlags() {
        String profile = System.getProperty("nmmp.protectionProfile");
        if (profile == null) profile = System.getenv("NMMP_PROTECTION_PROFILE");
        if (profile == null || profile.equals("observe")) {
            return ProtectionManifest.POLICY_CHECK_DEBUG | ProtectionManifest.POLICY_CHECK_MAPS
                    | ProtectionManifest.POLICY_CHECK_ENVIRONMENT;
        }
        if (profile.equals("enforce")) {
            return ProtectionManifest.POLICY_ENFORCE
                    | ProtectionManifest.POLICY_CHECK_DEBUG
                    | ProtectionManifest.POLICY_CHECK_MAPS
                    | ProtectionManifest.POLICY_CHECK_ENVIRONMENT;
        }
        throw new IllegalArgumentException(
                "nmmp.protectionProfile/NMMP_PROTECTION_PROFILE must be observe or enforce");
    }
}

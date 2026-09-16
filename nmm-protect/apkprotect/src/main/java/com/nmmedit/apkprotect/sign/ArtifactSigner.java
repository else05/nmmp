package com.nmmedit.apkprotect.sign;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.*;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;

/** Offline Ed25519 signing. Only getRawPublicKey() is suitable for APK embedding. */
public final class ArtifactSigner {
    public static final String APK_ENTRY = "assets/runtime/artifact.sig";
    private static final String LEGACY_APK_ENTRY = String.join("", "assets/", "nm", "mp", "/artifact.sig");
    private static final byte[] PUBLIC_PREFIX = {0x30,0x2a,0x30,0x05,0x06,0x03,0x2b,0x65,0x70,0x03,0x21,0x00};
    private final PrivateKey privateKey;
    private final PublicKey publicKey;
    private final byte[] rawPublicKey;

    public ArtifactSigner(PrivateKey privateKey, PublicKey publicKey) throws GeneralSecurityException {
        if (privateKey == null || publicKey == null) throw new InvalidKeyException("Artifact signing keys required");
        byte[] encoded = publicKey.getEncoded();
        if (encoded == null || encoded.length != PUBLIC_PREFIX.length + 32
                || !Arrays.equals(PUBLIC_PREFIX, Arrays.copyOf(encoded, PUBLIC_PREFIX.length))) {
            throw new InvalidKeyException("Artifact public key must be Ed25519 X.509");
        }
        this.privateKey = privateKey;
        this.publicKey = publicKey;
        this.rawPublicKey = Arrays.copyOfRange(encoded, PUBLIC_PREFIX.length, encoded.length);
        // Fail before starting a build if the provider or key pair is unusable.
        signMessage(new byte[]{'A','R','T','-','K','E','Y','-','C','H','E','C','K'});
    }

    public static ArtifactSigner load(Path privatePkcs8, Path publicX509) throws IOException, GeneralSecurityException {
        byte[] secret = boundedKey(privatePkcs8);
        try {
            KeyFactory factory = KeyFactory.getInstance("Ed25519");
            return new ArtifactSigner(factory.generatePrivate(new PKCS8EncodedKeySpec(secret)),
                    factory.generatePublic(new X509EncodedKeySpec(boundedKey(publicX509))));
        } finally { Arrays.fill(secret, (byte) 0); }
    }

    public static ArtifactSigner loadConfigured() throws IOException {
        String privatePath = System.getProperty("nmmp.artifact.privateKey");
        String publicPath = System.getProperty("nmmp.artifact.publicKey");
        if (privatePath == null || privatePath.trim().isEmpty() || publicPath == null || publicPath.trim().isEmpty()) {
            throw new IOException("APK protection requires -Dnmmp.artifact.privateKey=<PKCS8 DER> and -Dnmmp.artifact.publicKey=<X509 DER>");
        }
        try { return load(java.nio.file.Paths.get(privatePath), java.nio.file.Paths.get(publicPath)); }
        catch (GeneralSecurityException e) { throw new IOException("Unable to initialize Ed25519 artifact signer", e); }
    }

    private static byte[] boundedKey(Path path) throws IOException {
        long size = Files.size(path);
        if (size <= 0 || size > 16384) throw new IOException("Invalid artifact key file size");
        try (java.io.InputStream input = Files.newInputStream(path)) {
            byte[] bytes = new byte[(int) size];
            int offset = 0;
            while (offset < bytes.length) {
                int count = input.read(bytes, offset, bytes.length - offset);
                if (count <= 0) throw new IOException("Incomplete artifact key file");
                offset += count;
            }
            if (input.read() != -1) throw new IOException("Artifact key file changed while reading");
            return bytes;
        }
    }

    public byte[] getRawPublicKey() { return rawPublicKey.clone(); }

    public static boolean isReservedApkEntry(String entryName) {
        return APK_ENTRY.equals(entryName) || LEGACY_APK_ENTRY.equals(entryName);
    }

    public byte[] sign(ArtifactInventory inventory) throws IOException, GeneralSecurityException {
        byte[] body = inventory.encode();
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (DataOutputStream output = new DataOutputStream(bytes)) {
            output.write(new byte[]{'A','R','T','S','I','G','0','1'});
            output.writeInt(1);
            output.writeInt(body.length);
            output.write(body);
            output.write(signMessage(bytes.toByteArray()));
        }
        return bytes.toByteArray();
    }

    private byte[] signMessage(byte[] message) throws GeneralSecurityException {
        Signature signer = Signature.getInstance("Ed25519");
        signer.initSign(privateKey);
        signer.update(message);
        byte[] signature = signer.sign();
        Signature verifier = Signature.getInstance("Ed25519");
        verifier.initVerify(publicKey);
        verifier.update(message);
        if (signature.length != 64 || !verifier.verify(signature)) {
            throw new InvalidKeyException("Artifact signing key pair does not match");
        }
        return signature;
    }
}

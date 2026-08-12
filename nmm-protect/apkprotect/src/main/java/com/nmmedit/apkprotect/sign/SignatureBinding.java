package com.nmmedit.apkprotect.sign;

import com.android.apksig.ApkVerifier;
import com.android.apksig.apk.ApkFormatException;

import javax.annotation.Nonnull;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.cert.CertificateEncodingException;
import java.security.cert.X509Certificate;
import java.security.NoSuchAlgorithmException;
import java.util.List;

public final class SignatureBinding {
    private static final long FNV_OFFSET_BASIS = 0xcbf29ce484222325L;
    private static final long FNV_PRIME = 0x100000001b3L;
    public static final int SIGNER_DIGEST_SIZE = 32;
    private static final byte[] SIGNER_DIGEST_XOR_MASK = {
            0x6d, 0x21, (byte) 0xa7, 0x4c, (byte) 0xf3, (byte) 0x98, 0x0b, (byte) 0xd5,
            0x7e, (byte) 0xc2, 0x36, (byte) 0x91, 0x5a, (byte) 0xe8, 0x44, (byte) 0xbf,
            0x13, 0x79, (byte) 0xcd, 0x02, (byte) 0xa6, 0x5f, (byte) 0xe1, (byte) 0x88,
            0x3c, (byte) 0xb4, 0x69, (byte) 0xd7, 0x20, (byte) 0xfa, (byte) 0x95, 0x4e
    };

    private SignatureBinding() {
    }

    @Nonnull
    public static byte[] readSingleSignerCertificate(@Nonnull File apk) throws IOException {
        final ApkVerifier.Result result;
        try {
            result = new ApkVerifier.Builder(apk).build().verify();
        } catch (ApkFormatException | NoSuchAlgorithmException e) {
            throw new IOException("Unable to verify input APK signature", e);
        }
        if (!result.isVerified()) {
            throw new IOException("Input APK signature verification failed: " + result.getErrors());
        }
        return encodeSingleCertificate(result.getSignerCertificates());
    }

    private static byte[] encodeSingleCertificate(List<X509Certificate> certificates)
            throws IOException {
        if (certificates.size() != 1) {
            throw new IOException("Signature binding requires exactly one APK signer, found: "
                    + certificates.size());
        }
        try {
            return certificates.get(0).getEncoded();
        } catch (CertificateEncodingException e) {
            throw new IOException("Unable to encode input APK signer certificate", e);
        }
    }

    public static long deriveMask(@Nonnull String packageName,
                                  @Nonnull byte[] certificate,
                                  long buildId) {
        long hash = FNV_OFFSET_BASIS;
        hash = update(hash, packageName.getBytes(StandardCharsets.UTF_8));
        hash = update(hash, (byte) 0);
        hash = update(hash, certificate);
        for (int i = 0; i < Long.BYTES; i++) {
            hash = update(hash, (byte) (buildId >>> (i * Byte.SIZE)));
        }
        return mix64(hash);
    }

    @Nonnull
    public static byte[] certificateSha256(@Nonnull byte[] certificate) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(certificate);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is unavailable", e);
        }
    }

    @Nonnull
    public static byte[] xorSignerDigest(@Nonnull byte[] digest) {
        if (digest.length != SIGNER_DIGEST_SIZE) {
            throw new IllegalArgumentException("Expected a 32-byte SHA-256 digest");
        }
        final byte[] result = new byte[SIGNER_DIGEST_SIZE];
        for (int i = 0; i < result.length; i++) {
            result[i] = (byte) (digest[i] ^ SIGNER_DIGEST_XOR_MASK[i]);
        }
        return result;
    }

    private static long update(long hash, byte[] data) {
        for (byte value : data) {
            hash = update(hash, value);
        }
        return hash;
    }

    private static long update(long hash, byte value) {
        return (hash ^ (value & 0xffL)) * FNV_PRIME;
    }

    private static long mix64(long value) {
        value = (value ^ (value >>> 30)) * 0xbf58476d1ce4e5b9L;
        value = (value ^ (value >>> 27)) * 0x94d049bb133111ebL;
        return value ^ (value >>> 31);
    }
}

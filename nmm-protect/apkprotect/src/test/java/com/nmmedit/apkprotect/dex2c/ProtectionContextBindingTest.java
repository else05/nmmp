package com.nmmedit.apkprotect.dex2c;

import com.nmmedit.apkprotect.sign.SignatureBinding;
import org.junit.Test;

import static org.junit.Assert.*;

public class ProtectionContextBindingTest {

    @Test
    public void wrapsSeedWithPackageCertificateAndBuildId() {
        final byte[] certificate = {0, 1, 2, 0x7f, (byte) 0x80, (byte) 0xff};
        final long buildId = 0x1020304050607080L;
        final long mask = SignatureBinding.deriveMask(
                "com.example.app", certificate, buildId);
        assertEquals(0x9a9657f125f96884L, mask);

        final long seed = 0x0123456789abcdefL;
        final ProtectionContext context = new ProtectionContext(
                seed, buildId, "com.example.app", certificate);
        assertTrue(context.isSignatureBound());
        assertEquals(seed ^ mask, context.getSeedData());
        assertNotEquals(seed, context.getSeedData());
        assertArrayEquals(new byte[]{
                        (byte) 0xda, 0x2c, (byte) 0xb6, (byte) 0xad,
                        0x17, 0x5b, (byte) 0xc9, 0x66,
                        (byte) 0xde, 0x5e, 0x79, (byte) 0xc6,
                        (byte) 0xe1, 0x67, 0x77, (byte) 0xf8,
                        (byte) 0xa9, (byte) 0x8b, 0x61, 0x0c,
                        0x24, 0x24, (byte) 0xa8, (byte) 0x94,
                        0x13, 0x2d, (byte) 0xf2, (byte) 0x81,
                        0x5b, (byte) 0xe5, 0x06, 0x77
                },
                context.getExpectedSignerSha256());
    }

    @Test
    public void legacyContextKeepsRawSeed() {
        final long seed = 0x0123456789abcdefL;
        final ProtectionContext context = new ProtectionContext(seed);
        assertFalse(context.isSignatureBound());
        assertEquals(seed, context.getSeedData());
        assertArrayEquals(new byte[SignatureBinding.SIGNER_DIGEST_SIZE],
                context.getExpectedSignerSha256());
    }

    @Test
    public void xorSignerDigestUsesFixedMaskAndIsReversible() {
        final byte[] digest = SignatureBinding.certificateSha256(
                new byte[]{0, 1, 2, 0x7f, (byte) 0x80, (byte) 0xff});
        final byte[] encoded = SignatureBinding.xorSignerDigest(digest);
        assertArrayEquals(new byte[]{
                (byte) 0xb7, 0x0d, 0x11, (byte) 0xe1,
                (byte) 0xe4, (byte) 0xc3, (byte) 0xc2, (byte) 0xb3,
                (byte) 0xa0, (byte) 0x9c, 0x4f, 0x57,
                (byte) 0xbb, (byte) 0x8f, 0x33, 0x47,
                (byte) 0xba, (byte) 0xf2, (byte) 0xac, 0x0e,
                (byte) 0x82, 0x7b, 0x49, 0x1c,
                0x2f, (byte) 0x99, (byte) 0x9b, 0x56,
                0x7b, 0x1f, (byte) 0x93, 0x39
        }, encoded);
        assertArrayEquals(digest, SignatureBinding.xorSignerDigest(encoded));
    }
}

package com.nmmedit.apkprotect.dex2c;

import org.junit.Test;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

public class MethodCodecTest {

    private static final byte[] INPUT = {
            0x12, 0x34, 0x56, 0x78,
            (byte) 0x9a, (byte) 0xbc, (byte) 0xde, (byte) 0xf0,
            0x00, 0x11, 0x22, 0x33,
            0x44, 0x55, 0x66, 0x77,
            (byte) 0x88, (byte) 0x99, (byte) 0xaa, (byte) 0xbb
    };

    @Test
    public void matchesNativeGoldenVector() {
        final MethodCodec codec = new MethodCodec(0x0123456789abcdefL);
        final byte[] expected = {
                (byte) 0xca, 0x23, (byte) 0xf2, (byte) 0xdb,
                (byte) 0xda, (byte) 0xc1, (byte) 0xe3, 0x62,
                0x05, (byte) 0xba, 0x48, 0x23,
                (byte) 0xb0, (byte) 0x88, (byte) 0xf7, 0x26,
                0x75, 0x55, 0x7b, (byte) 0x80
        };

        final byte[] encoded = codec.transform(
                INPUT,
                0x10203040L,
                MethodCodec.DOMAIN_CODE);

        assertArrayEquals(expected, encoded);
        assertArrayEquals(
                INPUT,
                codec.transform(encoded, 0x10203040L, MethodCodec.DOMAIN_CODE));
        assertEquals(0x104656e9, MethodCodec.hash(INPUT));
    }

    @Test
    public void separatesCodecDomains() {
        final MethodCodec codec = new MethodCodec(0x0123456789abcdefL);

        final byte[] code = codec.transform(INPUT, 7, MethodCodec.DOMAIN_CODE);
        final byte[] tries = codec.transform(INPUT, 7, MethodCodec.DOMAIN_TRIES);
        final byte[] strings = codec.transform(INPUT, 7, MethodCodec.DOMAIN_STRING);

        org.junit.Assert.assertFalse(java.util.Arrays.equals(code, tries));
        org.junit.Assert.assertFalse(java.util.Arrays.equals(code, strings));
        org.junit.Assert.assertFalse(java.util.Arrays.equals(tries, strings));
    }
}

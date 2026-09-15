package com.nmmedit.apkprotect.dex2c;

import org.junit.Test;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

import static org.junit.Assert.*;

public class ProtectionManifestTest {
    @Test public void invalidIdentityFailsAtGeneration() {
        for (String name : new String[]{"bad\u0000package", ""}) {
            try {
                new ProtectionManifest(1, 3, 7, name, true, bytes(1), bytes(2), 0);
                fail("invalid bound package accepted");
            } catch (IllegalArgumentException expected) { }
        }
        try {
            new ProtectionManifest(1, 3, 7, "", false, bytes(1), bytes(2), 0);
            fail("unbound manifest with signer accepted");
        } catch (IllegalArgumentException expected) { }
    }
    private static byte[] bytes(int seed) {
        byte[] value = new byte[32];
        for (int i = 0; i < value.length; ++i) value[i] = (byte) (seed + i * 17);
        return value;
    }

    private static ProtectionManifest.Entry entry(long id, int seed) {
        return new ProtectionManifest.Entry(id, 100 + seed, 3 + seed, 7 + seed, 2 + seed,
                bytes(seed), bytes(seed + 1), bytes(seed + 2));
    }

    private static ProtectionManifest manifest(long buildId, byte[] key) {
        return new ProtectionManifest(buildId, 3, 7, "org.example.phase2", true,
                bytes(40), key, ProtectionManifest.POLICY_CHECK_DEBUG | ProtectionManifest.POLICY_CHECK_MAPS);
    }

    @Test public void canonicalManifestIsSortedAuthenticatedAndBound() {
        byte[] key = bytes(70);
        ProtectionManifest first = manifest(0x1020304050607080L, key);
        first.add(entry(9, 1));
        first.add(entry(2, 2));
        ProtectionManifest.Built built = first.build();
        assertSame(built, first.build());
        assertTrue(ProtectionManifest.verify(built));
        ByteBuffer data = ByteBuffer.wrap(built.getBytes()).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals("NMMPMF01", new String(Arrays.copyOfRange(built.getBytes(), 0, 8)));
        assertEquals(ProtectionManifest.VERSION, data.getInt(8));
        assertEquals(built.getBytes().length, data.getInt(12));
        assertEquals(2, data.getInt(32));
        assertEquals(20_000, data.getInt(40));
        assertEquals(2, data.getInt(44));
        int entries = 96 + data.getInt(48);
        assertEquals(2, Integer.toUnsignedLong(data.getInt(entries)));
        assertEquals(9, Integer.toUnsignedLong(data.getInt(entries + 120)));

        byte[] tampered = built.getBytes();
        for (int offset : new int[]{8, 20, 64, entries, tampered.length - 1}) {
            tampered[offset] ^= 1;
            assertFalse(ProtectionManifest.verify(
                    tampered, built.getTag(), built.getId(), built.getKeyXor()));
            tampered[offset] ^= 1;
        }
        byte[] wrongTag = built.getTag(); wrongTag[0] ^= 1;
        assertFalse(ProtectionManifest.verify(
                built.getBytes(), wrongTag, built.getId(), built.getKeyXor()));
        byte[] wrongKey = built.getKeyXor(); wrongKey[31] ^= 1;
        assertFalse(ProtectionManifest.verify(
                built.getBytes(), built.getTag(), built.getId(), wrongKey));
    }

    @Test public void duplicateModulesAndCrossBuildMixingAreRejected() {
        ProtectionManifest first = manifest(1, bytes(1));
        first.add(entry(4, 1));
        try {
            first.add(entry(4, 2));
            fail("duplicate module accepted");
        } catch (IllegalArgumentException expected) {
            assertTrue(expected.getMessage().contains("Duplicate"));
        }
        ProtectionManifest.Built a = first.build();
        ProtectionManifest second = manifest(2, bytes(2));
        second.add(entry(4, 1));
        ProtectionManifest.Built b = second.build();
        assertFalse(Arrays.equals(a.getBytes(), b.getBytes()));
        assertFalse(ProtectionManifest.verify(a.getBytes(), b.getTag(), a.getId(), b.getKeyXor()));
    }

    @Test public void buildProfilesAreAuthenticatedIntoPolicyFlags() {
        String previous = System.getProperty("nmmp.protectionProfile");
        try {
            System.setProperty("nmmp.protectionProfile", "observe");
            ProtectionContext observe = new ProtectionContext(10);
            observe.addManifestEntry(entry(0, 1));
            int observeFlags = ByteBuffer.wrap(observe.buildManifest().getBytes())
                    .order(ByteOrder.LITTLE_ENDIAN).getInt(36);
            assertEquals(ProtectionManifest.POLICY_CHECK_DEBUG | ProtectionManifest.POLICY_CHECK_MAPS
                            | ProtectionManifest.POLICY_CHECK_ENVIRONMENT,
                    observeFlags);

            System.setProperty("nmmp.protectionProfile", "enforce");
            ProtectionContext enforce = new ProtectionContext(10);
            enforce.addManifestEntry(entry(0, 1));
            int enforceFlags = ByteBuffer.wrap(enforce.buildManifest().getBytes())
                    .order(ByteOrder.LITTLE_ENDIAN).getInt(36);
            assertEquals(observeFlags | ProtectionManifest.POLICY_ENFORCE, enforceFlags);
        } finally {
            if (previous == null) System.clearProperty("nmmp.protectionProfile");
            else System.setProperty("nmmp.protectionProfile", previous);
        }
    }
}

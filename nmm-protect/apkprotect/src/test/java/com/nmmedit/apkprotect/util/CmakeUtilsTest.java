package com.nmmedit.apkprotect.util;

import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class CmakeUtilsTest {

    @Test
    public void writesOnlyXoredSignerDigestToGeneratedConfig() throws Exception {
        final byte[] certificate = {0, 1, 2, 0x7f, (byte) 0x80, (byte) 0xff};
        final ProtectionContext context =
                ProtectionContext.createBound("com.example.app", certificate);
        final File directory = Files.createTempDirectory("nmmp-codec-config").toFile();
        final File config = new File(directory, "VmCodecConfig.h");

        CmakeUtils.writeCodecConfig(config, context);

        final String content = new String(
                Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8);
        assertTrue(content.contains(
                "static const uint8_t NMMP_VM_EXPECTED_SIGNER_XOR[32]"));
        assertTrue(content.contains(
                "0xb7, 0x0d, 0x11, 0xe1, 0xe4, 0xc3, 0xc2, 0xb3, "
                        + "0xa0, 0x9c, 0x4f, 0x57, 0xbb, 0x8f, 0x33, 0x47, "
                        + "0xba, 0xf2, 0xac, 0x0e, 0x82, 0x7b, 0x49, 0x1c, "
                        + "0x2f, 0x99, 0x9b, 0x56, 0x7b, 0x1f, 0x93, 0x39"));
        assertFalse(content.contains(
                "0xda, 0x2c, 0xb6, 0xad, 0x17, 0x5b, 0xc9, 0x66"));
    }
}

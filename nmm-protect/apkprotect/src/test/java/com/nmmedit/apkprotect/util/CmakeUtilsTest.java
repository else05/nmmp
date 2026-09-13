package com.nmmedit.apkprotect.util;

import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import org.junit.Test;
import org.junit.Rule;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;
import java.util.Enumeration;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class CmakeUtilsTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    @Test
    public void bundledTemplatePassesAndPreFixTemplateIsRejected() throws Exception {
        File current = temporary.newFile("current.zip");
        try (InputStream input = CmakeUtils.class.getResourceAsStream("/vmsrc.zip")) {
            assertTrue(input != null);
            Files.copy(input, current.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
        Method validate = CmakeUtils.class.getDeclaredMethod("validateVmTemplate", File.class);
        validate.setAccessible(true);
        validate.invoke(null, current);
        File stale = temporary.newFile("template5.zip");
        try (ZipFile source = new ZipFile(current);
             ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(stale.toPath()))) {
            Enumeration<? extends ZipEntry> entries = source.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                output.putNextEntry(new ZipEntry(entry.getName()));
                try (InputStream input = source.getInputStream(entry)) {
                    if (entry.getName().equals("vm/include/VmCodecConfig.h")) {
                        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
                        FileUtils.copyStream(input, bytes);
                        String config = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
                        String version = "#define NMMP_VM_TEMPLATE_VERSION " + ProtectionContext.TEMPLATE_VERSION;
                        assertTrue(config.contains(version));
                        output.write(config.replace(version, "#define NMMP_VM_TEMPLATE_VERSION 5")
                                .getBytes(StandardCharsets.UTF_8));
                    } else FileUtils.copyStream(input, output);
                }
                output.closeEntry();
            }
        }
        try {
            validate.invoke(null, stale);
            fail("Pre-fix template accepted");
        } catch (InvocationTargetException failure) {
            assertTrue(failure.getCause() instanceof IOException);
            assertTrue(failure.getCause().getMessage().contains("VM 模板版本不匹配"));
        }

        File unlinked = temporary.newFile("unlinked-phase2.zip");
        try (ZipFile source = new ZipFile(current);
             ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(unlinked.toPath()))) {
            Enumeration<? extends ZipEntry> entries = source.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                output.putNextEntry(new ZipEntry(entry.getName()));
                try (InputStream input = source.getInputStream(entry)) {
                    if (entry.getName().equals("vm/CMakeLists.txt")) {
                        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
                        FileUtils.copyStream(input, bytes);
                        String cmake = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
                        assertTrue(cmake.contains("ProtectionPolicy.cpp"));
                        output.write(cmake.replace("ProtectionPolicy.cpp", "ProtectionPolicy.missing")
                                .getBytes(StandardCharsets.UTF_8));
                    } else FileUtils.copyStream(input, output);
                }
                output.closeEntry();
            }
        }
        try {
            validate.invoke(null, unlinked);
            fail("Unlinked phase2 runtime accepted");
        } catch (InvocationTargetException failure) {
            assertTrue(failure.getCause() instanceof IOException);
            assertTrue(failure.getCause().getMessage().contains("未链接 phase2"));
        }
    }

    @Test
    public void rejectsMissingEmptyAndDirectoryLoaderSources() throws Exception {
        File current = temporary.newFile("complete.zip");
        try (InputStream input = CmakeUtils.class.getResourceAsStream("/vmsrc.zip")) {
            Files.copy(input, current.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
        Method validate = CmakeUtils.class.getDeclaredMethod("validateVmTemplate", File.class);
        validate.setAccessible(true);
        for (String required : new String[]{"loader/Once.c", "loader/Envelope.c", "loader/InnerBootstrap.c"}) {
            for (int mode = 0; mode < 3; ++mode) {
                File broken = temporary.newFile();
                try (ZipFile source = new ZipFile(current);
                     ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(broken.toPath()))) {
                    Enumeration<? extends ZipEntry> entries = source.entries();
                    while (entries.hasMoreElements()) {
                        ZipEntry entry = entries.nextElement();
                        if (entry.getName().equals(required)) {
                            if (mode != 0) {
                                output.putNextEntry(new ZipEntry(required + (mode == 2 ? "/" : "")));
                                output.closeEntry();
                            }
                        } else {
                            output.putNextEntry(new ZipEntry(entry.getName()));
                            try (InputStream input = source.getInputStream(entry)) {
                                FileUtils.copyStream(input, output);
                            }
                            output.closeEntry();
                        }
                    }
                }
                try {
                    validate.invoke(null, broken);
                    fail("Invalid loader source accepted: " + required + ", mode=" + mode);
                } catch (InvocationTargetException failure) {
                    assertTrue(failure.getCause() instanceof IOException);
                    assertTrue(failure.getCause().getMessage().contains(required));
                }
            }
        }
    }

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

    @Test
    public void writesProtectionManifestIdentityConfig() throws Exception {
        ProtectionContext context = new ProtectionContext(7);
        byte[] digest = new byte[ProtectionManifest.DIGEST_SIZE];
        context.addManifestEntry(new ProtectionManifest.Entry(
                0, 8, 1, 1, 1, digest, digest, digest));
        ProtectionManifest.Built manifest = context.buildManifest();
        File directory = temporary.newFolder("manifest-config");

        CmakeUtils.writeProtectionManifestConfig(directory, manifest);

        String content = new String(Files.readAllBytes(new File(
                directory, "vm/include/ProtectionManifestConfig.h").toPath()),
                StandardCharsets.UTF_8);
        assertTrue(content.contains("#define NMMP_PROTECTION_MANIFEST_VERSION 1"));
        assertTrue(content.contains(String.format("0x%02x", manifest.getId()[0] & 0xff)));
    }
}

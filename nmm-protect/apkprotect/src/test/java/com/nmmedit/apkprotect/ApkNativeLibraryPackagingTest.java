package com.nmmedit.apkprotect;

import com.android.zipflinger.Entry;
import com.android.zipflinger.ZipArchive;
import com.android.zipflinger.ZipMap;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class ApkNativeLibraryPackagingTest {
    @Rule public final TemporaryFolder temporaryFolder = new TemporaryFolder();

    @Test
    public void extractNativeLibsFalseStoresAndAlignsGeneratedLibrary() throws Exception {
        File generated = temporaryFolder.newFile("libgenerated.so");
        Files.write(generated.toPath(), "generated".getBytes(StandardCharsets.UTF_8));
        File output = temporaryFolder.newFile("output.apk");
        assertTrue(output.delete());

        try (ZipArchive archive = new ZipArchive(output.toPath())) {
            archive.add(ApkProtect.nativeLibrarySource(generated,
                    "lib/arm64-v8a/libgenerated.so", true));
        }

        ZipMap map = ZipMap.from(output.toPath());
        assertStoredAndAligned(map.getEntries().get("lib/arm64-v8a/libgenerated.so"));
    }

    @Test
    public void extractNativeLibsTrueKeepsCompressedLibraryPolicy() throws Exception {
        File generated = temporaryFolder.newFile("libgenerated.so");
        Files.write(generated.toPath(), "generated".getBytes(StandardCharsets.UTF_8));
        File output = temporaryFolder.newFile("output.apk");
        assertTrue(output.delete());

        try (ZipArchive archive = new ZipArchive(output.toPath())) {
            archive.add(ApkProtect.nativeLibrarySource(generated,
                    "lib/arm64-v8a/libgenerated.so", false));
        }

        ZipMap map = ZipMap.from(output.toPath());
        Entry generatedEntry = map.getEntries().get("lib/arm64-v8a/libgenerated.so");
        assertTrue(generatedEntry.isCompressed());
        assertEquals(0, generatedEntry.getPayloadLocation().first % 4);
    }

    private static void assertStoredAndAligned(Entry entry) {
        assertFalse(entry.isCompressed());
        assertEquals(0, entry.getPayloadLocation().first % ApkProtect.NATIVE_LIBRARY_ALIGNMENT);
    }
}

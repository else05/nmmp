package com.nmmedit.apkprotect.util;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import static org.junit.Assert.*;

public class ApkExtractionTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private File archive(String... entries) throws IOException {
        File zip = temporary.newFile();
        try (ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(zip.toPath()))) {
            for (String entry : entries) {
                output.putNextEntry(new ZipEntry(entry));
                if (!entry.endsWith("/")) output.write("source".getBytes(StandardCharsets.UTF_8));
                output.closeEntry();
            }
        }
        return zip;
    }

    @Test public void rejectsUnsafePathsBeforeWritingAnySelectedFile() throws Exception {
        for (String entry : new String[]{"../escaped.c", "nested/../../escaped.c", "/escaped.c",
                "C:/escaped.c", "nested\\..\\escaped.c", "file.c:stream", "./alias.c"}) {
            File output = temporary.newFolder();
            try {
                ApkUtils.extractFiles(archive("valid.c", entry), ".*", output);
                fail("Accepted unsafe entry: " + entry);
            } catch (IOException expected) {
                assertFalse(new File(output, "valid.c").exists());
                assertFalse(new File(output.getParentFile(), "escaped.c").exists());
            }
        }
    }

    @Test public void extractsNestedFilesAndSkipsDirectoryEntries() throws Exception {
        File output = temporary.newFolder();
        List<File> files = ApkUtils.extractFiles(archive("vm/", "vm/include/", "vm/include/test.h"), ".*", output);
        assertEquals(1, files.size());
        assertEquals("source", new String(Files.readAllBytes(new File(output, "vm/include/test.h").toPath()),
                StandardCharsets.UTF_8));
    }

    @Test public void ignoresEntriesOutsideRequestedSelection() throws Exception {
        File output = temporary.newFolder();
        List<File> files = ApkUtils.extractFiles(archive("../escaped.c", "classes.dex"), "classes\\.dex", output);
        assertEquals(1, files.size());
        assertTrue(new File(output, "classes.dex").isFile());
        assertFalse(new File(output.getParentFile(), "escaped.c").exists());
    }
}

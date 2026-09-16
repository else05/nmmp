package com.nmmedit.apkprotect.sign;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.*;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.*;

import static org.junit.Assert.*;

public class ArtifactInventoryTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();

    private File source(String name, String contents) throws IOException {
        File file = new File(temporary.getRoot(), name);
        Files.write(file.toPath(), contents.getBytes(StandardCharsets.UTF_8));
        return file;
    }

    private Map<String, Map<File, File>> libraries(File file) {
        return Collections.singletonMap("arm64-v8a", Collections.singletonMap(file, file));
    }

    private ArtifactInventory capture() throws IOException {
        return ArtifactInventory.capture(17, "org.example.fixture",
                Collections.singletonList(source("classes.dex", "final-dex")),
                libraries(source("libfixture.so", "final-native")));
    }

    private Map<String, String> entries() {
        Map<String, String> entries = new LinkedHashMap<>();
        entries.put("classes.dex", "final-dex");
        entries.put("lib/arm64-v8a/libfixture.so", "final-native");
        entries.put("assets/data", "unrelated");
        return entries;
    }

    private File apk(Map<String, String> entries, boolean stored) throws IOException {
        File file = temporary.newFile();
        try (ZipOutputStream output = new ZipOutputStream(new FileOutputStream(file))) {
            for (Map.Entry<String, String> value : entries.entrySet()) {
                byte[] bytes = value.getValue().getBytes(StandardCharsets.UTF_8);
                ZipEntry entry = new ZipEntry(value.getKey());
                if (stored) {
                    CRC32 crc = new CRC32();
                    crc.update(bytes);
                    entry.setMethod(ZipEntry.STORED);
                    entry.setSize(bytes.length);
                    entry.setCrc(crc.getValue());
                }
                output.putNextEntry(entry);
                output.write(bytes);
                output.closeEntry();
            }
        }
        return file;
    }

    private void rejected(ArtifactInventory inventory, Map<String, String> entries) throws IOException {
        File file = apk(entries, false);
        try { inventory.verifyApk(file); fail("invalid APK accepted"); }
        catch (IOException expected) { }
    }

    @Test public void storedAndDeflatedFinalBytesMatch() throws IOException {
        ArtifactInventory inventory = capture();
        inventory.verifyApk(apk(entries(), true));
        inventory.verifyApk(apk(entries(), false));
    }

    @Test public void modifiedDexAndNativeAreRejected() throws IOException {
        ArtifactInventory inventory = capture();
        for (String name : Arrays.asList("classes.dex", "lib/arm64-v8a/libfixture.so")) {
            Map<String, String> entries = entries();
            entries.put(name, "X" + entries.get(name).substring(1));
            rejected(inventory, entries);
            entries.put(name, "short");
            rejected(inventory, entries);
        }
    }

    @Test public void missingAndUnexpectedEntriesAreRejected() throws IOException {
        ArtifactInventory inventory = capture();
        for (String name : Arrays.asList("classes.dex", "lib/arm64-v8a/libfixture.so")) {
            Map<String, String> entries = entries();
            entries.remove(name);
            rejected(inventory, entries);
        }
        for (String name : Arrays.asList("classes2.dex", "classes0.dex", "classes01.dex")) {
            Map<String, String> entries = entries();
            entries.put(name, "unexpected");
            rejected(inventory, entries);
        }
    }

    @Test public void duplicateZipNamesAreRejected() throws IOException {
        ArtifactInventory inventory = capture();
        Map<String, String> entries = entries();
        entries.put("assets/dupA", "one");
        entries.put("assets/dupB", "two");
        File apk = apk(entries, true);
        byte[] data = Files.readAllBytes(apk.toPath());
        byte[] name = "assets/dupB".getBytes(StandardCharsets.US_ASCII);
        int replacements = 0;
        for (int i = 0; i <= data.length - name.length; ++i) {
            if (Arrays.equals(Arrays.copyOfRange(data, i, i + name.length), name)) {
                data[i + name.length - 1] = 'A';
                ++replacements;
            }
        }
        assertEquals(2, replacements); // local header and central directory
        Files.write(apk.toPath(), data);
        try { inventory.verifyApk(apk); fail("duplicate accepted"); }
        catch (IOException expected) { assertTrue(expected.getMessage().contains("Duplicate")); }
    }

    @Test public void captureFreezesBytesBeforePackaging() throws IOException {
        ArtifactInventory inventory = capture();
        byte[] frozen = inventory.encode();
        source("classes.dex", "later-dex");
        Map<String, String> changed = entries();
        changed.put("classes.dex", "later-dex");
        rejected(inventory, changed);
        assertArrayEquals(frozen, inventory.encode());
    }

    @Test public void canonicalInventoryBindsIdentityAndSortsEntries() throws IOException {
        File main = source("classes.dex", "main"), second = source("classes2.dex", "second");
        Map<String, Map<File, File>> libs = libraries(source("libfixture.so", "native"));
        byte[] first = ArtifactInventory.capture(17, "org.example.fixture", Arrays.asList(main, second), libs).encode();
        assertArrayEquals(first, ArtifactInventory.capture(17, "org.example.fixture", Arrays.asList(second, main), libs).encode());
        assertFalse(Arrays.equals(first, ArtifactInventory.capture(18, "org.example.fixture", Arrays.asList(main, second), libs).encode()));
        assertFalse(Arrays.equals(first, ArtifactInventory.capture(17, "org.example.other", Arrays.asList(main, second), libs).encode()));
        try (DataInputStream input = new DataInputStream(new ByteArrayInputStream(first))) {
            byte[] magic = new byte[8];
            input.readFully(magic);
            assertEquals("ARTINV01", new String(magic, StandardCharsets.US_ASCII));
            assertEquals(1, input.readInt());
            assertEquals(17, input.readLong());
            byte[] identity = new byte[input.readInt()];
            input.readFully(identity);
            assertEquals("org.example.fixture", new String(identity, StandardCharsets.UTF_8));
            assertEquals(3, input.readInt());
            for (String expected : Arrays.asList("classes.dex", "classes2.dex", "lib/arm64-v8a/libfixture.so")) {
                byte[] name = new byte[input.readInt()];
                input.readFully(name);
                assertEquals(expected, new String(name, StandardCharsets.US_ASCII));
                assertTrue(input.readLong() > 0);
                byte[] digest = new byte[32];
                input.readFully(digest);
            }
            assertEquals(-1, input.read());
        }
    }

    @Test public void invalidSourceSetsFail() throws IOException {
        File main = source("classes.dex", "main");
        Map<String, Map<File, File>> libs = libraries(source("libfixture.so", "native"));
        for (List<File> files : Arrays.asList(Collections.<File>emptyList(), Arrays.asList(main, main),
                Collections.singletonList(source("classes1.dex", "bad")))) {
            try { ArtifactInventory.capture(1, "org.example", files, libs); fail("invalid source accepted"); }
            catch (IOException expected) { }
        }
        try {
            ArtifactInventory.capture(1, "org.example", Collections.singletonList(main), Collections.emptyMap());
            fail("missing core libraries accepted");
        } catch (IOException expected) { }
    }

    @Test public void oversizedAndEmptyArtifactsFailBeforeHashing() throws IOException {
        File main = source("classes.dex", "main");
        File library = source("libfixture.so", "native");
        for (long length : new long[]{0, 256L * 1024 * 1024 + 1}) {
            try (RandomAccessFile file = new RandomAccessFile(main, "rw")) { file.setLength(length); }
            try {
                ArtifactInventory.capture(1, "org.example", Collections.singletonList(main), libraries(library));
                fail("invalid size accepted");
            } catch (IOException expected) { assertTrue(expected.getMessage().contains("size")); }
        }
    }
}

import com.nmmedit.apkprotect.sign.ArtifactInventory;
import com.nmmedit.apkprotect.sign.ArtifactSigner;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Uses the production Java serializer; fixture bytes contain no signing secrets. */
class GenerateArtifactFixtures {
    private static void zip(Path path, Map<String, byte[]> entries, boolean stored) throws Exception {
        zip(path, entries, stored, true);
    }
    private static void zip(Path path, Map<String, byte[]> entries, boolean stored, boolean storeEnvelope) throws Exception {
        try (ZipOutputStream output = new ZipOutputStream(Files.newOutputStream(path))) {
            for (Map.Entry<String, byte[]> item : entries.entrySet()) {
                ZipEntry entry = new ZipEntry(item.getKey());
                if (stored || (storeEnvelope && ArtifactSigner.APK_ENTRY.equals(item.getKey()))) {
                    CRC32 crc = new CRC32();
                    crc.update(item.getValue());
                    entry.setMethod(ZipEntry.STORED);
                    entry.setSize(item.getValue().length);
                    entry.setCrc(crc.getValue());
                }
                output.putNextEntry(entry);
                output.write(item.getValue());
                output.closeEntry();
            }
        }
    }
    private static void aligned(Path path, Map<String, byte[]> entries) throws Exception {
        Files.deleteIfExists(path);
        try (com.android.zipflinger.ZipArchive output = new com.android.zipflinger.ZipArchive(path)) {
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                com.android.zipflinger.Source source = com.android.zipflinger.Sources.from(
                        new java.io.ByteArrayInputStream(entry.getValue()), entry.getKey(),
                        ArtifactSigner.APK_ENTRY.equals(entry.getKey()) ? 0 : java.util.zip.Deflater.DEFAULT_COMPRESSION);
                source.align(4);
                output.add(source);
            }
        }
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("fixture directory required");
        Path root = Path.of(args[0]);
        Files.createDirectories(root);
        Path main = root.resolve("classes.dex"), second = root.resolve("classes2.dex"), library = root.resolve("libfixture.so");
        Files.write(main, "final-dex".getBytes(StandardCharsets.UTF_8));
        Files.write(second, "second-dex".getBytes(StandardCharsets.UTF_8));
        Files.write(library, "final-native".getBytes(StandardCharsets.UTF_8));
        ArtifactInventory inventory = ArtifactInventory.capture(17, "org.example.fixture",
                Arrays.asList(second.toFile(), main.toFile()),
                Collections.singletonMap("arm64-v8a", Collections.singletonMap(library.toFile(), library.toFile())));
        Files.write(root.resolve("artifact-inventory.bin"), inventory.encode());
        // Ephemeral test-only pair. Only public key and signed fixture leave this process.
        KeyPair key = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
        ArtifactSigner signer = new ArtifactSigner(key.getPrivate(), key.getPublic());
        Files.write(root.resolve("artifact-signed.bin"), signer.sign(inventory));
        Files.write(root.resolve("artifact-public.bin"), signer.getRawPublicKey());
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("classes.dex", Files.readAllBytes(main));
        entries.put("classes2.dex", Files.readAllBytes(second));
        entries.put("lib/arm64-v8a/libfixture.so", Files.readAllBytes(library));
        entries.put("assets/unrelated", new byte[]{1,2,3});
        zip(root.resolve("stored.apk"), entries, true);
        zip(root.resolve("deflated.apk"), entries, false);
        Map<String, byte[]> changed = new LinkedHashMap<>(entries);
        changed.put("classes.dex", "other-dex".getBytes(StandardCharsets.UTF_8));
        zip(root.resolve("changed.apk"), changed, true);
        changed = new LinkedHashMap<>(entries);
        changed.put("classes3.dex", new byte[]{1});
        zip(root.resolve("extra.apk"), changed, false);
        changed = new LinkedHashMap<>(entries);
        changed.remove("lib/arm64-v8a/libfixture.so");
        zip(root.resolve("missing.apk"), changed, true);
        Map<String, byte[]> signedEntries = new LinkedHashMap<>(entries);
        signedEntries.put(ArtifactSigner.APK_ENTRY, signer.sign(inventory));
        zip(root.resolve("signed-valid.apk"), signedEntries, false);
        aligned(root.resolve("signed-aligned.apk"), signedEntries);
        zip(root.resolve("signed-compressed.apk"), signedEntries, false, false);
        signedEntries.put("classes.dex", "other-dex".getBytes(StandardCharsets.UTF_8));
        zip(root.resolve("signed-changed.apk"), signedEntries, false);
        signedEntries.put("classes.dex", entries.get("classes.dex"));
        byte[] invalid = signer.sign(inventory);
        invalid[invalid.length - 1] ^= 1;
        signedEntries.put(ArtifactSigner.APK_ENTRY, invalid);
        zip(root.resolve("signed-invalid.apk"), signedEntries, false);
        byte[] large = new byte[100000];
        new java.util.Random(17).nextBytes(large);
        Files.write(main, large);
        inventory = ArtifactInventory.capture(17, "org.example.fixture", Arrays.asList(second.toFile(), main.toFile()),
                Collections.singletonMap("arm64-v8a", Collections.singletonMap(library.toFile(), library.toFile())));
        Files.write(root.resolve("large-inventory.bin"), inventory.encode());
        entries.put("classes.dex", large);
        zip(root.resolve("large.apk"), entries, false);
    }
}

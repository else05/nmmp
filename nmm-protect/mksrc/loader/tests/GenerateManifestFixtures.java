import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** Deterministic test identities only; never a release signing key. */
class GenerateManifestFixtures {
    private static void write(Path directory, byte[] body, byte[] key, byte[] keyXor) throws Exception {
        Files.createDirectories(directory);
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key, "HmacSHA256"));
        Files.write(directory.resolve("manifest.bin"), body);
        Files.write(directory.resolve("manifest.tag"), mac.doFinal(body));
        Files.write(directory.resolve("manifest.id"), Arrays.copyOf(MessageDigest.getInstance("SHA-256").digest(body), 16));
        Files.write(directory.resolve("manifest.keyxor"), keyXor);
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("fixture output directory required");
        byte[] key = new byte[32], digest = new byte[32];
        int flags = ProtectionManifest.POLICY_CHECK_DEBUG | ProtectionManifest.POLICY_CHECK_MAPS
                | ProtectionManifest.POLICY_CHECK_ENVIRONMENT;
        ProtectionManifest manifest = new ProtectionManifest(0, 3, 7, "", false, digest, key, flags);
        for (int id = 0; id < 2; id++) manifest.add(new ProtectionManifest.Entry(id, 16, 1, 1, 1, digest, digest, digest));
        ProtectionManifest.Built built = manifest.build();
        Path root = Path.of(args[0]);
        write(root, built.getBytes(), key, built.getKeyXor());
        byte[] old = built.getBytes();
        ByteBuffer.wrap(old).order(ByteOrder.LITTLE_ENDIAN).putInt(32, 1);
        write(root.resolve("old-policy"), old, key, built.getKeyXor());
        byte[] unsupported = built.getBytes();
        ByteBuffer.wrap(unsupported).order(ByteOrder.LITTLE_ENDIAN).putInt(36, flags | 16);
        write(root.resolve("unknown-flags"), unsupported, key, built.getKeyXor());
    }
}

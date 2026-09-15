import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.KeyPair;
import java.security.KeyPairGenerator;

/** Explicit local application-test key, never a default or a release key. */
class GenerateArtifactTestKey {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("dedicated test key directory required");
        Path directory = Path.of(args[0]);
        Files.createDirectories(directory);
        Path secret = directory.resolve("TEST-ONLY-artifact-private.der");
        Path publicKey = directory.resolve("TEST-ONLY-artifact-public.der");
        if (Files.exists(secret) || Files.exists(publicKey)) throw new IllegalStateException("Refusing to overwrite existing test keys");
        KeyPair pair = KeyPairGenerator.getInstance("Ed25519").generateKeyPair();
        Files.write(secret, pair.getPrivate().getEncoded(), StandardOpenOption.CREATE_NEW);
        Files.write(publicKey, pair.getPublic().getEncoded(), StandardOpenOption.CREATE_NEW);
        System.out.println("Created explicit local application-test keys; not for release signing.");
    }
}

package com.nmmedit.apkprotect.sign;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.File;
import java.nio.file.Files;
import java.security.*;
import java.util.Arrays;
import java.util.Collections;
import static org.junit.Assert.*;

public class ArtifactSignerTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private KeyPair pair() throws GeneralSecurityException { return KeyPairGenerator.getInstance("Ed25519").generateKeyPair(); }
    private ArtifactInventory inventory() throws Exception {
        File dex = new File(temporary.getRoot(), "classes.dex"), so = new File(temporary.getRoot(), "libfixture.so");
        Files.write(dex.toPath(), new byte[]{1,2,3});
        Files.write(so.toPath(), new byte[]{4,5,6});
        return ArtifactInventory.capture(17, "org.example.fixture", Collections.singletonList(dex),
                Collections.singletonMap("arm64-v8a", Collections.singletonMap(so, so)));
    }
    @Test public void signsHeaderAndBodyWithStandardEd25519() throws Exception {
        KeyPair key = pair();
        ArtifactSigner signer = new ArtifactSigner(key.getPrivate(), key.getPublic());
        ArtifactInventory inventory = inventory();
        byte[] envelope = signer.sign(inventory);
        assertEquals("assets/runtime/artifact.sig", ArtifactSigner.APK_ENTRY);
        assertFalse(ArtifactSigner.APK_ENTRY.toLowerCase(java.util.Locale.ROOT).contains("nmmp"));
        assertTrue(ArtifactSigner.isReservedApkEntry(ArtifactSigner.APK_ENTRY));
        assertTrue(ArtifactSigner.isReservedApkEntry(String.join("", "assets/", "nm", "mp", "/artifact.sig")));
        assertArrayEquals(new byte[]{'A','R','T','S','I','G','0','1'}, Arrays.copyOf(envelope, 8));
        assertArrayEquals(envelope, signer.sign(inventory));
        Signature verifier = Signature.getInstance("Ed25519");
        verifier.initVerify(key.getPublic());
        verifier.update(envelope, 0, envelope.length - 64);
        assertTrue(verifier.verify(Arrays.copyOfRange(envelope, envelope.length - 64, envelope.length)));
        assertArrayEquals(inventory.encode(), Arrays.copyOfRange(envelope, 16, envelope.length - 64));
        for (int offset : new int[]{0, 8, 12, 16, envelope.length - 65}) {
            byte[] changed = envelope.clone();
            changed[offset] ^= 1;
            verifier.initVerify(key.getPublic());
            verifier.update(changed, 0, changed.length - 64);
            assertFalse(verifier.verify(Arrays.copyOfRange(changed, changed.length - 64, changed.length)));
        }
        byte[] raw = signer.getRawPublicKey();
        raw[0] ^= 1;
        assertFalse(Arrays.equals(raw, signer.getRawPublicKey()));
    }
    @Test public void wrongKeyPairAndAlgorithmFailEarly() throws Exception {
        KeyPair first = pair(), second = pair();
        try { new ArtifactSigner(first.getPrivate(), second.getPublic()); fail("mismatched keys accepted"); }
        catch (GeneralSecurityException expected) { }
        KeyPair rsa = KeyPairGenerator.getInstance("RSA").generateKeyPair();
        try { new ArtifactSigner(rsa.getPrivate(), rsa.getPublic()); fail("wrong algorithm accepted"); }
        catch (GeneralSecurityException expected) { }
    }
    @Test public void loadsExplicitPkcs8AndX509Files() throws Exception {
        KeyPair key = pair();
        File secret = temporary.newFile(), publicFile = temporary.newFile();
        Files.write(secret.toPath(), key.getPrivate().getEncoded());
        Files.write(publicFile.toPath(), key.getPublic().getEncoded());
        ArtifactSigner loaded = ArtifactSigner.load(secret.toPath(), publicFile.toPath());
        ArtifactSigner direct = new ArtifactSigner(key.getPrivate(), key.getPublic());
        ArtifactInventory inventory = inventory();
        assertArrayEquals(direct.sign(inventory), loaded.sign(inventory));
        Files.write(secret.toPath(), new byte[0]);
        try { ArtifactSigner.load(secret.toPath(), publicFile.toPath()); fail("empty private key accepted"); }
        catch (java.io.IOException expected) { }
        Files.write(secret.toPath(), new byte[16385]);
        try { ArtifactSigner.load(secret.toPath(), publicFile.toPath()); fail("oversized private key accepted"); }
        catch (java.io.IOException expected) { }
    }

    @Test public void missingBuildConfigurationFailsExplicitly() throws Exception {
        String oldPrivate = System.getProperty("nmmp.artifact.privateKey");
        String oldPublic = System.getProperty("nmmp.artifact.publicKey");
        try {
            System.clearProperty("nmmp.artifact.privateKey");
            System.clearProperty("nmmp.artifact.publicKey");
            try { ArtifactSigner.loadConfigured(); fail("missing signing configuration accepted"); }
            catch (java.io.IOException expected) { assertTrue(expected.getMessage().contains("nmmp.artifact.privateKey")); }
        } finally {
            if (oldPrivate != null) System.setProperty("nmmp.artifact.privateKey", oldPrivate);
            if (oldPublic != null) System.setProperty("nmmp.artifact.publicKey", oldPublic);
        }
    }
}

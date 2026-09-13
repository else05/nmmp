package com.nmmedit.apkprotect.dex2c;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class GlobalDexConfigPhase2Test {

    @Test
    public void protectedBranchesEmitManifestAndStickyInitialization() throws Exception {
        File root = new File("build/phase2-loader-fixtures");
        assertTrue(root.mkdirs() || root.isDirectory());
        verifyBranch(new File(root, "unbound"), false);
        verifyBranch(new File(root, "bound"), true);
    }

    private static void verifyBranch(File directory, boolean bound) throws Exception {
        assertTrue(directory.mkdirs() || directory.isDirectory());
        ProtectionContext context = bound
                ? new ProtectionContext(0x1020304050607080L, 0x1122334455667788L,
                        "test.package", new byte[]{1, 2, 3})
                : new ProtectionContext(0x1020304050607080L);
        byte[] digest = new byte[ProtectionManifest.DIGEST_SIZE];
        for (int moduleId = 0; moduleId < 2; ++moduleId) {
            digest[0] = (byte) moduleId;
            context.addManifestEntry(new ProtectionManifest.Entry(
                    moduleId, 16, 1, 1, 1, digest, digest, digest));
        }
        ProtectionManifest.Built manifest = context.buildManifest();
        Files.write(new File(directory, "manifest.bin").toPath(), manifest.getBytes());
        Files.write(new File(directory, "manifest.tag").toPath(), manifest.getTag());
        Files.write(new File(directory, "manifest.keyxor").toPath(), manifest.getKeyXor());
        Files.write(new File(directory, "manifest.id").toPath(), manifest.getId());

        GlobalDexConfig global = new GlobalDexConfig(directory, context);
        global.addDexConfig(new DexConfig(directory, "classes.dex"));
        global.addDexConfig(new DexConfig(directory, "classes2.dex"));
        global.generateJniInitCode();

        String source = new String(Files.readAllBytes(global.getInitCodeFile().toPath()),
                StandardCharsets.UTF_8);
        assertTrue(source.contains("gNmmpProtectionManifest"));
        assertTrue(source.contains("gNmmpProtectionTag"));
        assertTrue(source.contains("gNmmpProtectionKeyXor"));
        assertTrue(source.contains("nmmpProtectionActivate"));
        assertTrue(source.contains("nmmpProtectionPolicyInitialize"));
        assertTrue(source.contains("vmInitRun"));
        assertTrue(source.contains("void nmmp_vm_fail(void)"));
        assertFalse(source.contains("vmCodecActivate(0)"));
        if (bound) {
            assertTrue(source.contains("bool nmmp_vm_activate(JNIEnv *env, jobject context)"));
            assertTrue(source.contains("vmBindingActivate(env, args->context)"));
        } else {
            assertTrue(source.contains("vmBindingActivate(env, NULL)"));
            assertTrue(source.contains("nmmpProtectionAllowCall(env)"));
        }
    }
}

import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.GlobalDexConfig;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import java.io.File;

/** Exercise both real generator branches; native tests supply JNI/cache stubs. */
class GenerateInitFixtures {
    public static void main(String[] args) throws Exception {
        for (boolean bound : new boolean[]{false, true}) {
            File directory = new File(args[0], bound ? "bound" : "unbound");
            if (!directory.mkdirs() && !directory.isDirectory()) throw new IllegalStateException("fixture directory");
            ProtectionContext context = bound
                    ? ProtectionContext.createBound("test.package", new byte[]{1, 2, 3})
                    : new ProtectionContext(0x1020304050607080L);
            byte[] digest = new byte[ProtectionManifest.DIGEST_SIZE];
            for (int moduleId = 0; moduleId < 2; moduleId++) {
                digest[0] = (byte) moduleId;
                context.addManifestEntry(new ProtectionManifest.Entry(
                        moduleId, 16, 1, 1, 1, digest, digest, digest));
            }
            GlobalDexConfig global = new GlobalDexConfig(directory, context);
            global.addDexConfig(new DexConfig(directory, "classes.dex"));
            global.addDexConfig(new DexConfig(directory, "classes2.dex"));
            global.generateJniInitCode();
        }
    }
}

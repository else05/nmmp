import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.GlobalDexConfig;
import java.io.File;

/** Exercise both real generator branches; native tests supply JNI/cache stubs. */
class GenerateInitFixtures {
    public static void main(String[] args) throws Exception {
        for (boolean bound : new boolean[]{false, true}) {
            File directory = new File(args[0], bound ? "bound" : "unbound");
            if (!directory.mkdirs() && !directory.isDirectory()) throw new IllegalStateException("fixture directory");
            GlobalDexConfig global = new GlobalDexConfig(directory, bound);
            global.addDexConfig(new DexConfig(directory, "classes.dex"));
            global.addDexConfig(new DexConfig(directory, "classes2.dex"));
            global.generateJniInitCode();
        }
    }
}

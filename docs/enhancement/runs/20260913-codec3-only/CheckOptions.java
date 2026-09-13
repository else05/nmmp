import com.nmmedit.apkprotect.BuildNativeLib;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.util.CmakeUtils;
import java.io.File;

public class CheckOptions {
    public static void main(String[] args) throws Exception {
        BuildNativeLib.validateProtectionOptions();
        ProtectionContext context = new ProtectionContext(1);
        if (context.getCodecVersion() != 3 || !context.getDecodeMode().equals("on-demand-v1"))
            throw new AssertionError("Unexpected default mode");
        if (args.length != 0) {
            var validate = CmakeUtils.class.getDeclaredMethod("validateVmTemplate", File.class);
            validate.setAccessible(true);
            validate.invoke(null, new File(args[0]));
        }
        System.out.println("CONFIG_PASS codec3 template5 private stage0");
    }
}

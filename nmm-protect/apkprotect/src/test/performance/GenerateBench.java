import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.nmmedit.apkprotect.BuildNativeLib;
import com.nmmedit.apkprotect.dex2c.*;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.RandomInstructionRewriter;
import com.nmmedit.apkprotect.dex2c.filters.ClassAndMethodFilter;
import com.nmmedit.apkprotect.util.CmakeUtils;
import java.io.File;
import java.util.Collections;

/** Uses the production generator and native build without an alternate VM wrapper. */
public final class GenerateBench {
    public static void main(String[] args) throws Exception {
        File dex = new File(args[0]), out = new File(args[1]);
        ProtectionContext context = ProtectionContext.create();
        RandomInstructionRewriter rewriter = new RandomInstructionRewriter();
        CmakeUtils.generateCSources(new File(out, "dex2c"), rewriter, context);
        ClassAnalyzer analyzer = new ClassAnalyzer();
        analyzer.setMinSdk(26);
        analyzer.loadDexFile(dex);
        GlobalDexConfig config = Dex2c.handleAllDex(Collections.singletonList(dex), new ClassAndMethodFilter() {
            public boolean acceptClass(ClassDef cls) { return cls.getType().equals("Lbench/BenchBody;"); }
            public boolean acceptMethod(Method method) { return !method.getName().startsWith("<"); }
        }, rewriter, analyzer, new File(out, "dex2c/generated"), context);
        DexConfig methods = config.getConfigs().get(0);
        if (methods.getMatchedMethodCount() != 8 || methods.getSkippedEmptyMethodCount() != 0 ||
                methods.getOffsetFromClassName("bench/BenchBody") != 0)
            throw new AssertionError("unexpected selected methods or registration offset");
        BuildNativeLib.build(new BuildNativeLib.CMakeOptions(System.getenv("CMAKE_PATH"),
                System.getenv("ANDROID_HOME"), System.getenv("ANDROID_NDK_HOME"), 26,
                out.getAbsolutePath(), BuildNativeLib.CMakeOptions.BuildType.RELEASE,
                "arm64-v8a"));
    }
}

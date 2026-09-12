import com.nmmedit.apkprotect.BuildNativeLib;
import java.io.File;

/** Rebuilds unchanged generated sources at the same API target for paired loader tests. */
public final class BuildRuntime {
    public static void main(String[] args) throws Exception {
        BuildNativeLib.build(new BuildNativeLib.CMakeOptions(System.getenv("CMAKE_PATH"),
                System.getenv("ANDROID_HOME"), System.getenv("ANDROID_NDK_HOME"), 26,
                new File(args[0]).getAbsolutePath(), BuildNativeLib.CMakeOptions.BuildType.RELEASE,
                "arm64-v8a", System.getenv("OMVLL_PLUGIN")));
    }
}

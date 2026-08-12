package com.nmmedit.apkprotect;

import org.junit.Test;

import java.util.List;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class BuildNativeLibTest {

    @Test
    public void omitsOmvllArgumentWhenPluginIsNotConfigured() {
        final BuildNativeLib.CMakeOptions options = createOptions(null);

        assertFalse(containsOmvllArgument(options.getCmakeArguments()));
    }

    @Test
    public void passesOmvllPluginToCmakeWhenConfigured() {
        final BuildNativeLib.CMakeOptions options = createOptions("/opt/omvll/omvll.so");

        assertTrue(options.getCmakeArguments().contains(
                "-DNMMP_OMVLL_PLUGIN=/opt/omvll/omvll.so"));
    }

    private static BuildNativeLib.CMakeOptions createOptions(String omvllPlugin) {
        return new BuildNativeLib.CMakeOptions(
                "/opt/cmake",
                "/opt/android-sdk",
                "/opt/android-ndk",
                21,
                "/tmp/nmmp",
                BuildNativeLib.CMakeOptions.BuildType.RELEASE,
                "arm64-v8a",
                omvllPlugin);
    }

    private static boolean containsOmvllArgument(List<String> arguments) {
        for (String argument : arguments) {
            if (argument.startsWith("-DNMMP_OMVLL_PLUGIN=")) {
                return true;
            }
        }
        return false;
    }
}

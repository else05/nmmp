package com.nmmedit.apkprotect;

import com.nmmedit.apkprotect.data.Prefs;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.util.FileUtils;
import org.jetbrains.annotations.NotNull;

import javax.annotation.Nonnull;
import java.io.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class BuildNativeLib {
    //库名称
    public static final String NMMP_NAME = "c++_en";

    public static boolean isPrivateLinkerEnabled() {
        final String value = System.getenv("NMMP_PRIVATE_LINKER");
        if (isEmpty(value) || "OFF".equalsIgnoreCase(value)) return false;
        if ("ON".equalsIgnoreCase(value)) return true;
        throw new IllegalArgumentException("NMMP_PRIVATE_LINKER must be ON or OFF");
    }

    public static Map<String, Map<File, File>> generateNativeLibs(@Nonnull File outDir,
                                                                  @Nonnull final List<String> abis) throws IOException {
        String cmakePath = System.getenv("CMAKE_PATH");
        if (isEmpty(cmakePath)) {
            System.err.println("No CMAKE_PATH");
            cmakePath = Prefs.cmakePath();
        }
        String sdkHome = System.getenv("ANDROID_HOME");
        if (isEmpty(sdkHome)) {
            sdkHome = System.getenv("ANDROID_SDK_HOME");
        }
        if (isEmpty(sdkHome)) {
            sdkHome = Prefs.sdkPath();
            System.err.println("No ANDROID_HOME or ANDROID_SDK_HOME. Default is " + sdkHome);
        }
        String ndkHome = System.getenv("ANDROID_NDK_HOME");
        if (isEmpty(ndkHome)) {
            ndkHome = Prefs.ndkPath();
            System.err.println("No ANDROID_NDK_HOME. Default is " + ndkHome);
        }
        String omvllPlugin = System.getenv("OMVLL_PLUGIN");
        if (!isEmpty(omvllPlugin)) {
            final File pluginFile = new File(omvllPlugin);
            if (!pluginFile.isFile()) {
                throw new IOException("O-MVLL plugin not found: " + pluginFile.getAbsolutePath());
            }
            omvllPlugin = pluginFile.getAbsolutePath();
            System.out.println("[nmmp] O-MVLL: enabled, plugin=" + omvllPlugin);
        } else {
            System.out.println("[nmmp] O-MVLL: disabled");
        }


        final Map<String, Map<File, File>> allLibs = new HashMap<>();

        for (String abi : abis) {
            if (ProtectionContext.configuredOnDemand() && !"arm64-v8a".equals(abi))
                throw new IOException("on-demand-v1 requires arm64-v8a: " + abi);
            final BuildNativeLib.CMakeOptions cmakeOptions = new BuildNativeLib.CMakeOptions(cmakePath,
                    sdkHome,
                    ndkHome, isPrivateLinkerEnabled() || ProtectionContext.configuredOnDemand() ? 26 : 21,
                    outDir.getAbsolutePath(),
                    BuildNativeLib.CMakeOptions.BuildType.RELEASE,
                    abi,
                    omvllPlugin);

            //删除上次创建的目录
            FileUtils.deleteFile(new File(cmakeOptions.getBuildPath()));

            final Map<File, File> files = BuildNativeLib.build(cmakeOptions);
            allLibs.put(abi, files);
        }
        return allLibs;
    }

    private static boolean isEmpty(String s) {
        return s == null || "".equals(s);
    }


    //编译出native lib，同时返回最后的so文件
    public static Map<File, File> build(@NotNull CMakeOptions options) throws IOException {

        final List<String> cmakeArguments = options.getCmakeArguments();
        //cmake
        execCmd(cmakeArguments);

        //cmake --build <dir>
        execCmd(Arrays.asList(
                options.getCmakeBinaryPath(),
                "--build",
                options.getBuildPath()
        ));
        //strip
        final Map<File, File> soMaps = options.getSharedObjectFileMap();
        for (Map.Entry<File, File> entry : soMaps.entrySet()) {
            execCmd(Arrays.asList(
                            options.getStripBinaryPath(),
                            "--strip-unneeded",
                            "-o",
                            entry.getValue().getAbsolutePath(),
                            entry.getKey().getAbsolutePath()
                    )
            );
        }

        //编译成功的so
        return soMaps;
    }

    private static void execCmd(List<String> cmds) throws IOException {
        System.out.println(cmds);
        final ProcessBuilder builder = new ProcessBuilder()
                .command(cmds)
                .redirectErrorStream(true);

        final Process process = builder.start();

        printOutput(process.getInputStream());

        try {
            final int exitStatus = process.waitFor();
            if (exitStatus != 0) {
                throw new IOException(String.format("Cmd '%s' exec failed", cmds.toString()));
            }
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    private static void printOutput(InputStream inputStream) throws IOException {
        final BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
        String line;
        while ((line = reader.readLine()) != null) {
            System.out.println(line);
        }
    }

    /**
     * cmake相关配置
     */
    public static class CMakeOptions {
        private final String cmakePath;

        private final String sdkHome;

        private final String ndkHome;
        private final int apiLevel;
        private final String projectHome;

        private final BuildType buildType;

        private final String abi;

        private final String omvllPlugin;

        public CMakeOptions(String cmakePath,
                            String sdkHome,
                            String ndkHome,
                            int apiLevel,
                            String projectHome,
                            BuildType buildType,
                            String abi,
                            String omvllPlugin) {
            this.cmakePath = cmakePath;
            this.sdkHome = sdkHome;
            this.ndkHome = ndkHome;
            this.apiLevel = apiLevel;
            this.projectHome = projectHome;
            this.buildType = buildType;
            this.abi = abi;
            this.omvllPlugin = omvllPlugin;
        }

        public String getCmakePath() {
            return cmakePath;
        }

        public String getSdkHome() {
            return sdkHome;
        }

        public String getNdkHome() {
            return ndkHome;
        }

        public int getApiLevel() {
            return apiLevel;
        }

        public String getProjectHome() {
            return projectHome;
        }

        public BuildType getBuildType() {
            return buildType;
        }

        public String getAbi() {
            return abi;
        }

        public String getOmvllPlugin() {
            return omvllPlugin;
        }

        @Nonnull
        public String getLibStripOutputDir() {
            return new File(new File(getProjectHome(), "obj/strip"), abi).getAbsolutePath();
        }

        @Nonnull
        public String getLibSymOutputDir() {
            return new File(new File(getProjectHome(), "obj/sym"), abi).getAbsolutePath();
        }

        //camke --build <BuildPath>
        public String getBuildPath() {
            return new File(getProjectHome(),
                    String.format(".cxx/cmake/%s/%s", getBuildType().getBuildTypeName(), getAbi())).getAbsolutePath();
        }

        public String getStripBinaryPath() {
            return new File(getNdkHome(), Prefs.ndkToolchains() + "/" +
                    Prefs.ndkAbi() + "/" + Prefs.ndkStrip()).getAbsolutePath();
        }

        public String getCmakeBinaryPath() {
            return new File(getCmakePath(), "/bin/cmake").getAbsolutePath();
        }

        public String getNinjaBinaryPath() {
            return new File(getCmakePath(), "/bin/ninja").getAbsolutePath();
        }

        public List<String> getCmakeArguments() {
            final List<String> arguments = new ArrayList<>(Arrays.asList(
                    getCmakeBinaryPath(),
                    String.format("-H%s", new File(getProjectHome(), "dex2c").getAbsoluteFile()),
                    String.format("-DCMAKE_TOOLCHAIN_FILE=%s", new File(getNdkHome(), "/build/cmake/android.toolchain.cmake").getAbsoluteFile()),
                    String.format("-DCMAKE_BUILD_TYPE=%s", getBuildType().getBuildTypeName()),
                    String.format("-DANDROID_ABI=%s", getAbi()),
                    String.format("-DANDROID_NDK=%s", getNdkHome()),
                    String.format("-DANDROID_PLATFORM=android-%d", getApiLevel()),
                    String.format("-DCMAKE_ANDROID_ARCH_ABI=%s", getAbi()),
                    String.format("-DCMAKE_ANDROID_NDK=%s", getNdkHome()),
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                    String.format("-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=%s", getLibSymOutputDir()),
                    String.format("-DCMAKE_MAKE_PROGRAM=%s", getNinjaBinaryPath()),
                    "-DCMAKE_SYSTEM_NAME=Android",
                    String.format("-DCMAKE_SYSTEM_VERSION=%d", getApiLevel()),
                    String.format("-B%s", getBuildPath()),
                    "-GNinja"));
            if (!isEmpty(getOmvllPlugin())) {
                arguments.add(String.format("-DNMMP_OMVLL_PLUGIN=%s", getOmvllPlugin()));
            }
            if (isPrivateLinkerEnabled()) {
                arguments.add("-DNMMP_PRIVATE_LINKER=ON");
            }
            arguments.add("-DNMMP_VM_DECODE_MODE=" + (ProtectionContext.configuredOnDemand() ? "on-demand-v1" : "legacy"));
            return arguments;
        }

        //未strip的so跟strip之后的so，
        @Nonnull
        public Map<File, File> getSharedObjectFileMap() {
            final HashMap<File, File> map = new HashMap<>();
            final String libSymOutputDir = getLibSymOutputDir();

            final File stripOutputDir = new File(getLibStripOutputDir());
            if (!stripOutputDir.exists()) stripOutputDir.mkdirs();

            final String vmp = "lib" + NMMP_NAME + ".so";
            File vmpFile = new File(libSymOutputDir, vmp);
            if (!vmpFile.exists()) {
                //windows
                vmpFile = new File(getBuildPath(), vmp);
            }
            if (!vmpFile.exists()) {
                throw new RuntimeException("Not Found so: " + vmpFile.getAbsolutePath());
            }
            map.put(vmpFile, new File(stripOutputDir, vmp));

            return map;
        }


        public enum BuildType {
            DEBUG("Debug"),
            RELEASE("Release");

            private final String buildTypeName;

            BuildType(String buildTypeName) {
                this.buildTypeName = buildTypeName;
            }

            public String getBuildTypeName() {
                return buildTypeName;
            }
        }
    }
}

package com.nmmedit.apkprotect.util;

import com.nmmedit.apkprotect.BuildNativeLib;
import com.nmmedit.apkprotect.dex2c.MethodCodec;
import com.nmmedit.apkprotect.dex2c.GeneratorRandom;
import com.nmmedit.apkprotect.dex2c.NativeProgram;
import com.nmmedit.apkprotect.dex2c.ProtectionManifest;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.sign.ApkVerifyCodeGenerator;
import com.nmmedit.apkprotect.sign.SignatureBinding;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class CmakeUtils {

    //根据指令重写规则,重新生成新的opcode
    public static void writeOpcodeHeaderFile(File source, InstructionRewriter instructionRewriter) throws IOException {
        final BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(
                new FileInputStream(source), StandardCharsets.UTF_8));

        final String collect = bufferedReader.lines().collect(Collectors.joining("\n"));
        final Pattern opcodePattern = Pattern.compile(
                "enum Opcode \\{.*?};",
                Pattern.MULTILINE | Pattern.DOTALL);
        final StringWriter opcodeContent = new StringWriter();
        final StringWriter gotoTableContent = new StringWriter();
        instructionRewriter.generateConfig(opcodeContent, gotoTableContent);
        String headerContent = opcodePattern
                .matcher(collect)
                .replaceAll(String.format("enum Opcode {\n%s};\n", opcodeContent.toString()));

        //根据opcode生成goto表
        final Pattern patternGotoTable = Pattern.compile(
                "_name\\[kNumPackedOpcodes\\] = \\{.*?};",
                Pattern.MULTILINE | Pattern.DOTALL);
        headerContent = patternGotoTable
                .matcher(headerContent)
                .replaceAll(String.format("_name[kNumPackedOpcodes] = {        \\\\\n%s};\n", gotoTableContent));

        try (Writer fileWriter = new OutputStreamWriter(
                new FileOutputStream(source), StandardCharsets.UTF_8)) {
            fileWriter.write(headerContent);
        }
    }

    //读取证书信息,并把公钥写入签名验证文件里,运行时对apk进行签名校验
    private static void writeApkVerifierFile(String packageName, File source, ApkVerifyCodeGenerator apkVerifyCodeGenerator) throws IOException {
        if (apkVerifyCodeGenerator == null) {
            return;
        }
        final BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(
                new FileInputStream(source), StandardCharsets.UTF_8));

        final String lines = bufferedReader.lines().collect(Collectors.joining("\n"));
        String dataPlaceHolder = "#define publicKeyPlaceHolder";

        String content = lines.replaceAll(dataPlaceHolder, dataPlaceHolder + apkVerifyCodeGenerator.generate());
        content = content.replaceAll("(#define PACKAGE_NAME) .*\n", "$1 \"" + packageName + "\"\n");

        try (Writer fileWriter = new OutputStreamWriter(
                new FileOutputStream(source), StandardCharsets.UTF_8)) {
            fileWriter.write(content);
        }
    }

    public static void writeCmakeFile(File cmakeTemp, String libName) throws IOException {
        final BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(
                new FileInputStream(cmakeTemp), StandardCharsets.UTF_8));

        String lines = bufferedReader.lines().collect(Collectors.joining("\n"));
        //定位cmake里的语句,防止替换错误
        String libNameFormat = "set\\(LIBNAME_PLACEHOLDER \"%s\"\\)";

        //替换原本libname
        lines = lines.replaceAll(String.format(libNameFormat, "nmmp"), String.format(libNameFormat, libName));

        try (Writer fileWriter = new OutputStreamWriter(
                new FileOutputStream(cmakeTemp), StandardCharsets.UTF_8)) {
            fileWriter.write(lines);
        }
    }


    public static void generateCSources(File srcDir,
                                        InstructionRewriter instructionRewriter,
                                        ProtectionContext protectionContext) throws IOException {
        final File vmsrcFile = new File(FileUtils.getHomePath(), "tools/vmsrc.zip");
        if (!vmsrcFile.exists()) {
            //警告：如果外部源码存在不会复制内部vmsrc.zip出去，需要删除外部源码文件才能保证vmsrc.zip正确更新
            vmsrcFile.getParentFile().mkdirs();
            //copy vmsrc.zip to external directory
            try (
                    InputStream inputStream = CmakeUtils.class.getResourceAsStream("/vmsrc.zip");
                    final FileOutputStream outputStream = new FileOutputStream(vmsrcFile);
            ) {
                FileUtils.copyStream(inputStream, outputStream);
            }
        }
        validateVmTemplate(vmsrcFile);
        final List<File> cSources = ApkUtils.extractFiles(vmsrcFile, ".*", srcDir);
        writeCodecConfig(new File(srcDir, "vm/include/VmCodecConfig.h"), protectionContext);
        try (Writer writer = new OutputStreamWriter(new FileOutputStream(
                new File(srcDir, "vm/include/NativeProgramConfig.h")), StandardCharsets.UTF_8)) {
            NativeProgram.root(protectionContext.getBuildId()).writeHeader(writer);
        }

        //处理指令及apk验证,生成新的c文件
        for (File source : cSources) {
            if (source.getName().endsWith("DexOpcodes.h")) {
                //根据指令重写规则重新生成DexOpcodes.h文件
                writeOpcodeHeaderFile(source, instructionRewriter);
            } else if (source.getName().equals("CMakeLists.txt")) {
                //处理cmake里配置的本地库名
                writeCmakeFile(source, BuildNativeLib.NMMP_NAME);
            } else if (source.getName().endsWith("vm.h")) {
                writeRandomResolver(source);
            } else if (source.getName().endsWith("JNIWrapper.h")) {
                writeRandomJNIWrapper(source);
            }
        }
    }

    private static void validateVmTemplate(File vmsrcFile) throws IOException {
        try (ZipFile zipFile = new ZipFile(vmsrcFile)) {
            for (String required : new String[]{
                    "CMakeLists.txt", "vm/CMakeLists.txt", "ConstantPool.c", "ConstantPool.h",
                    "loader/Outer.c", "loader/InnerBootstrap.c", "loader/Bootstrap.h",
                    "loader/Envelope.c", "loader/Envelope.h", "loader/Loader.c", "loader/Loader.h",
                    "loader/Once.c", "loader/Once.h", "loader/Stage0.h", "loader/Seal.java",
                    "loader/inner.exports", "loader/outer.exports",
                    "vm/ProtectionManifest.cpp", "vm/ProtectionPolicy.cpp",
                    "vm/include/ProtectionManifest.h", "vm/include/ProtectionPolicy.h",
                    "vm/include/ProtectionPolicyTypes.h", "vm/include/ProtectionPolicyInternal.h",
                    "vm/include/ProtectionManifestConfig.h",
                    "vm/JavaEnvironmentChecks.cpp", "vm/include/JavaEnvironmentChecks.h",
                    "vm/ModuleOrigins.cpp", "vm/include/ModuleOrigins.h",
                    "vm/OuterIntegrity.cpp", "vm/include/OuterIntegrity.h",
                    "vm/OuterImports.cpp",
                    "vm/ArtMethodChecks.cpp", "vm/include/ArtMethodChecks.h",
                    "vm/ArtifactInventory.cpp", "vm/ArtifactApk.cpp", "vm/ArtifactSignature.cpp",
                    "vm/include/ArtifactInventory.h", "vm/include/ArtifactApk.h", "vm/include/ArtifactSignature.h",
                    "vm/include/ArtifactKeyConfig.h",
                    "loader/vendor/monocypher/monocypher-ed25519.c", "loader/vendor/monocypher/monocypher-ed25519.h",
                    "loader/vendor/monocypher/monocypher.c", "loader/vendor/monocypher/monocypher.h"}) {
                requireZipEntry(zipFile, required, vmsrcFile);
            }
            requireZipEntry(zipFile, "loader/PrivateLinker.cmake", vmsrcFile);
            requireZipEntry(zipFile, "loader/pack.py", vmsrcFile);
            requireZipEntry(zipFile, "loader/Stage0.c", vmsrcFile);
            requireZipEntry(zipFile, "loader/stage0.py", vmsrcFile);
            requireZipEntry(zipFile, "loader/native_formats.py", vmsrcFile);
            final ZipEntry loaderVersion = requireZipEntry(zipFile, "loader/LoaderVersion.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/PrivateLoaderState.h", vmsrcFile);
            try (InputStream inputStream = zipFile.getInputStream(loaderVersion);
                 ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
                FileUtils.copyStream(inputStream, outputStream);
                final Matcher loaderMatcher = Pattern.compile(
                        "#define\\s+NMMP_PRIVATE_LOADER_FORMAT_VERSION\\s+(\\d+)")
                        .matcher(new String(outputStream.toByteArray(), StandardCharsets.UTF_8));
                if (!loaderMatcher.find() || Integer.parseInt(loaderMatcher.group(1)) != 1) {
                    throw new IOException("Private loader template format version mismatch: " + vmsrcFile);
                }
            }
            requireZipEntry(zipFile, "vm/VmCodec.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/Module.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/VmInit.c", vmsrcFile);
            requireZipEntry(zipFile, "vm/NativeVm.c", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/NativeFormats.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/NativeVm.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/VmBinding.cpp", vmsrcFile);
            if (!readZipEntry(zipFile, zipFile.getEntry("vm/VmBinding.cpp")).contains("nmmpVerifySignedArtifactApk")) {
                throw new IOException("VM template lacks signed artifact activation: " + vmsrcFile);
            }
            requireZipEntry(zipFile, "vm/Arm64Syscall.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/ApkV2Signer.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/Sha256.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/VmCodec.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/VmBinding.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/Arm64Syscall.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/ApkV2Signer.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/Sha256.h", vmsrcFile);
            final ZipEntry vmCmakeEntry = requireZipEntry(zipFile, "vm/CMakeLists.txt", vmsrcFile);
            final String vmCmake = readZipEntry(zipFile, vmCmakeEntry);
            if (!vmCmake.contains("ProtectionManifest.cpp")
                    || !vmCmake.contains("ProtectionPolicy.cpp") || !vmCmake.contains("ArtifactSignature.cpp")
                    || !vmCmake.contains("JavaEnvironmentChecks.cpp") || !vmCmake.contains("ModuleOrigins.cpp")
                    || !vmCmake.contains("OuterIntegrity.cpp")
                    || !vmCmake.contains("OuterImports.cpp")
                    || !vmCmake.contains("ArtMethodChecks.cpp")) {
                throw new IOException("VM 模板未链接 phase2 运行模块: " + vmsrcFile.getAbsolutePath());
            }
            final ZipEntry configEntry = requireZipEntry(
                    zipFile,
                    "vm/include/VmCodecConfig.h",
                    vmsrcFile);
            final String config;
            try (InputStream inputStream = zipFile.getInputStream(configEntry);
                 ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
                FileUtils.copyStream(inputStream, outputStream);
                config = new String(outputStream.toByteArray(), StandardCharsets.UTF_8);
            }
            final Pattern pattern = Pattern.compile(
                    "#define\\s+NMMP_VM_TEMPLATE_VERSION\\s+(\\d+)");
            final Matcher matcher = pattern.matcher(config);
            if (!matcher.find()
                    || Integer.parseInt(matcher.group(1)) != ProtectionContext.TEMPLATE_VERSION) {
                throw new IOException(String.format(
                        Locale.ROOT,
                        "VM 模板版本不匹配: %s，期望版本 %d",
                        vmsrcFile.getAbsolutePath(),
                        ProtectionContext.TEMPLATE_VERSION));
            }
        }
    }

    private static ZipEntry requireZipEntry(ZipFile zipFile,
                                            String entryName,
                                            File vmsrcFile) throws IOException {
        final ZipEntry entry = zipFile.getEntry(entryName);
        if (entry == null || entry.isDirectory() || entry.getSize() <= 0) {
            throw new IOException(String.format(
                    Locale.ROOT,
                    "VM 模板文件缺少或无效 %s: %s，期望版本 %d",
                    entryName,
                    vmsrcFile.getAbsolutePath(),
                    ProtectionContext.TEMPLATE_VERSION));
        }
        return entry;
    }

    private static String readZipEntry(ZipFile zipFile, ZipEntry entry) throws IOException {
        try (InputStream inputStream = zipFile.getInputStream(entry);
             ByteArrayOutputStream outputStream = new ByteArrayOutputStream()) {
            FileUtils.copyStream(inputStream, outputStream);
            return new String(outputStream.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    static void writeCodecConfig(File configFile,
                                 ProtectionContext protectionContext) throws IOException {
        final File parent = configFile.getParentFile();
        if (!parent.exists() && !parent.mkdirs()) {
            throw new IOException("无法创建 VM 配置目录: " + parent.getAbsolutePath());
        }
        final byte[] encodedSignerDigest = protectionContext.isSignatureBound()
                ? SignatureBinding.xorSignerDigest(protectionContext.getExpectedSignerSha256())
                : new byte[SignatureBinding.SIGNER_DIGEST_SIZE];
        final String content = String.format(
                Locale.ROOT,
                "#ifndef NMMP_VM_CODEC_CONFIG_H\n"
                        + "#define NMMP_VM_CODEC_CONFIG_H\n\n"
                        + "#include <stdint.h>\n\n"
                        + "#define NMMP_VM_TEMPLATE_VERSION %d\n"
                        + "#define NMMP_VM_CODEC_VERSION %d\n"
                        + "#define NMMP_VM_SIGNATURE_BINDING %d\n"
                        + "#define NMMP_VM_SEED_DATA UINT64_C(0x%016x)\n"
                        + "#define NMMP_VM_BUILD_ID UINT64_C(0x%016x)\n"
                        + "#define NMMP_VM_PACKAGE_NAME \"%s\"\n"
                        + "static const uint8_t NMMP_VM_EXPECTED_SIGNER_XOR[32] = {%s};\n"
                        + "#define NMMP_VM_DOMAIN_CODE UINT32_C(0x%08x)\n"
                        + "#define NMMP_VM_DOMAIN_TRIES UINT32_C(0x%08x)\n"
                        + "#define NMMP_VM_DOMAIN_STRING UINT32_C(0x%08x)\n\n"
                        + "#endif\n",
                ProtectionContext.TEMPLATE_VERSION,
                protectionContext.getCodecVersion(),
                protectionContext.isSignatureBound() ? 1 : 0,
                protectionContext.getSeedData(),
                protectionContext.getBuildId(),
                protectionContext.getPackageName(),
                formatByteArray(encodedSignerDigest),
                MethodCodec.DOMAIN_CODE,
                MethodCodec.DOMAIN_TRIES,
                MethodCodec.DOMAIN_STRING);
        try (Writer writer = new OutputStreamWriter(
                new FileOutputStream(configFile),
                StandardCharsets.UTF_8)) {
            writer.write(content);
        }
    }

    public static void writeProtectionManifestConfig(File srcDir,
                                                     ProtectionManifest.Built manifest) throws IOException {
        final File configFile = new File(srcDir, "vm/include/ProtectionManifestConfig.h");
        final File parent = configFile.getParentFile();
        if (!parent.exists() && !parent.mkdirs()) {
            throw new IOException("无法创建保护清单配置目录: " + parent.getAbsolutePath());
        }
        final String content = "#ifndef NMMP_PROTECTION_MANIFEST_CONFIG_H\n"
                + "#define NMMP_PROTECTION_MANIFEST_CONFIG_H\n\n"
                + "#include <stdint.h>\n\n"
                + "#define NMMP_PROTECTION_MANIFEST_VERSION " + ProtectionManifest.VERSION + "\n"
                + "static const uint8_t NMMP_PROTECTION_MANIFEST_ID[16] = {"
                + formatByteArray(manifest.getId()) + "};\n\n#endif\n";
        try (Writer writer = new OutputStreamWriter(
                new FileOutputStream(configFile), StandardCharsets.UTF_8)) {
            writer.write(content);
        }
    }

    public static void writeArtifactKeyConfig(File srcDir, byte[] publicKey) throws IOException {
        if (publicKey == null || publicKey.length != 32) throw new IOException("Expected 32-byte Ed25519 public key");
        File file = new File(srcDir, "vm/include/ArtifactKeyConfig.h");
        java.nio.file.Files.createDirectories(file.getParentFile().toPath());
        String content = "#ifndef NMMP_ARTIFACT_KEY_CONFIG_H\n#define NMMP_ARTIFACT_KEY_CONFIG_H\n#include <stdint.h>\n"
                + "#define NMMP_ARTIFACT_KEY_CONFIGURED 1\nstatic const uint8_t NMMP_ARTIFACT_PUBLIC_KEY[32] = {"
                + formatByteArray(publicKey) + "};\n#endif\n";
        java.nio.file.Files.write(file.toPath(), content.getBytes(StandardCharsets.UTF_8));
    }

    private static String formatByteArray(byte[] data) {
        final StringBuilder builder = new StringBuilder(data.length * 6);
        for (int i = 0; i < data.length; i++) {
            if (i != 0) {
                builder.append(", ");
            }
            builder.append(String.format(Locale.ROOT, "0x%02x", data[i] & 0xff));
        }
        return builder.toString();
    }

    private static void writeRandomResolver(File source) throws IOException {
        final BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(
                new FileInputStream(source), StandardCharsets.UTF_8));
        String lines = bufferedReader.lines().collect(Collectors.joining("\n"));
        final Pattern p = Pattern.compile("typedef struct \\{([^}]*?)} vmResolver;", Pattern.MULTILINE | Pattern.DOTALL);
        final Matcher matcherResolver = p.matcher(lines);
        if (matcherResolver.find()) {
            final String body = matcherResolver.group(1);
            //match function pointer
            final Pattern funcPattern = Pattern.compile("([^();]* \\**\\(\\*[a-zA-z0-9]*\\)\\([^();]*\\);)", Pattern.MULTILINE | Pattern.DOTALL);
            final ArrayList<String> funcs = new ArrayList<>();
            final Matcher matcher = funcPattern.matcher(body);
            while (matcher.find()) {
                funcs.add(matcher.group(1));
            }


            try (Writer fileWriter = new OutputStreamWriter(
                    new FileOutputStream(source), StandardCharsets.UTF_8)) {
                final String doc = matcherResolver.replaceAll("typedef struct {\n" +
                        randomList(funcs, GeneratorRandom.create("resolver-layout")) +
                        "} vmResolver;");
                fileWriter.write(doc);
            }
        }
    }

    private static void writeRandomJNIWrapper(File file) throws IOException {
        final BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8));
        String lines = bufferedReader.lines().collect(Collectors.joining("\n"));
        final Pattern p = Pattern.compile("typedef struct \\{([^}]*?)} JNIWrapper;", Pattern.MULTILINE | Pattern.DOTALL);
        final Matcher matcherWrapper = p.matcher(lines);
        if (matcherWrapper.find()) {
            final String body = matcherWrapper.group(1);
            //match function pointer
            final Pattern funcPattern = Pattern.compile("([^();]* \\**\\(\\*[a-zA-z0-9]*\\)\\([^();]*\\);)", Pattern.MULTILINE | Pattern.DOTALL);
            final ArrayList<String> funcs = new ArrayList<>();
            final Matcher matcher = funcPattern.matcher(body);
            while (matcher.find()) {
                funcs.add(matcher.group(1));
            }


            try (Writer fileWriter = new OutputStreamWriter(
                    new FileOutputStream(file), StandardCharsets.UTF_8)) {
                final String doc = matcherWrapper.replaceAll("typedef struct {\n" +
                        randomList(funcs, GeneratorRandom.create("jni-layout")) +
                        "} JNIWrapper;");
                fileWriter.write(doc);
            }
        }
    }

    private static String randomList(List<String> list, Random random) {
        final StringBuilder sb = new StringBuilder();
        final int size = list.size();
        for (int i = 0; i < size; i++) {
            final int idx = random.nextInt(list.size());
            sb.append(list.get(idx));
            sb.append('\n');
            list.remove(idx);
        }
        return sb.toString();
    }
}

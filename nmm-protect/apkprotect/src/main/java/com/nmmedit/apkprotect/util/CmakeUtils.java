package com.nmmedit.apkprotect.util;

import com.nmmedit.apkprotect.BuildNativeLib;
import com.nmmedit.apkprotect.dex2c.MethodCodec;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.sign.ApkVerifyCodeGenerator;

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
            requireZipEntry(zipFile, "vm/Codec.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/VmCodec.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/VmBinding.cpp", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/VmCodec.h", vmsrcFile);
            requireZipEntry(zipFile, "vm/include/VmBinding.h", vmsrcFile);
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
        if (entry == null) {
            throw new IOException(String.format(
                    Locale.ROOT,
                    "VM 模板缺少 %s: %s，期望版本 %d",
                    entryName,
                    vmsrcFile.getAbsolutePath(),
                    ProtectionContext.TEMPLATE_VERSION));
        }
        return entry;
    }

    private static void writeCodecConfig(File configFile,
                                         ProtectionContext protectionContext) throws IOException {
        final File parent = configFile.getParentFile();
        if (!parent.exists() && !parent.mkdirs()) {
            throw new IOException("无法创建 VM 配置目录: " + parent.getAbsolutePath());
        }
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
                        + "#define NMMP_VM_DOMAIN_CODE UINT32_C(0x%08x)\n"
                        + "#define NMMP_VM_DOMAIN_TRIES UINT32_C(0x%08x)\n"
                        + "#define NMMP_VM_DOMAIN_STRING UINT32_C(0x%08x)\n\n"
                        + "#endif\n",
                ProtectionContext.TEMPLATE_VERSION,
                ProtectionContext.CODEC_VERSION,
                protectionContext.isSignatureBound() ? 1 : 0,
                protectionContext.getSeedData(),
                protectionContext.getBuildId(),
                protectionContext.getPackageName(),
                MethodCodec.DOMAIN_CODE,
                MethodCodec.DOMAIN_TRIES,
                MethodCodec.DOMAIN_STRING);
        try (Writer writer = new OutputStreamWriter(
                new FileOutputStream(configFile),
                StandardCharsets.UTF_8)) {
            writer.write(content);
        }
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
                        randomList(funcs) +
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
                        randomList(funcs) +
                        "} JNIWrapper;");
                fileWriter.write(doc);
            }
        }
    }

    private static String randomList(List<String> list) {
        final StringBuilder sb = new StringBuilder();
        final int size = list.size();
        for (int i = 0; i < size; i++) {
            final Random random = new Random();
            final int idx = random.nextInt(list.size());
            sb.append(list.get(idx));
            sb.append('\n');
            list.remove(idx);
        }
        return sb.toString();
    }
}

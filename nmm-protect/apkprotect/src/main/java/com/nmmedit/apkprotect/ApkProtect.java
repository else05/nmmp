package com.nmmedit.apkprotect;

import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedClassDef;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.DexFile;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import com.android.zipflinger.*;
import com.nmmedit.apkprotect.andres.AxmlEdit;
import com.nmmedit.apkprotect.dex2c.Dex2c;
import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.GlobalDexConfig;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.dex2c.converter.structs.RegisterNativesUtilClassDef;
import com.nmmedit.apkprotect.dex2c.converter.structs.ApplicationInitClassDef;
import com.nmmedit.apkprotect.dex2c.filters.ClassAndMethodFilter;
import com.nmmedit.apkprotect.sign.SignatureBinding;
import com.nmmedit.apkprotect.sign.ArtifactInventory;
import com.nmmedit.apkprotect.sign.ArtifactSigner;
import com.nmmedit.apkprotect.util.ApkUtils;
import com.nmmedit.apkprotect.util.CmakeUtils;
import com.nmmedit.apkprotect.util.FileUtils;

import javax.annotation.Nonnull;
import java.io.*;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.Deflater;

public class ApkProtect {

    public static final String ANDROID_MANIFEST_XML = "AndroidManifest.xml";
    public static final String ANDROID_APP_APPLICATION = "android.app.Application";
    private static final String GENERATED_APPLICATION = "com.google.libc.NativeApplication";
    static final long NATIVE_LIBRARY_ALIGNMENT = 4L * 1024L;
    private final ApkFolders apkFolders;
    private final InstructionRewriter instructionRewriter;
    private final ClassAndMethodFilter filter;

    private final ClassAnalyzer classAnalyzer;

    private ApkProtect(ApkFolders apkFolders,
                       InstructionRewriter instructionRewriter,
                       ClassAndMethodFilter filter,
                       ClassAnalyzer classAnalyzer
    ) {
        this.apkFolders = apkFolders;

        this.instructionRewriter = instructionRewriter;

        this.filter = filter;
        this.classAnalyzer = classAnalyzer;

    }

    public void run() throws IOException {
        final File apkFile = apkFolders.getInApk();
        final File zipExtractDir = apkFolders.getZipExtractTempDir();
        final File inventoryFile = new File(apkFolders.getOutputApk().getPath() + ".artifact-inventory.bin");
        java.nio.file.Files.deleteIfExists(inventoryFile.toPath());
        final ArtifactSigner artifactSigner = ArtifactSigner.loadConfigured();
        try {
            byte[] manifestBytes = ApkUtils.getFile(apkFile, ANDROID_MANIFEST_XML);
            if (manifestBytes == null) {
                //错误apk文件
                throw new RuntimeException("Not is apk");
            }
            final boolean storeNativeLibraries = AxmlEdit.isExtractNativeLibsFalse(manifestBytes);

            final String packageName = AxmlEdit.getPackageName(manifestBytes);
            final ProtectionContext protectionContext = ProtectionContext.createBound(
                    packageName,
                    SignatureBinding.readSingleSignerCertificate(apkFile));

            //生成一些需要改变的c代码(随机opcode后的头文件及apk验证代码等)
            CmakeUtils.generateCSources(
                    apkFolders.getDex2cSrcDir(),
                    instructionRewriter,
                    protectionContext);

            CmakeUtils.writeArtifactKeyConfig(apkFolders.getDex2cSrcDir(), artifactSigner.getRawPublicKey());

            //解压得到所有classesN.dex
            List<File> files = getClassesFiles(apkFile, zipExtractDir);
            if (files.isEmpty()) {
                throw new RuntimeException("No classes.dex");
            }
            final int minSdk = AxmlEdit.getMinSdk(manifestBytes);

            classAnalyzer.setMinSdk(minSdk);

            if (minSdk < 23) {
                //todo 加载android5的sdk,以保证能正确分析一些有问题的代码
            }

            //


            //先加载apk包含的所有dex文件,以便分析一些有问题的代码
            for (File file : files) {
                classAnalyzer.loadDexFile(file);
            }

            String applicationName = AxmlEdit.getApplicationName(manifestBytes);
            final boolean generateApplication = applicationName.isEmpty();
            if (generateApplication) {
                applicationName = GENERATED_APPLICATION;
                requireClassAbsent(files, classDotNameToType(applicationName));
                manifestBytes = AxmlEdit.renameApplicationName(manifestBytes, applicationName);
                if (manifestBytes == null) {
                    throw new IOException("Unable to set generated Application in AndroidManifest.xml");
                }
            } else {
                applicationName = normalizeApplicationName(packageName, applicationName);
                requireClassInDexFiles(files, classDotNameToType(applicationName));
            }


            //globalConfig里面configs顺序和classesN.dex文件列表一样
            final GlobalDexConfig globalConfig = Dex2c.handleAllDex(files,
                    filter,
                    instructionRewriter,
                    classAnalyzer,
                    apkFolders.getCodeGeneratedDir(),
                    protectionContext);
            CmakeUtils.writeProtectionManifestConfig(
                    apkFolders.getDex2cSrcDir(), protectionContext.buildManifest());


            //需要放在主dex里的类
            final Set<String> mainDexClassTypeSet = new HashSet<>();
            //todo 可能需要通过外部配置来保留主dex需要的class


            //在处理过的class的静态初始化方法里插入调用注册本地方法的指令
            //static {
            //    NativeUtils.initClass(0);
            //}

            final ArrayList<File> outDexFiles = injectInstructionAndWriteToFile(
                    globalConfig,
                    mainDexClassTypeSet,
                    60000,
                    apkFolders.getTempDexDir(),
                    generateApplication ? null : classDotNameToType(applicationName));


            final List<String> abis = getAbis(apkFile);

            final Map<String, Map<File, File>> nativeLibs = BuildNativeLib.generateNativeLibs(apkFolders.getOutRootDir(), abis);

            File mainDex = outDexFiles.get(0);

            final File newManDex = internNativeUtilClassDef(
                    mainDex,
                    globalConfig,
                    BuildNativeLib.NMMP_NAME,
                    generateApplication ? classDotNameToType(applicationName) : null);
            //替换为新的dex
            outDexFiles.set(0, newManDex);

            final ArtifactInventory inventory = ArtifactInventory.capture(
                    protectionContext.getBuildId(), packageName, outDexFiles, nativeLibs);
            final byte[] signedInventory;
            try { signedInventory = artifactSigner.sign(inventory); }
            catch (java.security.GeneralSecurityException e) { throw new IOException("Artifact signing failed", e); }

            final File outputApk = apkFolders.getOutputApk();
            if (outputApk.exists()) {
                outputApk.delete();
            }
            try (
                    //输出的zip文件
                    final ZipArchive zipArchive = new ZipArchive(outputApk.toPath());
            ) {
                final ZipMap zipMap = ZipMap.from(apkFile.toPath());
                //添加原apk不被修改的数据
                zipCopy(zipMap, zipArchive, ZipSource.COMPRESSION_NO_CHANGE);

                //add AndroidManifest.xml
                final Source androidManifestSource = Sources.from(new ByteArrayInputStream(manifestBytes), ANDROID_MANIFEST_XML, Deflater.DEFAULT_COMPRESSION);
                androidManifestSource.align(4);
                zipArchive.add(androidManifestSource);

                final Source artifactSource = Sources.from(new ByteArrayInputStream(signedInventory),
                        ArtifactSigner.APK_ENTRY, Deflater.NO_COMPRESSION);
                artifactSource.align(4);
                zipArchive.add(artifactSource);

                //add classesX.dex
                for (File file : outDexFiles) {
                    final Source source = Sources.from(file, file.getName(), Deflater.DEFAULT_COMPRESSION);
                    source.align(4);
                    zipArchive.add(source);
                }

                //add native libs
                for (Map.Entry<String, Map<File, File>> entry : nativeLibs.entrySet()) {
                    final String abi = entry.getKey();
                    for (File file : entry.getValue().values()) {
                        zipArchive.add(nativeLibrarySource(
                                file, "lib/" + abi + "/" + file.getName(), storeNativeLibraries));
                    }

                }
            }
            inventory.verifyApk(outputApk);
            java.nio.file.Files.write(inventoryFile.toPath(), inventory.encode());
        } finally {
            //删除解压缓存目录
            FileUtils.deleteFile(zipExtractDir);
        }
    }

    //根据apk里文件得到abi，如果没有本地库则返回所有
    private static List<String> getAbis(File apk) throws IOException {
        final Pattern pattern = Pattern.compile("lib/(.*)/.*\\.so");
        Set<String> abis = new HashSet<>();
        try (ZipArchive zipArchive = new ZipArchive(apk.toPath())) {
            for (String entry : zipArchive.listEntries()) {
                final Matcher matcher = pattern.matcher(entry);
                if (matcher.matches()) {
                    abis.add(matcher.group(1));
                }
            }
        }
        if (abis.isEmpty()) {
            // A Java-only APK has no native ABI constraint. Use the current
            // single supported runtime, not obsolete multi-ABI preferences.
            return Collections.singletonList("arm64-v8a");
        }
        return new ArrayList<>(abis);
    }

    private static List<File> getClassesFiles(File apkFile, File zipExtractDir) throws IOException {
        List<File> files = ApkUtils.extractFiles(apkFile, "classes(\\d+)*\\.dex", zipExtractDir);
        //根据classes索引大小排序
        files.sort((file, t1) -> {
            final String numb = file.getName().replace("classes", "").replace(".dex", "");
            final String numb2 = t1.getName().replace("classes", "").replace(".dex", "");
            int n, n2;
            if ("".equals(numb)) {
                n = 0;
            } else {
                n = Integer.parseInt(numb);
            }
            if ("".equals(numb2)) {
                n2 = 0;
            } else {
                n2 = Integer.parseInt(numb2);
            }
            return n - n2;
        });
        return files;
    }


    private static File dexWriteToFile(DexPool dexPool, int index, File dexOutDir) throws IOException {
        if (!dexOutDir.exists()) dexOutDir.mkdirs();

        File outDexFile;
        if (index == 0) {
            outDexFile = new File(dexOutDir, "classes.dex");
        } else {
            outDexFile = new File(dexOutDir, String.format("classes%d.dex", index + 1));
        }
        dexPool.writeTo(new FileDataStore(outDexFile));

        return outDexFile;
    }

    private static List<String> getApplicationClassesFromMainDex(GlobalDexConfig globalConfig, String applicationClass) throws IOException {
        final List<String> mainDexClassList = new ArrayList<>();
        String tmpType = classDotNameToType(applicationClass);
        mainDexClassList.add(tmpType);
        for (DexConfig config : globalConfig.getConfigs()) {
            DexBackedDexFile dexFile = DexBackedDexFile.fromInputStream(
                    null,
                    new BufferedInputStream(new FileInputStream(config.getShellDexFile())));
            final Set<? extends DexBackedClassDef> classes = dexFile.getClasses();
            ClassDef classDef;
            while (true) {
                classDef = getClassDefFromType(classes, tmpType);
                if (classDef == null) {
                    break;
                }
                if (classDotNameToType(ANDROID_APP_APPLICATION).equals(classDef.getSuperclass())) {
                    return mainDexClassList;
                }
                tmpType = classDef.getSuperclass();
                mainDexClassList.add(tmpType);
            }


        }
        return mainDexClassList;
    }

    private static ClassDef getClassDefFromType(Set<? extends ClassDef> classDefSet, String type) {
        for (ClassDef classDef : classDefSet) {
            if (classDef.getType().equals(type)) {
                return classDef;
            }
        }
        return null;
    }

    /**
     * 给处理过的class注入静态初始化方法,同时dex适当拆分防止dex索引异常
     * 不缓存写好的dexpool,每次切换dexpool时马上把失效的dexpool写入文件,减小内存占用
     *
     * @param globalConfig
     * @param mainClassSet
     * @param maxPoolSize
     * @param dexOutDir
     * @return
     * @throws IOException
     */
    public static ArrayList<File> injectInstructionAndWriteToFile(GlobalDexConfig globalConfig,
                                                                  Set<String> mainClassSet,
                                                                  int maxPoolSize,
                                                                  File dexOutDir) throws IOException {
        return injectInstructionAndWriteToFile(
                globalConfig, mainClassSet, maxPoolSize, dexOutDir, null);
    }

    public static ArrayList<File> injectInstructionAndWriteToFile(GlobalDexConfig globalConfig,
                                                                  Set<String> mainClassSet,
                                                                  int maxPoolSize,
                                                                  File dexOutDir,
                                                                  String applicationType
    ) throws IOException {

        final ArrayList<File> dexFiles = new ArrayList<>();

        DexPool lastDexPool = null;

        Opcodes opcodes = null;

        final List<DexConfig> configs = globalConfig.getConfigs();
        final String initClass = "L" + configs.get(0).getRegisterNativesClassName() + ";";
        final String initMethod = configs.get(0).getRegisterNativesMethodName();
        //第一个dex为main dex
        //提前处理主dex里的类
        for (DexConfig config : configs) {

            DexBackedDexFile dexNativeFile = DexBackedDexFile.fromInputStream(
                    null,
                    new BufferedInputStream(new FileInputStream(config.getShellDexFile())));
            if (lastDexPool == null) {
                opcodes = dexNativeFile.getOpcodes();
                lastDexPool = new DexPool(opcodes);
            }

            for (ClassDef classDef : dexNativeFile.getClasses()) {
                if (mainClassSet.contains(classDef.getType())) {
                    //可能保留的类太多,导超出致dex引用,又没再接收返回的dex而导致丢失class
                    Dex2c.injectCallRegisterNativeInsns(config, lastDexPool, mainClassSet,
                            maxPoolSize, applicationType, initClass, initMethod);
                }
            }

        }

        for (int i = 0; i < configs.size(); i++) {
            DexConfig config = configs.get(i);
            final List<DexPool> retPools = Dex2c.injectCallRegisterNativeInsns(
                    config, lastDexPool, mainClassSet, maxPoolSize,
                    applicationType, initClass, initMethod);
            if (retPools.isEmpty()) {
                throw new RuntimeException("Dex inject instruction error");
            }
            if (retPools.size() > 1) {
                for (int k = 0; k < retPools.size() - 1; k++) {
                    final int size = dexFiles.size();
                    final File file = dexWriteToFile(retPools.get(k), size, dexOutDir);
                    dexFiles.add(file);
                }

                lastDexPool = retPools.get(retPools.size() - 1);
                if (i == configs.size() - 1) {
                    final int size = dexFiles.size();
                    final File file = dexWriteToFile(lastDexPool, size, dexOutDir);
                    dexFiles.add(file);
                }
            } else {
                final int size = dexFiles.size();
                final File file = dexWriteToFile(retPools.get(0), size, dexOutDir);
                dexFiles.add(file);

                lastDexPool = new DexPool(opcodes);
            }
        }

        return dexFiles;
    }

    /**
     * 复制dex里所有类到新的dex里
     *
     * @param oldDexFile 原dex
     * @param newDex     目标dex
     */
    public static void copyDex(@Nonnull DexFile oldDexFile,
                               @Nonnull DexPool newDex) {
        for (ClassDef classDef : oldDexFile.getClasses()) {
            newDex.internClass(classDef);
        }
    }

    //在主dex里增加NativeUtil类
    //返回处理后的dex文件
    public static File internNativeUtilClassDef(@Nonnull File mainDex,
                                                @Nonnull GlobalDexConfig globalConfig,
                                                @Nonnull String libName) throws IOException {
        return internNativeUtilClassDef(mainDex, globalConfig, libName, null);
    }

    public static File internNativeUtilClassDef(@Nonnull File mainDex,
                                                @Nonnull GlobalDexConfig globalConfig,
                                                @Nonnull String libName,
                                                String generatedApplicationType) throws IOException {


        DexFile mainDexFile = DexBackedDexFile.fromInputStream(
                null,
                new BufferedInputStream(new FileInputStream(mainDex)));

        DexPool newDex = new DexPool(mainDexFile.getOpcodes());


        copyDex(mainDexFile, newDex);

        final ArrayList<String> nativeMethodNames = new ArrayList<>();
        for (DexConfig config : globalConfig.getConfigs()) {
            nativeMethodNames.add(config.getRegisterNativesMethodName());
        }


        final String utilType =
                "L" + globalConfig.getConfigs().get(0).getRegisterNativesClassName() + ";";
        newDex.internClass(new RegisterNativesUtilClassDef(utilType, nativeMethodNames, libName));
        if (generatedApplicationType != null) {
            newDex.internClass(new ApplicationInitClassDef(
                    generatedApplicationType, utilType, nativeMethodNames.get(0)));
        }

        final File injectLoadLib = new File(mainDex.getParent(), "injectLoadLib");
        if (!injectLoadLib.exists()) injectLoadLib.mkdirs();

        final File newFile = new File(injectLoadLib, mainDex.getName());
        newDex.writeTo(new FileDataStore(newFile));

        return newFile;
    }

    private static void zipCopy(ZipMap zipMap, ZipArchive outArchive, int compressionLevel) throws IOException {
        //忽略一些需要修改的文件
        final Pattern regex = Pattern.compile(
                "classes(\\d)*\\.dex" +
                        "|META-INF/.*\\.(RSA|DSA|EC|SF|MF)" +
                        "|AndroidManifest\\.xml");
        //处理后的zip数据
        final ZipSource zipSource = new ZipSource(zipMap);
        for (String entryName : zipMap.getEntries().keySet()) {
            if (regex.matcher(entryName).matches() || ArtifactSigner.isReservedApkEntry(entryName)) {
                continue;
            }
            //不改变压缩数据,4字节对齐
            zipSource.select(entryName, entryName, ZipSource.COMPRESSION_NO_CHANGE, 4);

            //如果需要对apk尽可能压缩, 大概有两种优化:
            //1. 增加压缩级别(需要改zipflinger),可以对需要压缩的文件重新使用zopfli的deflate算法进行极致压缩, https://github.com/eustas/CafeUndZopfli.git
            //2. 对不能压缩的文件,其中如果是png图片使用其他png压缩工具, https://github.com/depsypher/pngtastic.git
        }
        outArchive.add(zipSource);
    }

    static Source nativeLibrarySource(File file, String entryName,
                                      boolean storeNativeLibraries) throws IOException {
        final Source source = Sources.from(file, entryName,
                storeNativeLibraries ? Deflater.NO_COMPRESSION : Deflater.DEFAULT_COMPRESSION);
        source.align(storeNativeLibraries ? NATIVE_LIBRARY_ALIGNMENT : 4);
        return source;
    }


    private static String classDotNameToType(String classDotName) {
        return "L" + classDotName.replace('.', '/') + ";";
    }

    private static String normalizeApplicationName(String packageName, String applicationName) {
        if (applicationName.startsWith(".")) return packageName + applicationName;
        if (applicationName.indexOf('.') < 0) return packageName + "." + applicationName;
        return applicationName;
    }

    private static void requireClassInDexFiles(List<File> dexFiles, String type) throws IOException {
        if (!containsClass(dexFiles, type)) {
            throw new IOException("Application class is not in the base APK dex files: " + type);
        }
    }

    private static void requireClassAbsent(List<File> dexFiles, String type) throws IOException {
        if (containsClass(dexFiles, type)) {
            throw new IOException("Generated Application class already exists: " + type);
        }
    }

    private static boolean containsClass(List<File> dexFiles, String type) throws IOException {
        for (File dexFile : dexFiles) {
            try (InputStream input = new BufferedInputStream(new FileInputStream(dexFile))) {
                final DexBackedDexFile dex = DexBackedDexFile.fromInputStream(null, input);
                if (getClassDefFromType(dex.getClasses(), type) != null) return true;
            }
        }
        return false;
    }


    public static class Builder {
        private final ApkFolders apkFolders;
        private InstructionRewriter instructionRewriter;
        private ClassAndMethodFilter filter;
        private ClassAnalyzer classAnalyzer;


        public Builder(ApkFolders apkFolders) {
            this.apkFolders = apkFolders;
        }

        public Builder setInstructionRewriter(InstructionRewriter instructionRewriter) {
            this.instructionRewriter = instructionRewriter;
            return this;
        }


        public Builder setFilter(ClassAndMethodFilter filter) {
            this.filter = filter;
            return this;
        }

        public Builder setClassAnalyzer(ClassAnalyzer classAnalyzer) {
            this.classAnalyzer = classAnalyzer;
            return this;
        }

        public ApkProtect build() {
            if (instructionRewriter == null) {
                throw new RuntimeException("instructionRewriter == null");
            }
            if (classAnalyzer == null) {
                throw new RuntimeException("classAnalyzer==null");
            }
            return new ApkProtect(apkFolders, instructionRewriter, filter, classAnalyzer);
        }
    }
}

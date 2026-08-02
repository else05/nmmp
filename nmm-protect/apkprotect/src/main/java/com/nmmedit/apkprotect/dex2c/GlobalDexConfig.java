package com.nmmedit.apkprotect.dex2c;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class GlobalDexConfig {

    private final ArrayList<DexConfig> configs = new ArrayList<>();

    private final File outputDir;
    private final boolean signatureBound;

    public GlobalDexConfig(File outputDir) {
        this(outputDir, false);
    }

    public GlobalDexConfig(File outputDir, boolean signatureBound) {
        this.outputDir = outputDir;
        this.signatureBound = signatureBound;
    }

    public File getInitCodeFile() {
        return new File(outputDir, "jni_init.c");
    }

    public void addDexConfig(DexConfig config) {
        configs.add(config);
    }

    public List<DexConfig> getConfigs() {
        return configs;
    }

    public void generateJniInitCode() throws IOException {
        try (
                final Writer writer = new OutputStreamWriter(
                        new FileOutputStream(getInitCodeFile()), StandardCharsets.UTF_8);
        ) {
            generateJniInitCode(writer);
        }
    }

    private void generateJniInitCode(Writer writer) throws IOException {
        if (signatureBound) {
            generateBoundJniInitCode(writer);
            return;
        }
        final StringBuilder includeStaOrExternFunc = new StringBuilder();

        final StringBuilder initCallSta = new StringBuilder();

        for (DexConfig config : configs) {
            final DexConfig.HeaderFileAndSetupFuncName setupFunc = config.getHeaderFileAndSetupFunc();
            includeStaOrExternFunc.append(String.format("extern void %s(JNIEnv *env);\n", setupFunc.setupFunctionName));
            initCallSta.append(String.format(
                    "    %s(env);\n"
                            + "    if ((*env)->ExceptionCheck(env)) return JNI_ERR;\n",
                    setupFunc.setupFunctionName));
        }

        writer.write(String.format(
                "#include <jni.h>\n" +
                        "#include \"GlobalCache.h\"\n" +
                        "\n" +
                        "//auto generated\n" +
                        "%s" +
                        "\n" +
                        "\n" +
                        "JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {\n" +
                        "    JNIEnv *env;\n" +
                        "    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {\n" +
                        "        return -1;\n" +
                        "    }\n" +
                        "    cacheInitial(env);\n" +
                        "    if ((*env)->ExceptionCheck(env)) return JNI_ERR;\n" +
                        "\n" +
                        "\n" +
                        "    //auto generated setup function\n" +
                        "%s" +
                        "\n" +
                        "\n" +
                        "    return JNI_VERSION_1_6;\n" +
                        "}\n\n\n",
                includeStaOrExternFunc.toString(), initCallSta.toString()));
    }

    private void generateBoundJniInitCode(Writer writer) throws IOException {
        final StringBuilder declarations = new StringBuilder();
        final StringBuilder setupCalls = new StringBuilder();
        final StringBuilder activateCalls = new StringBuilder();
        for (DexConfig config : configs) {
            final String setup = config.getHeaderFileAndSetupFunc().setupFunctionName;
            declarations.append(String.format(
                    "extern void %s(JNIEnv *env);\nextern bool %s_activate(JNIEnv *env);\n",
                    setup, setup));
            setupCalls.append(String.format(
                    "    %s(env);\n    if ((*env)->ExceptionCheck(env)) return JNI_ERR;\n",
                    setup));
            activateCalls.append(String.format(
                    "    if (!%s_activate(env) || (*env)->ExceptionCheck(env)) goto failed;\n",
                    setup));
        }

        writer.write("#include <jni.h>\n#include <stdbool.h>\n#include <pthread.h>\n");
        writer.write("#include \"GlobalCache.h\"\n#include \"VmBinding.h\"\n\n");
        writer.write(declarations.toString());
        writer.write(
                "\nstatic pthread_mutex_t gNmmpInitLock = PTHREAD_MUTEX_INITIALIZER;\n"
                        + "static volatile int gNmmpInitState = 0;\n\n"
                        + "bool nmmp_vm_is_ready(void) {\n"
                        + "    return gNmmpInitState == 2;\n"
                        + "}\n\n"
                        + "bool nmmp_vm_activate(JNIEnv *env, jobject context) {\n"
                        + "    pthread_mutex_lock(&gNmmpInitLock);\n"
                        + "    if (gNmmpInitState == 2) {\n"
                        + "        pthread_mutex_unlock(&gNmmpInitLock);\n"
                        + "        return true;\n"
                        + "    }\n"
                        + "    if (gNmmpInitState == -1) {\n"
                        + "        pthread_mutex_unlock(&gNmmpInitLock);\n"
                        + "        return false;\n"
                        + "    }\n"
                        + "    gNmmpInitState = 1;\n"
                        + "    if (!vmBindingActivate(env, context)) goto failed;\n");
        writer.write(activateCalls.toString());
        writer.write(
                "    gNmmpInitState = 2;\n"
                        + "    pthread_mutex_unlock(&gNmmpInitLock);\n"
                        + "    return true;\n"
                        + "failed:\n"
                        + "    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);\n"
                        + "    gNmmpInitState = -1;\n"
                        + "    pthread_mutex_unlock(&gNmmpInitLock);\n"
                        + "    return false;\n"
                        + "}\n\n"
                        + "JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {\n"
                        + "    JNIEnv *env;\n"
                        + "    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {\n"
                        + "        return -1;\n"
                        + "    }\n"
                        + "    cacheInitial(env);\n"
                        + "    if ((*env)->ExceptionCheck(env)) return JNI_ERR;\n");
        writer.write(setupCalls.toString());
        writer.write("    return JNI_VERSION_1_6;\n}\n");
    }
}

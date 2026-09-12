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
                        "#include \"GlobalCache.h\"\n#include \"VmCodec.h\"\n" +
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
                        "    if (!vmCodecActivate(0)) return JNI_ERR;\n" +
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
                    "extern void %s(JNIEnv *env);\nextern bool %s_activate(JNIEnv *env);\nextern bool %s_finish(JNIEnv *env);\n",
                    setup, setup, setup));
            setupCalls.append(String.format(
                    "    %s(env);\n    if ((*env)->ExceptionCheck(env)) return JNI_ERR;\n",
                    setup));
            activateCalls.append(String.format(
                    "    if (!%s_activate(env) || (*env)->ExceptionCheck(env)) goto failed;\n",
                    setup));
        }

        writer.write("#include <jni.h>\n#include <stdbool.h>\n#include <pthread.h>\n");
        writer.write("#include \"GlobalCache.h\"\n#include \"VmBinding.h\"\n#include \"VmInit.h\"\n\n");
        writer.write(declarations.toString());
        writer.write(
                "\nstatic VmInit gNmmpInit = NMMP_VM_INIT;\n"
                        + "static VmInit gNmmpRegister = NMMP_VM_INIT;\n"
                        + "void nmmp_vm_fail(void) { vmInitFail(&gNmmpInit); }\n"
                        + "bool nmmp_vm_failed(void) { return __atomic_load_n(&gNmmpInit.state, __ATOMIC_ACQUIRE) == 3; }\n"
                        + "typedef struct { JNIEnv *env; jobject context; } InitArguments;\n"
                        + "bool nmmp_vm_is_ready(void) { return __atomic_load_n(&gNmmpInit.state, __ATOMIC_ACQUIRE) == 2; }\n"
                        + "bool nmmp_vm_require_ready(void) { return vmInitRequireReady(&gNmmpInit); }\n"
                        + "static bool nmmp_initialize(void *argument) {\n"
                        + "    InitArguments *args = (InitArguments *) argument;\n"
                        + "    JNIEnv *env = args->env;\n"
                        + "    if (!vmBindingActivate(env, args->context)) goto failed;\n");
        writer.write(activateCalls.toString());
        writer.write(
                "    return true;\n"
                        + "failed:\n"
                        + "    return false;\n"
                        + "}\n\n"
                        + "static bool nmmp_register_pending(void *argument) {\n"
                        + "    JNIEnv *env = (JNIEnv *) argument;\n");
        for (DexConfig config : configs)
            writer.write("    if (!" + config.getHeaderFileAndSetupFunc().setupFunctionName + "_finish(env)) return false;\n");
        writer.write("    return nmmp_vm_is_ready() && !(*env)->ExceptionCheck(env);\n}\n\n"
                        + "bool nmmp_vm_activate(JNIEnv *env, jobject context) {\n"
                        + "    InitArguments args = {env, context};\n"
                        + "    if (!vmInitRun(&gNmmpInit, nmmp_initialize, &args)) return false;\n"
                        + "    if (!vmInitIsOwner(&gNmmpRegister) && !vmInitRun(&gNmmpRegister, nmmp_register_pending, env)) {\n"
                        + "        nmmp_vm_fail(); return false;\n"
                        + "    }\n"
                        + "    return nmmp_vm_is_ready() && !(*env)->ExceptionCheck(env);\n}\n\n"
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

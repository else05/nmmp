package com.nmmedit.dex2c;

import com.nmmedit.apkprotect.dex2c.Dex2c;
import com.nmmedit.apkprotect.dex2c.DexConfig;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.NoneInstructionRewriter;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import org.junit.Test;

import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.Assert.assertTrue;

/** Exports actual generated functions for the JNI fault-injection native test. */
public class JniRegistrationFixtureTest {
    @Test public void exportRegistrationFunctions() throws Exception {
        File root = new File("build/jni-registration-fixtures");
        for (boolean bound : new boolean[]{false, true}) {
            String branch = bound ? "bound" : "unbound";
            File directory = new File(root, branch);
            assertTrue(directory.mkdirs() || directory.isDirectory());
            ClassAnalyzer analyzer = new ClassAnalyzer();
            try (InputStream input = getClass().getResourceAsStream("/classes2.dex")) {
                analyzer.loadDexFile(DexBackedDexFile.fromInputStream(null, input));
            }
            ProtectionContext context = bound
                    ? ProtectionContext.createBound("com.example.app", new byte[]{1, 2, 3})
                    : new ProtectionContext(0x0123456789abcdefL);
            DexConfig config;
            try (InputStream input = getClass().getResourceAsStream("/classes2.dex")) {
                config = Dex2c.handleDex(input, "classes.dex", Dex2cTest.testFilter,
                        analyzer, new NoneInstructionRewriter(), directory, context);
            }
            String source = new String(Files.readAllBytes(config.getNativeFunctionsFile().toPath()),
                    StandardCharsets.UTF_8);
            Matcher matcher = Pattern.compile("static void (\\w+)\\(JNIEnv \\*env, jclass jcls, jint dataIdx\\)").matcher(source);
            assertTrue(matcher.find());
            String callback = matcher.group(1);
            if (!bound) {
                Files.write(new File(root, "register.inc").toPath(),
                        function(source, matcher.start()).replace(callback, "fixture_register")
                                .getBytes(StandardCharsets.UTF_8));
            } else {
                Files.write(new File(root, "bound_register.inc").toPath(),
                        function(source, matcher.start()).replace(callback, "fixture_bound_register")
                                .getBytes(StandardCharsets.UTF_8));
            }
            int setup = source.indexOf("void classes_setup(JNIEnv *env)");
            assertTrue(setup >= 0);
            Files.write(new File(root, branch + "_setup.inc").toPath(),
                    function(source, setup).replace("classes_setup", branch + "_setup")
                            .replace(callback, "fixture_register").getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String function(String source, int start) {
        int depth = 0;
        for (int i = source.indexOf('{', start); i < source.length(); ++i) {
            if (source.charAt(i) == '{') ++depth;
            if (source.charAt(i) == '}' && --depth == 0) return source.substring(start, i + 1) + "\n";
        }
        throw new AssertionError("Unterminated generated function");
    }
}

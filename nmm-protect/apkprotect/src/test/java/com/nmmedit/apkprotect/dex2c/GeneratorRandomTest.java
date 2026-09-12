package com.nmmedit.apkprotect.dex2c;

import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.RandomInstructionRewriter;
import com.nmmedit.apkprotect.util.CmakeUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.io.StringWriter;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.*;

public class GeneratorRandomTest {
    private static final String[] SEEDS = {
            "0123456789abcdef", "fedcba9876543210", "6a09e667f3bcc909"};
    private String previousSeed, previousMode;

    @Before public void saveSettings() {
        previousSeed = System.getProperty("nmmp.testSeed");
        previousMode = System.getProperty("vmDecodeMode");
        System.setProperty("vmDecodeMode", "on-demand-v1");
    }

    @After public void restoreSettings() {
        restore("nmmp.testSeed", previousSeed);
        restore("vmDecodeMode", previousMode);
    }

    private static void restore(String name, String value) {
        if (value == null) System.clearProperty(name); else System.setProperty(name, value);
    }

    /** Run the real property/environment resolution in an isolated JVM. */
    public static final class SettingsProbe {
        public static void main(String[] args) {
            Random random = GeneratorRandom.create("settings-probe");
            Long seed = GeneratorRandom.configuredTestSeed();
            System.out.println(seed == null ? "SECURE=" + (random instanceof SecureRandom)
                    : String.format("SEED=%016x RANDOM=%016x", seed, random.nextLong()));
        }
    }

    private static String probe(String property, String environment, boolean success) throws Exception {
        List<String> args = new ArrayList<>();
        args.add(new File(System.getProperty("java.home"), "bin/java").getPath());
        if (property != null) args.add("-Dnmmp.testSeed=" + property);
        args.add("-cp");
        args.add(new File(GeneratorRandom.class.getProtectionDomain().getCodeSource().getLocation().toURI()).getPath()
                + File.pathSeparator
                + new File(GeneratorRandomTest.class.getProtectionDomain().getCodeSource().getLocation().toURI()).getPath());
        args.add(SettingsProbe.class.getName());
        ProcessBuilder builder = new ProcessBuilder(args).redirectErrorStream(true);
        builder.environment().remove("JAVA_TOOL_OPTIONS");
        builder.environment().remove("_JAVA_OPTIONS");
        builder.environment().remove("JDK_JAVA_OPTIONS");
        if (environment == null) builder.environment().remove("NMMP_TEST_SEED");
        else builder.environment().put("NMMP_TEST_SEED", environment);
        Process process = builder.start();
        try {
            assertTrue("settings probe timed out", process.waitFor(10, TimeUnit.SECONDS));
            StringBuilder output = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) output.append(line).append('\n');
            }
            assertEquals(output.toString(), success, process.exitValue() == 0);
            return output.toString();
        } finally {
            process.destroy();
        }
    }

    @Test public void productionSettingsPrecedenceAndSecureDefault() throws Exception {
        assertEquals("SECURE=true\n", probe(null, null, true));
        String fromEnvironment = probe(null, SEEDS[0], true);
        assertTrue(fromEnvironment.startsWith("SEED=" + SEEDS[0]));
        assertEquals(fromEnvironment, probe(SEEDS[0], "invalid-shadowed-value", true));
        assertTrue(probe(SEEDS[1], SEEDS[0], true).startsWith("SEED=" + SEEDS[1]));
        assertTrue(probe("", SEEDS[0], false).contains("exactly 16 hexadecimal digits"));
        assertTrue(probe(null, "0x" + SEEDS[0], false).contains("exactly 16 hexadecimal digits"));
    }

    @Test public void strictUnsignedHexAndInvalidSeedStopsGenerators() {
        for (String invalid : new String[]{"", "123", "0123456789abcde", "0123456789abcdef0",
                "0x0123456789abcdef", "-123456789abcdef", " 123456789abcdef", "0123456789abcdeg"}) {
            System.setProperty("nmmp.testSeed", invalid);
            expectInvalid(() -> GeneratorRandom.create("x"));
            expectInvalid(ProtectionContext::create);
            expectInvalid(RandomInstructionRewriter::new);
            expectInvalid(() -> NativeProgram.root(77));
            expectInvalid(() -> new DemandModule(1, 2, 3));
        }
        System.setProperty("nmmp.testSeed", "FEDCBA9876543210");
        assertEquals(Long.valueOf(0xfedcba9876543210L), GeneratorRandom.configuredTestSeed());
    }

    private static void expectInvalid(Runnable action) {
        try { action.run(); fail("invalid seed accepted"); }
        catch (IllegalArgumentException expected) {
            assertTrue(expected.getMessage().contains("exactly 16 hexadecimal digits"));
        }
    }

    @Test public void streamsAreIndependentOfOtherConsumersAndStableIds() {
        System.setProperty("nmmp.testSeed", SEEDS[0]);
        long expected = GeneratorRandom.create("opcode").nextLong();
        Random noise = GeneratorRandom.create("native-root", 77);
        for (int i = 0; i < 10000; ++i) noise.nextLong();
        assertEquals(expected, GeneratorRandom.create("opcode").nextLong());
        assertNotEquals(expected, GeneratorRandom.create("context").nextLong());
        assertNotEquals(GeneratorRandom.create("demand", 1).nextLong(),
                GeneratorRandom.create("demand", 2).nextLong());
    }

    @Test public void realOpcodeNativeModuleAndContextReproduceAcrossThreeSeeds() throws Exception {
        Set<String> opcodeVariants = new HashSet<>(), nativeVariants = new HashSet<>(), moduleVariants = new HashSet<>();
        Set<Long> roots = new HashSet<>(), buildIds = new HashSet<>();
        for (String seed : SEEDS) {
            System.setProperty("nmmp.testSeed", seed);
            ProtectionContext a = ProtectionContext.createBound("test.seed", new byte[]{1, 2, 3});
            String opcode = opcode();
            NativeProgram.Program nativeProgram = NativeProgram.root(77);
            byte[] module = module(9);
            // Consume unrelated streams before repeating the actual public entry points.
            module(10); NativeProgram.root(78);
            ProtectionContext b = ProtectionContext.createBound("test.seed", new byte[]{1, 2, 3});
            assertEquals(a.getBuildSeed(), b.getBuildSeed());
            assertEquals(a.getBuildId(), b.getBuildId());
            assertEquals(a.getSeedData(), b.getSeedData());
            assertEquals(a.getBuildSeed(), ProtectionContext.create().getBuildSeed());
            assertEquals(opcode, opcode());
            NativeProgram.Program again = NativeProgram.root(77);
            assertArrayEquals(nativeProgram.code, again.code);
            assertArrayEquals(nativeProgram.opcodes, again.opcodes);
            assertEquals(nativeProgram.key, again.key);
            assertArrayEquals(module, module(9));
            NativeProgram.Result result = NativeProgram.run(nativeProgram, new long[]{1, 123, 456, 77});
            assertTrue(result.success); assertEquals(123 ^ 456, result.value);
            assertFalse(NativeProgram.run(nativeProgram, new long[]{1, 123, 456, 78}).success);
            assertEquals(0, new RandomInstructionRewriter().replaceOpcode(com.android.tools.smali.dexlib2.Opcode.NOP));
            opcodeVariants.add(opcode); nativeVariants.add(Arrays.toString(nativeProgram.code));
            moduleVariants.add(Arrays.toString(module)); roots.add(a.getBuildSeed()); buildIds.add(a.getBuildId());
        }
        assertEquals(3, opcodeVariants.size()); assertEquals(3, nativeVariants.size());
        assertEquals(3, moduleVariants.size()); assertEquals(3, roots.size()); assertEquals(3, buildIds.size());
    }

    private static String opcode() throws Exception {
        StringWriter opcodes = new StringWriter(), gotos = new StringWriter();
        new RandomInstructionRewriter().generateConfig(opcodes, gotos);
        return opcodes.toString() + gotos;
    }

    private static byte[] module(long moduleId) {
        long root = 0x0123456789abcdefL;
        DemandModule module = new DemandModule(root, moduleId, 77);
        for (int id = 1; id <= 8; ++id) {
            long tag = DemandCodec.tag("LSeed;->method" + id + "()V");
            byte[] boundaries = {1};
            byte[] code = DemandCodec.transformCode(new byte[]{0x0e, 0},
                    DemandCodec.seed(root, id, tag, 2, 0), boundaries);
            module.add(id, 2, 0, new DemandCodec.Encoded(code, new byte[0], boundaries, tag));
        }
        return module.build().blob;
    }

    @Test public void productionLayoutWritersUseSeparateReproducibleStreams() throws Exception {
        StringBuilder body = new StringBuilder("typedef struct {\n");
        for (int i = 0; i < 20; ++i) body.append("    void (*fn").append(i).append(")(void);\n");
        Set<String> resolverVariants = new HashSet<>(), wrapperVariants = new HashSet<>();
        for (String seed : SEEDS) {
            System.setProperty("nmmp.testSeed", seed);
            String resolver = layout("writeRandomResolver", body + "} vmResolver;\n");
            String wrapper = layout("writeRandomJNIWrapper", body + "} JNIWrapper;\n");
            assertEquals(resolver, layout("writeRandomResolver", body + "} vmResolver;\n"));
            assertEquals(wrapper, layout("writeRandomJNIWrapper", body + "} JNIWrapper;\n"));
            assertNotEquals(resolver.replace("vmResolver", "JNIWrapper"), wrapper);
            for (int i = 0; i < 20; ++i) {
                assertTrue(resolver.contains("(*fn" + i + ")(void);"));
                assertTrue(wrapper.contains("(*fn" + i + ")(void);"));
            }
            resolverVariants.add(resolver); wrapperVariants.add(wrapper);
        }
        assertEquals(3, resolverVariants.size()); assertEquals(3, wrapperVariants.size());
    }

    private static String layout(String name, String source) throws Exception {
        Path file = Files.createTempFile("nmmp-seed-layout", ".h");
        try {
            Files.write(file, source.getBytes(StandardCharsets.UTF_8));
            Method method = CmakeUtils.class.getDeclaredMethod(name, File.class);
            method.setAccessible(true);
            method.invoke(null, file.toFile());
            return new String(Files.readAllBytes(file), StandardCharsets.UTF_8);
        } finally {
            // Existing template writers retain their reader until GC; Windows denies immediate deletion.
            file.toFile().deleteOnExit();
        }
    }
}

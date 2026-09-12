import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.google.gson.GsonBuilder;
import com.nmmedit.apkprotect.dex2c.MethodCodec;
import com.nmmedit.apkprotect.dex2c.DemandCodec;
import com.android.tools.smali.dexlib2.util.MethodUtil;
import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.MyMethodUtil;
import com.nmmedit.apkprotect.dex2c.converter.ResolverCodeGenerator;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.InstructionRewriter;
import com.nmmedit.apkprotect.sign.SignatureBinding;
import java.io.*;
import java.nio.file.*;
import java.nio.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import java.util.zip.ZipFile;

/** Offline validation only. Recompute rewritten bytes from each selected implementation DEX. */
public class AuditDemandMethods {
    static String match(String text, String expression) {
        Matcher matcher = Pattern.compile(expression, Pattern.DOTALL | Pattern.MULTILINE).matcher(text);
        if (!matcher.find()) throw new IllegalStateException("Missing generated field: " + expression);
        return matcher.group(1);
    }

    static long field(String text, String name) {
        return Long.decode(match(text, "\\." + name + "=(0x[0-9a-f]+|[0-9]+)"));
    }

    static byte[] array(String body, String name) {
        if (body.contains("const u1 *" + name + " = NULL")) return new byte[0];
        String content = match(body, name + "\\[\\] = \\{(.*?)\\};");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        Matcher matcher = Pattern.compile("0x([0-9a-f]{2})").matcher(content);
        while (matcher.find()) bytes.write(Integer.parseInt(matcher.group(1), 16));
        return bytes.toByteArray();
    }

    static String hash(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    static void require(boolean condition, String message) {
        if (!condition) throw new IllegalStateException(message);
    }

    static class FrozenRewriter extends InstructionRewriter {
        final Map<String, Integer> numbers = new HashMap<>();
        FrozenRewriter(String header) {
            super(Opcodes.forDexVersion(39));
            Matcher matcher = Pattern.compile("OP_(\\w+)\\s*=\\s*0x([0-9a-f]+)").matcher(header);
            while (matcher.find()) numbers.put(matcher.group(1), Integer.parseInt(matcher.group(2), 16));
        }
        public int replaceOpcode(Opcode opcode) {
            return numbers.get(opcode.name.replace('-', '_').replace('/', '_').toUpperCase(Locale.ROOT));
        }
        protected List<Opcode> getOpcodeList() { throw new UnsupportedOperationException("Audit does not generate tables"); }
    }

    static void auditInt(OutputStream out, int x) throws IOException {
        for (int b = 0; b < 4; ++b) out.write(x >>> (8 * b));
    }

    public static void main(String[] args) throws Exception {
        Path work = Path.of(args[0]);
        Path out = Path.of(args[1]);
        Path generated = work.resolve("input/build/dex2c/generated");
        Path configPath = work.resolve("input/build/dex2c/vm/include/VmCodecConfig.h");
        String config = Files.readString(configPath);
        require(config.contains("#define NMMP_VM_CODEC_VERSION 3"), "Unexpected codec version");
        require(config.contains("#define NMMP_VM_SIGNATURE_BINDING 1"), "Signature binding disabled");
        long seedData = Long.parseUnsignedLong(match(config, "NMMP_VM_SEED_DATA UINT64_C\\(0x([0-9a-f]+)\\)"), 16);
        long buildId = Long.parseUnsignedLong(match(config, "NMMP_VM_BUILD_ID UINT64_C\\(0x([0-9a-f]+)\\)"), 16);
        byte[] certificate = SignatureBinding.readSingleSignerCertificate(work.resolve("input/original.apk").toFile());
        long seed = seedData ^ SignatureBinding.deriveMask("org.savior.sync", certificate, buildId);
        MethodCodec codec = new MethodCodec(seed);
        ClassAnalyzer analyzer = new ClassAnalyzer();
        analyzer.setMinSdk(26);
        try (ZipFile zip = new ZipFile(work.resolve("input/original.apk").toFile())) {
            for (var entry : Collections.list(zip.entries())) {
                if (entry.getName().matches("classes[0-9]*\\.dex")) {
                    try (InputStream input = new BufferedInputStream(zip.getInputStream(entry))) {
                        analyzer.loadDexFile(DexBackedDexFile.fromInputStream(null, input));
                    }
                }
            }
        }
        FrozenRewriter rewriter = new FrozenRewriter(Files.readString(work.resolve("input/build/dex2c/vm/DexOpcodes.h")));
        List<Map<String, Object>> rows = new ArrayList<>();
        Set<Long> ids = new HashSet<>();
        SortedSet<String> descriptors = new TreeSet<>();
        Map<String, Integer> formats = new TreeMap<>();
        Map<String, Integer> opcodes = new TreeMap<>();
        Set<String> unsupported = Set.of("Format35mi", "Format35ms", "Format3rmi", "Format3rms", "Format45cc", "Format4rcc", "UnresolvedOdexInstruction");
        long dexId = 0;
        for (Path impl : Files.list(generated).filter(p -> p.toString().endsWith("_impl.dex")).sorted().toList()) {
            DexBackedDexFile dex;
            try (InputStream input = new BufferedInputStream(Files.newInputStream(impl))) {
                dex = DexBackedDexFile.fromInputStream(null, input);
            }
            String source = Files.readString(impl.resolveSibling(impl.getFileName().toString().replace("_impl.dex", "_native_functions.c")));
            byte[] blob = array(source, "nmmpModuleBlob");
            ByteBuffer header = ByteBuffer.wrap(blob).order(ByteOrder.LITTLE_ENDIAN);
            long moduleId = Integer.toUnsignedLong(header.getInt(16));
            int count = header.getInt(20);
            require(header.getInt(44) == blob.length && header.getLong(48) == buildId, "Module identity");
            Map<Long, Integer> directory = new HashMap<>();
            long previous = -1;
            for (int i = 0; i < count; ++i) {
                long token = Integer.toUnsignedLong(header.getInt(64 + i * 8));
                int offset = header.getInt(68 + i * 8);
                require(token > previous && offset >= 64 + count * 8 && (long)offset + 56 <= header.getInt(28), "Directory bounds/order");
                directory.put(token, offset); previous = token;
            }
            byte[] maps = codec.transform(Arrays.copyOfRange(blob, header.getInt(28), header.getInt(28) + 1024), moduleId, 7);
            byte[] moduleBounds = codec.transform(Arrays.copyOfRange(blob, header.getInt(32), header.getInt(32) + header.getInt(36)), moduleId, 8);
            ByteArrayOutputStream nativeAudit = new ByteArrayOutputStream();
            auditInt(nativeAudit, (int)seed); auditInt(nativeAudit, (int)(seed >>> 32));
            auditInt(nativeAudit, (int)moduleId); auditInt(nativeAudit, (int)buildId); auditInt(nativeAudit, (int)(buildId >>> 32));
            auditInt(nativeAudit, blob.length); nativeAudit.write(blob); auditInt(nativeAudit, count);
            Map<String, String> bodies = new HashMap<>();
            Matcher wrappers = Pattern.compile("^(?:static|JNIEXPORT) \\w+ (Java_\\w+)\\([^\\n]*\\) \\{(.*?)^}", Pattern.MULTILINE | Pattern.DOTALL).matcher(source);
            while (wrappers.find()) {
                if (wrappers.group(2).contains("vmExecuteToken("))
                    require(bodies.put(wrappers.group(1), wrappers.group(2)) == null, "Duplicate wrapper");
            }
            ResolverCodeGenerator resolver = new ResolverCodeGenerator(dex, analyzer, new ProtectionContext(seed), dexId++);
            rewriter.loadReferences(resolver.getReferences(), analyzer);
            for (ClassDef cls : dex.getClasses()) for (Method method : cls.getMethods()) {
                if (method.getImplementation() == null) continue;
                String descriptor = method.getDefiningClass() + "->" + method.getName() + "(" + String.join("", method.getParameterTypes()) + ")" + method.getReturnType();
                String type = method.getDefiningClass();
                String wrapper = MyMethodUtil.getJniFunctionName(type.substring(1, type.length() - 1), method.getName(), method.getParameterTypes(), method.getReturnType());
                String body = bodies.remove(wrapper);
                require(body != null, "Missing wrapper: " + descriptor);
                long token = Long.parseUnsignedLong(match(body, "vmExecuteToken\\(env, &nmmpModule, UINT32_C\\(0x([0-9a-f]+)\\)"), 16);
                Integer offset = directory.remove(token);
                require(offset != null, "Missing token: " + descriptor);
                byte[] record = codec.transform(Arrays.copyOfRange(blob, offset, offset + 56), token, 6);
                ByteBuffer r = ByteBuffer.wrap(record).order(ByteOrder.LITTLE_ENDIAN);
                require(r.getInt(48) == 3 && r.getInt(52) == MethodCodec.hash(Arrays.copyOf(record, 52)), "Record integrity");
                long methodId = Integer.toUnsignedLong(r.getInt(0));
                require(ids.add(methodId), "Duplicate method ID");
                require(descriptors.add(descriptor), "Duplicate method descriptor");
                int units = 0;
                for (Instruction instruction : method.getImplementation().getInstructions()) {
                    String format = instruction.getOpcode().format.name();
                    require(!unsupported.contains(format), "Unsupported selected format: " + descriptor + " " + format);
                    formats.merge(format, 1, Integer::sum);
                    opcodes.merge(instruction.getOpcode().name, 1, Integer::sum);
                    units += instruction.getCodeUnits();
                }
                byte[] code = rewriter.rewriteInstructions(method);
                byte[] tries = rewriter.handleTries(method.getImplementation());
                byte[] encodedCode = Arrays.copyOfRange(blob, r.getInt(20), r.getInt(20) + r.getInt(24));
                byte[] encodedTries = Arrays.copyOfRange(blob, r.getInt(28), r.getInt(28) + r.getInt(32));
                byte[] boundaries = Arrays.copyOfRange(moduleBounds, r.getInt(36), r.getInt(36) + r.getInt(40));
                int ins = MethodUtil.getParameterRegisterCount(method.getParameterTypes(), (method.getAccessFlags() & 8) != 0);
                DemandCodec.Encoded expected = DemandCodec.encode(method, code, tries, seed, methodId, ins);
                require(code.length == units * 2, "Instruction width lost: " + descriptor);

                require(Arrays.equals(encodedTries, expected.tries), "Encoded tries mismatch: " + descriptor);
                require(Arrays.equals(boundaries, expected.boundaries), "Boundary recipe mismatch: " + descriptor);
                long methodSeed = DemandCodec.seed(seed, methodId, expected.descriptorTag, method.getImplementation().getRegisterCount(), ins);
                require(r.getLong(4) == expected.descriptorTag && r.getInt(12) == method.getImplementation().getRegisterCount()
                        && r.getInt(16) == ins && r.getInt(44) == (methodSeed & 3), "Method context mismatch");
                byte[] decoded = DemandCodec.transformCode(encodedCode, methodSeed, boundaries);
                for (int pc = 0; pc < decoded.length / 2; ++pc)
                    if (DemandCodec.kind(boundaries, pc) == 1)
                        decoded[pc * 2] = maps[r.getInt(44) * 256 + (decoded[pc * 2] & 255)];
                require(Arrays.equals(code, decoded), "Mapped code byte mismatch: " + descriptor);
                auditInt(nativeAudit, (int)token); auditInt(nativeAudit, r.getInt(12));
                auditInt(nativeAudit, code.length); auditInt(nativeAudit, tries.length); auditInt(nativeAudit, boundaries.length);
                nativeAudit.write(code); nativeAudit.write(tries); nativeAudit.write(boundaries);
                require(Arrays.equals(tries, new MethodCodec(methodSeed).transform(encodedTries, 0, 5)), "Try byte mismatch: " + descriptor);
                Map<String, Object> row = new LinkedHashMap<>();
                row.put("descriptor", descriptor); row.put("methodId", methodId);
                row.put("dex", impl.getFileName().toString()); row.put("codeBytes", code.length);
                row.put("triesBytes", tries.length); row.put("codeSha256", hash(code)); row.put("triesSha256", hash(tries));
                row.put("bytewiseRewrittenCodeAndTriesMatch", true); rows.add(row);
            }
            require(bodies.isEmpty(), "Unaccounted wrappers: " + bodies.keySet());
            require(directory.isEmpty(), "Unaccounted module tokens");
            Files.write(out.resolve(impl.getFileName().toString().replace("_impl.dex", ".native-audit")), nativeAudit.toByteArray());
        }
        require(rows.size() == 526, "Selected method count differs from build report");
        Set<String> packagedNativeMethods = new HashSet<>();
        try (ZipFile zip = new ZipFile(Path.of(args[2]).toFile())) {
            for (var entry : Collections.list(zip.entries())) {
                if (!entry.getName().matches("classes[0-9]*\\.dex")) continue;
                DexBackedDexFile dex;
                try (InputStream input = new BufferedInputStream(zip.getInputStream(entry))) {
                    dex = DexBackedDexFile.fromInputStream(null, input);
                }
                for (ClassDef cls : dex.getClasses()) for (Method method : cls.getMethods()) {
                    String descriptor = method.getDefiningClass() + "->" + method.getName() + "(" + String.join("", method.getParameterTypes()) + ")" + method.getReturnType();
                    if (!descriptors.contains(descriptor)) continue;
                    require((method.getAccessFlags() & 0x100) != 0 && method.getImplementation() == null,
                            "Selected method still has a Java body: " + descriptor);
                    require(packagedNativeMethods.add(descriptor), "Duplicate packaged method");
                }
            }
        }
        require(packagedNativeMethods.equals(descriptors), "Packaged native method set differs from audited set");
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("methods", rows); report.put("count", rows.size());
        report.put("packagedNativeMethodSetExactMatch", true);
        report.put("instructionFormats", formats); report.put("opcodes", opcodes);
        report.put("generatedConfigSha256", hash(Files.readAllBytes(configPath)));
        report.put("signerSha256", hash(certificate));
        report.put("scope", "Offline generated codec-3 code/tries versus freshly rewritten selected implementation DEX; not proof of complete runtime semantic coverage");
        Files.writeString(out.resolve("all-method-audit.json"), new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(report));
        Files.write(out.resolve("selected-descriptors.txt"), descriptors);
        System.out.println("PASS: " + rows.size() + " selected methods, code and tries bytewise equal; no dropped instruction widths or unsupported formats");
    }
}

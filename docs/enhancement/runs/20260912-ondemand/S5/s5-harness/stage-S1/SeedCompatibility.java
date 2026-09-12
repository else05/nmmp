import com.nmmedit.apkprotect.dex2c.ProtectionContext;
import com.nmmedit.apkprotect.dex2c.converter.instructionrewriter.RandomInstructionRewriter;
import com.nmmedit.apkprotect.util.CmakeUtils;
import java.io.*;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;

public final class SeedCompatibility {
    private static String hash(String text) throws Exception {
        byte[] bytes = MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));
        StringBuilder result = new StringBuilder();
        for (byte value : bytes) result.append(String.format("%02x", value & 255));
        return result.toString();
    }
    public static void main(String[] args) throws Exception {
        for (String seed : new String[]{"0123456789abcdef", "fedcba9876543210", "6a09e667f3bcc909"}) {
            System.setProperty("nmmp.testSeed", seed);
            ProtectionContext context = ProtectionContext.createBound("test.seed", new byte[]{1,2,3});
            StringWriter opcodes = new StringWriter(), gotos = new StringWriter();
            new RandomInstructionRewriter().generateConfig(opcodes, gotos);
            StringBuilder line = new StringBuilder(String.format("%s %016x %016x %016x %s", seed,
                    context.getBuildSeed(), context.getBuildId(), context.getSeedData(), hash(opcodes.toString() + gotos)));
            for (String kind : new String[]{"Resolver", "JNIWrapper"}) {
                StringBuilder source = new StringBuilder("typedef struct {\n");
                for (int i=0;i<20;++i) source.append("    void (*fn").append(i).append(")(void);\n");
                source.append("} ").append(kind.equals("Resolver") ? "vmResolver" : kind).append(";\n");
                Path file = Paths.get(args[0], seed + "-" + kind + ".h");
                Files.write(file, source.toString().getBytes(StandardCharsets.UTF_8));
                Method writer = CmakeUtils.class.getDeclaredMethod("writeRandom" + kind, File.class);
                writer.setAccessible(true); writer.invoke(null, file.toFile());
                line.append(' ').append(hash(new String(Files.readAllBytes(file), StandardCharsets.UTF_8)));
            }
            System.out.println(line);
        }
    }
}

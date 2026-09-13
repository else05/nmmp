import com.nmmedit.apkprotect.dex2c.NativeProgram;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/** Reproduce the unbound development VM header; APK generation overwrites it. */
public class GenerateDefaultRoot {
    public static void main(String[] args) throws Exception {
        System.setProperty("nmmp.testSeed", "0000000000000000");
        try (java.io.Writer writer = Files.newBufferedWriter(Path.of(args[0]), StandardCharsets.UTF_8)) {
            writer.write("// Development root program for build ID 0. Replaced for every protected build.\n");
            NativeProgram.root(0).writeHeader(writer);
        }
    }
}

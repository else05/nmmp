package com.nmmedit.apkprotect.dex2c;

import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.immutable.*;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction11x;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction21c;
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableStringReference;
import com.android.tools.smali.dexlib2.writer.io.MemoryDataStore;
import com.android.tools.smali.dexlib2.writer.pool.DexPool;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.nmmedit.apkprotect.dex2c.converter.References;
import com.nmmedit.apkprotect.dex2c.converter.ResolverCodeGenerator;
import com.nmmedit.apkprotect.util.ModifiedUtf8;
import org.junit.Test;

import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.io.ByteArrayOutputStream;

import static org.junit.Assert.*;

/** Cross-language fixture: native tests execute the actual generated resolver. */
public class ResolverStringsFixtureTest {
    @Test public void exportEncryptedResolver() throws Exception {
        String value = "a\u0000中文\ud83d\ude00";
        ImmutableMethod method = new ImmutableMethod("Ltest/Strings;", "run",
                Collections.singletonList(new ImmutableMethodParameter("[Ljava/lang/String;", Collections.emptySet(), null)),
                "Ljava/lang/String;", 9, Collections.emptySet(), Collections.emptySet(),
                new ImmutableMethodImplementation(2, Arrays.asList(
                        new ImmutableInstruction21c(Opcode.CONST_STRING, 0, new ImmutableStringReference("")),
                        new ImmutableInstruction21c(Opcode.CONST_STRING, 0, new ImmutableStringReference(value)),
                        new ImmutableInstruction11x(Opcode.RETURN_OBJECT, 0)),
                        Collections.emptyList(), Collections.emptyList()));
        ImmutableField field = new ImmutableField("Ltest/Strings;", "count", "I", 9, null,
                Collections.emptySet(), Collections.emptySet());
        DexPool pool = new DexPool(Opcodes.forApi(26));
        pool.internClass(new ImmutableClassDef("Ltest/Strings;", 1, "Ljava/lang/Object;",
                Collections.emptyList(), null, Collections.emptyList(),
                Collections.singletonList(field), Collections.singletonList(method)));
        MemoryDataStore store = new MemoryDataStore();
        pool.writeTo(store);
        DexBackedDexFile dex = new DexBackedDexFile(Opcodes.forApi(26), store.getData());
        ClassAnalyzer analyzer = new ClassAnalyzer();
        analyzer.loadDexFile(dex);
        ProtectionContext context = new ProtectionContext(0x0123456789abcdefL);
        ResolverCodeGenerator generator = new ResolverCodeGenerator(dex, analyzer, context, 1);
        StringWriter output = new StringWriter();
        generator.generate(output);
        String code = output.toString();
        assertTrue(code.contains("static const u1 gBaseStrPtr"));
        assertFalse(code.contains("decodeStringPool"));
        assertFalse(code.contains("STRING_BY_ID"));

        String array = code.substring(code.indexOf("static const u1 gBaseStrPtr"));
        array = array.substring(0, array.indexOf("};"));
        Matcher bytes = Pattern.compile("0x([0-9a-f]{2}),").matcher(array);
        ByteArrayOutputStream cipher = new ByteArrayOutputStream();
        while (bytes.find()) cipher.write(Integer.parseInt(bytes.group(1), 16));
        ByteArrayOutputStream plain = new ByteArrayOutputStream();
        for (String text : generator.getReferences().getStringPool()) {
            plain.write(ModifiedUtf8.encode(text)); plain.write(0);
        }
        assertArrayEquals(plain.toByteArray(), context.getMethodCodec().transform(cipher.toByteArray(), 1, MethodCodec.DOMAIN_STRING));

        References refs = generator.getReferences();
        StringBuilder indices = new StringBuilder();
        indices.append("#define TEST_CLASS ").append(refs.getTypeItemIndex("Ltest/Strings;")).append("u\n");
        indices.append("#define TEST_ARRAY ").append(refs.getTypeItemIndex("[Ljava/lang/String;")).append("u\n");
        indices.append("#define TEST_STRING ").append(refs.getConstantStringPool().indexOf(value)).append("u\n");
        indices.append("#define TEST_EMPTY ").append(refs.getConstantStringPool().indexOf("")).append("u\n");
        indices.append("static const unsigned char expectedString[] = {");
        for (byte b : ModifiedUtf8.encode(value)) indices.append(b & 255).append(',');
        indices.append("0};\nstatic const unsigned char expectedManifest[] = {");
        for (byte b : generator.getManifestDigest()) indices.append(b & 255).append(',');
        indices.append("};\n");
        Path directory = Paths.get("build/resolver-string-fixtures");
        Files.createDirectories(directory);
        Files.write(directory.resolve("resolver.inc"), code.getBytes(StandardCharsets.UTF_8));
        Files.write(directory.resolve("indices.inc"), indices.toString().getBytes(StandardCharsets.UTF_8));
    }
}

package com.nmmedit.apkprotect.dex2c.filters;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.immutable.ImmutableClassDef;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodParameter;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction11n;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction11x;
import com.nmmedit.apkprotect.deobfus.MappingReader;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class ProguardMappingConfigTest {

    @Test
    public void testMatchesMethodWithObfuscatedOwner() throws IOException {
        final String mapping =
                "org.example.Original -> a.b:\n"
                        + "    void target() -> c\n";
        final SimpleRules rules = new SimpleRules();
        rules.parse(new StringReader(
                "class org.example.Original {\n"
                        + "  target;\n"
                        + "}\n"));
        final ProguardMappingConfig filter = new ProguardMappingConfig(
                new BasicKeepConfig(),
                new MappingReader(new ByteArrayInputStream(
                        mapping.getBytes(StandardCharsets.UTF_8))),
                rules);

        final ImmutableMethod method = new ImmutableMethod(
                "La/b;",
                "c",
                Collections.emptyList(),
                "V",
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        0,
                        Arrays.asList(
                                new ImmutableInstruction10x(Opcode.NOP),
                                new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                        Collections.emptyList(),
                        Collections.emptyList()));
        final ImmutableClassDef classDef = new ImmutableClassDef(
                "La/b;",
                AccessFlags.PUBLIC.getValue(),
                "Ljava/lang/Object;",
                Collections.emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Collections.singletonList(method));

        assertTrue(filter.acceptClass(classDef));
        assertTrue(filter.acceptMethod(method));
    }

    @Test
    public void testMatchesMethodWithObfuscatedReturnType() throws IOException {
        final String mapping =
                "org.example.Result -> x.y:\n"
                        + "org.example.Original -> a.b:\n"
                        + "    org.example.Result target() -> c\n";
        final SimpleRules rules = new SimpleRules();
        rules.parse(new StringReader(
                "class org.example.Original {\n"
                        + "  target;\n"
                        + "}\n"));
        final ProguardMappingConfig filter = new ProguardMappingConfig(
                new BasicKeepConfig(),
                new MappingReader(new ByteArrayInputStream(
                        mapping.getBytes(StandardCharsets.UTF_8))),
                rules);

        final ImmutableMethod method = new ImmutableMethod(
                "La/b;",
                "c",
                Collections.emptyList(),
                "Lx/y;",
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        1,
                        Arrays.asList(
                                new ImmutableInstruction11n(Opcode.CONST_4, 0, 0),
                                new ImmutableInstruction11x(Opcode.RETURN_OBJECT, 0)),
                        Collections.emptyList(),
                        Collections.emptyList()));
        final ImmutableClassDef classDef = new ImmutableClassDef(
                "La/b;",
                AccessFlags.PUBLIC.getValue(),
                "Ljava/lang/Object;",
                Collections.emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Collections.singletonList(method));

        assertTrue(filter.acceptClass(classDef));
        assertTrue(filter.acceptMethod(method));
    }

    @Test
    public void testMatchesR8InlinedMethodByOriginalRule() throws IOException {
        final String mapping =
                "org.example.Receiver -> a.b:\n"
                        + "    1:10:void target(int,int):12:12 -> \u8bdc\u5766\n"
                        + "    1:10:void onReceive(android.content.Context,android.content.Intent):20 -> \u8bdc\u5766\n"
                        + "    11:20:void ignored():30:30 -> \u8bdc\u5766\n"
                        + "    11:20:void other(java.lang.String):40 -> \u8bdc\u5766\n";
        final SimpleRules rules = new SimpleRules();
        rules.parse(new StringReader(
                "class org.example.Receiver {\n"
                        + "  target;\n"
                        + "}\n"));
        final ProguardMappingConfig filter = new ProguardMappingConfig(
                new BasicKeepConfig(),
                new MappingReader(new ByteArrayInputStream(
                        mapping.getBytes(StandardCharsets.UTF_8))),
                rules);

        final ImmutableMethod callback = new ImmutableMethod(
                "La/b;",
                "\u8bdc\u5766",
                Arrays.asList(
                        new ImmutableMethodParameter(
                                "Landroid/content/Context;", Collections.emptySet(), null),
                        new ImmutableMethodParameter(
                                "Landroid/content/Intent;", Collections.emptySet(), null)),
                "V",
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        2,
                        Arrays.asList(
                                new ImmutableInstruction10x(Opcode.NOP),
                                new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                        Collections.emptyList(),
                        Collections.emptyList()));
        final ImmutableMethod other = new ImmutableMethod(
                "La/b;",
                "\u8bdc\u5766",
                Collections.singletonList(new ImmutableMethodParameter(
                        "Ljava/lang/String;", Collections.emptySet(), null)),
                "V",
                AccessFlags.PUBLIC.getValue() | AccessFlags.STATIC.getValue(),
                Collections.emptySet(),
                Collections.emptySet(),
                new ImmutableMethodImplementation(
                        1,
                        Arrays.asList(
                                new ImmutableInstruction10x(Opcode.NOP),
                                new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                        Collections.emptyList(),
                        Collections.emptyList()));
        final ImmutableClassDef classDef = new ImmutableClassDef(
                "La/b;",
                AccessFlags.PUBLIC.getValue(),
                "Ljava/lang/Object;",
                Collections.emptyList(),
                null,
                Collections.emptyList(),
                Collections.emptyList(),
                Arrays.asList(callback, other));

        assertTrue(filter.acceptClass(classDef));
        assertTrue(filter.acceptMethod(callback));
        assertFalse(filter.acceptMethod(other));

        final ByteArrayOutputStream output = new ByteArrayOutputStream();
        final PrintStream originalOut = System.out;
        try {
            System.setOut(new PrintStream(output, true, StandardCharsets.UTF_8.name()));
            filter.onMethodConverted(callback);
            filter.onMethodConverted(callback);
            filter.printReport();
        } finally {
            System.setOut(originalOut);
        }

        final String log = output.toString(StandardCharsets.UTF_8.name());
        final String detailPrefix = "[nmmp] R8 inline match:";
        assertTrue(log.contains(
                detailPrefix
                        + " Lorg/example/Receiver;->target(II)V"
                        + " => La/b;->\\u8bdc\\u5766"
                        + "(Landroid/content/Context;Landroid/content/Intent;)V"));
        assertEquals(log.indexOf(detailPrefix), log.lastIndexOf(detailPrefix));
        assertTrue(log.contains(
                "[nmmp] R8 inline:     source methods=1, residual methods=1"));
        for (int i = 0; i < log.length(); i++) {
            final char c = log.charAt(i);
            assertTrue(c == '\r' || c == '\n' || c >= 0x20 && c <= 0x7e);
        }
        assertEquals(1, filter.getInlineSourceMethodCount());
        assertEquals(1, filter.getInlineResidualMethodCount());
    }
}

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
import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;

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
                        + "    1:10:void target(int,int):12:12 -> d\n"
                        + "    1:10:void onReceive(android.content.Context,android.content.Intent):20 -> d\n"
                        + "    11:20:void ignored():30:30 -> d\n"
                        + "    11:20:void other(java.lang.String):40 -> d\n";
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
                "d",
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
                "d",
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
    }
}

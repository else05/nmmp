package com.nmmedit.apkprotect.dex2c.filters;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.immutable.*;
import com.android.tools.smali.dexlib2.immutable.instruction.ImmutableInstruction10x;
import com.nmmedit.apkprotect.deobfus.MappingReader;
import com.nmmedit.apkprotect.dex2c.converter.ClassAnalyzer;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import org.junit.Test;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import static org.junit.Assert.*;

public class CrossClassInlineTest {
    private ProguardMappingConfig filter(String rules) throws IOException {
        SimpleRules parsed = new SimpleRules();
        parsed.parse(new StringReader(rules));
        String mapping = "example.Caller -> x:\n"
                + "    1:2:void example.Source.b():10:10 -> a\n"
                + "    1:2:void example.Source.c():11:11 -> a\n"
                + "    1:2:void caller():20 -> a\n"
                + "    3:4:void example.Source.b():10:10 -> d\n"
                + "    3:4:void second():30 -> d\n"
                + "    void untouched() -> e\n";
        return new ProguardMappingConfig(new BasicKeepConfig(), new MappingReader(
                new ByteArrayInputStream(mapping.getBytes(StandardCharsets.UTF_8))), parsed);
    }

    private ImmutableMethod method(String name, int flags) {
        return new ImmutableMethod("Lx;", name, Collections.emptyList(), "V", flags,
                Collections.emptySet(), Collections.emptySet(),
                new ImmutableMethodImplementation(0, Arrays.asList(
                        new ImmutableInstruction10x(Opcode.NOP),
                        new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                        Collections.emptyList(), Collections.emptyList()));
    }

    private ImmutableClassDef owner(ImmutableMethod... methods) {
        return new ImmutableClassDef("Lx;", 1, "Ljava/lang/Object;", Collections.emptyList(),
                null, Collections.emptyList(), Collections.emptyList(), Arrays.asList(methods));
    }

    @Test public void selectsOnlyCarriersWhenSourceClassWasRemoved() throws Exception {
        ProguardMappingConfig filter = filter("class example.Source { b; }");
        ImmutableMethod a = method("a", 9), d = method("d", 9), e = method("e", 9);
        assertTrue(filter.acceptClass(owner(a, d, e)));
        assertTrue(filter.acceptMethod(a));
        assertTrue(filter.acceptMethod(d));
        assertFalse(filter.acceptMethod(e));
        assertTrue(report(filter).contains("R8 cross-class inline: source methods=0, residual methods=0"));
        filter.onMethodConverted(a);
        filter.onMethodConverted(a);
        filter.onMethodConverted(d);
        assertTrue(report(filter).contains("R8 cross-class inline: source methods=1, residual methods=2"));
    }

    @Test public void multipleSourcesAndDirectRuleCountCarrierOnce() throws Exception {
        ProguardMappingConfig filter = filter("class example.Source { b;c; }\nclass example.Caller { caller; }");
        ImmutableMethod a = method("a", 9);
        assertTrue(filter.acceptClass(owner(a)));
        assertTrue(filter.acceptMethod(a));
        filter.onMethodConverted(a);
        filter.onMethodConverted(a);
        String log = report(filter);
        assertTrue(log.contains("R8 inline:     source methods=2, residual methods=1"));
        assertTrue(log.contains("R8 cross-class inline: source methods=2, residual methods=1"));
    }

    @Test public void doesNotUseUnrelatedOwnersRulesOrInventHierarchy() throws Exception {
        ImmutableMethod a = method("a", 9);
        assertFalse(filter("class example.Other { b; }").acceptClass(owner(a)));
        assertFalse(filter("class example.Source extends example.Base { b; }").acceptClass(owner(a)));
        assertTrue(filter("class example.S* { b; }").acceptClass(owner(a)));
    }

    @Test public void retainsBasicMethodExclusions() throws Exception {
        ImmutableMethod synthetic = method("a", 9 | AccessFlags.SYNTHETIC.getValue());
        ProguardMappingConfig filter = filter("class example.Source { b; }");
        assertFalse(filter.acceptClass(owner(synthetic)));
        assertFalse(filter.acceptMethod(synthetic));
    }

    @Test public void resolvesSourceHierarchyBeforeSourceClassIsVisited() throws Exception {
        final ImmutableClassDef source = new ImmutableClassDef("Lexample/Source;", 1,
                "Lexample/Base;", Collections.singletonList("Lexample/Interface;"), null,
                Collections.emptyList(), Collections.emptyList(), Collections.emptyList());
        ClassAnalyzer analyzer = new ClassAnalyzer() {
            @Override public ClassDef getClassDef(String type) {
                return source.getType().equals(type) ? source : null;
            }
        };
        for (String constraint : Arrays.asList("extends example.Base", "implements example.Interface")) {
            ProguardMappingConfig filter = filter("class example.Source " + constraint + " { b; }");
            filter.setClassAnalyzer(analyzer);
            ImmutableMethod a = method("a", 9);
            assertTrue(filter.acceptClass(owner(a)));
            assertTrue(filter.acceptMethod(a));
            filter.onMethodConverted(a);
            assertTrue(report(filter).contains("R8 cross-class inline: source methods=1, residual methods=1"));
        }
    }

    private String report(ProguardMappingConfig filter) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        PrintStream previous = System.out;
        try (PrintStream output = new PrintStream(bytes, true, "UTF-8")) {
            System.setOut(output);
            filter.printReport();
        } finally {
            System.setOut(previous);
        }
        return bytes.toString("UTF-8");
    }
}

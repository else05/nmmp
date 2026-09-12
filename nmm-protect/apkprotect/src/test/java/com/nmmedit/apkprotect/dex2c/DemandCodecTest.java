package com.nmmedit.apkprotect.dex2c;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethod;
import com.android.tools.smali.dexlib2.immutable.ImmutableMethodImplementation;
import com.android.tools.smali.dexlib2.immutable.instruction.*;
import org.junit.Test;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.*;
import static org.junit.Assert.*;

public class DemandCodecTest {
    private static byte[] hex(String value) {
        byte[] bytes = new byte[value.length() / 2];
        for (int i = 0; i < bytes.length; ++i) bytes[i] = (byte)Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        return bytes;
    }
    @Test public void independentCrossLanguageVector() throws Exception {
        JsonObject v;
        try (InputStreamReader r = new InputStreamReader(getClass().getResourceAsStream("/demand-vector.json"), StandardCharsets.UTF_8)) {
            v = JsonParser.parseReader(r).getAsJsonObject();
        }
        long root = Long.parseUnsignedLong(v.get("root").getAsString(), 16);
        long tag = Long.parseUnsignedLong(v.get("tag").getAsString(), 16);
        long seed = Long.parseUnsignedLong(v.get("seed").getAsString(), 16);
        assertEquals(tag, DemandCodec.tag(v.get("descriptor").getAsString()));
        assertEquals(seed, DemandCodec.seed(root, 0xfedcba98L, tag, 9, 3));
        byte[] plain = hex(v.get("plain").getAsString()), boundaries = hex(v.get("boundaries").getAsString());
        assertArrayEquals(hex(v.get("code").getAsString()), DemandCodec.transformCode(plain, seed, boundaries));
        assertArrayEquals(hex(v.get("tries").getAsString()), new MethodCodec(seed).transform(plain, 0, ReaderFormats.TRIES));
        assertNotEquals(seed, DemandCodec.seed(root, 0xfedcba98L, tag, 10, 3));
    }
    @Test public void rejectsJumpIntoOperand() {
        ImmutableMethod m = new ImmutableMethod("LTest;", "bad", Collections.emptyList(), "V", 8,
                Collections.emptySet(), Collections.emptySet(), new ImmutableMethodImplementation(2,
                Arrays.asList(new ImmutableInstruction20t(Opcode.GOTO_16, 1), new ImmutableInstruction10x(Opcode.RETURN_VOID)),
                Collections.emptyList(), Collections.emptyList()));
        try { DemandCodec.encode(m, new byte[6], new byte[0], 1, 2, 0); fail(); }
        catch (IllegalArgumentException e) { assertTrue(e.getMessage().contains("branch target")); }
    }
    @Test public void modesAreExplicit() {
        assertEquals(2, new ProtectionContext(1).getCodecVersion());
        assertEquals(3, new ProtectionContext(1, true).getCodecVersion());
        assertEquals("on-demand-v1", new ProtectionContext(1, true).getDecodeMode());
    }
}

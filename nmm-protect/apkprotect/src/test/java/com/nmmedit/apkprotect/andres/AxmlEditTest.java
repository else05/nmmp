package com.nmmedit.apkprotect.andres;

import apk.arsc.Chunk;
import apk.arsc.ResourceFile;
import apk.arsc.ResourceValue;
import apk.arsc.SerializableResource;
import apk.arsc.StringPoolChunk;
import apk.arsc.XmlAttribute;
import apk.arsc.XmlChunk;
import apk.arsc.XmlStartElementChunk;
import com.google.common.io.ByteStreams;
import com.google.common.io.Files;
import org.junit.Assert;
import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;

public class AxmlEditTest {

    @Test
    public void testRenameApplicationName() throws IOException {
        final InputStream stream = getClass().getResourceAsStream("/binManifest.xml");
        final byte[] bytes = ByteStreams.toByteArray(stream);
        final byte[] newData = AxmlEdit.renameApplicationName(bytes, "com.nmmedit.protect.LoadLibApp");

        final File to = File.createTempFile("aaa", "Manifest.xml");
        Files.write(newData, to);

    }
    @Test
    public void testGetMinSdk() throws IOException {
        final InputStream stream = getClass().getResourceAsStream("/binManifest.xml");
        final byte[] bytes = ByteStreams.toByteArray(stream);
        final int minSdk = AxmlEdit.getMinSdk(bytes);
    }

    @Test
    public void testExtractNativeLibsFalse() throws IOException {
        byte[] manifest = manifestWithExtractNativeLibs(false);
        Assert.assertTrue(AxmlEdit.isExtractNativeLibsFalse(manifest));
    }

    @Test
    public void testExtractNativeLibsTrue() throws IOException {
        byte[] manifest = manifestWithExtractNativeLibs(true);
        Assert.assertFalse(AxmlEdit.isExtractNativeLibsFalse(manifest));
    }

    @Test
    public void testExtractNativeLibsAbsent() throws IOException {
        Assert.assertFalse(AxmlEdit.isExtractNativeLibsFalse(manifestBytes()));
    }

    private byte[] manifestWithExtractNativeLibs(boolean value) throws IOException {
        ResourceFile file = new ResourceFile(manifestBytes());
        for (Chunk chunk : file.getChunks()) {
            if (!(chunk instanceof XmlChunk)) continue;
            XmlChunk xml = (XmlChunk) chunk;
            StringPoolChunk strings = null;
            XmlStartElementChunk application = null;
            for (Chunk child : xml.getChunks().values()) {
                if (child instanceof StringPoolChunk) strings = (StringPoolChunk) child;
                if (child instanceof XmlStartElementChunk
                        && "application".equals(((XmlStartElementChunk) child).getName())) {
                    application = (XmlStartElementChunk) child;
                }
            }
            if (strings == null || application == null) continue;
            int namespace = strings.indexOf("http://schemas.android.com/apk/res/android");
            if (namespace < 0) namespace = strings.addString("http://schemas.android.com/apk/res/android");
            int name = strings.addString("extractNativeLibs");
            int rawValue = strings.addString(Boolean.toString(value));
            ResourceValue typedValue = ResourceValue.builder()
                    .size(ResourceValue.SIZE)
                    .type(ResourceValue.Type.INT_BOOLEAN)
                    .data(value ? 1 : 0)
                    .build();
            application.addAttribute(application.getAttributes().size(), XmlAttribute.create(
                    namespace, name, rawValue, typedValue, application));
            return file.toByteArray(SerializableResource.SHRINK);
        }
        throw new AssertionError("application element not found");
    }

    private byte[] manifestBytes() throws IOException {
        try (InputStream stream = getClass().getResourceAsStream("/binManifest.xml")) {
            return ByteStreams.toByteArray(stream);
        }
    }
}

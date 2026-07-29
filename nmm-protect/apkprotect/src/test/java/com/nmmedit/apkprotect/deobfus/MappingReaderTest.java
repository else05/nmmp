package com.nmmedit.apkprotect.deobfus;

import com.nmmedit.apkprotect.util.ApkUtils;
import org.junit.Test;

import javax.annotation.Nonnull;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class MappingReaderTest {

    @Test
    public void textParse() throws IOException {
        final File tempFile = getMappingFile();

        final MappingReader reader = new MappingReader(tempFile);
        final AtomicBoolean unicodeClassSeen = new AtomicBoolean();

        reader.parse(new MappingProcessor() {
            @Override
            public void processClassMapping(String className, String newClassName) {
                if ("org.example.Original".equals(className)) {
                    assertEquals("ڴ.ݚ", newClassName);
                    unicodeClassSeen.set(true);
                }
            }

            @Override
            public void processFieldMapping(String className, String fieldType, String fieldName, String newClassName, String newFieldName) {

            }

            @Override
            public void processMethodMapping(String className, int firstLineNumber, int lastLineNumber, String methodReturnType, String methodName, String methodArguments, String newClassName, int newFirstLineNumber, int newLastLineNumber, String newMethodName) {
                System.out.println(methodArguments);
            }
        });
        assertTrue(unicodeClassSeen.get());
        tempFile.delete();

    }

    @Nonnull
    private File getMappingFile() throws IOException {
        final File tempFile = File.createTempFile("map", ".temp");
        try (
                InputStream input = getClass().getResourceAsStream("/mapping.txt");
                final FileOutputStream output = new FileOutputStream(tempFile);
        ) {
            ApkUtils.copyStream(input, output);
        }
        return tempFile;
    }
}

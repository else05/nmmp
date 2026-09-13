package com.nmmedit.apkprotect.util;


import com.android.zipflinger.ZipArchive;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedList;
import java.util.List;
import java.util.regex.Pattern;

/**
 * Utilities for working with apk files.
 */
public final class ApkUtils {

    private ApkUtils() {
    } // Prevent instantiation


    /**
     * Returns a file whose name matches {@code filename}, or null if no file was found.
     *
     * @param apkFile  The file containing the apk zip archive.
     * @param filename The full filename (e.g. res/raw/foo.bar).
     * @return A byte array containing the contents of the matching file, or null if not found.
     * @throws IOException Thrown if there's a matching file, but it cannot be read from the apk.
     */
    public static byte[] getFile(File apkFile, String filename) throws IOException {
        try (ZipArchive apkZip = new ZipArchive(apkFile.toPath())) {
            final InputStream input = apkZip.getInputStream(filename);
            if (input == null) {
                return null;
            }
            try (InputStream in = input) {
                return toByteArray(in);
            }
        }
    }

    private static byte[] toByteArray(InputStream in) throws IOException {
        byte[] buf = new byte[4 * 1024];
        int len;
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        while ((len = in.read(buf)) != -1) {
            baos.write(buf, 0, len);
        }
        return baos.toByteArray();
    }

    public static void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[4 * 1024];
        int len;
        while ((len = in.read(buf)) != -1) {
            out.write(buf, 0, len);
        }
    }


    public static List<File> extractFiles(File apkFile, String regex, File outDir) throws IOException {
        return extractFiles(apkFile, Pattern.compile(regex), outDir);
    }

    public static List<File> extractFiles(File apkFile, Pattern regex, File outDir) throws IOException {
        try (ZipArchive apkZip = new ZipArchive(apkFile.toPath())) {
            Path root = outDir.getCanonicalFile().toPath();
            // Validate the complete selection before creating or overwriting files.
            for (String entry : apkZip.listEntries()) {
                if (regex.matcher(entry).matches()) extractionTarget(root, entry);
            }
            List<File> result = new LinkedList<>();
            for (String entry : apkZip.listEntries()) {
                if (regex.matcher(entry).matches() && !entry.endsWith("/")) {
                    File file = extractionTarget(root, entry);
                    Files.createDirectories(file.getParentFile().toPath());
                    final InputStream input = apkZip.getInputStream(entry);
                    if (input == null) {//directory?
                        continue;
                    }
                    try (InputStream in = input;
                         FileOutputStream output = new FileOutputStream(file)) {
                        copyStream(in, output);
                        result.add(file);
                    }
                }
            }
            return result;
        }
    }

    private static File extractionTarget(Path root, String entry) throws IOException {
        if (entry.isEmpty() || entry.startsWith("/") || entry.indexOf('\\') >= 0 || entry.indexOf(':') >= 0) {
            throw new IOException("Unsafe archive entry: " + entry);
        }
        for (String part : entry.split("/")) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                throw new IOException("Unsafe archive entry: " + entry);
            }
        }
        File target = root.resolve(entry).toFile().getCanonicalFile();
        if (!target.toPath().startsWith(root) || target.toPath().equals(root)) {
            throw new IOException("Archive entry escapes output directory: " + entry);
        }
        return target;
    }
}

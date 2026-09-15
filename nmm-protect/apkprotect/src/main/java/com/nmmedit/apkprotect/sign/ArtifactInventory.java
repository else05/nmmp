package com.nmmedit.apkprotect.sign;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Frozen final DEX/core-SO inventory. This unsigned build record is not a trust root. */
public final class ArtifactInventory {
    private static final long MAX_ENTRY_BYTES = 256L * 1024 * 1024;
    private static final long MAX_TOTAL_BYTES = 1024L * 1024 * 1024;
    private static final int MAX_ENTRIES = 2048;
    private final long buildId;
    private final byte[] packageName;
    private final SortedMap<String, Entry> entries;

    private static final class Entry {
        final long size;
        final byte[] digest;
        Entry(long size, byte[] digest) { this.size = size; this.digest = digest; }
    }

    private ArtifactInventory(long buildId, String packageName, SortedMap<String, Entry> entries) {
        this.buildId = buildId;
        this.packageName = packageName.getBytes(StandardCharsets.UTF_8);
        this.entries = Collections.unmodifiableSortedMap(entries);
    }

    public static ArtifactInventory capture(long buildId, String packageName,
                                            List<File> dexFiles,
                                            Map<String, Map<File, File>> nativeLibs) throws IOException {
        if (packageName == null || packageName.isEmpty() || packageName.indexOf('\0') >= 0
                || packageName.getBytes(StandardCharsets.UTF_8).length > 1024) {
            throw new IOException("Invalid artifact package identity");
        }
        SortedMap<String, File> files = new TreeMap<>();
        for (File file : dexFiles) {
            String name = file.getName();
            if (!name.matches("classes(?:[2-9]|[1-9][0-9]+)?\\.dex")) {
                throw new IOException("Invalid final DEX name: " + name);
            }
            add(files, name, file);
        }
        if (!files.containsKey("classes.dex")) throw new IOException("Missing final classes.dex");
        int dexCount = files.size();
        for (Map.Entry<String, Map<File, File>> abi : nativeLibs.entrySet()) {
            for (File file : abi.getValue().values()) {
                String name = "lib/" + abi.getKey() + "/" + file.getName();
                if (!name.matches("lib/[a-zA-Z0-9_-]+/[a-zA-Z0-9_+.-]+\\.so")) {
                    throw new IOException("Invalid final native path: " + name);
                }
                add(files, name, file);
            }
        }
        if (files.size() == dexCount) throw new IOException("Missing final core native libraries");
        SortedMap<String, Entry> entries = new TreeMap<>();
        long total = 0;
        for (Map.Entry<String, File> file : files.entrySet()) {
            long reportedSize = Files.size(file.getValue().toPath());
            if (reportedSize <= 0 || reportedSize > MAX_ENTRY_BYTES || reportedSize > MAX_TOTAL_BYTES - total) {
                throw new IOException("Invalid artifact size: " + file.getKey());
            }
            try (InputStream input = new BufferedInputStream(new FileInputStream(file.getValue()))) {
                Entry entry = hash(input, MAX_ENTRY_BYTES);
                if (entry.size == 0 || entry.size > MAX_TOTAL_BYTES - total) {
                    throw new IOException("Invalid artifact size: " + file.getKey());
                }
                total += entry.size;
                entries.put(file.getKey(), entry);
            }
        }
        return new ArtifactInventory(buildId, packageName, entries);
    }

    private static void add(Map<String, File> files, String name, File file) throws IOException {
        if (name.length() > 1024 || files.containsKey(name) || files.size() >= MAX_ENTRIES) {
            throw new IOException("Duplicate or excessive artifact entry: " + name);
        }
        files.put(name, file);
    }

    private static Entry hash(InputStream input, long limit) throws IOException {
        final MessageDigest hash;
        try { hash = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
        byte[] buffer = new byte[16384];
        long size = 0;
        for (int count; (count = input.read(buffer)) != -1;) {
            if (count == 0 || count > limit - size) throw new IOException("Artifact read exceeds contract");
            size += count;
            hash.update(buffer, 0, count);
        }
        return new Entry(size, hash.digest());
    }

    /** Checks the actual packaged bytes, without trusting source-file sizes or ZIP CRCs. */
    public void verifyApk(File apk) throws IOException {
        Set<String> found = new HashSet<>();
        Set<String> allNames = new HashSet<>();
        try (ZipFile zip = new ZipFile(apk)) {
            Enumeration<? extends ZipEntry> iterator = zip.entries();
            while (iterator.hasMoreElements()) {
                ZipEntry file = iterator.nextElement();
                String name = file.getName();
                if (!allNames.add(name)) throw new IOException("Duplicate APK entry: " + name);
                Entry expected = entries.get(name);
                if (expected == null) {
                    if (name.matches("classes[0-9]*\\.dex")) throw new IOException("Unexpected DEX: " + name);
                    continue;
                }
                if (file.isDirectory() || file.getSize() != expected.size) {
                    throw new IOException("Artifact size differs: " + name);
                }
                try (InputStream input = zip.getInputStream(file)) {
                    Entry actual = hash(input, expected.size);
                    if (actual.size != expected.size || !MessageDigest.isEqual(actual.digest, expected.digest)) {
                        throw new IOException("Artifact digest differs: " + name);
                    }
                }
                found.add(name);
            }
        }
        if (!found.equals(entries.keySet())) throw new IOException("Missing packaged artifact entries");
    }

    /** Big-endian v1 inventory; names sorted by ASCII, hashes cover uncompressed bytes. */
    public byte[] encode() throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (DataOutputStream output = new DataOutputStream(bytes)) {
            output.write(new byte[]{'N','M','M','P','A','I','N','V'});
            output.writeInt(1);
            output.writeLong(buildId);
            output.writeInt(packageName.length);
            output.write(packageName);
            output.writeInt(entries.size());
            for (Map.Entry<String, Entry> entry : entries.entrySet()) {
                byte[] name = entry.getKey().getBytes(StandardCharsets.US_ASCII);
                output.writeInt(name.length);
                output.write(name);
                output.writeLong(entry.getValue().size);
                output.write(entry.getValue().digest);
            }
        }
        return bytes.toByteArray();
    }
}

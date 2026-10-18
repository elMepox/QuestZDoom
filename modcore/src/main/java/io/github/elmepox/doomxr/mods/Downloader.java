package io.github.elmepox.doomxr.mods;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Downloads a file trying each URL in turn, verifies SHA-256 when known, and moves it into place
 * atomically, so a half-finished download never shows up in the library. Zip archives (the idgames norm)
 * are unpacked, keeping only mod payload files. Catalog entries that must stay as a zip use .pk3.
 */
public final class Downloader {
    public interface Progress { void onBytes(long done, long total); }

    /** Extensions the engine can load; everything else in an archive (txt, bat, ...) is skipped. */
    private static final String[] PAYLOAD = { ".wad", ".pk3", ".pk7", ".ipk3", ".ipk7", ".deh", ".bex", ".zip" };

    private final HttpFetcher http;

    public Downloader(HttpFetcher http) { this.http = http; }

    /** Fetches {@code ref} into {@code dir}; returns the files added (several if it was an archive). */
    public List<File> fetch(CatalogEntry.FileRef ref, File dir, Progress progress) throws IOException {
        return fetch(ref.urls, ref.name, ref.sha256, ref.size, dir, progress);
    }

    public List<File> fetch(List<String> urls, String name, String sha256, long size, File dir, Progress progress)
            throws IOException {
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("cannot create " + dir);
        File part = File.createTempFile("dl-", ".part", dir);
        IOException last = new IOException("no urls");
        try {
            for (String url : urls) {
                try {
                    String hex = copy(http.open(url), part, size, progress);
                    if (!sha256.isEmpty() && !sha256.equalsIgnoreCase(hex)) {
                        throw new IOException("checksum mismatch from " + url);
                    }
                    return place(part, safeName(name), dir);
                } catch (IOException e) {
                    last = e; // try the next mirror
                }
            }
            throw last;
        } finally {
            part.delete();
        }
    }

    private static List<File> place(File part, String name, File dir) throws IOException {
        List<File> out = new ArrayList<>();
        if (name.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            try (ZipInputStream zin = new ZipInputStream(Files.newInputStream(part.toPath()))) {
                for (ZipEntry e; (e = zin.getNextEntry()) != null; ) {
                    if (e.isDirectory() || !isPayload(e.getName())) continue;
                    // Flatten and sanitise: never trust entry paths (zip-slip).
                    File target = new File(dir, safeName(e.getName()));
                    File tmp = File.createTempFile("ex-", ".part", dir);
                    try (OutputStream os = Files.newOutputStream(tmp.toPath())) {
                        byte[] buf = new byte[8192];
                        for (int n; (n = zin.read(buf)) > 0; ) os.write(buf, 0, n);
                    }
                    Files.move(tmp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    out.add(target);
                }
            }
            if (out.isEmpty()) throw new IOException("archive has no loadable files");
        } else {
            File target = new File(dir, name);
            Files.move(part.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            out.add(target);
        }
        return out;
    }

    static boolean isPayload(String entryName) {
        String n = entryName.toLowerCase(Locale.ROOT);
        for (String ext : PAYLOAD) if (n.endsWith(ext)) return true;
        return false;
    }

    /** Last path component with anything odd removed. */
    static String safeName(String name) {
        String n = name.replace('\\', '/');
        n = n.substring(n.lastIndexOf('/') + 1).replaceAll("[^A-Za-z0-9._ +()\\-]", "_");
        if (n.isEmpty() || n.equals(".") || n.equals("..")) throw new IllegalArgumentException("bad file name");
        return n;
    }

    private static String copy(InputStream in, File to, long total, Progress progress) throws IOException {
        MessageDigest md;
        try {
            md = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
        try (InputStream is = in; OutputStream os = Files.newOutputStream(to.toPath())) {
            byte[] buf = new byte[32 * 1024];
            long done = 0;
            for (int n; (n = is.read(buf)) > 0; ) {
                os.write(buf, 0, n);
                md.update(buf, 0, n);
                done += n;
                if (progress != null) progress.onBytes(done, total);
            }
        }
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) sb.append(String.format("%02x", b));
        return sb.toString();
    }
}

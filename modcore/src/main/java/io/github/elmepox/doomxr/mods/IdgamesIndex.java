package io.github.elmepox.doomxr.mods;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.GZIPInputStream;

/**
 * Search over the idgames archive without its web API (doomworld.com is behind a Cloudflare challenge):
 * the mirrors publish {@code ls-laR.gz}, a recursive {@code ls -l} listing of the whole archive. It is
 * downloaded once a day, cached on disk and searched locally. Descriptions live in a {@code .txt} next to
 * each zip and are fetched on demand.
 */
public final class IdgamesIndex {
    /** Mirrors laid out as {@code <base>/<dir>/<filename>}, tried in order (checked on 2026-10-18). */
    public static final List<String> DEFAULT_MIRRORS = List.of(
            "https://youfailit.net/pub/idgames/",
            "https://ftp.fu-berlin.de/pc/games/idgames/");

    public static final String LISTING = "ls-laR.gz";
    public static final long MAX_AGE_MS = 24L * 60 * 60 * 1000;

    public static final class Entry {
        /** Directory without leading or trailing slash, e.g. {@code levels/doom2/megawads}. */
        public final String dir;
        public final String filename;
        public final long size;
        /** As printed by ls ("Jan 17 2005" or "Oct 17 21:33"). */
        public final String date;

        Entry(String dir, String filename, long size, String date) {
            this.dir = dir;
            this.filename = filename;
            this.size = size;
            this.date = date;
        }

        public String title() {
            return filename.substring(0, filename.length() - 4);
        }

        /** Base game guessed from the folder: "doom2", "doom", or "" for themes and the rest. */
        public String iwad() {
            String d = "/" + dir.toLowerCase(Locale.ROOT) + "/";
            if (d.startsWith("/levels/doom2/") || d.startsWith("/combos/")) return "doom2";
            if (d.startsWith("/levels/doom/")) return "doom";
            return "";
        }

        public List<String> downloadUrls(List<String> mirrors) { return urls(mirrors, filename); }

        /** The author's text file (description, credits) next to the zip. */
        public List<String> textUrls(List<String> mirrors) { return urls(mirrors, title() + ".txt"); }

        private List<String> urls(List<String> mirrors, String name) {
            List<String> out = new ArrayList<>();
            for (String m : mirrors) out.add((m.endsWith("/") ? m : m + "/") + dir + "/" + name);
            return out;
        }
    }

    private static final Pattern FILE = Pattern.compile(
            "^-[rwxsStT-]{9}[+@.]?\\s+\\d+\\s+\\S+\\s+\\S+\\s+(\\d+)\\s+([A-Z][a-z]{2}\\s+\\d{1,2}\\s+(?:\\d{4}|\\d{1,2}:\\d{2}))\\s+(.+)$");

    private final List<Entry> entries;

    public IdgamesIndex(List<Entry> entries) { this.entries = Collections.unmodifiableList(entries); }

    public int size() { return entries.size(); }

    /** Parses a plain-text recursive listing; only {@code .zip} files are kept. */
    public static IdgamesIndex parse(InputStream plain) throws IOException {
        List<Entry> out = new ArrayList<>();
        String dir = "";
        try (BufferedReader r = new BufferedReader(new InputStreamReader(plain, StandardCharsets.ISO_8859_1))) {
            for (String line; (line = r.readLine()) != null; ) {
                if (line.endsWith(":") && !line.startsWith("-") && !line.startsWith(" ")) {
                    dir = line.substring(0, line.length() - 1);
                    if (dir.startsWith("./")) dir = dir.substring(2);
                    while (dir.startsWith("/")) dir = dir.substring(1);
                    while (dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
                    continue;
                }
                Matcher m = FILE.matcher(line);
                if (!m.matches()) continue;
                String name = m.group(3);
                if (!name.toLowerCase(Locale.ROOT).endsWith(".zip") || dir.isEmpty()) continue;
                out.add(new Entry(dir, name, Long.parseLong(m.group(1)), m.group(2).replaceAll("\\s+", " ")));
            }
        }
        return new IdgamesIndex(out);
    }

    /**
     * Loads the index from the cache when it is fresh, else downloads {@value #LISTING} from the first
     * mirror that answers; falls back to a stale cache when every mirror fails.
     */
    public static IdgamesIndex load(HttpFetcher http, List<String> mirrors, File cache, long maxAgeMs)
            throws IOException {
        boolean fresh = cache.isFile() && System.currentTimeMillis() - cache.lastModified() < maxAgeMs;
        if (!fresh) {
            IOException last = null;
            for (String m : mirrors) {
                File tmp = new File(cache.getPath() + ".part");
                try (InputStream in = http.open((m.endsWith("/") ? m : m + "/") + LISTING)) {
                    Files.copy(in, tmp.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    IdgamesIndex check = parseGzip(tmp); // do not replace a good cache with garbage
                    if (check.size() == 0) throw new IOException("empty listing from " + m);
                    Files.move(tmp.toPath(), cache.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    return check;
                } catch (IOException e) {
                    last = e;
                    tmp.delete();
                }
            }
            if (!cache.isFile()) throw last != null ? last : new IOException("no mirrors");
        }
        return parseGzip(cache);
    }

    static IdgamesIndex parseGzip(File f) throws IOException {
        try (InputStream in = new GZIPInputStream(Files.newInputStream(f.toPath()))) {
            return parse(in);
        }
    }

    /** All words of the query must appear in the file name or folder; better name matches come first. */
    public List<Entry> search(String query, int limit) {
        String[] words = query == null ? new String[0] : query.toLowerCase(Locale.ROOT).trim().split("\\s+");
        if (words.length == 0 || words[0].isEmpty()) return new ArrayList<>();
        List<Entry> hits = new ArrayList<>();
        for (Entry e : entries) {
            String name = e.filename.toLowerCase(Locale.ROOT);
            String path = e.dir.toLowerCase(Locale.ROOT);
            boolean all = true;
            for (String w : words) if (!name.contains(w) && !path.contains(w)) { all = false; break; }
            if (all) hits.add(e);
        }
        final String q = String.join(" ", words);
        hits.sort(Comparator.comparingInt((Entry e) -> rank(e, q)).thenComparing(e -> e.filename));
        return hits.size() > limit ? new ArrayList<>(hits.subList(0, limit)) : hits;
    }

    private static int rank(Entry e, String q) {
        String t = e.title().toLowerCase(Locale.ROOT);
        if (t.equals(q)) return 0;
        if (t.startsWith(q)) return 1;
        if (t.contains(q)) return 2;
        return 3;
    }
}

package io.github.elmepox.doomxr.mods;

import static org.junit.jupiter.api.Assertions.*;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class ModcoreTest {
    private static HttpFetcher fake(Map<String, byte[]> routes) {
        return url -> {
            byte[] b = routes.get(url);
            if (b == null) throw new IOException("HTTP 404 for " + url);
            return new ByteArrayInputStream(b);
        };
    }

    private static String sha(byte[] b) throws Exception {
        StringBuilder sb = new StringBuilder();
        for (byte x : MessageDigest.getInstance("SHA-256").digest(b)) sb.append(String.format("%02x", x));
        return sb.toString();
    }

    @Test
    void catalogParsesSearchesAndSkipsBadEntries() throws Exception {
        String json = "{\"schema\":1,\"updated\":\"2026-10-17\",\"entries\":["
                + "{\"id\":\"a\",\"title\":\"Eviternity\",\"iwad\":\"doom2\",\"type\":\"map\",\"files\":[{\"name\":\"e.wad\",\"urls\":[\"u\"]}]},"
                + "{\"title\":\"no id\"},"
                + "{\"id\":\"b\",\"title\":\"Brutal\",\"description\":\"gameplay mod\",\"type\":\"gameplay\"}]}";
        Catalog c = Catalog.parse(json);
        assertEquals(2, c.entries.size());
        assertEquals(1, c.search("evit", null, null).size());
        assertEquals(1, c.search("", CatalogEntry.Type.GAMEPLAY, "doom2").size());
        assertThrows(Exception.class, () -> Catalog.parse("{\"schema\":2,\"entries\":[]}"));
    }

    private static final String LISTING =
            ".:\ntotal 4\n-rw-r--r--   1 ftp ftp  1234 Jan  1  2000 README.txt\n\n"
            + "./levels/doom2/megawads:\ntotal 9\n"
            + "-rw-r--r--   1 ftp ftp  7531818 Mar 12  2003 scythe.zip\n"
            + "-rw-r--r--   1 ftp ftp     5000 Mar 12  2003 scythe.txt\n"
            + "-rw-r--r--   1 ftp ftp 90000000 Oct 17 21:33 eviternity.zip\n"
            + "drwxr-xr-x   2 ftp ftp     4096 Mar 12  2003 sub\n\n"
            + "./levels/doom/a-c:\n"
            + "-rw-r--r--+  1 ftp ftp     1111 Mar 12  2003 scythe_old.zip\n"
            + "-rw-r--r--   1 ftp ftp     2222 Mar 12  2003 other.ZIP\n";

    private static IdgamesIndex index() throws Exception {
        return IdgamesIndex.parse(new ByteArrayInputStream(LISTING.getBytes(StandardCharsets.ISO_8859_1)));
    }

    @Test
    void idgamesIndexParsesSearchesAndBuildsUrls() throws Exception {
        IdgamesIndex idx = index();
        assertEquals(4, idx.size()); // zips only, case-insensitive extension
        List<IdgamesIndex.Entry> hits = idx.search("scythe", 10);
        assertEquals(List.of("scythe.zip", "scythe_old.zip"), List.of(hits.get(0).filename, hits.get(1).filename));
        assertEquals("doom2", hits.get(0).iwad());
        assertEquals("doom", hits.get(1).iwad());
        assertEquals(7531818L, hits.get(0).size);
        assertEquals(1, idx.search("megawads evit", 10).size()); // words may match folder and name
        assertEquals(0, idx.search("", 10).size());
        assertEquals(List.of("https://m1/pub/levels/doom2/megawads/scythe.zip", "https://m2/levels/doom2/megawads/scythe.zip"),
                hits.get(0).downloadUrls(List.of("https://m1/pub", "https://m2/")));
        assertEquals("https://m1/levels/doom2/megawads/scythe.txt", hits.get(0).textUrls(List.of("https://m1/")).get(0));
    }

    @Test
    void idgamesIndexParsesRealMirrorLines() throws Exception {
        // Lines copied from youfailit.net/pub/idgames/ls-laR.gz
        String real = "./levels/doom2/megawads:\ntotal 1\n"
                + "-rw-rw-r-- 1 idgmaint idgames 969307 Jun 17  2024 100lnm.zip\n"
                + "-rw-rw-r-- 1 idgmaint idgames  12999 Oct 17 01:05 1024abc.zip\n"
                + "-rw-rw-r-- 1 idgmaint idgames   1000 Oct 17 01:05 1024abc.txt\n"
                + "-rw-rw-r-- 1 fpv      idgames   2000 Jun 17  2014 .message\n"
                + "drwxrwxr-x 2 idgmaint idgames   4096 Oct 17 01:05 incoming\n";
        IdgamesIndex idx = IdgamesIndex.parse(new ByteArrayInputStream(real.getBytes(StandardCharsets.ISO_8859_1)));
        assertEquals(2, idx.size());
        assertEquals("levels/doom2/megawads", idx.search("100lnm", 5).get(0).dir);
        assertEquals("Oct 17 01:05", idx.search("1024abc", 5).get(0).date);
    }

    @Test
    void idgamesIndexCachesAndFallsBackToStaleCache(@TempDir File dir) throws Exception {
        ByteArrayOutputStream gz = new ByteArrayOutputStream();
        try (java.util.zip.GZIPOutputStream z = new java.util.zip.GZIPOutputStream(gz)) {
            z.write(LISTING.getBytes(StandardCharsets.ISO_8859_1));
        }
        File cache = new File(dir, "ls.gz");
        // first mirror dead, second serves the listing
        IdgamesIndex a = IdgamesIndex.load(fake(Map.of("https://b/ls-laR.gz", gz.toByteArray())),
                List.of("https://a/", "https://b/"), cache, IdgamesIndex.MAX_AGE_MS);
        assertEquals(4, a.size());
        // fresh cache: no network needed
        assertEquals(4, IdgamesIndex.load(fake(Map.of()), List.of("https://a/"), cache, IdgamesIndex.MAX_AGE_MS).size());
        // stale cache and every mirror down: keep using the cache
        assertEquals(4, IdgamesIndex.load(fake(Map.of()), List.of("https://a/"), cache, -1).size());
        // garbage from a mirror must not replace the cache
        File cache2 = new File(dir, "ls2.gz");
        assertThrows(IOException.class, () -> IdgamesIndex.load(fake(Map.of("https://a/ls-laR.gz", new byte[] {1, 2, 3})),
                List.of("https://a/"), cache2, IdgamesIndex.MAX_AGE_MS));
        assertFalse(cache2.exists());
    }

    @Test
    void downloaderFallsBackToNextMirrorAndVerifiesHash(@TempDir File dir) throws Exception {
        byte[] data = "PWAD-data".getBytes(StandardCharsets.UTF_8);
        Map<String, byte[]> routes = new HashMap<>();
        routes.put("https://bad/x.wad", "corrupt".getBytes(StandardCharsets.UTF_8));
        routes.put("https://good/x.wad", data);
        Downloader d = new Downloader(fake(routes));
        List<File> out = d.fetch(List.of("https://dead/x.wad", "https://bad/x.wad", "https://good/x.wad"),
                "x.wad", sha(data), data.length, dir, null);
        assertEquals(1, out.size());
        assertArrayEquals(data, Files.readAllBytes(out.get(0).toPath()));
        assertEquals(1, dir.list().length, "no .part leftovers");
        assertThrows(IOException.class, () -> d.fetch(List.of("https://bad/x.wad"), "y.wad", sha(data), 0, dir, null));
        assertEquals(1, dir.list().length);
    }

    @Test
    void zipIsUnpackedFlatWithoutZipSlipOrJunk(@TempDir File dir) throws Exception {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(bo)) {
            for (String n : new String[] { "map.wad", "../../evil.wad", "readme.txt", "sub/res.pk3" }) {
                z.putNextEntry(new ZipEntry(n));
                z.write(1);
                z.closeEntry();
            }
        }
        Downloader d = new Downloader(fake(Map.of("https://m/a.zip", bo.toByteArray())));
        List<File> out = d.fetch(List.of("https://m/a.zip"), "a.zip", "", 0, dir, null);
        assertEquals(3, out.size());
        for (File f : out) assertEquals(dir, f.getParentFile());
        assertFalse(new File(dir, "readme.txt").exists());
        assertTrue(new File(dir, "evil.wad").exists());
        assertFalse(new File(dir.getParentFile(), "evil.wad").exists());
    }

    private static File wad(File dir, String name, String magic, String... lumps) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(12 + 16 * lumps.length).order(ByteOrder.LITTLE_ENDIAN);
        b.put(magic.getBytes(StandardCharsets.US_ASCII)).putInt(lumps.length).putInt(12);
        for (String l : lumps) {
            b.putInt(0).putInt(0);
            byte[] n = new byte[8];
            System.arraycopy(l.getBytes(StandardCharsets.US_ASCII), 0, n, 0, l.length());
            b.put(n);
        }
        File f = new File(dir, name);
        Files.write(f.toPath(), b.array());
        return f;
    }

    @Test
    void iwadDetection(@TempDir File dir) throws Exception {
        assertEquals("doom2", IwadDetector.detect(wad(dir, "DOOM2.WAD", "IWAD", "MAP01")));
        assertEquals("plutonia", IwadDetector.detect(wad(dir, "plutonia.wad", "IWAD", "MAP01")));
        assertEquals("freedoom2", IwadDetector.detect(wad(dir, "freedoom2.wad", "IWAD", "MAP01")));
        assertEquals("doom", IwadDetector.detect(wad(dir, "renamed.wad", "IWAD", "E1M1")));
        assertNull(IwadDetector.detect(wad(dir, "mod.wad", "PWAD", "MAP01")));
        File lib = new File(dir, "iw");
        lib.mkdir();
        wad(lib, "doom2.wad", "IWAD", "MAP01");
        wad(lib, "mod.wad", "PWAD", "MAP01");
        assertEquals(1, new ModLibrary(dir, lib).iwads().size());
    }

    @Test
    void profileRoundTripAndArguments() throws Exception {
        LaunchProfile p = new LaunchProfile("T", "doom2.wad", List.of("a.wad", "b.pk3"), List.of("+skill", "3"));
        LaunchProfile q = LaunchProfile.fromJson(p.toJson());
        assertEquals(List.of("-iwad", "doom2.wad", "-file", "/m/a.wad", "/m/b.pk3", "+skill", "3"), q.toArguments("/m"));
        assertEquals(List.of(), new LaunchProfile("x", "", List.of(), List.of()).toArguments("/m"));
    }
}

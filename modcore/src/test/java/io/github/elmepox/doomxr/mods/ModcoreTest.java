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

    @Test
    void idgamesParsesOneAndManyHitsAndErrors() throws Exception {
        String many = "{\"content\":{\"file\":[{\"id\":1,\"title\":\"A\",\"dir\":\"levels/doom2/a-c/\",\"filename\":\"a.zip\"},"
                + "{\"id\":2,\"title\":\"B\",\"dir\":\"levels/doom2/a-c/\",\"filename\":\"b.zip\"}]}}";
        assertEquals(2, IdgamesClient.parseSearch(many).size());
        String one = "{\"content\":{\"file\":{\"id\":1,\"title\":\"A\",\"dir\":\"x/\",\"filename\":\"a.zip\"}}}";
        List<IdgamesClient.Result> r = IdgamesClient.parseSearch(one);
        assertEquals(1, r.size());
        assertEquals(List.of("https://m1/pub/x/a.zip", "https://m2/x/a.zip"),
                IdgamesClient.downloadUrls(r.get(0), List.of("https://m1/pub", "https://m2/")));
        assertThrows(IdgamesClient.ApiException.class,
                () -> IdgamesClient.parseSearch("{\"error\":{\"type\":\"Query Too Small\",\"message\":\"too small\"}}"));
        assertThrows(IdgamesClient.ApiException.class, () -> new IdgamesClient(fake(Map.of())).search("ab", IdgamesClient.SearchType.TITLE));
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

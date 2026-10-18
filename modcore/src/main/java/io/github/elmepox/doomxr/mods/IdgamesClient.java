package io.github.elmepox.doomxr.mods;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Client for the idgames archive API v3 (https://doomworld.com/idgames/api/). Only {@code action=search}
 * is used; downloads go through {@link #downloadUrls} against mirrors.
 *
 * Mirror hosts are NOT part of the API and are kept configurable; the defaults are unverified and
 * must be checked before release.
 */
public final class IdgamesClient {
    public static final String API = "https://www.doomworld.com/idgames/api/api.php";

    /**
     * idgames mirrors laid out as {@code <base>/<dir><filename>}. The first two are the ones used by the
     * goidgames client; the others are unverified. The downloader tries them in order.
     */
    public static final List<String> DEFAULT_MIRRORS = List.of(
            "https://www.quaddicted.com/files/idgames/",
            "https://ftpmirror1.infania.net/pub/idgames/",
            "https://youfailit.net/pub/idgames/",
            "https://ftp.fu-berlin.de/pc/games/idgames/");

    public enum SearchType { FILENAME, TITLE, AUTHOR, DESCRIPTION, TEXTFILE }

    public static final class Result {
        public final int id;
        public final String title, dir, filename, author, description;
        public final long size;
        public final String date;
        public final double rating;
        public final int votes;

        Result(JSONObject o) {
            id = o.optInt("id");
            title = o.optString("title");
            dir = o.optString("dir");
            filename = o.optString("filename");
            author = o.optString("author");
            description = o.optString("description");
            size = o.optLong("size");
            date = o.optString("date");
            rating = o.optDouble("rating", 0);
            votes = o.optInt("votes");
        }
    }

    /** API-level error ("Query Too Small" etc.). */
    public static final class ApiException extends IOException {
        public ApiException(String message) { super(message); }
    }

    private final HttpFetcher http;
    private final String api;

    public IdgamesClient(HttpFetcher http) { this(http, API); }

    public IdgamesClient(HttpFetcher http, String apiUrl) {
        this.http = http;
        this.api = apiUrl;
    }

    public List<Result> search(String query, SearchType type) throws IOException {
        if (query == null || query.trim().length() < 3) throw new ApiException("Query Too Small");
        String url = api + "?action=search&out=json&sort=rating&dir=desc&type="
                + type.name().toLowerCase() + "&query=" + URLEncoder.encode(query.trim(), StandardCharsets.UTF_8);
        return parseSearch(readAll(http.open(url)));
    }

    static List<Result> parseSearch(String body) throws IOException {
        try {
            JSONObject root = new JSONObject(body);
            JSONObject err = root.optJSONObject("error");
            if (err != null) throw new ApiException(err.optString("message", err.optString("type", "error")));
            JSONObject content = root.optJSONObject("content");
            if (content == null) return new ArrayList<>();
            // The API returns an object for one hit and an array for several.
            Object file = content.opt("file");
            List<Result> out = new ArrayList<>();
            if (file instanceof JSONArray) {
                JSONArray a = (JSONArray) file;
                for (int i = 0; i < a.length(); i++) out.add(new Result(a.getJSONObject(i)));
            } else if (file instanceof JSONObject) {
                out.add(new Result((JSONObject) file));
            }
            return out;
        } catch (JSONException e) {
            throw new IOException("bad idgames response", e);
        }
    }

    /** Candidate download URLs for a search hit, one per mirror, in order. */
    public static List<String> downloadUrls(Result r, List<String> mirrors) {
        List<String> out = new ArrayList<>();
        String dir = r.dir.startsWith("/") ? r.dir.substring(1) : r.dir;
        if (!dir.isEmpty() && !dir.endsWith("/")) dir += "/";
        for (String m : mirrors) out.add((m.endsWith("/") ? m : m + "/") + dir + r.filename);
        return out;
    }

    private static String readAll(InputStream in) throws IOException {
        try (InputStream is = in) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            for (int n; (n = is.read(buf)) > 0; ) bo.write(buf, 0, n);
            return bo.toString(StandardCharsets.UTF_8);
        }
    }
}

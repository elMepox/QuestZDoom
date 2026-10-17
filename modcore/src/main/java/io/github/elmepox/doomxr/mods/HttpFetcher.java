package io.github.elmepox.doomxr.mods;

import java.io.IOException;
import java.io.InputStream;

/** Network seam so the idgames client and downloader are testable without a network. */
public interface HttpFetcher {
    /** Opens {@code url} and returns its body; the caller closes it. Throws on non-2xx. */
    InputStream open(String url) throws IOException;

    /** Default implementation over HttpURLConnection with a project User-Agent. */
    static HttpFetcher standard(String userAgent) {
        return url -> {
            java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
            c.setRequestProperty("User-Agent", userAgent);
            c.setConnectTimeout(15_000);
            c.setReadTimeout(30_000);
            c.setInstanceFollowRedirects(true);
            int code = c.getResponseCode();
            if (code / 100 != 2) {
                c.disconnect();
                throw new IOException("HTTP " + code + " for " + url);
            }
            return c.getInputStream();
        };
    }
}

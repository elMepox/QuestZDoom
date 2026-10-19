package io.github.elmepox.doomxr.mods;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * The curated catalog: a JSON file the app downloads at start-up, so broken links are fixed by editing
 * {@code catalog/catalog.json} in the repository, with no new APK. Unknown fields are ignored, and a
 * newer {@code schema} than we understand is rejected instead of half-parsed.
 */
public final class Catalog {
    public static final int SUPPORTED_SCHEMA = 1;

    public final int schema;
    public final String updated;
    public final List<CatalogEntry> entries;

    public Catalog(int schema, String updated, List<CatalogEntry> entries) {
        this.schema = schema;
        this.updated = updated;
        this.entries = Collections.unmodifiableList(entries);
    }

    public static Catalog parse(String json) throws JSONException {
        JSONObject root = new JSONObject(json);
        int schema = root.getInt("schema");
        if (schema > SUPPORTED_SCHEMA) {
            throw new JSONException("catalog schema " + schema + " is newer than supported " + SUPPORTED_SCHEMA);
        }
        List<CatalogEntry> entries = new ArrayList<>();
        JSONArray arr = root.getJSONArray("entries");
        for (int i = 0; i < arr.length(); i++) {
            try {
                entries.add(CatalogEntry.fromJson(arr.getJSONObject(i)));
            } catch (JSONException e) {
                // One bad entry must not hide the rest of the catalog.
            }
        }
        return new Catalog(schema, root.optString("updated", ""), entries);
    }

    /** Case-insensitive match on title/description; empty query returns everything. */
    public List<CatalogEntry> search(String query, CatalogEntry.Type type, String iwad) {
        String q = query == null ? "" : query.trim().toLowerCase(Locale.ROOT);
        List<CatalogEntry> out = new ArrayList<>();
        for (CatalogEntry e : entries) {
            if (type != null && e.type != type) continue;
            if (iwad != null && !iwad.isEmpty() && !e.iwad.isEmpty() && !e.iwad.equalsIgnoreCase(iwad)) continue;
            if (!q.isEmpty() && !e.title.toLowerCase(Locale.ROOT).contains(q)
                    && !e.description.toLowerCase(Locale.ROOT).contains(q)) continue;
            out.add(e);
        }
        return out;
    }
}

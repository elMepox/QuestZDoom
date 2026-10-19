package io.github.elmepox.doomxr.mods;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** One downloadable mod (or a ready-made launch profile) from the curated catalog or idgames. */
public final class CatalogEntry {
    public enum Type { MAP, GAMEPLAY, RESOURCE;
        static Type parse(String s) {
            if (s != null) for (Type t : values()) if (t.name().equalsIgnoreCase(s)) return t;
            return MAP;
        }
    }

    /** A single file to download. {@code sha256} may be empty (idgames gives none). */
    public static final class FileRef {
        public final String name;
        public final String sha256;
        public final long size;
        public final List<String> urls;

        public FileRef(String name, String sha256, long size, List<String> urls) {
            this.name = name;
            this.sha256 = sha256 == null ? "" : sha256.toLowerCase();
            this.size = size;
            this.urls = Collections.unmodifiableList(new ArrayList<>(urls));
        }
    }

    public final String id;
    public final String title;
    public final String description;
    /** Required base game, e.g. "doom2", "doom", "freedoom2"; empty when any works. */
    public final String iwad;
    public final Type type;
    public final String homepage;
    public final List<FileRef> files;
    /** File names in the order they are passed to {@code -file}; empty means "as listed in files". */
    public final List<String> loadOrder;
    public final List<String> extraArgs;
    /** Set by the weekly link check: true when no mirror answered. */
    public final boolean broken;

    public CatalogEntry(String id, String title, String description, String iwad, Type type, String homepage,
                        List<FileRef> files, List<String> loadOrder, List<String> extraArgs, boolean broken) {
        this.id = id;
        this.title = title;
        this.description = description;
        this.iwad = iwad;
        this.type = type;
        this.homepage = homepage;
        this.files = files;
        this.loadOrder = loadOrder;
        this.extraArgs = extraArgs;
        this.broken = broken;
    }

    static CatalogEntry fromJson(JSONObject o) throws JSONException {
        List<FileRef> files = new ArrayList<>();
        JSONArray fa = o.optJSONArray("files");
        if (fa != null) {
            for (int i = 0; i < fa.length(); i++) {
                JSONObject f = fa.getJSONObject(i);
                files.add(new FileRef(f.getString("name"), f.optString("sha256", ""), f.optLong("size", 0),
                        strings(f.optJSONArray("urls"))));
            }
        }
        return new CatalogEntry(o.getString("id"), o.getString("title"), o.optString("description", ""),
                o.optString("iwad", ""), Type.parse(o.optString("type", null)), o.optString("homepage", ""),
                files, strings(o.optJSONArray("loadOrder")), strings(o.optJSONArray("args")),
                o.optBoolean("broken", false));
    }

    static List<String> strings(JSONArray a) {
        List<String> out = new ArrayList<>();
        if (a != null) for (int i = 0; i < a.length(); i++) out.add(a.optString(i));
        return out;
    }
}

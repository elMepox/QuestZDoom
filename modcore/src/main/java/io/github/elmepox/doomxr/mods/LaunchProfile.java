package io.github.elmepox.doomxr.mods;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * What the panel hands to the immersive activity: replaces the old commandline.txt written by an
 * external launcher. Travels as one JSON string in an Intent extra and is stored the same way on disk.
 */
public final class LaunchProfile {
    public static final String INTENT_EXTRA = "doomxr_profile";

    public final String name;
    /** IWAD file name inside the user dir (e.g. "doom2.wad"); empty means the bundled Freedoom. */
    public final String iwad;
    /** Mod files in load order, as file names inside the mods dir. */
    public final List<String> files;
    public final List<String> extraArgs;

    public LaunchProfile(String name, String iwad, List<String> files, List<String> extraArgs) {
        this.name = name;
        this.iwad = iwad == null ? "" : iwad;
        this.files = files;
        this.extraArgs = extraArgs;
    }

    /** Engine arguments; {@code modsDir} is prefixed to every mod file. */
    public List<String> toArguments(String modsDir) {
        List<String> a = new ArrayList<>();
        if (!iwad.isEmpty()) { a.add("-iwad"); a.add(iwad); }
        if (!files.isEmpty()) {
            a.add("-file");
            String base = modsDir.endsWith("/") ? modsDir : modsDir + "/";
            for (String f : files) a.add(base + f);
        }
        a.addAll(extraArgs);
        return a;
    }

    public String toJson() {
        try {
            return new JSONObject().put("name", name).put("iwad", iwad)
                    .put("files", new JSONArray(files)).put("args", new JSONArray(extraArgs)).toString();
        } catch (JSONException e) {
            throw new IllegalStateException(e);
        }
    }

    public static LaunchProfile fromJson(String json) throws JSONException {
        JSONObject o = new JSONObject(json);
        return new LaunchProfile(o.optString("name"), o.optString("iwad"),
                CatalogEntry.strings(o.optJSONArray("files")), CatalogEntry.strings(o.optJSONArray("args")));
    }

    /** Profile for a catalog entry using its declared load order. */
    public static LaunchProfile forEntry(CatalogEntry e, String iwadFile) {
        List<String> files = new ArrayList<>(e.loadOrder);
        if (files.isEmpty()) for (CatalogEntry.FileRef f : e.files) files.add(f.name);
        return new LaunchProfile(e.title, iwadFile, files, e.extraArgs);
    }
}

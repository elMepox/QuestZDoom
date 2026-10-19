package io.github.elmepox.doomxr.mods;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/** Lists what is in the mods and IWAD folders; rescans on every call so USB-copied files show up at once. */
public final class ModLibrary {
    private final File modsDir, iwadDir;

    public ModLibrary(File modsDir, File iwadDir) {
        this.modsDir = modsDir;
        this.iwadDir = iwadDir;
    }

    public List<File> mods() {
        List<File> out = new ArrayList<>();
        File[] fs = modsDir.listFiles();
        if (fs != null) {
            Arrays.sort(fs);
            for (File f : fs) if (f.isFile() && Downloader.isPayload(f.getName())) out.add(f);
        }
        return out;
    }

    /** Base games found in the IWAD folder, as file -> game id. */
    public List<String[]> iwads() {
        List<String[]> out = new ArrayList<>();
        File[] fs = iwadDir.listFiles();
        if (fs != null) {
            Arrays.sort(fs);
            for (File f : fs) {
                if (!f.isFile() || !f.getName().toLowerCase(Locale.ROOT).endsWith(".wad")) continue;
                String game = IwadDetector.detect(f);
                if (game != null) out.add(new String[] { f.getName(), game });
            }
        }
        return out;
    }
}

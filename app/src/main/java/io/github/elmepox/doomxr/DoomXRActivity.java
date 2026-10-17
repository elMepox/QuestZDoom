package io.github.elmepox.doomxr;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Hosts the UZDoom engine through SDL2. Flat SDL window with an OpenXR layer (see cpp/xr) on top.
 *
 * Layout on the device:
 *   getExternalFilesDir()/            user content: drop IWADs (freedoom1.wad, doom.wad ...) here
 *   getExternalFilesDir()/config      UZDoom config + savegames (XDG_CONFIG_HOME)
 *   getFilesDir()/data/games/uzdoom   engine resources unpacked from the APK (XDG_DATA_HOME)
 */
public class DoomXRActivity extends SDLActivity {
    private static final String TAG = "DoomXR";
    private static final String ASSET_ROOT = "uzdoom";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            prepareEnvironment();
        } catch (IOException | ErrnoException e) {
            Log.e(TAG, "Failed to prepare engine environment", e);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "openal", "main" };
    }

    @Override
    protected String[] getArguments() {
        // UZDoom's GLES renderer; Vulkan is not built in.
        java.util.List<String> args = new java.util.ArrayList<>(java.util.Arrays.asList(
                "+vid_preferbackend", "2", "+gl_es", "1",
                // vr_mode 15 = OpenXR stereo; falls back to the flat window if OpenXR cannot start.
                "+vr_mode", "15"));
        // No commercial IWAD supplied by the user: fall back to the bundled Freedoom.
        if (!hasUserIwad()) {
            args.add("-iwad");
            args.add("freedoom2.wad");
        }
        return args.toArray(new String[0]);
    }

    private static final String[] KNOWN_IWADS = {
        "doom.wad", "doom1.wad", "doomu.wad", "doom2.wad", "plutonia.wad", "tnt.wad",
        "heretic.wad", "hexen.wad", "strife1.wad", "freedoom1.wad", "freedoom2.wad", "chex.wad"
    };

    private boolean hasUserIwad() {
        File userDir = getExternalFilesDir(null);
        if (userDir == null) userDir = getFilesDir();
        String[] names = userDir.list();
        if (names == null) return false;
        for (String n : names) {
            for (String k : KNOWN_IWADS) if (k.equalsIgnoreCase(n)) return true;
        }
        return false;
    }

    private void prepareEnvironment() throws IOException, ErrnoException {
        File userDir = getExternalFilesDir(null);
        if (userDir == null) userDir = getFilesDir();
        File dataHome = new File(getFilesDir(), "data");
        File engineDir = new File(dataHome, "games/uzdoom");
        File configHome = new File(userDir, "config");
        File cacheHome = new File(getCacheDir(), "xdg");
        for (File d : new File[] { engineDir, configHome, cacheHome }) {
            if (!d.isDirectory() && !d.mkdirs()) Log.w(TAG, "Could not create " + d);
        }

        Os.setenv("HOME", userDir.getAbsolutePath(), true);
        Os.setenv("XDG_DATA_HOME", dataHome.getAbsolutePath(), true);
        Os.setenv("XDG_CONFIG_HOME", configHome.getAbsolutePath(), true);
        Os.setenv("XDG_CACHE_HOME", cacheHome.getAbsolutePath(), true);
        // The engine looks for uzdoom.pk3, soundfonts/ and fm_banks/ relative to "progdir", and for
        // IWADs in DOOMWADDIR / DOOMWADPATH (user dir first, then the unpacked Freedoom).
        Os.setenv("DOOMXR_PROGDIR", engineDir.getAbsolutePath(), true);
        Os.setenv("DOOMWADDIR", userDir.getAbsolutePath(), true);
        Os.setenv("DOOMWADPATH", userDir.getAbsolutePath() + ":" + engineDir.getAbsolutePath(), true);

        copyAssets(getAssets(), ASSET_ROOT, engineDir);
        copyAssets(getAssets(), "freedoom", engineDir);
        Log.i(TAG, "Put IWADs into " + userDir.getAbsolutePath());
    }

    /** Recursively copies APK assets, skipping files that already match in size. */
    private static void copyAssets(AssetManager am, String assetPath, File dest) throws IOException {
        String[] children = am.list(assetPath);
        if (children != null && children.length > 0) {
            if (!dest.isDirectory() && !dest.mkdirs()) throw new IOException("mkdir " + dest);
            for (String child : children) {
                copyAssets(am, assetPath + "/" + child, new File(dest, child));
            }
            return;
        }
        try (InputStream in = am.open(assetPath)) {
            if (dest.isFile() && dest.length() == in.available()) return;
            try (OutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[64 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            }
        }
    }
}

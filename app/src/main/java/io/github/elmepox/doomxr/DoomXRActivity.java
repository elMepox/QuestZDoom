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
 * Hosts the UZDoom engine through SDL2. Stage 2: flat window. The OpenXR layer replaces this later.
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
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        // UZDoom's GLES renderer; Vulkan is not built in.
        return new String[] { "+vid_preferbackend", "2", "+gl_es", "1" };
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
        Os.setenv("DOOMWADDIR", userDir.getAbsolutePath(), true);

        copyAssets(getAssets(), ASSET_ROOT, engineDir);
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

package io.github.elmepox.doomxr.mods;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Recognises base games among imported files without hashes of commercial data: checks the WAD header
 * magic ("IWAD") and the map lumps, then the file name to tell the families apart.
 */
public final class IwadDetector {
    private IwadDetector() {}

    /** Returns a game id ("doom", "doom2", "plutonia", "tnt", "heretic", "hexen", "freedoom1", ...) or null. */
    public static String detect(File f) {
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] head = new byte[12];
            raf.readFully(head);
            if (!"IWAD".equals(new String(head, 0, 4, StandardCharsets.US_ASCII))) return null;
            ByteBuffer h = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN);
            int count = h.getInt(4), dirOfs = h.getInt(8);
            if (count <= 0 || count > 200_000 || dirOfs < 12 || dirOfs + 16L * count > raf.length()) return null;
            raf.seek(dirOfs);
            byte[] dir = new byte[16 * count];
            raf.readFully(dir);
            boolean e1m1 = false, map01 = false;
            for (int i = 0; i < count; i++) {
                String lump = new String(dir, i * 16 + 8, 8, StandardCharsets.US_ASCII).replace("\0", "");
                if (lump.equals("E1M1")) e1m1 = true;
                if (lump.equals("MAP01")) map01 = true;
            }
            return classify(f.getName().toLowerCase(Locale.ROOT), e1m1, map01);
        } catch (IOException e) {
            return null;
        }
    }

    static String classify(String name, boolean e1m1, boolean map01) {
        for (String known : new String[] { "freedoom1", "freedoom2", "plutonia", "tnt", "heretic", "hexen",
                "strife1", "chex", "doom2", "doom1", "doomu", "doom" }) {
            if (name.startsWith(known)) return known.equals("doom1") || known.equals("doomu") ? "doom" : known;
        }
        // Renamed file: fall back to map naming.
        if (map01) return "doom2";
        if (e1m1) return "doom";
        return null;
    }
}

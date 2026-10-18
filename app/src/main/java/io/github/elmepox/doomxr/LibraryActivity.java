package io.github.elmepox.doomxr;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import io.github.elmepox.doomxr.mods.Catalog;
import io.github.elmepox.doomxr.mods.CatalogEntry;
import io.github.elmepox.doomxr.mods.Downloader;
import io.github.elmepox.doomxr.mods.HttpFetcher;
import io.github.elmepox.doomxr.mods.IdgamesClient;
import io.github.elmepox.doomxr.mods.LaunchProfile;
import io.github.elmepox.doomxr.mods.ModLibrary;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 2D panel (Horizon OS shows it as a window; ray and pinch work with plain views): library of mods,
 * catalog and idgames search, import, and the Play button that hands a {@link LaunchProfile} to the
 * immersive {@link DoomXRActivity}.
 */
public class LibraryActivity extends Activity {
    static final String CATALOG_URL =
            "https://raw.githubusercontent.com/elmepox/questzdoom/doomxr/catalog/catalog.json";
    private static final int REQ_IMPORT = 1;

    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final HttpFetcher http = HttpFetcher.standard(
            // Some hosts (Doomworld sits behind a WAF) answer 403 to tool-looking agents, so present as a normal client.
            "Mozilla/5.0 (Linux; Android 14; Quest 3) AppleWebKit/537.36 (KHTML, like Gecko) "
                    + "Chrome/126.0 Mobile Safari/537.36 DoomXR/0.1");

    private File modsDir, iwadDir;
    private ModLibrary library;
    private LinearLayout content;
    private TextView status;
    private Spinner iwadSpinner;
    private final List<CheckBox> modBoxes = new ArrayList<>();
    private Catalog catalog;
    private boolean onLibrary;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        iwadDir = getExternalFilesDir(null) != null ? getExternalFilesDir(null) : getFilesDir();
        modsDir = new File(iwadDir, "mods");
        modsDir.mkdirs();
        library = new ModLibrary(modsDir, iwadDir);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 32, 32, 32);
        LinearLayout tabs = new LinearLayout(this);
        tabs.addView(button("Библиотека", v -> showLibrary()));
        tabs.addView(button("Каталог", v -> showCatalog()));
        tabs.addView(button("Импорт", v -> showImport()));
        root.addView(tabs);
        status = new TextView(this);
        root.addView(status);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        ScrollView sv = new ScrollView(this);
        sv.addView(content);
        root.addView(sv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
        showLibrary();
        loadCatalogAsync();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Files copied over USB while the panel was open show up on return.
        if (onLibrary) showLibrary();
    }

    @Override
    protected void onDestroy() {
        io.shutdownNow();
        super.onDestroy();
    }

    // --- screens ---------------------------------------------------------------------------

    private void showLibrary() {
        onLibrary = true;
        content.removeAllViews();
        modBoxes.clear();
        List<String> iwads = new ArrayList<>();
        iwads.add("Freedoom (встроенный)");
        List<String> iwadFiles = new ArrayList<>();
        iwadFiles.add("");
        for (String[] w : library.iwads()) {
            iwads.add(w[0] + " (" + w[1] + ")");
            iwadFiles.add(w[0]);
        }
        iwadSpinner = new Spinner(this);
        iwadSpinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, iwads));
        iwadSpinner.setTag(iwadFiles);
        content.addView(label("Игра (IWAD)"));
        content.addView(iwadSpinner);
        content.addView(label("Моды (порядок загрузки сверху вниз)"));
        List<File> mods = library.mods();
        if (mods.isEmpty()) content.addView(label("Пока пусто. Скачайте мод в «Каталоге» или импортируйте файл."));
        for (File f : mods) {
            CheckBox cb = new CheckBox(this);
            cb.setText(f.getName());
            modBoxes.add(cb);
            content.addView(cb);
        }
        content.addView(button("Играть в VR", v -> play()));
    }

    private void showCatalog() {
        onLibrary = false;
        content.removeAllViews();
        EditText q = new EditText(this);
        q.setHint("Поиск по idgames (от 3 символов)");
        q.setSingleLine();
        content.addView(q);
        LinearLayout results = new LinearLayout(this);
        results.setOrientation(LinearLayout.VERTICAL);
        content.addView(button("Искать в idgames", v -> searchIdgames(q.getText().toString(), results)));
        content.addView(label("Избранное"));
        if (catalog == null || catalog.entries.isEmpty()) {
            content.addView(label("Каталог пока пуст или не загрузился."));
        } else {
            for (CatalogEntry e : catalog.search("", null, null)) {
                String t = e.title + (e.broken ? " (недоступен)" : "");
                Button bt = button(t + " — скачать", v -> downloadEntry(e));
                bt.setEnabled(!e.broken);
                content.addView(bt);
            }
        }
        content.addView(results);
    }

    private void showImport() {
        onLibrary = false;
        content.removeAllViews();
        content.addView(label("Файл из «Загрузок» или другой папки"));
        content.addView(button("Выбрать файл…", v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            startActivityForResult(i, REQ_IMPORT);
        }));
        content.addView(label("Скачать по ссылке (zip, wad, pk3)"));
        EditText url = new EditText(this);
        url.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
        url.setHint("https://…");
        content.addView(url);
        content.addView(button("Скачать", v -> {
            String u = url.getText().toString().trim();
            if (u.isEmpty()) return;
            String name = u.substring(u.lastIndexOf('/') + 1);
            runBg("Скачиваю " + name, () -> {
                new Downloader(http).fetch(List.of(u), name, "", 0, modsDir, null);
            });
        }));
        content.addView(label("Папка для копирования по USB:\n" + modsDir.getAbsolutePath()
                + "\nIWAD кладите в:\n" + iwadDir.getAbsolutePath()));
    }

    // --- actions ---------------------------------------------------------------------------

    @SuppressWarnings("unchecked")
    private void play() {
        List<String> files = new ArrayList<>();
        for (CheckBox cb : modBoxes) if (cb.isChecked()) files.add(cb.getText().toString());
        List<String> iwadFiles = (List<String>) iwadSpinner.getTag();
        String iwad = iwadFiles.get(iwadSpinner.getSelectedItemPosition());
        LaunchProfile p = new LaunchProfile("Library", iwad, files, new ArrayList<>());
        // Panel -> immersive: new task, then drop the panel (Horizon OS hybrid-app switching).
        Intent i = new Intent(this, DoomXRActivity.class)
                .putExtra(LaunchProfile.INTENT_EXTRA, p.toJson())
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
        finishAndRemoveTask();
    }

    private void searchIdgames(String query, LinearLayout results) {
        results.removeAllViews();
        runBg("Ищу «" + query + "»", () -> {
            List<IdgamesClient.Result> hits = new IdgamesClient(http).search(query, IdgamesClient.SearchType.TITLE);
            ui.post(() -> {
                if (hits.isEmpty()) results.addView(label("Ничего не найдено."));
                for (IdgamesClient.Result r : hits) {
                    results.addView(button(r.title + " · " + r.author + " · " + r.rating + " — скачать", v ->
                            runBg("Скачиваю " + r.filename, () -> new Downloader(http).fetch(
                                    IdgamesClient.downloadUrls(r, IdgamesClient.DEFAULT_MIRRORS),
                                    r.filename, "", r.size, modsDir, null))));
                }
            });
        });
    }

    private void downloadEntry(CatalogEntry e) {
        runBg("Скачиваю " + e.title, () -> {
            for (CatalogEntry.FileRef f : e.files) new Downloader(http).fetch(f, modsDir, null);
        });
    }

    private void loadCatalogAsync() {
        File cache = new File(getCacheDir(), "catalog.json");
        io.execute(() -> {
            String json = null;
            try (InputStream in = http.open(CATALOG_URL)) {
                json = new String(in.readAllBytes(), StandardCharsets.UTF_8);
                Files.write(cache.toPath(), json.getBytes(StandardCharsets.UTF_8));
            } catch (IOException e) {
                try { // offline: last downloaded copy
                    if (cache.isFile()) json = new String(Files.readAllBytes(cache.toPath()), StandardCharsets.UTF_8);
                } catch (IOException ignored) { }
            }
            try {
                if (json != null) catalog = Catalog.parse(json);
            } catch (Exception ignored) { }
        });
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req != REQ_IMPORT || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        runBg("Импортирую", () -> {
            String name = "import.wad";
            try (android.database.Cursor c = getContentResolver().query(uri, null, null, null, null)) {
                if (c != null && c.moveToFirst()) name = c.getString(c.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME));
            }
            final String n = name;
            // Reuse the downloader's unpack/sanitise path by serving the picked file as a stream.
            new Downloader(url -> getContentResolver().openInputStream(uri))
                    .fetch(List.of(uri.toString()), n, "", 0, modsDir, null);
        });
    }

    // --- helpers ---------------------------------------------------------------------------

    private interface Job { void run() throws Exception; }

    private void runBg(String what, Job job) {
        status.setText(what + "…");
        io.execute(() -> {
            String result;
            try {
                job.run();
                result = "Готово";
            } catch (Exception e) {
                result = "Ошибка: " + e.getMessage();
            }
            final String r = result;
            ui.post(() -> {
                status.setText(r);
                // New files appear in the library list.
                if (onLibrary) showLibrary();
            });
        });
    }

    private Button button(String text, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        return b;
    }

    private TextView label(String text) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setPadding(0, 16, 0, 8);
        return t;
    }
}

package com.mh4u.port;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Process;
import android.provider.DocumentsContract;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Settings -> Import save / Export save.
 *
 * MH4U keeps its save as files: system, user1..user3 in the save data (title/00040000/00126300/data/00000001/), and
 * the guild cards, event quests and the Palico in its extra data (extdata/00000000/00001263/). The same layout as
 * Citra and Azahar, and as Checkpoint's backups on a 3DS (the save files alone).
 *
 * Import: the source is found by a search of the storage, or chosen with the folder browser (an emulator's whole user
 * folder, its sdmc folder or the one inside it, a save's own folder, an Export folder: 4u-native #16); it is checked and
 * copied into save_import/, then the app restarts. GameActivity applies it before the game starts (applyPending): a
 * copy of the current save goes to save_backups/<date>, then it is replaced whole (SaveFolder.copySave: staged, then
 * renamed into place), in the save folder when one is chosen.
 * Export: a copy of the save in a folder the player chooses (the system's folder picker, as MHGU's SaveExportActivity):
 * a new folder "MH4U save <date>" in it, in Azahar's layout (sdmc/...), for an emulator or a 3DS.
 */
public class SaveActivity extends Activity {
    static final String EXTRA_EXPORT = "export";
    private static final int PICK_FOLDER = 1;
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int ALERT = Color.rgb(255, 150, 120);
    private static final String ZEROS = "00000000000000000000000000000000";
    private static final String[] SAVE_FILES = {"system", "user1", "user2", "user3"};
    // the save archive's format record (FormatSaveData's values for MH4U, the same in Citra, Azahar and here)
    private static final byte[] METADATA = {0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0};

    private boolean es, export;
    private TextView status;
    private Button exportButton;
    private LinearLayout list;

    static File home(Context c) { return AppHome.home(c); }
    static File userDir(Context c) { return new File(home(c), "user"); }
    // (the save is where Settings -> Save folder says: the user folder's sdmc/, or the chosen folder's)
    static File saveDir(Context c, String title) { return SaveFolder.dataDir(SaveFolder.active(c), title); }
    static File extDir(Context c, String title) { return SaveFolder.extDir(SaveFolder.active(c), title); }
    private static File importDir(Context c) { return new File(home(c), "save_import"); }

    // ---- applying an import before the game starts (GameActivity.onCreate)
    /**
     * The save staged in save_import/<title>/{data, extdata} takes the current one's place, which is copied to
     * save_backups first. Nothing is deleted before the new save is copied whole and checked: the staged files are
     * laid out as a save root of their own and SaveFolder.copySave moves them in by renames. If anything fails, the
     * current save stays as it was and the import is tried again at the next start.
     */
    static void applyPending(Context c) {
        File staged = importDir(c), ready = new File(staged, "ready");
        if (!ready.exists()) return;
        File root = SaveFolder.active(c), from = new File(staged, "root");
        try {
            SaveFolder.deleteTree(from);
            int titles = 0;
            for (String t : SaveFolder.TITLES) {
                File src = new File(staged, t);
                if (!new File(src, "data/system").isFile()) continue;
                titles++;
                SaveFolder.copyTree(new File(src, "data"), SaveFolder.dataDir(from, t));
                File meta = SaveFolder.metaFile(root, t);
                if (meta.isFile()) SaveFolder.copyChecked(meta, SaveFolder.metaFile(from, t));
                else writeBytes(SaveFolder.metaFile(from, t), METADATA);
                if (new File(src, "extdata").isDirectory()) SaveFolder.copyTree(new File(src, "extdata"), SaveFolder.extDir(from, t));
            }
            if (titles == 0) throw new IOException("nothing staged");
            File backup = SaveFolder.describe(root).valid ? SaveFolder.backup(c, root, "import") : null;
            SaveFolder.copySave(from, root);
            SaveFolder.deleteTree(staged);
            android.util.Log.i("MH4U", "save import applied in " + root + (backup != null ? ", the old save in " + backup : ""));
        } catch (IOException e) {  // (the save stays as it was, the import is tried again at the next start)
            SaveFolder.deleteTree(from);
            android.util.Log.e("MH4U", "save import: " + e.getMessage());
        }
    }

    // ---- the screen
    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
    private TextView text(String s, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }
    private GradientDrawable plate(int fill, int r) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setStroke(dp(2), EDGE);
        d.setCornerRadius(dp(r));
        return d;
    }
    private Button button(String s, boolean main) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(main ? GOLD : CREAM);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        if (main) b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(plate(BRONZE, 6));
        b.setPadding(dp(18), 0, dp(18), 0);
        return b;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        es = "es".equals(Locale.getDefault().getLanguage());
        export = getIntent().getBooleanExtra(EXTRA_EXPORT, false);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackground(plate(PANEL, 8));
        box.setPadding(dp(16), dp(12), dp(16), dp(12));
        TextView title = text(export ? (es ? "EXPORTAR PARTIDA" : "EXPORT SAVE") : (es ? "IMPORTAR PARTIDA" : "IMPORT SAVE"), 19, GOLD, true);
        title.setGravity(Gravity.CENTER);
        box.addView(title);
        TextView intro = text(export
                ? (es ? "Elige una carpeta y ahí se crea otra, \"MH4U save\" con la fecha, con la partida (y las tarjetas de gremio, misiones de evento y el Felyne) como la usan Azahar y Citra. Se copia lo último que guardaste en el juego."
                      : "Choose a folder and a new one is made there, \"MH4U save\" with the date, with the save (and the guild cards, event quests and the Palico) as Azahar and Citra use it. It copies what you last saved in the game.")
                : (es ? "Elige de dónde traer la partida. La actual queda guardada en save_backups antes de reemplazarla."
                      : "Choose where to bring the save from. The current one is kept in save_backups before it is replaced."),
                14, CREAM, false);
        intro.setPadding(0, dp(6), 0, dp(8));
        box.addView(intro);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        box.addView(list);
        status = text("", 14, GOLD, false);
        status.setPadding(0, dp(8), 0, dp(4));
        box.addView(status);
        Button back = button(es ? "Volver" : "Back", false);
        back.setOnClickListener(v -> finish());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(44));
        lp.gravity = Gravity.CENTER_HORIZONTAL;
        box.addView(back, lp);

        ScrollView scroll = new ScrollView(this);
        LinearLayout page = new LinearLayout(this);
        page.setGravity(Gravity.CENTER);
        page.setBackgroundColor(WINE);
        page.setPadding(dp(32), dp(16), dp(32), dp(16));
        int room = getResources().getDisplayMetrics().widthPixels - dp(64);
        page.addView(box, new LinearLayout.LayoutParams(Math.min(dp(620), room), LinearLayout.LayoutParams.WRAP_CONTENT));
        scroll.addView(page);
        scroll.setBackgroundColor(WINE);
        setContentView(scroll);

        if (export) {
            exportButton = button(es ? "Elegir carpeta" : "Choose a folder", true);
            exportButton.setOnClickListener(v -> {
                try {
                    startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_FOLDER);
                } catch (android.content.ActivityNotFoundException e) {
                    say(es ? "Este teléfono no tiene selector de carpetas." : "This phone has no folder picker.", true);
                }
            });
            list.addView(exportButton, row());
            if (!hasSave()) {
                exportButton.setEnabled(false);
                say(es ? "Todavía no hay una partida para exportar." : "There is no save to export yet.", true);
            }
        } else {
            // (4u-native #16: a save the search doesn't reach, in any folder the player can point at)
            Button folder = button(es ? "Elegir carpeta" : "Choose a folder", true);
            folder.setOnClickListener(v -> browse());
            list.addView(folder, row());
            status.setText(es ? "Buscando partidas..." : "Looking for saves...");
            new Thread(this::findSources).start();
        }
    }

    // ---- choosing the folder the save is in (the game's folder browser, as MHGU's import has a picker)
    private void browse() {
        if (Build.VERSION.SDK_INT >= 30 ? !Environment.isExternalStorageManager()
                : checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            say(es ? "El juego no tiene permiso para leer tus carpetas: elige otra vez la carpeta del juego en Ajustes para darlo."
                   : "The game has no permission to read your folders: choose the game folder again in Settings to give it.", true);
            return;
        }
        FolderBrowser.open(this, null, es ? "ELIGE LA CARPETA DE TU PARTIDA" : "CHOOSE YOUR SAVE'S FOLDER",
                dir -> source(dir, 0) != null,
                (dir, holds) -> holds ? (es ? "✓ Aquí hay una partida." : "✓ There is a save here.")
                                      : (es ? "Entra a la carpeta de tu partida (system, user1...) o a la de tu emulador (sdmc)."
                                            : "Go into your save's folder (system, user1...) or your emulator's (sdmc)."),
                dir -> {
                    Source s = source(dir, 0);
                    if (s != null && inUse(s.data)) {
                        say(es ? "Esa es la partida que el juego usa ahora." : "That is the save the game uses now.", true);
                        return;
                    }
                    if (s == null) {
                        say(es ? "En esa carpeta no hay una partida de MH4U. Elige la que tiene system y user1, o la carpeta sdmc de tu emulador."
                               : "There is no MH4U save in that folder. Choose the one with system and user1, or your emulator's sdmc folder.", true);
                        return;
                    }
                    stage(s);
                }, null, false);
    }

    /**
     * The save in a folder the player chose: the folder of the save's files itself (system and a user file: a
     * Checkpoint backup, Azahar's "Open Save Data Location"), or one with the emulators' layout under it (an emulator's
     * user folder, its sdmc folder or the one inside it, an Export folder), a few levels down. null: none.
     */
    private Source source(File dir, int depth) {
        if (dir == null || depth > 4) return null;
        if (looksLikeSave(dir)) {
            Source s = new Source();
            s.data = dir;
            s.title = "00126300";
            File data = dir.getParentFile(), titleDir = data != null ? data.getParentFile() : null;
            String t = titleDir != null ? titleDir.getName().toLowerCase(Locale.ROOT) : "";
            if (dir.getName().equals("00000001") && data.getName().equals("data") && (t.equals("00126300") || t.equals("00126100"))) {
                s.title = t;
                File id1 = titleDir.getParentFile() != null ? titleDir.getParentFile().getParentFile() : null;
                File ext = id1 == null ? null : new File(id1, "extdata/00000000/" + (t.equals("00126100") ? "00001261" : "00001263"));
                s.ext = ext != null && ext.isDirectory() ? ext : null;
            }
            s.label = describe(dir);
            return s;
        }
        if (dir.getName().equals("Nintendo 3DS")) {
            Source best = null;
            File[] ids0 = dir.listFiles(File::isDirectory);
            if (ids0 != null)
                for (File id0 : ids0) {
                    File[] ids1 = id0.listFiles(File::isDirectory);
                    if (ids1 == null) continue;
                    for (File id1 : ids1)
                        for (String t : SaveFolder.TITLES) {
                            Source s = source(new File(id1, "title/00040000/" + t + "/data/00000001"), 4);
                            if (s != null && (best == null || new File(s.data, "system").lastModified() > new File(best.data, "system").lastModified()))
                                best = s;
                        }
                }
            return best;
        }
        File n3ds = new File(dir, "Nintendo 3DS"), sdmc = new File(dir, "sdmc");
        if (n3ds.isDirectory()) return source(n3ds, depth + 1);
        if (sdmc.isDirectory()) return source(sdmc, depth + 1);
        if (depth >= 2) return null;
        File[] kids = dir.listFiles(f -> f.isDirectory() && !f.getName().startsWith("."));
        if (kids == null || kids.length > 40) return null;
        for (File k : kids) {  // (an emulator's folder: azahar-emu/sdmc, citra-emu/sdmc, an Export folder's sdmc)
            if (new File(k, "sdmc").isDirectory() || new File(k, "Nintendo 3DS").isDirectory()) {
                Source s = source(k, depth + 1);
                if (s != null) return s;
            }
        }
        return null;
    }

    private LinearLayout.LayoutParams row() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(46));
        lp.setMargins(0, dp(3), 0, dp(3));
        return lp;
    }

    // ---- where saves are: an emulator's user folder (sdmc/...) or a folder with the save files themselves
    private static final class Source {
        File data, ext;
        String title, label;
    }

    private void findSources() {
        List<Source> found = new ArrayList<>();
        File root = Environment.getExternalStorageDirectory();
        List<File> starts = new ArrayList<>();
        starts.add(root);
        File[] mine = getExternalFilesDirs(null);
        if (mine != null)
            for (File f : mine) {
                if (f == null) continue;
                String p = f.getAbsolutePath();
                int cut = p.indexOf("/Android/data/");
                if (cut > 0 && !new File(p.substring(0, cut)).equals(root)) starts.add(new File(p.substring(0, cut)));
            }
        for (File s : starts) scan(s, 0, found);
        runOnUiThread(() -> show(found));
    }

    private void scan(File dir, int depth, List<Source> out) {
        if (depth > 6 || out.size() > 20) return;
        String name = dir.getName();
        if (name.equals("Nintendo 3DS")) {  // (an emulator's: the save is 6 levels further, past the search's depth)
            Source s = source(dir, 0);
            if (s != null && !inUse(s.data)) out.add(s);
            return;
        }
        if (name.equals("00000001") && dir.getParentFile() != null && dir.getParentFile().getName().equals("data")) {
            File titleDir = dir.getParentFile().getParentFile();
            String t = titleDir != null ? titleDir.getName().toLowerCase(Locale.ROOT) : "";
            if ((t.equals("00126300") || t.equals("00126100")) && new File(dir, "system").exists()) {
                Source s = new Source();
                s.data = dir;
                s.title = t;
                // <sdmc>/Nintendo 3DS/<id0>/<id1>/title/00040000/<t>/data/00000001 -> <id1>/extdata/00000000/0000126x
                File id1 = titleDir.getParentFile() != null ? titleDir.getParentFile().getParentFile() : null;
                File ext = id1 == null ? null : new File(id1, "extdata/00000000/" + (t.equals("00126100") ? "00001261" : "00001263"));
                s.ext = ext != null && ext.isDirectory() ? ext : null;
                s.label = describe(dir);
                if (!inUse(dir)) out.add(s);
                return;
            }
        }
        if (depth > 0 && looksLikeSave(dir)) {
            Source s = new Source();  // a Checkpoint backup, or the files copied by hand
            s.data = dir;
            s.title = "00126300";
            s.label = describe(dir);
            out.add(s);
            return;
        }
        File[] kids = dir.listFiles(f -> f.isDirectory() && !f.getName().startsWith("."));
        if (kids == null) return;
        for (File k : kids) {
            if (depth == 0 && k.getName().equals("Android")) {
                File data = new File(k, "data");  // (other apps' folders: Android 11 and later keep them closed)
                File[] apps = data.listFiles(File::isDirectory);
                if (apps != null) for (File a : apps) if (!a.getName().equals(getPackageName())) scan(new File(a, "files"), 2, out);
                continue;
            }
            scan(k, depth + 1, out);
        }
    }

    /** A save the game itself uses (its own folder, or the save folder chosen): not something to import. */
    private boolean inUse(File dir) {
        String p = dir.getAbsolutePath();
        return p.startsWith(userDir(this).getAbsolutePath() + "/") || p.startsWith(SaveFolder.active(this).getAbsolutePath() + "/");
    }

    /** system (512 bytes) and at least one hunter's file. */
    private static boolean looksLikeSave(File dir) {
        if (new File(dir, "system").length() != 512) return false;
        for (int i = 1; i <= 3; i++) {
            long n = new File(dir, "user" + i).length();
            if (n > 0 && n < 512 * 1024) return true;
        }
        return false;
    }

    private String describe(File dir) {
        String p = dir.getAbsolutePath();
        String where = p.contains("citra") ? "Citra" : p.contains("azahar") || p.contains("Azahar") ? "Azahar"
                     : p.contains("lime3ds") ? "Lime3DS" : p.contains("Checkpoint") ? "Checkpoint" : null;
        if (where == null) {  // (the folder the player would know it by: the one that holds sdmc, else the save's own)
            String root = Environment.getExternalStorageDirectory().getAbsolutePath();
            int cut = p.indexOf("/sdmc/");
            String place = cut > 0 ? p.substring(0, cut) : dir.getName().equals("00000001") || dir.getName().equals("data")
                    ? dir.getParent() : p;
            where = place.startsWith(root + "/") ? place.substring(root.length() + 1) : new File(place).getName();
        }
        int users = 0;
        for (int i = 1; i <= 3; i++) if (new File(dir, "user" + i).exists()) users++;
        String date = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT).format(new Date(new File(dir, "system").lastModified()));
        return where + " · " + users + (es ? (users == 1 ? " personaje" : " personajes") : (users == 1 ? " hunter" : " hunters")) + " · " + date;
    }

    private void show(List<Source> found) {
        status.setText(found.isEmpty()
                ? (es ? "La búsqueda no encontró ninguna partida de MH4U. Usa Elegir carpeta y entra a la carpeta de tu partida (system, user1...) o a la de tu emulador. Una partida dentro de Android/data de otra app hay que copiarla afuera primero."
                      : "The search found no MH4U save. Use Choose a folder and go to your save's folder (system, user1...) or your emulator's. A save inside another app's Android/data has to be copied out first.")
                : (es ? "Toca la partida que quieres traer, o elige su carpeta." : "Tap the save to bring in, or choose its folder."));
        for (Source s : found) {
            Button b = button("▸  " + s.label, true);
            b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            b.setOnClickListener(v -> stage(s));
            list.addView(b, row());
        }
    }

    private void stage(Source s) {
        File staged = importDir(this);
        deleteTree(staged);
        File to = new File(staged, s.title);
        File data = new File(to, "data");
        data.mkdirs();
        try {
            for (String f : SAVE_FILES) {
                File from = new File(s.data, f);
                if (from.exists()) SaveFolder.copyChecked(from, new File(data, f));
            }
            if (s.ext != null) SaveFolder.copyTree(s.ext, new File(to, "extdata"));
        } catch (IOException e) {
            deleteTree(staged);
            android.util.Log.e("MH4U", "save import: " + e.getMessage());
            say(es ? "No se pudo copiar esa partida. Cópiala a la memoria del teléfono y vuelve a intentarlo."
                   : "That save could not be copied. Copy it to the phone's storage and try again.", true);
            return;
        }
        writeBytes(new File(staged, "ready"), new byte[0]);
        status.setText(es ? "Listo. El juego se reinicia y carga la partida." : "Done. The game restarts and loads the save.");
        list.removeAllViews();
        // the game may hold the old save in memory and write it at any moment: it is replaced before it starts again
        Intent intent = new Intent(this, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        startActivity(intent);
    }

    private void say(String text, boolean bad) {
        status.setTextColor(bad ? ALERT : GOLD);
        status.setText(text);
    }

    private boolean hasSave() {
        for (String t : new String[] {"00126300", "00126100"})
            if (new File(saveDir(this, t), "system").exists()) return true;
        return false;
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        exportButton.setEnabled(false);
        say(es ? "Copiando..." : "Copying...", false);
        new Thread(() -> {
            String folder = null, error = null;
            try {
                folder = doExport(tree);
            } catch (IOException | RuntimeException e) {
                android.util.Log.e("MH4U", "save export: " + e);
                error = e.getMessage();
            }
            final String done = folder, why = error;
            runOnUiThread(() -> {
                exportButton.setEnabled(true);
                if (done != null)
                    say(es ? "Listo: carpeta \"" + done + "\". Copia su carpeta sdmc sobre la de Azahar o Citra."
                           : "Done: folder \"" + done + "\". Copy its sdmc folder over Azahar's or Citra's.", false);
                else
                    say((es ? "No se pudo copiar: " : "Could not copy it: ") + why, true);
            });
        }).start();
    }

    /** A new folder in the one chosen, with the save in Azahar's layout; the files' sizes checked. Its name. */
    private String doExport(Uri tree) throws IOException {
        ContentResolver cr = getContentResolver();
        Uri parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        String name = "MH4U save " + new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT).format(new Date());
        Uri out = DocumentsContract.createDocument(cr, parent, DocumentsContract.Document.MIME_TYPE_DIR, name);
        if (out == null) throw new IOException(es ? "esa carpeta no admite archivos nuevos" : "that folder takes no new files");
        int copied = 0;
        for (String t : new String[] {"00126300", "00126100"}) {
            File save = saveDir(this, t);
            if (!new File(save, "system").exists()) continue;
            Uri base = dirs(cr, out, "sdmc", "Nintendo 3DS", ZEROS, ZEROS);
            Uri title = dirs(cr, base, "title", "00040000", t, "data");
            copyOut(cr, save, title);  // (data/00000001/, the save's own folder name)
            File meta = new File(save.getParentFile(), "00000001.metadata");
            if (meta.exists()) copyOut(cr, meta, title);
            File ext = extDir(this, t);
            if (ext.isDirectory()) copyOut(cr, ext, dirs(cr, base, "extdata", "00000000"));
            copied++;
        }
        if (copied == 0) throw new IOException(es ? "todavía no hay una partida" : "there is no save yet");
        android.util.Log.i("MH4U", "save export: " + name);
        return name;
    }

    /** The folders `names` under `at`, made one inside the other (a folder already there is used). */
    private static Uri dirs(ContentResolver cr, Uri at, String... names) throws IOException {
        for (String n : names) at = child(cr, at, n, true);
        return at;
    }

    /** The child called `name` of the folder `dir`, made if it isn't there (a folder, or an empty file). */
    private static Uri child(ContentResolver cr, Uri dir, String name, boolean folder) throws IOException {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(dir, DocumentsContract.getDocumentId(dir));
        try (android.database.Cursor c = cr.query(kids, new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            while (c != null && c.moveToNext())
                if (name.equals(c.getString(1))) return DocumentsContract.buildDocumentUriUsingTree(dir, c.getString(0));
        }
        Uri made = DocumentsContract.createDocument(cr, dir, folder ? DocumentsContract.Document.MIME_TYPE_DIR : "application/octet-stream", name);
        if (made == null) throw new IOException(name);
        return made;
    }

    /** A file or a folder of the app's into the folder `to` (a document folder), under its own name. */
    private static void copyOut(ContentResolver cr, File from, Uri to) throws IOException {
        if (from.isDirectory()) {
            Uri dir = child(cr, to, from.getName(), true);
            File[] list = from.listFiles();
            if (list != null) for (File f : list) copyOut(cr, f, dir);
            return;
        }
        Uri file = child(cr, to, from.getName(), false);
        long copied = 0;
        try (FileInputStream in = new FileInputStream(from); OutputStream out = cr.openOutputStream(file, "wt")) {
            if (out == null) throw new IOException(from.getName());
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; copied += n) out.write(buf, 0, n);
        }
        if (copied != from.length()) throw new IOException(from.getName());
    }

    // ---- files
    static void copyTree(File from, File to) {
        if (!from.exists()) return;
        if (from.isDirectory()) {
            to.mkdirs();
            File[] list = from.listFiles();
            if (list != null) for (File f : list) copyTree(f, new File(to, f.getName()));
            return;
        }
        to.getParentFile().mkdirs();
        try (FileInputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (IOException e) {
            android.util.Log.e("MH4U", "copy " + from + ": " + e);
        }
    }

    static void deleteTree(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File k : list) deleteTree(k);
        f.delete();
    }

    static void writeBytes(File f, byte[] bytes) {
        f.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(bytes);
        } catch (IOException e) {
            android.util.Log.e("MH4U", "write " + f + ": " + e);
        }
    }
}

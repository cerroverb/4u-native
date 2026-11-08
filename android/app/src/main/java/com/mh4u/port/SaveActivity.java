package com.mh4u.port;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
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
 * Import: the source is checked and copied into save_import/, then the app restarts. GameActivity applies it before
 * the game starts (applyPending): it keeps a copy of the current save in save_backups/<date>, then replaces it.
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

    static File home(Context c) {
        File f = c.getExternalFilesDir(null);
        return f != null ? f : c.getFilesDir();
    }
    static File userDir(Context c) { return new File(home(c), "user"); }
    private static File sdmc(Context c) { return new File(userDir(c), "sdmc/Nintendo 3DS/" + ZEROS + "/" + ZEROS); }
    static File saveDir(Context c, String title) { return new File(sdmc(c), "title/00040000/" + title + "/data/00000001"); }
    static File extDir(Context c, String title) {
        return new File(sdmc(c), "extdata/00000000/" + (title.equals("00126100") ? "00001261" : "00001263"));
    }
    private static File importDir(Context c) { return new File(home(c), "save_import"); }

    // ---- applying an import before the game starts (GameActivity.onCreate)
    static void applyPending(Context c) {
        File staged = importDir(c), ready = new File(staged, "ready");
        if (!ready.exists()) return;
        String title = "00126300";
        File[] titles = staged.listFiles(File::isDirectory);
        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT).format(new Date());
        File backup = new File(home(c), "save_backups/" + stamp);
        for (String t : new String[] {"00126300", "00126100"}) {
            File src = new File(staged, t);
            if (!src.isDirectory()) continue;
            title = t;
            File save = saveDir(c, t), ext = extDir(c, t);
            copyTree(save, new File(backup, "data"));
            copyTree(ext, new File(backup, "extdata"));
            if (new File(src, "data").isDirectory()) {
                deleteTree(save);
                copyTree(new File(src, "data"), save);
                File meta = new File(save.getParentFile(), "00000001.metadata");
                if (!meta.exists()) writeBytes(meta, METADATA);
            }
            if (new File(src, "extdata").isDirectory()) {
                deleteTree(ext);
                copyTree(new File(src, "extdata"), ext);
            }
        }
        deleteTree(staged);
        android.util.Log.i("MH4U", "save import applied for " + title + ", the old save in " + backup);
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
            status.setText(es ? "Buscando partidas..." : "Looking for saves...");
            new Thread(this::findSources).start();
        }
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
                if (!dir.getAbsolutePath().startsWith(userDir(this).getAbsolutePath())) out.add(s);
                return;
            }
        }
        if (depth > 0 && new File(dir, "system").isFile() && new File(dir, "user1").isFile() && looksLikeSave(dir)) {
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

    private static boolean looksLikeSave(File dir) {
        File system = new File(dir, "system"), user = new File(dir, "user1");
        return system.length() == 512 && user.length() > 0 && user.length() < 512 * 1024;
    }

    private String describe(File dir) {
        String p = dir.getAbsolutePath();
        String where = p.contains("citra") ? "Citra" : p.contains("azahar") || p.contains("Azahar") ? "Azahar"
                     : p.contains("lime3ds") ? "Lime3DS" : p.contains("Checkpoint") ? "Checkpoint" : dir.getParentFile().getName();
        int users = 0;
        for (int i = 1; i <= 3; i++) if (new File(dir, "user" + i).exists()) users++;
        String date = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT).format(new Date(new File(dir, "system").lastModified()));
        return where + " · " + users + (es ? (users == 1 ? " personaje" : " personajes") : (users == 1 ? " hunter" : " hunters")) + " · " + date;
    }

    private void show(List<Source> found) {
        status.setText(found.isEmpty()
                ? (es ? "No se encontró ninguna partida de MH4U. Copia la carpeta de la partida (system, user1...) al teléfono y vuelve a abrir esto."
                      : "No MH4U save was found. Copy the save's folder (system, user1...) to the phone and open this again.")
                : (es ? "Toca la partida que quieres traer." : "Tap the save to bring in."));
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
        for (String f : SAVE_FILES) {
            File from = new File(s.data, f);
            if (from.exists()) copyTree(from, new File(data, f));
        }
        if (s.ext != null) copyTree(s.ext, new File(to, "extdata"));
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

package com.mh4u.port;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Settings -> Cheats / Mods (app_native/content.cpp, through GameActivity.showCheatImport / showModImport /
 * showModFolderImport): the phone's file picker, as DriverActivity's, then a plate over the game while what was picked is
 * copied into the user folder, where the game reads it:
 *   - cheats: a .txt of Gateway codes (Azahar's cheats format) to user/cheats/import.txt, which the settings merge into
 *     the game's cheats file (user/cheats/0004000000126300.txt), every cheat it brings off;
 *   - a mod: its .zip, unpacked, or its folder, copied, to user/load/mods/<name>/ (romfs/ and/or exefs/ in it). The folder
 *     with those parts is found up to three levels down (MyMod/0004000000126300/romfs: the 3DS emulators' layout); of the
 *     title folders 0004000000126300 is taken before the update's 0004000E00126300, which mods carry as a copy. A mod that
 *     changes the game's code starts off (load_order.txt): the code library must be made with it first.
 * The outcome goes to import_result.txt beside it, which the settings read when the game comes back:
 *   ok / <name> [/ 1 for a mod that changes the code]      or      error / <why> / <the system's words>
 */
public class ImportActivity extends Activity {
    static final String KIND = "kind";
    static final int CHEATS = 0, MOD_ZIP = 1, MOD_FOLDER = 2;
    private static final String TAG = "MH4U";
    private static final int PICK = 1;
    private static final long MAX_CHEATS = 4L << 20;
    private static final int WINE = Color.rgb(38, 18, 14), CREAM = Color.rgb(240, 228, 200), GOLD = Color.rgb(226, 182, 92);
    private static final int EDGE = Color.rgb(196, 150, 70);
    private static final String TITLE = "0004000000126300", UPDATE = "0004000E00126300";

    /** A reason the settings put in words (content.h ContentImport::error). */
    private static final class Refused extends IOException {
        final String code;
        Refused(String code, String detail) {
            super(detail);
            this.code = code;
        }
    }

    private int kind;
    private boolean es, busy;

    private File home() { return AppHome.home(this); }

    /** The user folder the game runs with: launch.txt's user=, else files/user. */
    private File userDir() {
        File launch = new File(home(), "launch.txt");
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(launch), StandardCharsets.UTF_8))) {
            for (String l; (l = r.readLine()) != null; ) {
                if (l.startsWith("user=") && l.length() > 5) return new File(l.substring(5).trim());
            }
        } catch (IOException ignored) {
        }
        return new File(home(), "user");
    }

    private File modsDir() { return new File(userDir(), "load/mods"); }
    private File target() { return kind == CHEATS ? new File(userDir(), "cheats") : modsDir(); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        es = "es".equals(Locale.getDefault().getLanguage());
        kind = getIntent().getIntExtra(KIND, CHEATS);
        if (state != null) return;  // (the picker is already up)
        Intent pick;
        if (kind == MOD_FOLDER) {
            pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        } else {
            pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            pick.addCategory(Intent.CATEGORY_OPENABLE);
            pick.setType("*/*");  // (a .txt or a .zip can come with any type the phone gives it)
        }
        try {
            startActivityForResult(pick, PICK);
        } catch (ActivityNotFoundException e) {
            result("error\nunreadable\n" + oneLine(e.getMessage()) + "\n");
            finish();
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!busy) super.onBackPressed();  // (a copy under way finishes first)
    }

    @Override
    protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        if (request != PICK || code != RESULT_OK || data == null || data.getData() == null) {
            finish();  // (nothing chosen: nothing to say)
            return;
        }
        final Uri picked = data.getData();
        busy = true;
        showPlate();
        new Thread(() -> {
            String outcome;
            try {
                outcome = "ok\n" + (kind == CHEATS ? importCheats(picked) : importMod(picked)) + "\n";
            } catch (Refused e) {
                outcome = "error\n" + e.code + "\n" + oneLine(e.getMessage()) + "\n";
            } catch (Exception e) {
                Log.e(TAG, "import: " + e);
                outcome = "error\nunreadable\n" + oneLine(e.getMessage()) + "\n";
            }
            result(outcome);
            runOnUiThread(() -> {
                busy = false;
                finish();
            });
        }, "import").start();
    }

    // ---- the plate while it copies
    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private void showPlate() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(28), dp(16), dp(28), dp(18));
        GradientDrawable plate = new GradientDrawable();
        plate.setColor(WINE);
        plate.setStroke(dp(2), EDGE);
        plate.setCornerRadius(dp(6));
        box.setBackground(plate);
        TextView title = new TextView(this);
        title.setText(kind == CHEATS ? (es ? "TRUCOS" : "CHEATS") : "MODS");
        title.setTextColor(GOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 19);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        box.addView(title);
        View rule = new View(this);
        rule.setBackgroundColor(EDGE);
        LinearLayout.LayoutParams ruleSize = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        ruleSize.setMargins(0, dp(10), 0, dp(12));
        box.addView(rule, ruleSize);
        TextView text = new TextView(this);
        text.setText(kind == CHEATS ? (es ? "Copiando los trucos…" : "Copying the cheats…")
                                    : (es ? "Copiando el mod…" : "Copying the mod…"));
        text.setTextColor(CREAM);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        text.setGravity(Gravity.CENTER);
        box.addView(text);
        FrameLayout page = new FrameLayout(this);
        page.setBackgroundColor(Color.argb(140, 0, 0, 0));  // (the game under it dimmed, as under the settings)
        FrameLayout.LayoutParams at = new FrameLayout.LayoutParams(dp(340), FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        page.addView(box, at);
        setContentView(page);
    }

    // ---- the outcome, for the settings
    private void result(String text) {
        File dir = target();
        dir.mkdirs();
        File tmp = new File(dir, "import_result.txt.tmp"), to = new File(dir, "import_result.txt");
        try (FileWriter w = new FileWriter(tmp)) {
            w.write(text);
        } catch (IOException e) {
            Log.e(TAG, "import: " + e);
            return;
        }
        if (!tmp.renameTo(to)) Log.e(TAG, "import: import_result.txt not written");
    }

    private static String oneLine(String s) {
        return s == null ? "" : s.replace('\n', ' ').replace('\r', ' ').trim();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
        } catch (Exception ignored) {
        }
        String last = uri.getLastPathSegment();
        if (last == null) return "";
        int cut = Math.max(last.lastIndexOf('/'), last.lastIndexOf(':'));
        return cut >= 0 ? last.substring(cut + 1) : last;
    }

    private static void delete(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) delete(k);
        f.delete();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

    // ---- cheats
    private String importCheats(Uri uri) throws IOException {
        File dir = target();
        if (!dir.isDirectory() && !dir.mkdirs()) throw new Refused("unwritable", dir.getPath());
        File to = new File(dir, "import.txt"), tmp = new File(dir, "import.txt.part");
        long total = 0;
        boolean section = false;
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new Refused("unreadable", "");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) {
                total += n;
                if (total > MAX_CHEATS) throw new Refused("too_large", total + " bytes");
                out.write(buf, 0, n);
            }
        } catch (IOException e) {
            tmp.delete();
            throw e;
        }
        // (a quick look: a cheats file has [Name] lines; the settings read it properly)
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(tmp), StandardCharsets.UTF_8))) {
            for (String l; (l = r.readLine()) != null && !section; ) {
                l = l.trim();
                section = l.length() > 2 && l.startsWith("[") && l.endsWith("]");
            }
        }
        if (!section) {
            tmp.delete();
            throw new Refused("no_cheats", "");
        }
        to.delete();
        if (!tmp.renameTo(to)) throw new Refused("unwritable", to.getPath());
        return oneLine(displayName(uri));
    }

    // ---- mods
    private static boolean isTitle(String n) { return n.matches("(?i)[0-9a-f]{16}"); }
    private static boolean isOurs(String n) { return n.matches("(?i)0004000[0e]00126[13]00"); }

    /** A mod's own folder: romfs/, romfs_ext/ or exefs/ in it, or a code patch of its own. */
    private static boolean isModRoot(File d) {
        for (String part : new String[] {"romfs", "romfs_ext", "exefs"}) if (new File(d, part).isDirectory()) return true;
        for (String file : new String[] {"code.ips", "code.bps", "code.bin"}) if (new File(d, file).isFile()) return true;
        return false;
    }

    private static boolean changesCode(File d) {
        for (String file : new String[] {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"})
            if (new File(d, file).isFile()) return true;
        return false;
    }

    /** The folder with the mod's parts, up to three levels down; the base game's title folder before the update's. */
    private static File findRoot(File d, int depth) {
        if (isModRoot(d)) return d;
        if (depth >= 3) return null;
        File[] kids = d.listFiles(File::isDirectory);
        if (kids == null) return null;
        Arrays.sort(kids, (a, b) -> rank(a.getName()) != rank(b.getName()) ? rank(a.getName()) - rank(b.getName())
                                                                             : a.getName().compareToIgnoreCase(b.getName()));
        for (File k : kids) {
            File r = findRoot(k, depth + 1);
            if (r != null) return r;
        }
        return null;
    }

    private static int rank(String name) {
        return name.equalsIgnoreCase(TITLE) ? 0 : name.equalsIgnoreCase(UPDATE) ? 1 : isTitle(name) ? 3 : 2;
    }

    private static String safeName(String s) {
        String n = s.replaceAll("[^A-Za-z0-9 ._()\\[\\]+-]+", "_").trim();
        while (n.startsWith(".") || n.startsWith("_")) n = n.substring(1);
        return n.length() > 60 ? n.substring(0, 60).trim() : n;
    }

    /** The mods the app copies from the game folder (GameFolderActivity.syncMods): their names stay theirs. */
    private Set<String> gameFolderMods() {
        Set<String> out = new HashSet<>();
        File list = new File(modsDir(), ".from_game_folder");
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(list), StandardCharsets.UTF_8))) {
            for (String l; (l = r.readLine()) != null; ) {
                int tab = l.indexOf('\t');
                String name = (tab >= 0 ? l.substring(0, tab) : l).trim();
                if (!name.isEmpty()) out.add(name);
            }
        } catch (IOException ignored) {
        }
        return out;
    }

    private String importMod(Uri uri) throws IOException {
        File mods = modsDir();
        if (!mods.isDirectory() && !mods.mkdirs()) throw new Refused("unwritable", mods.getPath());
        File tmp = new File(mods, ".new");
        delete(tmp);
        if (!tmp.mkdirs()) throw new Refused("unwritable", tmp.getPath());
        String shown;
        try {
            if (kind == MOD_ZIP) {
                shown = displayName(uri).replaceAll("(?i)\\.zip$", "");
                unzip(uri, tmp);
            } else {
                Uri tree = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));
                shown = displayName(tree);
                copyTree(uri, DocumentsContract.getTreeDocumentId(uri), tmp, 0);
            }
            File root = findRoot(tmp, 0);
            if (root == null) throw new Refused("no_parts", "");
            // a title folder of another game (or another region's)
            for (File p = root; p != null && !p.equals(tmp); p = p.getParentFile())
                if (isTitle(p.getName()) && !isOurs(p.getName())) throw new Refused("not_this_game", p.getName());
            // its name: the .zip's or the folder's; a title folder or a part's name says nothing of the mod
            String name = safeName(shown);
            if (name.isEmpty() || isTitle(name) || name.equalsIgnoreCase("romfs") || name.equalsIgnoreCase("exefs")
                    || name.equalsIgnoreCase("mods")) {
                String own = root.equals(tmp) ? "" : safeName(root.getName());
                name = own.isEmpty() || isTitle(own) ? "Mod" : own;
            }
            Set<String> fromGame = gameFolderMods();
            String base = name;
            for (int i = 2; fromGame.contains(name) || name.equalsIgnoreCase(TITLE); i++) name = base + " (" + i + ")";
            boolean code = changesCode(root);
            File to = new File(mods, name);
            delete(to);  // (the same mod again: the new copy replaces it, its place and state kept)
            if (!root.renameTo(to)) throw new Refused("unwritable", to.getPath());
            if (code) keepOff(name);
            Log.i(TAG, "mod import: " + name + (code ? " (it changes the code)" : ""));
            return name + "\n" + (code ? "1" : "0");
        } finally {
            delete(tmp);
        }
    }

    /** A mod that changes the code starts off: load_order.txt gets "# off: <name>" unless it lists the name already. */
    private void keepOff(String name) throws IOException {
        File list = new File(modsDir(), "load_order.txt");
        List<String> lines = new ArrayList<>();
        if (list.isFile()) {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(list), StandardCharsets.UTF_8))) {
                for (String l; (l = r.readLine()) != null; ) lines.add(l);
            }
        }
        for (String l : lines) {
            String t = l.trim();
            if (t.equals(name) || t.equals("# off: " + name)) return;
        }
        if (lines.isEmpty())
            lines.add("# Written by the app: the mods in load order, one per line; \"# off: name\" keeps one that is off.");
        lines.add("# off: " + name);
        try (OutputStream o = new FileOutputStream(list)) {
            o.write((String.join("\n", lines) + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    private void unzip(Uri uri, File to) throws IOException {
        String canon = to.getCanonicalPath() + File.separator;
        boolean any = false;
        try (InputStream in = getContentResolver().openInputStream(uri); ZipInputStream z = new ZipInputStream(in)) {
            for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                File out = new File(to, e.getName());
                if (!out.getCanonicalPath().startsWith(canon)) continue;  // (nothing outside the folder)
                any = true;
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(out)) {
                    copy(z, o);
                }
            }
        } catch (IllegalArgumentException e) {  // (a name the zip can't decode)
            throw new Refused("unreadable", e.getMessage());
        }
        if (!any) throw new Refused("not_zip", "");
    }

    /** A folder of the picker's tree, copied (DocumentsContract: no file paths needed). */
    private void copyTree(Uri tree, String docId, File to, int depth) throws IOException {
        if (depth > 12) return;
        if (!to.isDirectory() && !to.mkdirs()) throw new Refused("unwritable", to.getPath());
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                         DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) throw new Refused("unreadable", "");
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.isEmpty() || name.startsWith(".") || name.contains("/")) continue;
                File out = new File(to, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyTree(tree, id, out, depth + 1);
                } else {
                    try (InputStream in = getContentResolver().openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                         OutputStream o = new FileOutputStream(out)) {
                        if (in == null) throw new Refused("unreadable", name);
                        copy(in, o);
                    }
                }
            }
        }
    }
}

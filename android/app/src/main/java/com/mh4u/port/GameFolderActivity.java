package com.mh4u.port;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The app's entry: when the game's files are at hand it starts the game; otherwise it shows what is needed and lets
 * the player choose the folder they are in. Nothing big is copied: the game and its update are read in place (the
 * runtime's source.txt), only the small files go into the app's folder (Dump.prepare). The folder's path stays in
 * game_folder.txt. Settings -> Game folder opens this screen with EXTRA_CHANGE.
 *
 * The folder holds: the game (.3ds, .cci or .cxi), the 1.3.0 update (.cia), both decrypted, the code library made on a
 * PC (libmh4u_code_native.so) and, if wanted, mods/ (the single-screen mod: mods/0004000000126300/...).
 */
public class GameFolderActivity extends Activity {
    static final String EXTRA_CHANGE = "change";
    static final String LIBRARY = "libmh4u_code_native.so";
    static final String ID = "libmh4u_code_native.id";  // (beside it: which version of the tools made it)
    private static final int PICK = 1, READ = 2, BUILD = 3;
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int ALERT = Color.rgb(255, 150, 120);

    private boolean es, change, waitingAccess;
    private TextView status;
    private Button pick, buildHere;
    private File buildFolder;  // (a folder with the game and its update but no usable library: BuilderActivity can make it)
    private long builtSeen;  // (the build whose library was looked at already: BuilderService.Job.started)
    private Dump dump;

    private File home() { return AppHome.home(this); }
    private File savedFile() { return new File(home(), "game_folder.txt"); }
    private File rejectedFile() { return new File(home(), "game_folder_bad.txt"); }
    private File gameDir() { return new File(getFilesDir(), "game"); }
    private File userDir() { return new File(home(), "user"); }

    private File saved() {
        try (FileInputStream in = new FileInputStream(savedFile())) {
            byte[] b = new byte[4096];
            int n = in.read(b);
            String path = n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
            return path.isEmpty() ? null : new File(path);
        } catch (Exception e) {
            return null;
        }
    }

    /** The folder's three files: [game, update, library], each null when missing. */
    private File[] contents(File dir) {
        File[] out = new File[3];
        File[] files = dir == null ? null : dir.listFiles(File::isFile);
        if (files == null) return out;
        for (File f : files) {
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.equals(LIBRARY) || (n.endsWith("_native.so") && out[2] == null)) out[2] = f;
            else if (n.endsWith(".3ds") || n.endsWith(".cci") || n.endsWith(".cxi") || n.endsWith(".cia")) {
                String kind = dump.describe(f);
                if ("game".equals(kind) && out[0] == null) out[0] = f;
                else if ("update".equals(kind) && out[1] == null) out[1] = f;
            }
        }
        return out;
    }

    /**
     * Which code library this version of the app runs with: the id mh4n's tools/build_game.py gives the libraries it makes
     * (scripts/build_app.sh puts it in the APK as code_library_id.txt). "" in an APK built without it: no check.
     */
    private String wantedId() { return wantedId(this); }

    static String wantedId(android.content.Context c) {
        try (java.io.InputStream in = c.getAssets().open("code_library_id.txt")) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (Exception e) {
            return "";
        }
    }

    /** The id build_game.py wrote beside a library (libmh4u_code_native.id), or "" when there is none. */
    private static String idBeside(File library) {
        String name = library.getName();
        int dot = name.lastIndexOf('.');
        File id = new File(library.getParentFile(), (dot > 0 ? name.substring(0, dot) : name) + ".id");
        try (FileInputStream in = new FileInputStream(id)) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (Exception e) {
            return "";
        }
    }

    /**
     * The id the library carries inside itself (mh4n tools/game_version.py: the exported rc_library_id, the same as the .id
     * file's), read from its ELF symbol table without loading it; "" for a library made before the tools wrote it.
     */
    static String idInside(File library) {
        try (RandomAccessFile f = new RandomAccessFile(library, "r")) {
            byte[] h = new byte[64];
            f.readFully(h);
            if (h[0] != 0x7F || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' || h[4] != 2 || h[5] != 1) return "";
            long shoff = le(h, 0x28, 8);
            int shentsize = (int) le(h, 0x3A, 2), shnum = (int) le(h, 0x3C, 2);
            if (shoff <= 0 || shentsize < 0x40 || shnum <= 0 || shnum > 4096) return "";
            byte[] sh = new byte[shentsize * shnum];
            f.seek(shoff);
            f.readFully(sh);
            for (int i = 0; i < shnum; i++) {
                int at = i * shentsize;
                if (le(sh, at + 4, 4) != 11) continue;  // SHT_DYNSYM
                long symOff = le(sh, at + 0x18, 8), symSize = le(sh, at + 0x20, 8);
                int link = (int) le(sh, at + 0x28, 4);
                if (link >= shnum || symSize > (8 << 20)) return "";
                int ls = link * shentsize;
                long strOff = le(sh, ls + 0x18, 8), strSize = le(sh, ls + 0x20, 8);
                if (strSize > (8 << 20)) return "";
                byte[] syms = new byte[(int) symSize], strs = new byte[(int) strSize];
                f.seek(symOff);
                f.readFully(syms);
                f.seek(strOff);
                f.readFully(strs);
                byte[] want = "rc_library_id".getBytes(StandardCharsets.US_ASCII);
                for (int k = 0; k + 24 <= syms.length; k += 24) {
                    int name = (int) le(syms, k, 4), shndx = (int) le(syms, k + 6, 2);
                    if (name <= 0 || name + want.length >= strs.length || strs[name + want.length] != 0) continue;
                    boolean same = true;
                    for (int c = 0; c < want.length && same; c++) same = strs[name + c] == want[c];
                    if (!same || shndx <= 0 || shndx >= shnum) continue;
                    int ss = shndx * shentsize;
                    long value = le(syms, k + 8, 8), secAddr = le(sh, ss + 0x10, 8), secOff = le(sh, ss + 0x18, 8);
                    byte[] id = new byte[64];
                    f.seek(secOff + (value - secAddr));
                    int n = f.read(id);
                    int len = 0;
                    while (len < n && id[len] != 0) len++;
                    return new String(id, 0, len, StandardCharsets.US_ASCII).trim();
                }
                return "";
            }
        } catch (Exception e) {
            android.util.Log.w("MH4U", "code library id: " + e);
        }
        return "";
    }

    private static long le(byte[] b, int at, int n) {
        long v = 0;
        for (int i = n - 1; i >= 0; i--) v = v << 8 | (b[at + i] & 0xFFL);
        return v;
    }

    /**
     * A library made with another version of the tools (another translator, its runtime or its tables): it would crash the
     * game, so it is found here, as MHGU's gamelib_id. Why, or null when it fits (or the APK has no id to compare with).
     * The id inside the library wins over the .id file beside it, and the two must agree: a .so and an .id copied from
     * two different builds are found here, before the game starts.
     */
    private String libraryVersionProblem(File library) {
        String want = wantedId();
        if (want.isEmpty() || library == null) return null;
        String beside = idBeside(library), inside = idInside(library);
        if (!inside.isEmpty() && !beside.isEmpty() && !inside.equals(beside)) {
            android.util.Log.w("MH4U", "code library " + library + ": id '" + inside + "' inside, '" + beside + "' in its .id file");
            return es ? "La " + LIBRARY + " y el " + ID + " que tiene al lado son de armados distintos (copiados de dos lugares). "
                        + "Copia juntos los dos archivos de un mismo armado, hechos con la herramienta de esta versión, y no "
                        + "uses una librería que hizo otra persona."
                      : "The " + LIBRARY + " and the " + ID + " beside it come from different builds (copied from two places). "
                        + "Copy both files of the same build together, made with this version's tool, and don't use a "
                        + "library someone else made.";
        }
        String have = inside.isEmpty() ? beside : inside;
        if (want.equals(have)) return null;
        android.util.Log.w("MH4U", "code library " + library + ": id '" + have + "', this app wants '" + want + "'");
        return have.isEmpty()
                ? (es ? "Esta " + LIBRARY + " no trae al lado su " + ID + ": se hizo con una versión anterior de la herramienta. "
                        + "Créala de nuevo con la herramienta de esta versión y copia las dos a la carpeta."
                      : "This " + LIBRARY + " has no " + ID + " beside it: it was made with an older version of the tool. "
                        + "Make it again with this version's tool and copy both files to the folder.")
                : (es ? "Esta " + LIBRARY + " es de otra versión de la app. Créala de nuevo con la herramienta de esta versión "
                        + "y copia las dos (" + LIBRARY + " y " + ID + ") a la carpeta."
                      : "This " + LIBRARY + " is from another version of the app. Make it again with this version's tool and "
                        + "copy both files (" + LIBRARY + " and " + ID + ") to the folder.");
    }

    private boolean ready(File dir) {
        if (dir == null || !dir.isDirectory()) return false;
        File source = new File(gameDir(), "source.txt");
        if (!source.exists() || !new File(gameDir(), "code.bin").exists()) return false;
        // the game folder was made from files that are still where they were
        try {
            String text = new String(java.nio.file.Files.readAllBytes(source.toPath()), StandardCharsets.UTF_8);
            for (String line : text.split("\n")) {
                int eq = line.indexOf('=');
                if (eq > 0 && !new File(line.substring(eq + 1).trim()).canRead()) return false;
            }
        } catch (Exception e) {
            return false;
        }
        return new File(dir, LIBRARY).canRead() || contents(dir)[2] != null;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Diag.install(this);  // (1.0.1: the versions seen, a Java crash's stack for Share log)
        declareGame();
        es = "es".equals(Locale.getDefault().getLanguage());
        dump = new Dump(es);
        change = getIntent().getBooleanExtra(EXTRA_CHANGE, false);
        if (change) {
            look();
            return;
        }
        // (a folder of the app's that another app made: given back, or the move to the private one. Its copies can take a
        // while, so on a thread of its own, over the plain wine screen the game starts from anyway)
        android.view.View plain = new android.view.View(this);
        plain.setBackgroundColor(WINE);
        setContentView(plain);
        // (the files' check and the mods' copy from the game folder too: a big mod there could hold the screen for long)
        new Thread(() -> {
            AppHome.ensure(this);
            final boolean right = startReady();
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                Notices.checkInBackground(this);
                if (right) proceed();
                else look();
            });
        }, "app-home").start();
    }

    /**
     * The game's files are right (the folder not being changed, nothing rejected): the game folder's mods copied in and
     * launch.txt written, for the game to start. False when this screen is needed (look). Files only: off the UI thread.
     */
    private boolean startReady() {
        File chosen = saved();
        if (change || rejectedFile().exists() || !hasAccess() || !ready(chosen)) return false;
        File library = contents(chosen)[2];
        if (libraryVersionProblem(library) != null) return false;
        syncMods(chosen);
        writeLaunch(chosen, library);
        return true;
    }

    /** The files of the game, once the app's folder is known and startReady said they are not right: this screen. */
    private void look() {
        boolean rejected = rejectedFile().exists();
        File chosen = saved();
        build();
        if ((rejected || (!change && hasAccess() && ready(chosen))) && chosen != null) {  // (also a library of another version: verify says so)
            verify(chosen);
        } else if (chosen != null && !change && hasAccess() && holdsGame(chosen)) {
            // (the game and its update are still there: what is missing is the library, or Dump's copy of the update;
            // verify says which, and offers to build the library on the phone)
            verify(chosen);
        } else if (chosen != null && !change) {
            status.setTextColor(ALERT);
            status.setText(hasAccess()
                    ? (es ? "Ya no se encuentran los archivos en " + chosen + ". Elige dónde están ahora." : "The files are no longer in " + chosen + ". Choose where they are now.")
                    : (es ? "El juego perdió el permiso para leer la carpeta. Vuelve a darlo." : "The game lost its permission to read the folder. Please give it again."));
        }
    }

    /** Samsung ignores the manifest's category: it counts the one set by whoever installed the app (Android lets only them). */
    private void declareGame() {
        try {
            getPackageManager().setApplicationCategoryHint(getPackageName(), ApplicationInfo.CATEGORY_GAME);
        } catch (RuntimeException e) {
            android.util.Log.i("MH4U", "category hint: " + e.getMessage());
        }
    }

    private void startGame() {
        GameActivity.entered = true;  // (the game starts in this process; a start that skipped this comes back here)
        startActivity(new Intent(this, GameActivity.class));
        overridePendingTransition(0, 0);
        finish();
    }

    /**
     * The files are right: what is left before the game (each in the game's style, over a plain wine screen): the app's
     * folder moved to the private one (AppHome), the save folder's start gate (SaveFolderActivity.gate), the
     * unofficial-build and update notices (Notices), then the game. As MHGU for Android's.
     */
    private void proceed() {
        if (status == null) {
            android.view.View plain = new android.view.View(this);
            plain.setBackgroundColor(WINE);
            setContentView(plain);
        }
        String moved = AppHome.movedNotice(this);
        if (moved != null) {
            Look.notice(this, es ? "EL JUEGO USA SU CARPETA PRIVADA" : "THE GAME USES ITS PRIVATE FOLDER",
                    es ? "Otra app (un administrador de archivos, por ejemplo con Shizuku) creó la carpeta del juego, " + moved
                         + ", y Android no deja que el juego escriba en ella. Así que el juego guarda sus ajustes, su registro y "
                         + "tu partida en su carpeta privada, y copió ahí sus propios archivos; lo demás que haya en esa carpeta "
                         + "queda donde está. Para llevar tu partida a otro lado usa Ajustes, Sistema, Tu partida: Exportar partida o Carpeta de partidas."
                       : "Another app (a file manager, for example through Shizuku) made the game's folder, " + moved + ", and "
                         + "Android won't let the game write in it. So the game keeps its settings, its log and your save in its "
                         + "private folder, and copied its own files there; anything else in that folder stays where it is. To "
                         + "take your save elsewhere, use Settings, System, Your game: Export save or Save folder.",
                    new Look.Choice(es ? "Continuar" : "Continue", true, this::proceed));
            return;
        }
        SaveFolderActivity.gate(this, () -> Notices.beforeGame(this, this::startGame));
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

    /** The folder as it must look: folders in gold, what goes in them in cream. */
    private CharSequence tree() {
        String[] lines = {
            es ? "Tu carpeta" : "Your folder",
            es ? "├─ el juego (.3ds o .cci)" : "├─ the game (.3ds or .cci)",
            es ? "├─ la actualización 1.3.0 (.cia)" : "├─ the 1.3.0 update (.cia)",
            "├─ " + LIBRARY + "  (+ .id)",
            es ? "└─ mods (si quieres)" : "└─ mods (if you like)",
            "     0004000000126300",
        };
        SpannableStringBuilder b = new SpannableStringBuilder();
        for (int i = 0; i < lines.length; i++) {
            int at = b.length();
            b.append(lines[i]).append(i + 1 < lines.length ? "\n" : "");
            boolean folder = i == 0 || i == 4;
            b.setSpan(new ForegroundColorSpan(folder ? GOLD : CREAM), at, b.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return b;
    }

    private void build() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        int pad = dp(14);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, pad, pad * 2, pad);

        TextView title = text(es ? "ARCHIVOS DEL JUEGO" : "GAME FILES", 20, GOLD, true);
        title.setGravity(Gravity.CENTER);
        page.addView(title);
        TextView intro = text(es ? "Elige la carpeta donde tienes tu copia del juego. Se lee desde ahí: no se copia nada."
                                 : "Choose the folder that holds your copy of the game. It is read from there: nothing is copied.",
                14, CREAM, false);
        intro.setGravity(Gravity.CENTER);
        intro.setPadding(0, dp(4), 0, dp(10));
        page.addView(intro);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);

        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setBackground(plate(PANEL, 6));
        left.setPadding(pad, dp(10), pad, dp(10));
        left.addView(text(es ? "LA CARPETA DEBE TENER ESTO" : "THE FOLDER MUST LOOK LIKE THIS", 13, GOLD, true));
        TextView tree = new TextView(this);
        tree.setText(tree());
        tree.setTypeface(Typeface.MONOSPACE);
        tree.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tree.setPadding(0, dp(6), 0, 0);
        left.addView(tree);
        columns.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        right.setPadding(dp(18), 0, 0, 0);
        right.addView(text(es ? "QUÉ TIENE QUE SER CADA UNO" : "WHAT EACH ONE MUST BE", 13, GOLD, true));
        TextView notes = text(es
                ? "• El juego y la actualización 1.3.0, de América, descifrados, de tu propia copia. Cualquier nombre.\n"
                  + "• " + LIBRARY + ": se crea una sola vez, en este teléfono (Crear en este teléfono) o en una PC con la herramienta de la guía.\n"
                  + "• mods: opcional, por ejemplo el mod de una sola pantalla.\n"
                  + "• Todo junto en la misma carpeta, unos 4 GB. Elige esa carpeta."
                : "• The game and its 1.3.0 update, USA, decrypted, from your own copy. Any names.\n"
                  + "• " + LIBRARY + ": made once, on this phone (Build on this phone) or on a PC with the guide's tool.\n"
                  + "• mods: optional, for example the single-screen mod.\n"
                  + "• All in the same folder, about 4 GB. Choose that folder.", 14, CREAM, false);
        notes.setPadding(0, dp(6), 0, 0);
        notes.setLineSpacing(dp(2), 1);
        right.addView(notes);
        columns.addView(right, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        page.addView(columns);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        status = text("", 14, GOLD, false);
        status.setGravity(Gravity.CENTER);
        status.setPadding(pad * 2, dp(6), pad * 2, dp(6));
        whole.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        pick = button("", true);
        pick.setOnClickListener(v -> choose());
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, dp(46));
        size.setMargins(dp(8), 0, dp(8), 0);
        row.addView(pick, size);
        buildHere = button(es ? "Crear en este teléfono" : "Build on this phone", true);
        buildHere.setOnClickListener(v -> {
            if (buildFolder != null)
                startActivityForResult(new Intent(this, BuilderActivity.class).putExtra(BuilderActivity.EXTRA_FOLDER,
                        buildFolder.getAbsolutePath()), BUILD);
        });
        buildHere.setVisibility(android.view.View.GONE);
        row.addView(buildHere, new LinearLayout.LayoutParams(size));
        Button leave = button(change ? (es ? "Cancelar" : "Cancel") : (es ? "Salir" : "Exit"), false);
        leave.setOnClickListener(v -> finish());
        LinearLayout.LayoutParams size2 = new LinearLayout.LayoutParams(dp(120), dp(46));
        size2.setMargins(dp(8), 0, dp(8), 0);
        row.addView(leave, size2);
        row.setPadding(0, 0, 0, dp(10));
        whole.addView(row);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {  // (the buttons stay above the system's bar)
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
        pick.requestFocus();  // (a console's pad: A presses it)
        refreshButton();
    }

    private void refreshButton() {
        if (pick != null) pick.setText(hasAccess() ? (es ? "Elegir carpeta" : "Choose folder")
                                                   : (es ? "Dar permiso y elegir carpeta" : "Give permission and choose folder"));
    }

    // ---- reading any folder: the plain file paths the game uses need this permission
    private boolean hasAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void choose() {
        if (hasAccess()) { openBrowser(null); return; }
        if (Build.VERSION.SDK_INT < 30) {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE}, READ);
            return;
        }
        waitingAccess = true;
        status.setTextColor(GOLD);
        status.setText(es ? "Activa el acceso a todos los archivos y vuelve. El juego solo lee esa carpeta: no cambia nada."
                          : "Turn on access to all files and come back. The game only reads that folder: it changes nothing.");
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        } catch (ActivityNotFoundException e) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshButton();
        // (the library was built while this screen waited, the builder reached from its notification: look again)
        BuilderService.Job j = BuilderService.job;
        if (buildFolder != null && j.status.equals("done") && j.started != builtSeen && buildFolder.getAbsolutePath().equals(j.folder)) {
            builtSeen = j.started;
            verify(buildFolder);
        }
        if (waitingAccess && hasAccess()) {
            waitingAccess = false;
            status.setText("");
            openBrowser(null);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        if (code == READ && hasAccess()) openBrowser(null);
        refreshButton();
    }

    /** Android's own folder picker: the browser's second way. */
    private void systemPicker() {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK);
        } catch (ActivityNotFoundException e) {
            status.setTextColor(ALERT);
            status.setText(es ? "Este dispositivo no tiene selector de carpetas." : "This device has no folder picker.");
        }
    }

    private void openBrowser(File at) {
        FolderBrowser.open(this, at, es ? "ELIGE LA CARPETA DEL JUEGO" : "CHOOSE THE GAME'S FOLDER",
                GameFolderActivity::holdsGame,
                (dir, game) -> game ? (es ? "✓ Aquí está el juego: el juego y su actualización." : "✓ The game is here: the game and its update.")
                                    : (es ? "Entra a la carpeta que tiene el juego y su actualización." : "Go into the folder that holds the game and its update."),
                this::verify, this::systemPicker, false);
    }

    /** A folder with the game in it (by its files' names: the headers are read when it is chosen). */
    private static boolean holdsGame(File d) {
        File[] files = d.listFiles(File::isFile);
        if (files == null) return false;
        boolean game = false, update = false;
        for (File f : files) {
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.endsWith(".3ds") || n.endsWith(".cci") || n.endsWith(".cxi")) game = true;
            if (n.endsWith(".cia")) update = true;
        }
        return game && update;
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code == BUILD) {  // (back from BuilderActivity: a library made, or not; the folder is looked at again)
            builtSeen = BuilderService.job.started;
            if (buildFolder != null) verify(buildFolder);
            return;
        }
        if (code != PICK || result != RESULT_OK || data == null || data.getData() == null) return;
        File dir = FolderBrowser.pathOf(data.getData());
        if (dir == null) {
            status.setTextColor(ALERT);
            status.setText(es ? "No se puede usar esa ubicación. Busca la carpeta en la memoria del teléfono o en la tarjeta SD."
                              : "That location can't be used. Find the folder in the phone's storage or on the SD card.");
            return;
        }
        verify(dir);
    }

    // ---- is it the right thing? (a background thread)
    private void verify(File picked) {
        pick.setEnabled(false);
        status.setTextColor(GOLD);
        status.setText(es ? "Comprobando los archivos..." : "Checking the files...");
        new Thread(() -> {
            String wrong = null;
            File[] c = contents(picked);
            if (c[0] == null && c[1] == null)
                wrong = es ? "En " + picked.getName() + " no están el juego ni su actualización. Elige la carpeta que los tiene."
                           : "The game and its update are not in " + picked.getName() + ". Choose the folder that holds them.";
            else if (c[0] == null)
                wrong = es ? "Falta el juego (.3ds o .cci, descifrado), al lado de la actualización."
                           : "The game (.3ds or .cci, decrypted) is missing, beside the update.";
            else if (c[1] == null)
                wrong = es ? "Falta la actualización 1.3.0 (.cia, descifrada), al lado del juego."
                           : "The 1.3.0 update (.cia, decrypted) is missing, beside the game.";
            else if (c[2] == null)
                wrong = es ? "Falta " + LIBRARY + " en esa carpeta. Créalo en este teléfono (Crear en este teléfono) o en una PC con la herramienta de la guía."
                           : LIBRARY + " is missing from that folder. Make it on this phone (Build on this phone) or on a PC with the guide's tool.";
            else if (!looksLikeLibrary(c[2]))
                wrong = es ? LIBRARY + " no es una librería para Android (arm64). Créalo de nuevo en este teléfono o con la herramienta de la guía."
                           : LIBRARY + " is not an Android (arm64) library. Make it again on this phone or with the guide's tool.";
            else
                wrong = libraryVersionProblem(c[2]);
            // (the game and its update are there, the library isn't usable: it can be made here)
            final File buildable = wrong != null && c[0] != null && c[1] != null ? picked : null;
            if (wrong == null) {
                try {
                    dump.prepare(c[0], c[1], gameDir());
                    syncMods(picked);
                    writeLaunch(picked, c[2]);
                } catch (Dump.Problem p) {
                    wrong = p.getMessage();
                } catch (Exception e) {
                    wrong = String.valueOf(e);
                }
            }
            final String problem = wrong;
            runOnUiThread(() -> {
                pick.setEnabled(true);
                buildFolder = buildable;
                buildHere.setVisibility(buildable != null ? android.view.View.VISIBLE : android.view.View.GONE);
                if (buildable != null) buildHere.requestFocus();
                if (problem != null) {
                    status.setTextColor(ALERT);
                    status.setText(problem);
                } else {
                    accept(picked);
                }
            });
        }).start();
    }

    private static boolean looksLikeLibrary(File f) {
        byte[] h = new byte[20];
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            r.readFully(h);
        } catch (Exception e) {
            return false;
        }
        // an ELF file, 64-bit, for AArch64 (e_machine 183)
        return h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 2 && (h[18] & 0xFF) == 183;
    }

    /**
     * The folder's mods/ into the user folder's load/mods/, where the runtime reads them beside the mods added in the
     * settings (mh4n native/src/mods/mod_list.h: every folder there is a mod; load_order.txt orders them and turns them
     * off). Each mod of mods/ becomes one folder:
     *   mods/0004000000126300/ (or only the update's 0004000E00126300/): Azahar's layout, as load/mods/0004000000126300/
     *   mods/<name>/0004000000126300/ (or 0004000E00126300/): load/mods/<name>/
     *   mods/<name>/ with romfs/, romfs_ext/ or exefs/ in it: load/mods/<name>/ (also from a folder up to three levels
     *   down: a zip unpacked with its own folder, Luma3DS's luma/titles/0004000000126300/)
     *   mods/<name>/ with the game's own folders (data/, eng/...) and no romfs/: load/mods/<name>/romfs/ (as the PC tools'
     *   mod manager takes them)
     * A mod only 1.0's search finds (further down, the game's own folders, the EUR game's title: 0.1.9.3 left it where it
     * was) comes in off ("# off:" in load_order.txt, as ImportActivity's code mods) and named in load/mods/.found_off,
     * which the Mods page shows until its list is saved: an update alone turns on no mod. The ones 0.1.9.3 found too
     * come as they did.
     * load/mods/.from_game_folder lists them (name, a tab, the source's file count, size and latest date): a mod is copied
     * again only when it changed there, and removed when it is gone from there. The settings' order and switches stay.
     */
    private void syncMods(File dir) {
        File from = new File(dir, "mods"), to = new File(userDir(), "load/mods"), mark = new File(to, ".from_game_folder");
        // what came from there before: name -> signature (the old empty mark: everything in load/mods came from there)
        Map<String, String> before = new LinkedHashMap<>();
        if (mark.isFile()) {
            boolean any = false;
            for (String line : readLines(mark)) {
                if (line.trim().isEmpty()) continue;
                any = true;
                int tab = line.indexOf('\t');
                before.put(tab >= 0 ? line.substring(0, tab) : line.trim(), tab >= 0 ? line.substring(tab + 1) : "");
            }
            File[] old = any ? null : to.listFiles(File::isDirectory);
            if (old != null) for (File f : old) if (!f.getName().startsWith(".")) before.put(f.getName(), "");
        }
        // what is there now: name -> the folder to copy
        Map<String, File> now = new LinkedHashMap<>();
        java.util.Set<String> bare = new java.util.HashSet<>();  // (the game's own folders: copied into a romfs/)
        java.util.Set<String> deeper = new java.util.HashSet<>();  // (found only by 1.0's search: they come in off)
        File[] kids = from.isDirectory() ? from.listFiles(File::isDirectory) : null;
        if (kids != null) {
            Arrays.sort(kids);
            File title = titleIn(from);
            if (title != null) {
                now.put(modTitle(), title);
                if (!modTitle().equals(MOD_TITLE) || !title.equals(titleIn(from, MOD_TITLE))) deeper.add(modTitle());
            }
            for (File k : kids) {
                if (k.getName().matches("(?i)[0-9a-f]{16}")) continue;  // (a title folder: done above, or another game's)
                File src = titleIn(k);
                if (src == null) src = modRootIn(k, 0);
                if (src == null && (src = gameFoldersIn(k, 0)) != null) bare.add(k.getName());
                if (src == null) continue;
                now.put(k.getName(), src);
                // (0.1.9.3's two ways: the USA title's folder in it, else romfs/, romfs_ext/ or exefs/ right in it)
                File before193 = titleIn(k, MOD_TITLE);
                if (before193 == null && isModRoot(k)) before193 = k;
                if (!src.equals(before193) || bare.contains(k.getName())) deeper.add(k.getName());
            }
        }
        // gone from there: gone from here
        for (String name : before.keySet())  // (only a plain folder name of load/mods, whatever the list says)
            if (!now.containsKey(name) && !name.isEmpty() && !name.startsWith(".") && !name.contains("/"))
                deleteTree(new File(to, name));
        StringBuilder list = new StringBuilder();
        for (Map.Entry<String, File> e : now.entrySet()) {
            String name = e.getKey(), sig = signature(e.getValue());
            File dest = new File(to, name);
            if (!before.containsKey(name) && dest.exists()) {  // (a mod of that name added in the settings: it stays)
                android.util.Log.w("MH4U", "mods: " + name + " is in the game folder and in the app: the app's is kept");
                continue;
            }
            if (!before.containsKey(name) && deeper.contains(name)) {  // (off before it is there: a start cut short keeps it so)
                try {
                    to.mkdirs();
                    ImportActivity.keepOff(to, name);
                    addLine(new File(to, FOUND_OFF), name);
                    android.util.Log.i("MH4U", "mods: " + name + " found in the game folder's mods by the deeper search: added off");
                } catch (Exception x) {
                    android.util.Log.e("MH4U", "mods: " + name + " not kept off (" + x + "): left out");
                    continue;
                }
            }
            if (!sig.equals(before.get(name)) || !dest.isDirectory()) {
                File tmp = new File(to, ".sync");
                deleteTree(tmp);
                copyTree(e.getValue(), bare.contains(name) ? new File(tmp, "romfs") : tmp);
                deleteTree(dest);
                if (!tmp.renameTo(dest)) android.util.Log.e("MH4U", "mods: " + name + " not copied");
            }
            list.append(name).append('\t').append(sig).append('\n');
        }
        if (list.length() == 0) {
            mark.delete();
            return;
        }
        to.mkdirs();
        try (FileOutputStream out = new FileOutputStream(mark)) {
            out.write(list.toString().getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            android.util.Log.e("MH4U", "mods: " + e);
        }
    }

    private static final String MOD_TITLE = "0004000000126300";
    /** load/mods/.found_off: the mods syncMods added off, one name a line (the Mods page's note: app_native/content.cpp). */
    static final String FOUND_OFF = ".found_off";

    /** The line added to a list file, unless it has it already. */
    private static void addLine(File list, String line) throws java.io.IOException {
        for (String l : readLines(list)) if (l.trim().equals(line)) return;
        try (FileOutputStream out = new FileOutputStream(list, true)) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    /**
     * The game's title as its mods folder is named: the program id of the game folder's exheader.bin (USA 0004000000126300,
     * EUR 0004000000126100; an update's id counts as the game's), else the USA one.
     */
    private String modTitle() {
        try (FileInputStream in = new FileInputStream(new File(gameDir(), "exheader.bin"))) {
            byte[] e = new byte[0x208];
            if (in.read(e) == e.length) {
                long id = le(e, 0x200, 8) & ~0x0000000E00000000L;
                if ((id >>> 32) == 0x00040000L) return String.format("%016X", id);
            }
        } catch (Exception ignored) {
        }
        return MOD_TITLE;
    }

    /** The game's title folder in dir: the base game's, else the update's (Azahar's GetModId makes them one). */
    private File titleIn(File dir) { return titleIn(dir, modTitle()); }

    /** The folder of that title in dir: the base game's, else the update's. */
    private static File titleIn(File dir, String title) {
        String update = title.substring(0, 7) + "E" + title.substring(8);
        File base = null, upd = null;
        File[] kids = dir.listFiles(File::isDirectory);
        if (kids != null)
            for (File k : kids) {
                if (k.getName().equalsIgnoreCase(title)) base = k;
                else if (k.getName().equalsIgnoreCase(update)) upd = k;
            }
        return base != null ? base : upd;
    }

    private static boolean isModRoot(File d) {
        for (String part : new String[] {"romfs", "romfs_ext", "exefs"}) if (new File(d, part).isDirectory()) return true;
        return false;
    }

    /** The folder with a mod's parts in d or up to three levels below it (the game's title folder first), or null. */
    private File modRootIn(File d, int depth) {  // (not static: titleIn reads the game's own title, USA or EUR)
        if (isModRoot(d)) return d;
        if (depth >= 3) return null;
        File t = titleIn(d);
        if (t != null) return t;
        File[] kids = d.listFiles(File::isDirectory);
        if (kids == null) return null;
        Arrays.sort(kids);
        for (File k : kids) {
            if (k.getName().matches("(?i)[0-9a-f]{16}")) continue;  // (another game's title folder)
            File r = modRootIn(k, depth + 1);
            if (r != null) return r;
        }
        return null;
    }

    /** A folder in d or up to two levels below holding the game's own folders (data/, eng/...: ImportActivity's), or null. */
    private static File gameFoldersIn(File d, int depth) {
        File[] kids = d.listFiles(File::isDirectory);
        if (kids == null) return null;
        for (File k : kids)
            for (String top : new String[] {"data", "eng", "fre", "ger", "ita", "spa", "jpn"})
                if (k.getName().equalsIgnoreCase(top)) return d;
        if (depth >= 2) return null;
        Arrays.sort(kids);
        for (File k : kids) {
            File r = gameFoldersIn(k, depth + 1);
            if (r != null) return r;
        }
        return null;
    }

    /** Its files' count, total size and latest date: what changes when the mod does. */
    private static String signature(File f) {
        long[] s = new long[3];
        sign(f, s);
        return s[0] + ":" + s[1] + ":" + s[2];
    }

    private static void sign(File f, long[] s) {
        File[] list = f.listFiles();
        if (list == null) {
            s[0]++;
            s[1] += f.length();
            s[2] = Math.max(s[2], f.lastModified());
            return;
        }
        for (File k : list) sign(k, s);
    }

    private static List<String> readLines(File f) {
        List<String> out = new ArrayList<>();
        try {
            for (String l : new String(java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) out.add(l);
        } catch (Exception ignored) {
        }
        return out;
    }

    private static void copyTree(File from, File to) {
        if (from.isDirectory()) {
            to.mkdirs();
            File[] list = from.listFiles();
            if (list != null) for (File f : list) copyTree(f, new File(to, f.getName()));
            return;
        }
        try (FileInputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (Exception ignored) {
        }
    }

    private static void deleteTree(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File k : list) deleteTree(k);
        f.delete();
    }

    /** What the native side starts with (app_native/android_main.cpp). */
    private void writeLaunch(File dir, File library) {
        userDir().mkdirs();
        String text = "game=" + gameDir().getAbsolutePath() + "\ncode=" + library.getAbsolutePath() + "\nuser="
                + userDir().getAbsolutePath() + "\n";
        try (FileOutputStream out = new FileOutputStream(new File(home(), "launch.txt"))) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            android.util.Log.e("MH4U", "launch.txt: " + e);
        }
    }

    private void accept(File dir) {
        try {
            AppHome.write(savedFile(), dir.getAbsolutePath());
        } catch (Exception e) {
            // (the app's folder can't be written at all: AppHome moves to the private one, and the choice goes there)
            android.util.Log.e("MH4U", "game_folder.txt: " + e);
            AppHome.ensure(this);
            try {
                AppHome.write(savedFile(), dir.getAbsolutePath());
            } catch (Exception e2) {
                status.setTextColor(ALERT);
                status.setText(es ? "No se pudo guardar la carpeta elegida: Android no deja que el juego escriba en " + home()
                                    + ". Comparte el registro desde Ajustes."
                                  : "The folder chosen couldn't be kept: Android won't let the game write in " + home()
                                    + ". Share the log from Settings.");
                return;
            }
        }
        rejectedFile().delete();
        if (change) {
            status.setTextColor(GOLD);
            status.setText(es ? "Listo: se usará la próxima vez que abras el juego." : "Done: it will be used the next time you open the game.");
            pick.setEnabled(false);
            return;
        }
        proceed();
    }
}

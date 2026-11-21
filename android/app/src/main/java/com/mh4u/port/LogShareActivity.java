package com.mh4u.port;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Locale;

/**
 * Compartir el registro: con el juego cerrado por un error (GameActivity.crashReport), o desde Ajustes.
 * Corre en su propio proceso (":share", como LogProvider), porque el del juego muere tras un error y lo que comparte
 * tiene que seguir disponible para la app que lo reciba. Junta en un archivo los datos del teléfono, el registro de esta
 * sesión y el de la anterior: el jugador elige a quién enviarlo; la app no manda nada por su cuenta.
 * (1.0.1) Diag.bundle arma el archivo, en un hilo propio: el teléfono y la app ahora, cómo terminaron sus últimos
 * procesos, la librería de código, los ajustes, los mods, las dos sesiones anteriores, la última importación, el último
 * armado y el registro del sistema.
 */
public class LogShareActivity extends Activity {
    static final String EXTRA_CRASH = "crash";
    private static final String TAG = "MH4U";
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int MAX_LOG = 2 << 20;  // (the end of each log: a debug session's can be large)

    private boolean es;
    private Thread systemLines;  // (after a crash) addSystemLines
    private Button shareButton;
    private boolean sharing;

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(String s, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private GradientDrawable plate(int fill) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setStroke(dp(2), EDGE);
        g.setCornerRadius(dp(6));
        return g;
    }

    private Button button(String label, boolean strong) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(strong ? GOLD : CREAM);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        b.setBackground(plate(BRONZE));
        return b;
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = "es".equals(Locale.getDefault().getLanguage());
        boolean crash = getIntent().getBooleanExtra(EXTRA_CRASH, false);
        if (crash && saved == null) {  // (once: not again when the screen turns)
            systemLines = new Thread(this::addSystemLines, "mh4u-system-log");
            systemLines.start();
        }

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(16), dp(24), dp(18));
        box.setBackground(plate(WINE));
        box.addView(text(crash ? (es ? "El juego se cerró por un error" : "The game stopped because of an error")
                               : (es ? "Compartir registro" : "Share log"), 19, GOLD, true));
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                                      LinearLayout.LayoutParams.WRAP_CONTENT);
        gap.setMargins(0, dp(12), 0, 0);
        box.addView(text(es ? "El registro explica qué pasó y en qué teléfono: el modelo y su sistema, la memoria, la batería y "
                              + "la temperatura, el tipo de red (Wi-Fi o datos, nunca su nombre), los ajustes y los mods de "
                              + "la app, y las últimas sesiones. Puedes enviarlo a quien te esté ayudando: se abre la lista "
                              + "de apps y tú eliges a dónde va. No lleva tu partida, cuentas, correo ni ubicación, pero "
                              + "puede incluir la dirección de tu red local si jugaste en LAN."
                            : "The log says what happened and on which phone: its model and system, the memory, the "
                              + "battery and the heat, the network's kind (Wi-Fi or mobile data, never its name), the app's "
                              + "settings and mods, and the last sessions. You can send it to whoever is helping you: the "
                              + "list of apps opens and you choose where it goes. Your save, accounts, e-mail and location "
                              + "are not in it, but it can hold your local network address if you played over LAN.",
                         15, CREAM, false), gap);

        // the code library refused the game's code: a code mod turned on or off since the library was made
        boolean library = crash && logSays("does not match");
        // a code mod the port can't load at all: one made for the emulators that makes the game's program bigger (as
        // Heavenfall's does: "code.bin is ... bytes but the exheader's segments take ...", 4u-native #20), or one whose
        // patch doesn't fit the code it is applied to ("cannot load ...: mods: <name>/exefs/code.ips: patch error")
        boolean codeMod = crash && !library && logSays("cannot load") && (logSays("Loader: code mod") || logSays(": mods: "));
        // the library itself (mh4n runtime: CodeLibrary::Load, Runtime::CheckLibraryBuild): made with another version of the
        // tools ("built against a different recomp.h", D035), a .so and an .id of two builds, or made with another build of
        // the code mod whose code goes past the game's data (Heavenfall's)
        boolean recompH = crash && logSays("different recomp.h");
        boolean twoBuilds = crash && logSays("come from different builds");
        boolean modBuild = crash && logSays("another build of the code mod");
        library = library || modBuild;
        if (recompH || twoBuilds) {
            box.addView(text(recompH
                    ? (es ? "La librería de código se hizo con otra versión de la herramienta (o la hizo otra persona). Vuelve a "
                            + "crearla con esta versión (Crear el código del juego, abajo, en este teléfono; o la herramienta de "
                            + "esta versión en una PC, con el .so y el .id juntos), y no uses una librería ajena."
                          : "The code library was made with another version of the tool (or by someone else). Build it again "
                            + "with this version (Build the game code, below, on this phone; or this version's tool on a PC, "
                            + "with the .so and the .id together), and don't use a library someone else made.")
                    : (es ? "La librería de código y el .id que tiene al lado son de armados distintos. Copia juntos los dos "
                            + "archivos de un mismo armado, hechos con la herramienta de esta versión."
                          : "The code library and the .id beside it come from different builds. Copy both files of the same "
                            + "build together, made with this version's tool."), 15, GOLD, false), gap);
        } else if (modBuild) {
            box.addView(text(es ? "La librería de código se hizo con otro armado del mod de código que agranda el programa (como "
                                  + "Heavenfall): cambiaste el mod o sus opciones. Vuelve a hacer la librería con los mods que "
                                  + "están activados, o vuelve a los mods del último inicio que funcionó."
                                : "The code library was made with another build of the code mod that makes the program bigger "
                                  + "(as Heavenfall does): the mod or its options changed. Make the library again with the mods "
                                  + "that are on, or go back to the mods of the last start that worked.", 15, GOLD, false), gap);
        } else if (library) {
            box.addView(text(es ? "La librería de código no coincide con el código del juego y los mods de código que están "
                                  + "activados (los que traen una carpeta exefs). Vuelve a crear la librería con esos mods "
                                  + "(Crear el código del juego, abajo: se hace en este teléfono y tarda un rato), o vuelve a los "
                                  + "mods del último inicio que funcionó."
                                : "The code library doesn't match the game's code with the code mods that are on (the ones "
                                  + "with an exefs folder). Build the library again with those mods (Build the game code, below: "
                                  + "it is made on this phone and takes a while), or go back to the mods of the last start that "
                                  + "worked.", 15, GOLD, false), gap);
        } else if (codeMod && logSays("exheader.bin:")) {
            // (a mod's exheader.bin the loader refused: another game's or region's, or not the build of its code.bin)
            box.addView(text(es ? "Un mod de código trae un exheader.bin que no va con el juego o con su propio code.bin (otro "
                                  + "juego o región, o archivos de dos armados del mod). Vuelve a armar el mod con tu juego, vuelve "
                                  + "a los mods del último inicio que funcionó, o apaga los mods de código."
                                : "A code mod brings an exheader.bin that doesn't go with the game or with its own code.bin "
                                  + "(another game or region, or files of two builds of the mod). Build the mod again from your "
                                  + "game, go back to the mods of the last start that worked, or turn the code mods off.",
                    15, GOLD, false), gap);
        } else if (codeMod) {
            box.addView(text(es ? "Un mod de código cambió el programa del juego de una forma que este port no puede cargar: "
                                  + "está hecho para los emuladores. Vuelve a los mods del último inicio que funcionó, o apaga "
                                  + "los mods de código."
                                : "A code mod changed the game's program in a way this port can't load: it is made for the "
                                  + "emulators. Go back to the mods of the last start that worked, or turn the code mods off.",
                    15, GOLD, false), gap);
        }

        // (1.0.2, S12) a GPU driver of the player's own ran this session (gpu_driver.cpp's line): said, with the way back
        String driver = crash ? logLine("GPU driver: device ") : null;
        if (driver != null && driver.contains(": the player's, ")) {
            box.addView(text(es ? "Esta sesión usó un driver de GPU propio (" + driver.substring(driver.indexOf("driver ")) + "). Si "
                                  + "el juego se sigue cerrando, elige Sistema en Opciones › Gráficos y audio › Driver de GPU."
                                : "This session ran a GPU driver of your own (" + driver.substring(driver.indexOf("driver ")) + "). If "
                                  + "the game keeps closing, choose System in Settings › Graphics & Audio › GPU driver.",
                    15, GOLD, false), gap);
        }

        // the way back first, alone on its row; then Share log and Close, side by side: nothing wider than the box
        // (three in one row came out wider than it and were cut on both sides: 4u-native #23)
        LinearLayout.LayoutParams first = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(46));
        first.setMargins(dp(8), dp(18), dp(8), 0);
        if ((library || codeMod) && new File(modsDir(), "load_order.ok").isFile()) {
            Button back = button(es ? "Volver a los mods del último inicio" : "Back to the mods of the last start", true);
            back.setOnClickListener(v -> {
                if (restoreMods()) startGame();
                else finish();
            });
            box.addView(back, first);
        } else if (codeMod) {
            Button off = button(es ? "Apagar los mods de código" : "Turn the code mods off", true);
            off.setOnClickListener(v -> {
                if (codeModsOff()) startGame();
                else finish();
            });
            box.addView(off, first);
        }
        if (recompH || twoBuilds || library) {  // (the library to make again: the builder, on this phone, as Settings › System
                                                 // › Game code has it; the game can't start to open those)
            Button build = button(es ? "Crear el código del juego" : "Build the game code", true);
            build.setOnClickListener(v -> {
                waitSystemLines();
                startActivity(new Intent(this, BuilderActivity.class).putExtra(BuilderActivity.EXTRA_FROM_ERROR, true));
                finish();
            });
            LinearLayout.LayoutParams next = new LinearLayout.LayoutParams(first);
            if (box.getChildAt(box.getChildCount() - 1) instanceof Button) next.setMargins(dp(8), dp(10), dp(8), 0);
            box.addView(build, next);
        }
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, dp(library || codeMod ? 10 : 18), 0, 0);
        Button share = button(es ? "Compartir registro" : "Share log", !(library || codeMod));
        share.setOnClickListener(v -> share());
        shareButton = share;
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, dp(46), 1);
        size.setMargins(dp(8), 0, dp(8), 0);
        row.addView(share, size);
        Button close = button(es ? "Cerrar" : "Close", false);
        close.setOnClickListener(v -> finish());
        row.addView(close, new LinearLayout.LayoutParams(size));
        box.addView(row);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(PANEL);
        scroll.setFillViewport(true);
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {  // (the buttons stay above the system's bar)
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        LinearLayout page = new LinearLayout(this);
        page.setGravity(Gravity.CENTER);
        page.setPadding(dp(32), dp(16), dp(32), dp(16));
        int room = getResources().getDisplayMetrics().widthPixels - dp(64);  // (never wider than the screen)
        page.addView(box, new LinearLayout.LayoutParams(Math.min(dp(520), room), LinearLayout.LayoutParams.WRAP_CONTENT));
        scroll.addView(page);
        setContentView(scroll);
    }

    private File home() { return AppHome.home(this); }

    /** The user folder the game ran with: launch.txt's user=, else files/user. */
    private File modsDir() {
        File user = new File(home(), "user");
        try (BufferedReader r = new BufferedReader(new FileReader(new File(home(), "launch.txt")))) {
            for (String l; (l = r.readLine()) != null; )
                if (l.startsWith("user=") && l.length() > 5) user = new File(l.substring(5).trim());
        } catch (IOException ignored) {
        }
        return new File(user, "load/mods");
    }

    /** The last line of the end of this session's log that holds `what` (its text from there on), or null. */
    private String logLine(String what) {
        File log = new File(home(), "log.txt");
        try (FileInputStream in = new FileInputStream(log)) {
            long skip = log.length() - (256 << 10);
            if (skip > 0) in.skip(skip);
            byte[] all = new byte[256 << 10];
            int n = 0;
            for (int k; n < all.length && (k = in.read(all, n, all.length - n)) > 0; ) n += k;
            String text = new String(all, 0, n, StandardCharsets.UTF_8);
            int at = text.lastIndexOf(what);
            if (at < 0) return null;
            int end = text.indexOf('\n', at);
            return text.substring(at + what.length(), end < 0 ? text.length() : end).trim();
        } catch (IOException e) {
            return null;
        }
    }

    /** The end of this session's log mentions `what`. */
    private boolean logSays(String what) {
        File log = new File(home(), "log.txt");
        try (FileInputStream in = new FileInputStream(log)) {
            long skip = log.length() - (256 << 10);
            if (skip > 0) in.skip(skip);
            byte[] all = new byte[256 << 10];
            int n = 0;
            for (int k; n < all.length && (k = in.read(all, n, all.length - n)) > 0; ) n += k;
            return new String(all, 0, n, StandardCharsets.UTF_8).contains(what);
        } catch (IOException e) {
            return false;
        }
    }

    /**
     * The mods as at the last start the code library took (load_order.ok, which the runtime writes then: mh4n
     * native/src/mods/mod_list.h): its list becomes load_order.txt, and a mod added since starts off.
     */
    private boolean restoreMods() {
        File mods = modsDir(), ok = new File(mods, "load_order.ok");
        StringBuilder text = new StringBuilder();
        java.util.HashSet<String> named = new java.util.HashSet<>();
        try (BufferedReader r = new BufferedReader(new FileReader(ok))) {
            for (String l; (l = r.readLine()) != null; ) {
                text.append(l).append('\n');
                String t = l.trim();
                if (t.startsWith("# off: ")) named.add(t.substring(7).trim());
                else if (!t.isEmpty() && !t.startsWith("#")) named.add(t);
            }
        } catch (IOException e) {
            Log.e(TAG, "mods: " + e);
            return false;
        }
        File[] dirs = mods.listFiles(File::isDirectory);
        if (dirs != null)
            for (File d : dirs) {
                String n = d.getName();
                if (n.startsWith(".") || n.startsWith("_") || named.contains(n)) continue;
                text.append("# off: ").append(n).append('\n');
            }
        try (OutputStream out = new FileOutputStream(new File(mods, "load_order.txt"))) {
            out.write(text.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.e(TAG, "mods: " + e);
            return false;
        }
        Log.i(TAG, "mods: back to those of the last start");
        return true;
    }

    /**
     * No list of the last start that worked: every code mod (a folder with an exefs folder, or a title folder with one)
     * turned off in load_order.txt, the rest as they were.
     */
    private boolean codeModsOff() {
        File mods = modsDir(), order = new File(mods, "load_order.txt");
        java.util.List<String> lines = new java.util.ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(order))) {
            for (String l; (l = r.readLine()) != null; ) lines.add(l);
        } catch (IOException ignored) {  // (no list yet: every mod was on, in the order of their names)
        }
        File[] dirs = mods.listFiles(File::isDirectory);
        if (dirs == null) return false;
        int n = 0;
        for (File d : dirs) {
            String name = d.getName();
            if (name.startsWith(".") || name.startsWith("_")) continue;
            boolean code = new File(d, "exefs").isDirectory() || new File(d, "0004000000126300/exefs").isDirectory()
                        || new File(d, "0004000E00126300/exefs").isDirectory();
            if (!code) continue;
            n++;
            boolean listed = false;
            for (int i = 0; i < lines.size(); i++) {
                String t = lines.get(i).trim();
                if (t.equals(name)) { lines.set(i, "# off: " + name); listed = true; }
                else if (t.equals("# off: " + name)) listed = true;
            }
            if (!listed) lines.add("# off: " + name);
        }
        if (n == 0) return false;
        StringBuilder text = new StringBuilder();
        for (String l : lines) text.append(l).append('\n');
        try (OutputStream out = new FileOutputStream(order)) {
            out.write(text.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.e(TAG, "mods: " + e);
            return false;
        }
        Log.i(TAG, "mods: " + n + " code mod(s) turned off");
        return true;
    }

    /**
     * After a crash: the system log's error lines of this app (an app reads only its own) at the end of the crashed
     * session's log.txt, once the game's process is gone (crash_screen.cpp ends it a moment after this screen shows).
     * They say what the game's own report can't: why the allocator or the C++ library stopped it ("Scudo ERROR: ...",
     * "terminating due to uncaught exception of type std::bad_alloc"), or the GPU driver's own words. (A Galaxy S23 FE's
     * Xclipse 920 stopped with a SIGABRT inside its driver's allocator, and the log had only the backtrace.)
     */
    private void addSystemLines() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        String game = getPackageName();
        for (int i = 0; i < 30; i++) {  // (3 s at most)
            boolean alive = false;
            List<ActivityManager.RunningAppProcessInfo> running = am.getRunningAppProcesses();
            if (running != null)
                for (ActivityManager.RunningAppProcessInfo p : running) alive |= game.equals(p.processName);
            if (!alive) break;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return;
            }
        }
        ArrayDeque<String> lines = new ArrayDeque<>();
        Process logcat = null;
        try {
            logcat = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "4000").redirectErrorStream(true).start();
            try (BufferedReader r = new BufferedReader(new InputStreamReader(logcat.getInputStream(), StandardCharsets.UTF_8))) {
                for (String l; (l = r.readLine()) != null; ) {
                    // "MM-DD HH:MM:SS.mmm  PID  TID L TAG: message": errors and fatal ones, not the game's own (in its log)
                    String[] f = l.trim().split("\\s+", 6);
                    if (f.length < 6 || f[5].startsWith("MH4U") || f[5].startsWith("SDL")) continue;
                    // (and the allocator's at any level: scudo says why it aborts in an info line)
                    if (!f[4].equals("E") && !f[4].equals("F") && !f[5].startsWith("scudo")) continue;
                    lines.addLast(l.length() > 400 ? l.substring(0, 400) : l);
                    if (lines.size() > 80) lines.removeFirst();
                }
            }
        } catch (IOException e) {
            lines.addLast("(logcat: " + e + ")");
        } finally {
            if (logcat != null) logcat.destroy();
        }
        StringBuilder s = new StringBuilder("\n===== the system log's errors of this app (logcat, the last ").append(lines.size()).append(") =====\n");
        for (String l : lines) s.append(l).append('\n');
        for (String l : Diag.exits(this, 2)) s.append(l).append('\n');  // (1.0.1: how Android saw the process end)
        try (OutputStream out = new FileOutputStream(new File(home(), "log.txt"), true)) {
            out.write(s.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.e(TAG, "system log: " + e);
        }
    }

    /** addSystemLines done (or 4 s gone): before the log is shared or the game starts again, which writes a new one. */
    private void waitSystemLines() {
        if (systemLines == null) return;
        try {
            systemLines.join(4000);
        } catch (InterruptedException ignored) {
        }
    }

    private void startGame() {
        waitSystemLines();
        Intent start = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (start != null) {
            start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(start);
        }
        finish();
    }

    // (1.0.1) the file is made on a thread of its own (the system log, the folders' sizes and the snapshot take a moment),
    // then the list of apps opens; Diag.bundle: the phone and the app, the code library, the options, the mods, the last
    // three sessions, the last save import and code build, the system log
    private void share() {
        if (sharing) return;
        sharing = true;
        final String label = shareButton.getText().toString();
        shareButton.setEnabled(false);
        shareButton.setText(es ? "Preparando…" : "Preparing…");
        final boolean crash = getIntent().getBooleanExtra(EXTRA_CRASH, false);
        new Thread(() -> {
            waitSystemLines();
            String header = device() + "share log: " + (crash ? "the error screen" : "from the settings") + ", "
                            + new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new java.util.Date()) + "\n";
            File report;
            try {
                report = Diag.bundle(this, header);
            } catch (Throwable t) {  // (never seen: the logs as before rather than no file)
                Log.e(TAG, "share log: " + t);
                report = Diag.plain(this, header, t);
            }
            final File shared = report;
            runOnUiThread(() -> {
                sharing = false;
                shareButton.setEnabled(true);
                shareButton.setText(label);
                if (isFinishing() || isDestroyed()) return;
                boolean zip = shared.getName().endsWith(".zip");
                Uri uri = LogProvider.uri(zip);
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType(zip ? "application/zip" : "text/plain");
                send.putExtra(Intent.EXTRA_SUBJECT, "MH4U log");
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.setClipData(ClipData.newRawUri("MH4U log", uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivity(Intent.createChooser(send, es ? "Compartir registro" : "Share log"));
            });
        }, "mh4u-share").start();
    }

    /** The phone, for whoever reads the log: nothing that names its owner. */
    private String device() {
        StringBuilder s = new StringBuilder("MH4U log\n");
        s.append("device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE).append(")\n");
        s.append("Android ").append(Build.VERSION.RELEASE).append(", API ").append(Build.VERSION.SDK_INT).append('\n');
        if (Build.VERSION.SDK_INT >= 31) s.append("chip: ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append('\n');
        s.append("hardware: ").append(Build.HARDWARE).append(", ").append(Build.BOARD).append('\n');
        ActivityManager.MemoryInfo mem = new ActivityManager.MemoryInfo();
        ((ActivityManager) getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(mem);
        s.append("memory: ").append(mem.totalMem >> 20).append(" MB\n");
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {  // (one of each: the core types)
            java.util.LinkedHashSet<String> seen = new java.util.LinkedHashSet<>();
            for (String line; (line = r.readLine()) != null; )
                if (line.startsWith("Features") || line.startsWith("CPU part") || line.startsWith("CPU implementer")) seen.add(line.replaceAll("\\s+", " "));
            for (String line : seen) s.append(line).append('\n');
        } catch (IOException e) {
            s.append("no /proc/cpuinfo\n");
        }
        return s.toString();
    }
}

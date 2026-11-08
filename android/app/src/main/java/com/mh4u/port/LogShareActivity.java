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
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Compartir el registro: con el juego cerrado por un error (GameActivity.crashReport), o desde Ajustes.
 * Corre en su propio proceso (":share", como LogProvider), porque el del juego muere tras un error y lo que comparte
 * tiene que seguir disponible para la app que lo reciba. Junta en un archivo los datos del teléfono, el registro de esta
 * sesión y el de la anterior: el jugador elige a quién enviarlo; la app no manda nada por su cuenta.
 */
public class LogShareActivity extends Activity {
    static final String EXTRA_CRASH = "crash";
    private static final String TAG = "MH4U";
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int MAX_LOG = 2 << 20;  // (the end of each log: a debug session's can be large)

    private boolean es;

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

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(16), dp(24), dp(18));
        box.setBackground(plate(WINE));
        box.addView(text(crash ? (es ? "El juego se cerró por un error" : "The game stopped because of an error")
                               : (es ? "Compartir registro" : "Share log"), 19, GOLD, true));
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                                                                      LinearLayout.LayoutParams.WRAP_CONTENT);
        gap.setMargins(0, dp(12), 0, 0);
        box.addView(text(es ? "El registro explica qué pasó y en qué teléfono. Puedes enviarlo a quien te esté ayudando: "
                              + "se abre la lista de apps y tú eliges a dónde va. No lleva tu partida, pero puede "
                              + "incluir la dirección de tu red local si jugaste en LAN."
                            : "The log says what happened and on which phone. You can send it to whoever is helping "
                              + "you: the list of apps opens and you choose where it goes. Your save is not in it, but "
                              + "it can hold your local network address if you played over LAN.", 15, CREAM, false), gap);

        // the code library refused the game's code: a code mod turned on or off since the library was made
        boolean library = crash && logSays("does not match");
        if (library) {
            box.addView(text(es ? "La librería de código no coincide con el código del juego y los mods de código que están "
                                  + "activados (los que traen una carpeta exefs). Vuelve a hacer la librería en la PC con esos "
                                  + "mods (el paso 1 de la guía), o vuelve a los mods del último inicio que funcionó."
                                : "The code library doesn't match the game's code with the code mods that are on (the ones "
                                  + "with an exefs folder). Make the library again on the PC with those mods (step 1 of the "
                                  + "guide), or go back to the mods of the last start that worked.", 15, GOLD, false), gap);
        }

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, dp(18), 0, 0);
        if (library && new File(modsDir(), "load_order.ok").isFile()) {
            Button back = button(es ? "Mods del último inicio" : "Mods of the last start", true);
            back.setOnClickListener(v -> {
                if (restoreMods()) startGame();
                else finish();
            });
            LinearLayout.LayoutParams wide = new LinearLayout.LayoutParams(dp(210), dp(46));
            wide.setMargins(dp(8), 0, dp(8), 0);
            row.addView(back, wide);
        }
        Button share = button(es ? "Compartir registro" : "Share log", true);
        share.setOnClickListener(v -> share());
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(dp(190), dp(46));
        size.setMargins(dp(8), 0, dp(8), 0);
        row.addView(share, size);
        Button close = button(es ? "Cerrar" : "Close", false);
        close.setOnClickListener(v -> finish());
        row.addView(close, new LinearLayout.LayoutParams(size));
        box.addView(row);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(PANEL);
        scroll.setFillViewport(true);
        LinearLayout page = new LinearLayout(this);
        page.setGravity(Gravity.CENTER);
        page.setPadding(dp(32), dp(16), dp(32), dp(16));
        int room = getResources().getDisplayMetrics().widthPixels - dp(64);  // (never wider than the screen)
        page.addView(box, new LinearLayout.LayoutParams(Math.min(dp(520), room), LinearLayout.LayoutParams.WRAP_CONTENT));
        scroll.addView(page);
        setContentView(scroll);
    }

    private File home() {
        File f = getExternalFilesDir(null);
        return f != null ? f : getFilesDir();
    }

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

    private void startGame() {
        Intent start = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (start != null) {
            start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(start);
        }
        finish();
    }

    private void share() {
        File report = LogProvider.report(this);
        try (OutputStream out = new FileOutputStream(report)) {
            out.write(device().getBytes(StandardCharsets.UTF_8));
            File home = getExternalFilesDir(null);
            if (home == null) home = getFilesDir();
            append(out, new File(home, "log.txt"), "this session");
            append(out, new File(home, "log_prev.txt"), "the session before");
        } catch (IOException e) {
            Log.e(TAG, "share log: " + e);
        }
        Uri uri = LogProvider.uri();
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, "MH4U log");
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.setClipData(ClipData.newRawUri("MH4U log", uri));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(Intent.createChooser(send, es ? "Compartir registro" : "Share log"));
    }

    private static void append(OutputStream out, File log, String title) throws IOException {
        out.write(("\n===== " + title + " (" + log.getName() + ", " + log.length() + " bytes) =====\n").getBytes(StandardCharsets.UTF_8));
        if (!log.exists()) return;
        try (FileInputStream in = new FileInputStream(log)) {
            if (log.length() > MAX_LOG) in.skip(log.length() - MAX_LOG);
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
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

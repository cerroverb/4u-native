package com.mh4u.port;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.StatFs;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * "Build the game code on this phone" (Settings, Game folder; or the game folder screen when the library is missing or
 * from another version): the player's own game and update from the chosen folder, and a code mod if they want one,
 * make the game's code library here, with no PC. BuilderService does the work; this screen shows what it uses, what it
 * needs (the download, the space, the time, the heat), and then how it goes, stage by stage, and what to do if it stops.
 * The same screen and texts as MHGU for Android's (for the files each game needs).
 *
 * The idea of building the library on the phone is Klark231's (github.com/klark231/mh4u-builder); this is the port's
 * own implementation of it.
 */
public class BuilderActivity extends Activity {
    static final String EXTRA_FOLDER = "folder";
    /** Started from the game's settings: the game is running and has to start again to load the new library. */
    static final String EXTRA_FROM_GAME = "from_game";
    /** Started from the error screen (a library that doesn't match the game's code): once made, Play starts the game. */
    static final String EXTRA_FROM_ERROR = "from_error";

    private boolean es, fromGame, fromError;
    private File folder, game, update;
    /** Which code mods go in: phone_build.py's option (--game-mods, --user-mods) and its folder, or none. */
    private static final class ModChoice {
        final String label, flag, path;
        ModChoice(String label, String flag, String path) { this.label = label; this.flag = flag; this.path = path; }
    }
    private final List<ModChoice> mods = new ArrayList<>();
    private int mod;  // index in mods
    private LinearLayout stagesBox;
    private TextView summary, status, detail, modRow, rightTitle;
    private ProgressBar bar;
    private Button go, close, free, again;
    private final Runnable refresh = this::show;
    /** While a build runs, the screen is shown again every second: its time goes on even when the build says nothing
     *  for a while (before, the whole screen stood still until the next line, which looked like a build stuck at 10 %). */
    private final android.os.Handler ticker = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (BuilderService.job.active()) show();
            ticker.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = Look.es();
        fromGame = getIntent().getBooleanExtra(EXTRA_FROM_GAME, false);
        fromError = getIntent().getBooleanExtra(EXTRA_FROM_ERROR, false);
        String path = getIntent().getStringExtra(EXTRA_FOLDER);
        if (path == null && !BuilderService.job.folder.isEmpty()) path = BuilderService.job.folder;  // (from the notification)
        if (path == null) path = BuilderTools.readText(new File(AppHome.home(this), "game_folder.txt"));
        folder = path.isEmpty() ? null : new File(path);
        look();
        build();
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        BuilderService.listen(refresh);
        show();
        ticker.postDelayed(tick, 1000);
    }

    @Override
    protected void onPause() {
        super.onPause();
        BuilderService.unlisten(refresh);
        ticker.removeCallbacks(tick);
    }

    /** The folder's game, update and code mods. */
    private void look() {
        game = update = null;
        mods.clear();
        if (folder == null) return;
        Dump dump = new Dump(es);
        File[] files = folder.listFiles(File::isFile);
        if (files != null) {
            Arrays.sort(files);
            for (File f : files) {
                String n = f.getName().toLowerCase(Locale.ROOT);
                if (!(n.endsWith(".3ds") || n.endsWith(".cci") || n.endsWith(".cxi") || n.endsWith(".cia"))) continue;
                String kind = dump.describe(f);
                if ("game".equals(kind) && game == null) game = f;
                else if ("update".equals(kind) && update == null) update = f;
            }
        }
        // the code mods the game will run with: the game folder's mods/ (what the app copies to its user folder at the
        // next start; a first build, before anything is copied), or the mods on in the app's user folder (load/mods:
        // those of the folder and those added in Settings, in their order), or none
        File from = new File(folder, "mods");
        List<String> inFolder = codeMods(from);
        if (hasCode(from)) inFolder.add(0, "0004000000126300");
        if (!inFolder.isEmpty())
            mods.add(new ModChoice((es ? "los de la carpeta mods/: " : "the folder's mods/: ") + String.join(", ", inFolder),
                    "--game-mods", from.getAbsolutePath()));
        File user = new File(AppHome.home(this), "user");
        List<String> added = codeMods(new File(user, "load/mods"));
        if (!added.isEmpty())
            mods.add(new ModChoice((es ? "los activados en Ajustes: " : "the ones on in Settings: ") + String.join(", ", added),
                    "--user-mods", user.getAbsolutePath()));
        mods.add(new ModChoice(es ? "ninguno" : "none", null, null));
        mod = 0;  // (a code mod in the folder is what the player plays with: built in by default)
    }

    /** The mods of a mods folder (one folder each) that change the game's code. */
    private static List<String> codeMods(File dir) {
        List<String> out = new ArrayList<>();
        File[] kids = dir.listFiles(File::isDirectory);
        if (kids == null) return out;
        Arrays.sort(kids);
        for (File k : kids)
            if (!k.getName().startsWith(".") && !k.getName().matches("(?i)[0-9a-f]{16}") && hasCode(k)) out.add(k.getName());
        return out;
    }

    /** A mod folder (a title folder in it, or exefs) with a code patch or a whole code.bin. */
    private static boolean hasCode(File dir) {
        List<File> roots = new ArrayList<>();
        roots.add(dir);
        for (String t : new String[] {"0004000000126300", "0004000E00126300", "0004000e00126300", "0004000000126100", "0004000E00126100"})
            roots.add(new File(dir, t));
        for (File r : roots)
            for (String f : new String[] {"exefs/code.ips", "exefs/code.bps", "exefs/code.bin", "code.ips", "code.bps", "code.bin"})
                if (new File(r, f).isFile()) return true;
        return false;
    }

    // ---- the screen
    private void build() {
        int pad = Look.dp(this, 14);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, Look.dp(this, 8), pad * 2, Look.dp(this, 6));
        TextView title = Look.text(this, es ? "CREAR EL CÓDIGO DEL JUEGO" : "BUILD THE GAME CODE", 20, Look.GOLD, true);
        title.setGravity(Gravity.CENTER);
        page.addView(title);
        TextView intro = Look.text(this, es
                ? "El código del juego se crea una vez, en este teléfono, desde tu propia copia. No se envía nada a ningún lado."
                : "The game's code is made once, on this phone, from your own copy. Nothing is sent anywhere.", 14, Look.CREAM, false);
        intro.setGravity(Gravity.CENTER);
        intro.setPadding(0, Look.dp(this, 2), 0, Look.dp(this, 8));
        page.addView(intro);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout left = panel(es ? "QUÉ USA" : "WHAT IT USES");
        summary = Look.text(this, "", 13, Look.CREAM, false);
        summary.setLineSpacing(Look.dp(this, 2), 1);
        left.addView(summary);
        modRow = Look.text(this, "", 13, Look.CREAM, false);
        modRow.setPadding(0, Look.dp(this, 6), 0, 0);
        left.addView(modRow);
        Button modButton = Look.button(this, es ? "Cambiar los mods de código" : "Change the code mods", false);
        modButton.setOnClickListener(v -> {
            if (BuilderService.busy()) return;
            mod = (mod + 1) % mods.size();
            show();
        });
        LinearLayout.LayoutParams mb = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 38));
        mb.setMargins(0, Look.dp(this, 6), 0, 0);
        left.addView(modButton, mb);
        modButton.setVisibility(mods.size() < 2 ? View.GONE : View.VISIBLE);
        // Start over: a build that was cut, cancelled or failed leaves its work folder and the record it goes on from
        // (inputs.txt); this deletes them, so the next build starts from the beginning (the tools stay unless chosen)
        again = Look.button(this, es ? "Empezar de cero" : "Start over", false);
        again.setOnClickListener(v -> {
            if (BuilderService.busy()) return;
            long size = BuilderService.halfDoneSize(this), tools = Math.max(0, BuilderTools.used(this) - size);
            Look.notice(this, es ? "¿EMPEZAR DE CERO?" : "START OVER?", es
                            ? "Se borra lo que dejó la creación sin terminar (" + mb(size) + "): sus archivos de trabajo y el registro "
                              + "desde donde seguía. La próxima vez que toques Crear, empieza desde el principio.\n\nLas herramientas "
                              + "descargadas (" + mb(tools) + ") quedan, salvo que elijas borrarlas también. La librería que ya está "
                              + "en tu carpeta no se toca."
                            : "What the unfinished build left (" + mb(size) + ") is deleted: its work files and the record it went on "
                              + "from. The next time you tap Build, it starts from the beginning.\n\nThe downloaded tools (" + mb(tools)
                              + ") stay, unless you choose to delete them too. The library already in your folder is not touched.",
                    new Look.Choice(es ? "Empezar de cero" : "Start over", true, () -> clear(false)),
                    new Look.Choice(es ? "Empezar de cero y borrar las herramientas" : "Start over and delete the tools", false,
                            () -> clear(true)),
                    new Look.Choice(es ? "Cancelar" : "Cancel", false, null));
        });
        LinearLayout.LayoutParams ab = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 38));
        ab.setMargins(0, Look.dp(this, 6), 0, 0);
        left.addView(again, ab);
        // (the tools stay for the next build: this gives their space back; a later build downloads them again)
        free = Look.button(this, "", false);
        free.setOnClickListener(v -> {
            if (BuilderService.busy()) return;
            long size = BuilderTools.used(this);
            Look.notice(this, es ? "BORRAR LAS HERRAMIENTAS" : "DELETE THE TOOLS", es
                            ? "Se borran las herramientas y lo que haya quedado de una creación (" + mb(size) + "). La librería que "
                              + "ya creaste queda en tu carpeta. Si vuelves a crearla, se descargan otra vez."
                            : "The tools and anything left from a build are deleted (" + mb(size) + "). The library you already "
                              + "made stays in your folder. If you build it again, they are downloaded again.",
                    new Look.Choice(es ? "Borrar" : "Delete", true, () -> clear(true)),
                    new Look.Choice(es ? "Cancelar" : "Cancel", false, null));
        });
        LinearLayout.LayoutParams fb = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 38));
        fb.setMargins(0, Look.dp(this, 6), 0, 0);
        left.addView(free, fb);
        columns.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        // (the right panel: what a build needs, before the first one; then its stages and how it goes, or why it stopped)
        LinearLayout right = panel("");
        rightTitle = (TextView) right.getChildAt(0);
        stagesBox = new LinearLayout(this);
        stagesBox.setOrientation(LinearLayout.VERTICAL);
        right.addView(stagesBox);
        detail = Look.text(this, "", 13, Look.CREAM, false);
        detail.setPadding(0, Look.dp(this, 6), 0, 0);
        detail.setLineSpacing(Look.dp(this, 2), 1);
        right.addView(detail);
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        gap.setMargins(Look.dp(this, 10), 0, 0, 0);
        columns.addView(right, gap);
        page.addView(columns);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(Look.WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        bar.setProgressTintList(android.content.res.ColorStateList.valueOf(Look.GOLD));
        bar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(Look.BRONZE));
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Look.dp(this, 10));
        bp.setMargins(pad * 2, Look.dp(this, 4), pad * 2, 0);
        whole.addView(bar, bp);
        status = Look.text(this, "", 14, Look.GOLD, false);
        status.setGravity(Gravity.CENTER);
        status.setPadding(pad * 2, Look.dp(this, 4), pad * 2, Look.dp(this, 6));
        whole.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        go = Look.button(this, "", true);
        go.setOnClickListener(v -> press());
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 46));
        size.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(go, size);
        close = Look.button(this, "", false);
        close.setOnClickListener(v -> leave());
        row.addView(close, new LinearLayout.LayoutParams(size));
        row.setPadding(0, 0, 0, Look.dp(this, 10));
        whole.addView(row);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
        go.requestFocus();
    }

    private LinearLayout panel(String head) {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setBackground(Look.plate(this, Look.PANEL, 6));
        int pad = Look.dp(this, 12);
        p.setPadding(pad, Look.dp(this, 8), pad, Look.dp(this, 10));
        p.addView(Look.text(this, head, 13, Look.GOLD, true));
        return p;
    }

    private static String mb(long bytes) { return (bytes >> 20) + " MB"; }

    /**
     * Start over (tools: and Delete the tools): BuilderService.startOver on a thread of its own (thousands of files: not
     * on the main thread), never while a run or its thread is going; meanwhile no run starts (BuilderService.clearing)
     * and this screen's buttons wait. Then the screen is as before any build: Build, not Go on, and no stages.
     */
    private void clear(boolean tools) {
        if (BuilderService.busy()) return;
        BuilderService.clearing = true;
        show();
        final android.content.Context app = getApplicationContext();
        final String dest = folder != null ? folder.getAbsolutePath() : BuilderService.job.folder;
        new Thread(() -> {
            try {
                BuilderService.startOver(app, dest, tools);
            } finally {
                BuilderService.clearing = false;
                BuilderService.forget(app);  // (and the screen shown again, on the main thread)
            }
        }, "builder-clear").start();
    }

    private long free() {
        try {
            return new StatFs(getFilesDir().getAbsolutePath()).getAvailableBytes();
        } catch (Exception e) {
            return Long.MAX_VALUE;
        }
    }

    /** The space a run needs now: the tools still missing, the work folder (it shrinks to nothing after a run). */
    private long needed() {
        long work = BuilderService.work(this).exists() ? BuilderTools.sizeOf(BuilderService.work(this)) : 0;
        return BuilderTools.toInstall(this) + Math.max(0, (850L << 20) - work);
    }

    private void show() {
        BuilderService.Job j = BuilderService.job;
        boolean active = j.active();
        // what it uses
        SpannableStringBuilder s = new SpannableStringBuilder();
        line(s, es ? "Carpeta: " : "Folder: ", folder == null ? (es ? "ninguna elegida" : "none chosen") : folder.getAbsolutePath(), folder != null);
        line(s, es ? "Juego: " : "Game: ", game == null ? (es ? "no está (.3ds o .cci)" : "missing (.3ds or .cci)") : game.getName(), game != null);
        line(s, es ? "Actualización: " : "Update: ", update == null ? (es ? "no está (.cia 1.3.0)" : "missing (1.3.0 .cia)") : update.getName(), update != null);
        line(s, es ? "Crea: " : "Makes: ", GameFolderActivity.LIBRARY + es(" (+ .id), en la carpeta", " (+ .id), in the folder"), true);
        summary.setText(s);
        modRow.setText(mods.size() < 2 ? (es ? "Mods de código: ninguno en la carpeta (el de una sola pantalla va en mods/)"
                                             : "Code mods: none in the folder (the single-screen mod goes in mods/)")
                                       : (es ? "Mods de código: " : "Code mods: ") + mods.get(mod).label);

        // the stages (once a build ran: before, the panel says what one needs)
        String interruptedRun = BuilderService.interrupted(this);
        boolean showStages = !j.status.equals("idle") || interruptedRun != null;
        rightTitle.setText(showStages ? (es ? "CÓMO VA" : "HOW IT GOES") : (es ? "QUÉ HACE FALTA" : "WHAT IT NEEDS"));
        stagesBox.setVisibility(showStages ? View.VISIBLE : View.GONE);
        stagesBox.removeAllViews();
        int at = Arrays.asList(BuilderService.STAGES).indexOf(j.stage);
        boolean ran = !j.status.equals("idle");
        for (int i = 0; i < BuilderService.STAGES.length; i++) {
            String name = BuilderService.STAGES[i];
            String mark;
            int color;
            if (!ran) { mark = "·"; color = Look.CREAM; }
            else if (j.status.equals("done") || i < at) { mark = "✓"; color = Look.GOLD; }
            else if (i == at && active) { mark = "▶"; color = Look.GOLD; }
            else if (i == at && j.status.equals("failed")) { mark = "✗"; color = Look.ALERT; }
            else { mark = "·"; color = Look.CREAM; }
            TextView t = Look.text(this, mark + "  " + BuilderText.stage(name, es)
                    + (name.equals("tools") && BuilderTools.toDownload(this) == 0 && (!ran || i < at) ? (es ? " (ya están)" : " (already here)") : ""),
                    13, color, i == at && active);
            t.setPadding(0, Look.dp(this, 3), 0, 0);
            stagesBox.addView(t);
        }

        // below: what it needs, or how it is going, or why it stopped
        long elapsed = System.currentTimeMillis() - j.started;
        String interrupted = BuilderService.interrupted(this);
        if (active) {
            detail.setTextColor(Look.CREAM);
            detail.setText((es ? "Tiempo: " : "Time: ") + BuilderText.minutes(elapsed, es) + eta(j)
                    + (es ? "\nSigue aunque apagues la pantalla o uses otra app. Deja el teléfono cargando."
                          : "\nIt goes on with the screen off or in another app. Keep the phone charging."));
        } else if (j.status.equals("failed")) {
            detail.setTextColor(Look.ALERT);
            detail.setText(BuilderText.failure(j.errorStage, j.errorText, es) + "\n\n" + (es ? "Detalle: " : "Details: ") + j.errorText);
        } else if (j.status.equals("done")) {
            detail.setTextColor(Look.CREAM);
            detail.setText((es ? "Listo en " : "Done in ") + BuilderText.minutes(elapsed, es) + ". " + (es
                    ? "La librería quedó en tu carpeta, junto al juego. Si cambias el mod de código, créala de nuevo."
                    : "The library is in your folder, beside the game. If you change the code mod, build it again."));
        } else {
            long dl = BuilderTools.toDownload(this), need = needed(), free = free();
            StringBuilder t = new StringBuilder();
            if (interrupted != null) t.append(es ? "La última creación se interrumpió: sigue desde donde quedó.\n\n"
                                                 : "The last build was interrupted: it goes on from where it stopped.\n\n");
            t.append(dl > 0 ? (es ? "• La primera vez descarga las herramientas, unos " + mb(dl) + ": Python (python.org) y Zig (PyPI), "
                                    + "software libre. Nunca descarga nada del juego.\n"
                                  : "• The first time it downloads the tools, about " + mb(dl) + ": Python (python.org) and Zig (PyPI), "
                                    + "free software. It never downloads anything of the game.\n")
                            : (es ? "• Las herramientas ya están en el teléfono.\n" : "• The tools are already on the phone.\n"));
            t.append(es ? "• Espacio: hacen falta " + mb(need) + " libres mientras se crea (hay " + mb(free) + "). Al terminar se "
                          + "borra lo temporal y quedan las herramientas (" + mb(BuilderTools.INSTALLED_SIZE) + ").\n"
                        : "• Space: it needs " + mb(need) + " free while it builds (there are " + mb(free) + "). When it ends the "
                          + "temporary files go and the tools stay (" + mb(BuilderTools.INSTALLED_SIZE) + ").\n");
            t.append(es ? "• Tiempo: de 15 minutos a 2 horas según el teléfono. Sigue con la pantalla apagada y, si se corta, sigue "
                          + "desde donde quedó.\n"
                        : "• Time: 15 minutes to 2 hours depending on the phone. It goes on with the screen off and, if it is cut, it "
                          + "goes on from where it stopped.\n");
            t.append(es ? "• El teléfono se calienta: déjalo cargando, sin funda si puedes. Caliente, va más despacio; muy caliente, "
                          + "se pausa solo hasta que se enfría."
                        : "• The phone gets warm: keep it charging, out of its case if you can. Warm, it goes slower; too hot, it "
                          + "pauses by itself until it cools down.");
            detail.setTextColor(Look.CREAM);
            detail.setText(t);
        }

        boolean clearing = BuilderService.clearing;
        int percent = j.status.equals("done") ? 100 : ran ? BuilderText.percent(j) : 0;
        bar.setIndeterminate(active && percent < 0);
        bar.setProgress(Math.max(0, percent));
        bar.setVisibility(ran ? View.VISIBLE : View.INVISIBLE);
        status.setTextColor(j.status.equals("failed") && !clearing ? Look.ALERT : Look.GOLD);
        status.setText(clearing ? (es ? "Borrando…" : "Deleting…")
                : active ? BuilderText.status(j, es) + (percent >= 0 ? "  (" + percent + " %)" : "")
                : j.status.equals("failed") ? (es ? "Se detuvo en: " : "Stopped at: ") + BuilderText.stage(j.stage, es)
                : j.status.equals("cancelled") ? (es ? "Cancelada. Puedes seguir cuando quieras." : "Cancelled. You can go on whenever you like.")
                : j.status.equals("done") ? (es ? "El código del juego está listo." : "The game code is ready.")
                : missing());

        boolean ready = folder != null && game != null && update != null;
        if (active) {
            go.setText(es ? "Cancelar" : "Cancel");
            go.setEnabled(true);
        } else if (j.status.equals("done")) {
            go.setText(fromGame ? (es ? "Reiniciar el juego" : "Restart the game") : (es ? "Jugar" : "Play"));
            go.setEnabled(true);
        } else {
            go.setText(j.status.equals("failed") || j.status.equals("cancelled") || interrupted != null
                    ? (es ? "Seguir" : "Go on") : (es ? "Crear" : "Build"));
            go.setEnabled(ready);
        }
        if (clearing) go.setEnabled(false);
        close.setText(active ? (es ? "Ocultar" : "Hide") : (es ? "Volver" : "Back"));
        boolean busy = BuilderService.busy();
        long used = busy ? 0 : BuilderTools.used(this);
        free.setText((es ? "Borrar las herramientas (" : "Delete the tools (") + mb(used) + ")");
        free.setVisibility(used > (1 << 20) ? View.VISIBLE : View.GONE);
        again.setVisibility(!busy && BuilderService.halfDone(this) ? View.VISIBLE : View.GONE);
    }

    private String es(String spanish, String english) { return es ? spanish : english; }

    private String eta(BuilderService.Job j) {
        if (!j.stage.equals("compile") || j.done < 3 || j.total <= j.done) return "";
        long spent = System.currentTimeMillis() - j.stageStarted;
        long left = spent * (j.total - j.done) / j.done;
        return (es ? " · quedan unos " : " · about ") + BuilderText.minutes(left, es) + (es ? "" : " left");
    }

    private String missing() {
        if (folder == null) return es ? "Primero elige la carpeta del juego." : "Choose the game's folder first.";
        if (game == null || update == null)
            return es ? "En la carpeta faltan el juego o su actualización." : "The game or its update is missing from the folder.";
        return es ? "Toca Crear para empezar." : "Tap Build to start.";
    }

    private void line(SpannableStringBuilder s, String label, String value, boolean ok) {
        int at = s.length();
        s.append(label);
        s.setSpan(new ForegroundColorSpan(Look.GOLD), at, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        at = s.length();
        s.append(value).append("\n");
        s.setSpan(new ForegroundColorSpan(ok ? Look.CREAM : Look.ALERT), at, s.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }

    private void press() {
        BuilderService.Job j = BuilderService.job;
        if (BuilderService.clearing) return;
        if (j.active()) {
            Look.notice(this, es ? "¿CANCELAR?" : "CANCEL?", es
                            ? "Lo ya hecho se guarda: la próxima vez sigue desde donde quedó."
                            : "What is done is kept: the next time it goes on from where it stopped.",
                    new Look.Choice(es ? "Cancelar la creación" : "Cancel the build", true,
                            () -> startService(new Intent(this, BuilderService.class).setAction(BuilderService.ACTION_CANCEL))),  // (running: no foreground start needed)
                    new Look.Choice(es ? "Seguir creando" : "Keep building", false, null));
            return;
        }
        if (j.status.equals("done")) {
            j.status = "idle";
            if (fromGame) {  // (the game holds the old library: the app starts again, as Import save does)
                startActivity(new Intent(this, PhoenixActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        .putExtra(PhoenixActivity.EXTRA_PID, android.os.Process.myPid()));
                finish();
                return;
            }
            if (fromError) {  // (the game closed: it starts as from the app's icon, with the new library)
                Intent start = getPackageManager().getLaunchIntentForPackage(getPackageName());
                if (start != null) startActivity(start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
                finish();
                return;
            }
            setResult(RESULT_OK);
            finish();
            return;
        }
        long need = needed(), free = free();
        if (free < need) {
            Look.notice(this, es ? "FALTA ESPACIO" : "NOT ENOUGH SPACE", es
                            ? "Hacen falta " + mb(need) + " libres en el teléfono y hay " + mb(free) + ". Libera espacio y vuelve a intentarlo."
                            : "It needs " + mb(need) + " free on the phone and there are " + mb(free) + ". Free some space and try again.",
                    new Look.Choice(es ? "Entendido" : "OK", true, null));
            return;
        }
        Intent run = new Intent(this, BuilderService.class)
                .putExtra(BuilderService.EXTRA_GAME, game.getAbsolutePath())
                .putExtra(BuilderService.EXTRA_UPDATE, update.getAbsolutePath())
                .putExtra(BuilderService.EXTRA_DEST, folder.getAbsolutePath());
        ModChoice m = mods.get(mod);
        if (m.flag != null) run.putExtra(BuilderService.EXTRA_MOD_FLAG, m.flag).putExtra(BuilderService.EXTRA_MOD, m.path);
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(run);
        else startService(run);
    }

    private void leave() {
        if (BuilderService.job.status.equals("done") && !fromGame) setResult(RESULT_OK);
        finish();
    }

    @Override
    public void onBackPressed() { leave(); }
}

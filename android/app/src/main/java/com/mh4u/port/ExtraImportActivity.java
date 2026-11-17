package com.mh4u.port;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Settings -> Import DLC and event quests (S37): the player's own event quests, from a folder they choose. MHGU for
 * Android has the same screen, name and texts (its event quests come in a save instead).
 *
 * MH4U keeps its downloaded event quests in its extra data (extdata/00000000/00001263/user/, 00001261 in Europe), as
 * the files quest1 to quest5 (328,704 bytes each in the USA game); the guild cards (card*, cardbox) and the Palico
 * (otomo*) are there too, and stay as they are: only quest files are copied. They come from the player's own copy: an
 * emulator's user folder (Azahar, Citra: its sdmc tree), a Checkpoint or JKSM extdata backup (the files themselves), or
 * any folder holding them. The app includes none and downloads none.
 *
 * As an imported save, nothing is written while the game runs (it may read or write its extra data at any moment): the
 * files are checked and copied into save_import_extra/<title>/, then the app restarts, and GameActivity applies them
 * before the game starts (applyPending): the quest files they replace go to save_backups/<date>-dlc first.
 *
 * DLC titles (1.0.1, DlcImport): the add-on content the player installed in Azahar (its user or sdmc folder, or the
 * title folder itself) or a decrypted .cia of their own, in the same folder chosen. They are checked as the runtime
 * reads them and copied into the game's own folder (user/sdmc/.../title/0004008c/<low>/) before the restart: the
 * runtime looks titles up when the game starts and reads them only when the game asks, never writes them.
 */
public class ExtraImportActivity extends Activity {
    private static final String TAG = "MH4U";
    private static final Pattern QUEST = Pattern.compile("quest[0-9]{1,2}");
    private static final long MAX_SIZE = 1 << 20;  // (a quest file is 328,704 bytes in the USA game)

    private boolean es;
    private TextView status;
    private Button main, leave;
    private Found found;  // what the folder chosen holds (null: none chosen yet)

    static File home(Context c) { return AppHome.home(c); }
    private static File stagedDir(Context c) { return new File(home(c), "save_import_extra"); }

    /** The quest files and DLC titles of a folder chosen, and which game they are for. */
    private static final class Found {
        List<File> quests = new ArrayList<>();
        String title;  // 00126300 the USA game, 00126100 Europe's; null: the path doesn't say
        File from;
        List<DlcImport.Title> dlc = new ArrayList<>();
    }

    // ---- applying an import before the game starts (GameActivity.onCreate)
    /**
     * The quest files staged in save_import_extra/<title>/ go into that game's extra data, in the save folder in use:
     * the ones of the same names that were there are copied to save_backups/<date>-dlc first; the other files (the
     * guild cards, the Palico) are not touched. If anything fails, what is staged stays and the next start tries again.
     */
    static void applyPending(Context c) {
        File staged = stagedDir(c), ready = new File(staged, "ready");
        if (!ready.exists()) return;
        try {
            int copied = 0;
            for (String t : SaveFolder.TITLES) {
                File[] files = new File(staged, t).listFiles(f -> f.isFile() && QUEST.matcher(f.getName()).matches());
                if (files == null || files.length == 0) continue;
                File user = new File(SaveFolder.extDir(SaveFolder.active(c), t), "user");
                if (!user.isDirectory()) throw new IOException("no extra data at " + user);
                File keep = null;
                for (File f : files) {
                    File old = new File(user, f.getName());
                    if (!old.isFile()) continue;
                    if (keep == null) {
                        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
                        keep = new File(SaveFolder.backups(c), stamp + "-dlc/" + t + "/extdata/user");
                    }
                    SaveFolder.copyChecked(old, new File(keep, f.getName()));
                }
                for (File f : files) {
                    SaveFolder.copyChecked(f, new File(user, f.getName()));
                    copied++;
                }
                Log.i(TAG, "dlc import: " + files.length + " quest files into " + user + (keep != null ? ", the old ones in " + keep : ""));
            }
            if (copied == 0) throw new IOException("nothing staged");
            SaveActivity.deleteTree(staged);
        } catch (IOException e) {
            Log.e(TAG, "dlc import: " + e.getMessage());
        }
    }

    // ---- the screen
    private LinearLayout column(String title, String body, boolean framed) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        if (framed) {
            c.setBackground(Look.plate(this, Look.PANEL, 6));
            c.setPadding(Look.dp(this, 14), Look.dp(this, 10), Look.dp(this, 14), Look.dp(this, 10));
        } else {
            c.setPadding(Look.dp(this, 18), 0, 0, 0);
        }
        c.addView(Look.text(this, title, 13, Look.GOLD, true));
        TextView t = Look.text(this, body, 13, Look.CREAM, false);
        t.setPadding(0, Look.dp(this, 4), 0, 0);
        t.setLineSpacing(Look.dp(this, 1), 1);
        c.addView(t);
        return c;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = Look.es();
        int pad = Look.dp(this, 14);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(pad * 2, Look.dp(this, 8), pad * 2, Look.dp(this, 6));

        TextView title = Look.text(this, es ? "IMPORTAR DLC Y MISIONES DE EVENTO" : "IMPORT DLC AND EVENT QUESTS", 20, Look.GOLD, true);
        title.setGravity(Gravity.CENTER);
        page.addView(title);
        TextView intro = Look.text(this, es ? "Trae las misiones de evento y el DLC que ya tienes, de un emulador o de una copia de tu consola."
                                            : "Bring the event quests and the DLC you already have, from an emulator or a backup of your console.", 14, Look.CREAM, false);
        intro.setGravity(Gravity.CENTER);
        intro.setPadding(0, Look.dp(this, 2), 0, Look.dp(this, 8));
        page.addView(intro);

        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        columns.addView(column(es ? "QUÉ CARPETA ELEGIR" : "WHICH FOLDER TO CHOOSE", es
                ? "Las misiones de evento son los archivos quest1 a quest5 de los datos extra del juego.\n"
                  + "• Azahar o Citra: la carpeta del emulador, la que tiene sdmc. Trae las misiones y el DLC instalado.\n"
                  + "• Checkpoint o JKSM: la copia de los datos extra (extdata).\n"
                  + "• DLC: la carpeta con su .cia descifrado, o la del título (title/0004008c/...).\n"
                  + "• Si vienen en un .zip, descomprímelo antes."
                : "The event quests are the files quest1 to quest5 of the game's extra data.\n"
                  + "• Azahar or Citra: the emulator's folder, the one with sdmc. It brings the quests and the installed DLC.\n"
                  + "• Checkpoint or JKSM: the backup of the extra data (extdata).\n"
                  + "• DLC: the folder with its decrypted .cia, or the title's folder (title/0004008c/...).\n"
                  + "• If they come in a .zip, unzip it first.", true),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        columns.addView(column(es ? "QUÉ VA A PASAR" : "WHAT WILL HAPPEN", es
                ? "• Solo se copian los archivos de misiones: tus cazadores, tarjetas de gremio y Felyne quedan como están.\n"
                  + "• Los archivos de misiones que se reemplazan quedan en una copia, en save_backups.\n"
                  + "• El DLC se copia a la carpeta del juego y reemplaza el que estaba. Tiene que estar descifrado.\n"
                  + "• El juego se reinicia: guarda antes lo que no quieras perder."
                : "• Only the quest files are copied: your hunters, guild cards and Palico stay as they are.\n"
                  + "• The quest files they replace are kept in a copy, in save_backups.\n"
                  + "• The DLC is copied into the game's folder and replaces the one there. It has to be decrypted.\n"
                  + "• The game restarts: save first if you have progress to keep.", false),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        page.addView(columns);
        TextView own = Look.text(this, es ? "La app no incluye ni descarga DLC ni misiones de evento: los traes tú, bajo tu responsabilidad."
                                          : "The app includes no DLC or event quests and downloads none: you bring your own, at your own risk.",
                12, Look.CREAM, false);
        own.setGravity(Gravity.CENTER);
        own.setAlpha(.75f);
        own.setPadding(0, Look.dp(this, 10), 0, 0);
        page.addView(own);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(page);
        LinearLayout whole = new LinearLayout(this);
        whole.setOrientation(LinearLayout.VERTICAL);
        whole.setBackgroundColor(Look.WINE);
        whole.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        status = Look.text(this, "", 14, Look.GOLD, false);
        status.setGravity(Gravity.CENTER);
        status.setPadding(pad * 2, Look.dp(this, 4), pad * 2, Look.dp(this, 4));
        status.setVisibility(View.GONE);
        whole.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, 0, 0, Look.dp(this, 10));
        main = Look.button(this, es ? "Elegir carpeta" : "Choose a folder", true);
        main.setOnClickListener(v -> { if (found != null) confirm(); else browse(); });
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, Look.dp(this, 46));
        size.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(main, size);
        leave = Look.button(this, es ? "Cancelar" : "Cancel", false);
        leave.setOnClickListener(v -> finish());
        LinearLayout.LayoutParams size2 = new LinearLayout.LayoutParams(Look.dp(this, 120), Look.dp(this, 46));
        size2.setMargins(Look.dp(this, 8), 0, Look.dp(this, 8), 0);
        row.addView(leave, size2);
        whole.addView(row);
        whole.setOnApplyWindowInsetsListener((v, insets) -> {  // (the buttons stay above the system's bar)
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(whole);
        main.requestFocus();  // (a controller: A presses it)
    }

    private void say(String s, boolean bad) {
        status.setTextColor(bad ? Look.ALERT : Look.GOLD);
        status.setText(s);
        status.setVisibility(View.VISIBLE);
    }

    private void browse() {
        if (Build.VERSION.SDK_INT >= 30 ? !Environment.isExternalStorageManager()
                : checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            say(es ? "El juego no tiene permiso para leer tus carpetas: elige otra vez la carpeta del juego en Ajustes para darlo."
                   : "The game has no permission to read your folders: choose the game folder again in Settings to give it.", true);
            return;
        }
        FolderBrowser.open(this, null, es ? "ELIGE LA CARPETA DE TUS MISIONES O TU DLC" : "CHOOSE YOUR QUESTS' OR DLC'S FOLDER",
                dir -> !questsIn(dir).isEmpty() || DlcImport.holds(dir),
                (dir, holds) -> holds ? (es ? "✓ Aquí hay misiones de evento o DLC." : "✓ There are event quests or DLC here.")
                                      : (es ? "Entra a la carpeta con quest1, quest2... o con el .cia del DLC, o elige la de tu emulador (la que tiene sdmc)."
                                            : "Go into the folder with quest1, quest2... or with the DLC's .cia, or choose your emulator's (the one with sdmc)."),
                this::check, null, false);
    }

    /** The quest files right in dir, or in its user/ (an extdata folder as the emulators keep it). */
    private static List<File> questsIn(File dir) {
        List<File> out = new ArrayList<>();
        for (File d : new File[] {dir, new File(dir, "user")}) {
            File[] files = d.listFiles(f -> f.isFile() && QUEST.matcher(f.getName().toLowerCase(Locale.ROOT)).matches());
            if (files != null && files.length > 0) {
                Arrays.sort(files);
                out.addAll(Arrays.asList(files));
                return out;
            }
        }
        return out;
    }

    /** The game an extra data folder's path says: its id, 00001263 (USA) or 00001261 (Europe). */
    private static String titleOf(File dir) {
        for (File d = dir; d != null; d = d.getParentFile()) {
            String n = d.getName().toLowerCase(Locale.ROOT);
            if (n.equals("00001263") || n.contains("0x01263")) return "00126300";
            if (n.equals("00001261") || n.contains("0x01261")) return "00126100";
        }
        return null;
    }

    /**
     * The quests in a folder chosen: in it (or its user/), in an emulator's tree under it (sdmc/Nintendo 3DS/<id0>/<id1>/
     * extdata/00000000/0000126x/user, as SaveActivity finds a save), or a couple of levels down (a Checkpoint backup's
     * folder of dates). null: none.
     */
    private static Found find(File dir, int depth) {
        if (dir == null || depth > 3) return null;
        List<File> here = questsIn(dir);
        if (!here.isEmpty()) {
            Found f = new Found();
            f.quests = here;
            f.from = here.get(0).getParentFile();
            f.title = titleOf(f.from);
            return f;
        }
        File n3ds = dir.getName().equals("Nintendo 3DS") ? dir : new File(dir, "Nintendo 3DS");
        if (!n3ds.isDirectory() && new File(dir, "sdmc/Nintendo 3DS").isDirectory()) n3ds = new File(dir, "sdmc/Nintendo 3DS");
        if (n3ds.isDirectory()) {
            Found best = null;
            File[] ids0 = n3ds.listFiles(File::isDirectory);
            if (ids0 != null)
                for (File id0 : ids0) {
                    File[] ids1 = id0.listFiles(File::isDirectory);
                    if (ids1 == null) continue;
                    for (File id1 : ids1)
                        for (String ext : new String[] {"00001263", "00001261"}) {
                            Found f = find(new File(id1, "extdata/00000000/" + ext), 3);
                            if (f != null && (best == null || f.quests.size() > best.quests.size())) best = f;
                        }
                }
            if (best != null) return best;
        }
        if (depth >= 2) return null;
        File[] kids = dir.listFiles(f -> f.isDirectory() && !f.getName().startsWith("."));
        if (kids == null || kids.length > 40) return null;
        Arrays.sort(kids, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));  // (a Checkpoint backup: the newest)
        for (File k : kids) {
            Found f = find(k, depth + 1);
            if (f != null) return f;
        }
        return null;
    }

    /** The game the player has: the game folder's (SaveFolder.gameTitle: else the one with a save, else the USA one). */
    private String playerTitle() {
        return SaveFolder.gameTitle(this);
    }

    private void check(File dir) {
        found = null;
        main.setEnabled(false);
        leave.setEnabled(false);
        say(es ? "Revisando la carpeta..." : "Checking the folder...", false);
        new Thread(() -> {
            Found q = find(dir, 0);
            List<DlcImport.Title> dlc = DlcImport.find(dir);
            if (q == null && !dlc.isEmpty()) {
                q = new Found();
                q.from = dir;
            }
            if (q != null) q.dlc = dlc;
            final Found f = q;
            String wrong = problem(f);
            runOnUiThread(() -> {
                main.setEnabled(true);
                leave.setEnabled(true);
                if (wrong != null) {
                    say(wrong, true);
                    main.setText(es ? "Elegir carpeta" : "Choose a folder");
                    return;
                }
                found = f;
                String quests = f.quests.isEmpty() ? "" : es
                        ? f.quests.size() + (f.quests.size() == 1 ? " archivo de misiones encontrado" : " archivos de misiones encontrados") + " (" + names(f) + "). "
                        : f.quests.size() + (f.quests.size() == 1 ? " quest file found" : " quest files found") + " (" + names(f) + "). ";
                say(quests + dlcLine(f) + (es ? "Al continuar se copian y el juego se reinicia." : "Going on copies them and restarts the game."), false);
                main.setText(es ? "Importar y reiniciar" : "Import and restart");
                main.requestFocus();
            });
        }).start();
    }

    /** "DLC: 0004008C00126300 (135 contents, 12 MB). " or "". */
    private String dlcLine(Found f) {
        StringBuilder b = new StringBuilder();
        for (DlcImport.Title t : f.dlc) {
            b.append(b.length() > 0 ? ", " : "DLC: ").append(t.id()).append(" (")
             .append(t.readable).append(es ? " contenidos, " : " contents, ").append(Math.max(1, t.bytes >> 20)).append(" MB)");
        }
        return b.length() > 0 ? b.append(". ").toString() : "";
    }

    private static String names(Found f) {
        StringBuilder b = new StringBuilder();
        for (File q : f.quests) b.append(b.length() > 0 ? ", " : "").append(q.getName());
        return b.toString();
    }

    /** What is wrong with what the folder holds, or null. */
    private String problem(Found f) {
        if (f == null)
            return es ? "En esa carpeta no hay misiones de evento ni DLC de MH4U. Elige la que tiene quest1, quest2... o el .cia del DLC, o la de tu emulador (la que tiene sdmc)."
                      : "There are no MH4U event quests or DLC in that folder. Choose the one with quest1, quest2... or the DLC's .cia, or your emulator's (the one with sdmc).";
        String mine = playerTitle();
        DlcImport check = new DlcImport(es);
        for (DlcImport.Title t : f.dlc) {
            check.check(t, mine);
            if (t.problem != null) return t.problem + (es ? " No se copió nada." : " Nothing was copied.");
            if (t.folder != null && t.folder.getAbsoluteFile().equals(DlcImport.target(SaveFolder.local(this), t).getAbsoluteFile()))
                return es ? "Ese es el DLC que el juego ya usa." : "That is the DLC the game already uses.";
        }
        long need = 0;
        for (DlcImport.Title t : f.dlc) need += t.bytes;
        if (need > 0 && SaveFolder.local(this).getUsableSpace() < need + (need >> 4) + (64 << 20))
            return es ? "No hay espacio para el DLC (" + (need >> 20) + " MB). Libera espacio y vuelve a intentarlo."
                      : "There is no room for the DLC (" + (need >> 20) + " MB). Free some space and try again.";
        if (f.quests.isEmpty()) return null;
        if (f.title != null && !f.title.equals(mine))
            return es ? "Esas misiones son de la versión " + (f.title.equals("00126100") ? "europea" : "de EE. UU.") + " y tu juego es de la otra: solo sirven las de la misma región."
                      : "Those quests are for the " + (f.title.equals("00126100") ? "European" : "USA") + " game and yours is the other one: only the same region's work.";
        for (File q : f.quests) {
            long n = q.length();
            if (n <= 0 || n > MAX_SIZE)
                return es ? q.getName() + " no parece un archivo de misiones (" + n + " bytes). No se copió nada."
                          : q.getName() + " doesn't look like a quest file (" + n + " bytes). Nothing was copied.";
            String p = q.getAbsolutePath(), own = home(this).getAbsolutePath(), save = SaveFolder.active(this).getAbsolutePath();
            if (p.startsWith(own + "/") || p.startsWith(save + "/"))
                return es ? "Esas son las misiones que el juego ya usa." : "Those are the quests the game already uses.";
        }
        if (!new File(SaveFolder.extDir(SaveFolder.active(this), mine), "user").isDirectory())
            return es ? "Todavía no hay datos extra del juego: empieza a jugar y guarda una vez, y vuelve a importarlas."
                      : "The game has no extra data yet: start playing and save once, then import them again.";
        return null;
    }

    private void confirm() {
        if (!found.dlc.isEmpty()) {
            main.setEnabled(false);
            leave.setEnabled(false);
            say(es ? "Copiando el DLC..." : "Copying the DLC...", false);
            final Found f = found;
            new Thread(() -> {
                try {
                    for (DlcImport.Title t : f.dlc) {
                        DlcImport.install(t, SaveFolder.local(this), (done, total) -> {
                            int pct = total > 0 ? (int) (done * 100 / total) : 100;
                            if (pct % 5 == 0) runOnUiThread(() -> say((es ? "Copiando el DLC... " : "Copying the DLC... ") + pct + " %", false));
                        });
                    }
                    runOnUiThread(this::stageQuestsAndRestart);
                } catch (java.io.IOException e) {
                    Log.e(TAG, "dlc import: " + e.getMessage());
                    runOnUiThread(() -> {
                        main.setEnabled(true);
                        leave.setEnabled(true);
                        found = null;
                        main.setText(es ? "Elegir carpeta" : "Choose a folder");
                        say(es ? "No se pudo copiar el DLC (" + e.getMessage() + "). No se cambió nada."
                               : "The DLC could not be copied (" + e.getMessage() + "). Nothing was changed.", true);
                    });
                }
            }).start();
            return;
        }
        stageQuestsAndRestart();
    }

    /** The quest files (if any) staged for GameActivity, then the restart. */
    private void stageQuestsAndRestart() {
        File staged = stagedDir(this);
        SaveActivity.deleteTree(staged);
        File to = new File(staged, playerTitle());
        try {
            for (File q : found.quests) SaveFolder.copyChecked(q, new File(to, q.getName().toLowerCase(Locale.ROOT)));
        } catch (IOException e) {
            SaveActivity.deleteTree(staged);
            Log.e(TAG, "dlc import: " + e.getMessage());
            boolean dlcIn = !found.dlc.isEmpty();
            say(es ? "No se pudieron copiar las misiones. Cópialas a la memoria del teléfono y vuelve a intentarlo. "
                     + (dlcIn ? "El DLC sí se copió: se usa desde el próximo inicio del juego." : "No se cambió nada.")
                   : "The quests could not be copied. Copy them to the phone's storage and try again. "
                     + (dlcIn ? "The DLC was copied: the game uses it from its next start." : "Nothing was changed."), true);
            found = null;
            main.setEnabled(true);
            leave.setEnabled(true);
            main.setText(es ? "Elegir carpeta" : "Choose a folder");
            return;
        }
        if (!found.quests.isEmpty()) {
            SaveActivity.writeBytes(new File(staged, "ready"), new byte[0]);
            Log.i(TAG, "dlc import: " + found.quests.size() + " quest files from " + found.from + " staged");
        }
        main.setEnabled(false);
        leave.setEnabled(false);
        say(es ? "Reiniciando..." : "Restarting...", false);
        // the game may hold its extra data open and write it at any moment: the files go in before it starts again
        Intent intent = new Intent(this, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        startActivity(intent);
        new Handler(Looper.getMainLooper()).postDelayed(() -> Process.killProcess(Process.myPid()), 200);
    }
}

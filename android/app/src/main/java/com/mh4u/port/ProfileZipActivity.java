package com.mh4u.port;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;
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
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Ajustes -> Perfiles de controles -> un perfil -> Exportar / Importar (1.0.2, S3): solo mueve el .zip. Exportar: el que
 * app_native/touch.cpp dejó en profile_export/ (el perfil, sus imágenes y sus botones combinados) se copia a la carpeta
 * que elige el jugador. Importar: el .zip que elige se copia como profile_import.zip en la carpeta de la app y la pantalla
 * se cierra; touch.cpp lo revisa y lo pone en el espacio (touch_profile_import_poll), y la página del perfil dice qué
 * pasó. El port de MHGU tiene la misma pantalla.
 */
public class ProfileZipActivity extends Activity {
    private static final String TAG = "MH4U";
    private static final int PICK = 1;
    private static final long MOST = 160L << 20;  // (a profile's .zip is far smaller: the runtime turns down past 128 MB)
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int ALERT = Color.rgb(255, 150, 120);

    private boolean es, in;
    private TextView status;
    private Button main;

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

    private GradientDrawable plate(int fill) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setStroke(dp(2), EDGE);
        d.setCornerRadius(dp(6));
        return d;
    }

    private Button button(String s, boolean strong) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(strong ? GOLD : CREAM);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        if (strong) b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(plate(BRONZE));
        return b;
    }

    /** The .zip app_native/touch.cpp made for this screen (touch_profile_export_app): the only one in profile_export. */
    private File made() {
        File[] list = new File(AppHome.home(this), "profile_export").listFiles();
        File best = null;
        if (list != null)
            for (File f : list)
                if (f.isFile() && f.getName().endsWith(".zip") && (best == null || f.lastModified() > best.lastModified())) best = f;
        return best;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = Look.es();
        in = getIntent().getBooleanExtra("import", false);

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(14), dp(24), dp(16));
        box.setBackground(plate(WINE));
        TextView title = text(in ? (es ? "IMPORTAR UN PERFIL" : "IMPORT A PROFILE") : (es ? "EXPORTAR UN PERFIL" : "EXPORT A PROFILE"),
                20, GOLD, true);
        title.setGravity(Gravity.CENTER);
        box.addView(title);
        TextView body = text(in
                ? (es ? "Elige un .zip hecho con Exportar. Se copia a la app y el perfil queda en el espacio que elegiste, con sus imágenes y sus botones combinados."
                      : "Choose a .zip made with Export. It is copied into the app, and the profile goes into the slot you chose, with its pictures and combined buttons.")
                : (es ? "Elige una carpeta y ahí se copia el .zip con el perfil, sus imágenes y sus botones combinados. Con Importar lo pones en otro espacio o en otro teléfono o PC."
                      : "Choose a folder and the .zip with the profile, its pictures and its combined buttons is copied there. Import puts it in another slot, or on another phone or PC."),
                14, CREAM, false);
        body.setPadding(0, dp(10), 0, dp(10));
        body.setLineSpacing(dp(1), 1);
        box.addView(body);
        status = text("", 14, GOLD, true);
        status.setGravity(Gravity.CENTER);
        box.addView(status);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, dp(12), 0, 0);
        main = button(in ? (es ? "Elegir un .zip" : "Choose a .zip") : (es ? "Elegir carpeta" : "Choose a folder"), true);
        main.setOnClickListener(v -> pick());
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(dp(190), dp(46));
        size.setMargins(dp(8), 0, dp(8), 0);
        row.addView(main, size);
        Button close = button(es ? "Cerrar" : "Close", false);
        close.setOnClickListener(v -> finish());
        row.addView(close, new LinearLayout.LayoutParams(size));
        box.addView(row);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(PANEL);
        scroll.setFillViewport(true);
        LinearLayout page = new LinearLayout(this);
        page.setGravity(Gravity.CENTER);
        page.setPadding(dp(32), dp(10), dp(32), dp(10));
        int room = getResources().getDisplayMetrics().widthPixels - dp(64);  // (never wider than the screen)
        page.addView(box, new LinearLayout.LayoutParams(Math.min(dp(600), room), LinearLayout.LayoutParams.WRAP_CONTENT));
        scroll.addView(page);
        setContentView(scroll);

        if (!in && made() == null) {
            main.setEnabled(false);
            say(es ? "No se pudo preparar el .zip del perfil." : "The profile's .zip couldn't be made.", true);
        }
    }

    private void pick() {
        Intent i;
        if (in) {
            i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");  // (file managers call a .zip by several types)
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        } else {
            i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        }
        try {
            startActivityForResult(i, PICK);
        } catch (RuntimeException e) {  // (no app to choose with)
            say(es ? "No hay ninguna app para elegir archivos." : "There is no app to choose files with.", true);
        }
    }

    private void say(String s, boolean bad) {
        status.setText(s);
        status.setTextColor(bad ? ALERT : GOLD);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        main.setEnabled(false);
        say(es ? "Copiando…" : "Copying…", false);
        new Thread(() -> {
            String done = null, error = null;
            try {
                done = in ? bringIn(uri) : sendOut(uri);
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "profile zip: " + e);
                error = e.getMessage();
            }
            final String ok = done, why = error;
            runOnUiThread(() -> {
                main.setEnabled(true);
                if (ok != null && in) finish();  // (the game takes it from here and says what it did)
                else if (ok != null) say((es ? "Listo: " : "Done: ") + ok, false);
                else say((es ? "No se pudo copiar: " : "Could not copy it: ") + why, true);
            });
        }).start();
    }

    /** The profile's .zip into the chosen folder, under its own name. That name. */
    private String sendOut(Uri tree) throws IOException {
        File from = made();
        if (from == null) throw new IOException(es ? "no hay .zip" : "no .zip");
        ContentResolver cr = getContentResolver();
        Uri parent = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        Uri to = DocumentsContract.createDocument(cr, parent, "application/zip", from.getName());
        if (to == null) throw new IOException(es ? "esa carpeta no admite archivos nuevos" : "that folder takes no new files");
        long copied = 0;
        try (FileInputStream inp = new FileInputStream(from); OutputStream out = cr.openOutputStream(to)) {
            if (out == null) throw new IOException(from.getName());
            byte[] buf = new byte[1 << 16];
            for (int n; (n = inp.read(buf)) > 0; copied += n) out.write(buf, 0, n);
        }
        if (copied != from.length()) throw new IOException(from.getName());
        Log.i(TAG, "profile zip: " + from.getName() + " exported");
        return from.getName();
    }

    /** The chosen .zip copied as profile_import.zip in the app's folder (a .part first: the game never reads half of it). */
    private String bringIn(Uri uri) throws IOException {
        File home = AppHome.home(this), part = new File(home, "profile_import.zip.part"), done = new File(home, "profile_import.zip");
        long copied = 0;
        try (InputStream inp = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(part)) {
            if (inp == null) throw new IOException("?");
            byte[] buf = new byte[1 << 16];
            for (int n; (n = inp.read(buf)) > 0; copied += n) {
                if (copied + n > MOST) throw new IOException(es ? "es demasiado grande para ser un perfil" : "too big to be a profile");
                out.write(buf, 0, n);
            }
        } catch (IOException e) {
            part.delete();
            throw e;
        }
        if (!part.renameTo(done)) {
            part.delete();
            throw new IOException("profile_import.zip");
        }
        Log.i(TAG, "profile zip: " + copied + " bytes in for the game to import");
        return done.getName();
    }
}

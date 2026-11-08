package com.mh4u.port;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Settings -> Graphics & Audio -> Import a driver (app_native/gpu_driver.cpp, through GameActivity.showDriverImport): a
 * Vulkan driver of the player's own for an Adreno GPU, at their own risk, as the MHGU port's DriverActivity takes it. The
 * phone's file picker for its .zip (meta.json and the library: the format of the emulators' driver packages, such as
 * Turnip's), then its 64-bit ARM libraries are unpacked into files/gpu_drivers/<folder>/ with driver.txt (its name and
 * version, its library, what meta.json says of it). Choosing it, and everything else, is the settings' (ImGui).
 *
 * The outcome goes to files/gpu_drivers/import.txt, which the settings read when the game comes back:
 *   ok / <folder> / <name>      or      error / no_library | not_arm64 | too_large | unreadable / <the system's words>
 * No screen of its own but a plate in the colours of the game's windows while the .zip is unpacked, over the game.
 */
public class DriverActivity extends Activity {
    private static final String TAG = "MH4U";
    private static final int PICK = 1;
    private static final long MAX_BYTES = 256L * 1024 * 1024;  // (a driver's libraries: some tens of MB)
    private static final int WINE = Color.rgb(38, 18, 14), CREAM = Color.rgb(240, 228, 200), GOLD = Color.rgb(226, 182, 92);
    private static final int EDGE = Color.rgb(196, 150, 70);

    /** A reason the settings put in words (gpu_driver.h GpuDriverImport::error). */
    private static final class Refused extends IOException {
        final String code;
        Refused(String code, String detail) {
            super(detail);
            this.code = code;
        }
    }

    private boolean es;
    private boolean busy;

    static File root(Activity a) { return new File(a.getFilesDir(), "gpu_drivers"); }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        es = "es".equals(Locale.getDefault().getLanguage());
        if (state != null) return;  // (the picker is already up)
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
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
        if (!busy) super.onBackPressed();  // (an unpacking under way finishes first)
    }

    @Override
    protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        if (request != PICK || code != RESULT_OK || data == null || data.getData() == null) {
            finish();  // (nothing chosen: nothing to say)
            return;
        }
        final Uri zip = data.getData();
        busy = true;
        showPlate();
        new Thread(() -> {
            String outcome;
            try {
                String[] done = install(zip);
                outcome = "ok\n" + done[0] + "\n" + done[1] + "\n";
            } catch (Refused e) {
                outcome = "error\n" + e.code + "\n" + oneLine(e.getMessage()) + "\n";
            } catch (Exception e) {
                Log.e(TAG, "driver import: " + e);
                outcome = "error\nunreadable\n" + oneLine(e.getMessage()) + "\n";
            }
            result(outcome);
            runOnUiThread(() -> {
                busy = false;
                finish();
            });
        }, "driver-import").start();
    }

    // ---- the plate while it unpacks
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
        title.setText(es ? "DRIVER DE GPU" : "GPU DRIVER");
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
        text.setText(es ? "Instalando el driver…" : "Installing the driver…");
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
        File dir = root(this);
        dir.mkdirs();
        File tmp = new File(dir, "import.txt.tmp"), to = new File(dir, "import.txt");
        try (FileWriter w = new FileWriter(tmp)) {
            w.write(text);
        } catch (IOException e) {
            Log.e(TAG, "driver import: " + e);
            return;
        }
        if (!tmp.renameTo(to)) Log.e(TAG, "driver import: import.txt not written");
    }

    private static String oneLine(String s) {
        return s == null ? "" : s.replace('\n', ' ').replace('\r', ' ').trim();
    }

    // ---- unpacking
    private static void delete(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) delete(k);
        f.delete();
    }

    private static List<String> lines(File f) {
        List<String> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            for (String l; (l = r.readLine()) != null; ) out.add(l.trim());
        } catch (IOException e) {
            return out;
        }
        return out;
    }

    /**
     * A library named as the system's own driver (vulkan.adreno.so) is never loaded: Android's interface has that one
     * open already, and the system's runs in its place (gpu_driver.cpp notices it). Such a library gets a name of its own.
     */
    private static String ownName(File dir, String library) {
        boolean system = library.equals("vulkan.adreno.so") || library.equals("libvulkan.so")
                || new File("/vendor/lib64/hw/" + library).exists() || new File("/system/lib64/hw/" + library).exists();
        if (!system) return library;
        String renamed = "vulkan.player.so";
        File to = new File(dir, renamed);
        to.delete();
        return new File(dir, library).renameTo(to) ? renamed : library;
    }

    private static boolean arm64(File so) throws IOException {
        byte[] h = new byte[20];
        try (FileInputStream in = new FileInputStream(so)) {
            if (in.read(h) != h.length) return false;
        }
        return h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 2 && h[18] == (byte) 0xb7 && h[19] == 0;
    }

    /** The zip's meta.json and its libraries into a folder of their own: { the folder, the name to show }. */
    private String[] install(Uri zip) throws Exception {
        File root = root(this), tmp = new File(root, ".new");
        delete(tmp);
        if (!tmp.mkdirs()) throw new IOException("no folder");
        JSONObject meta = null;
        List<String> libs = new ArrayList<>();
        long total = 0;
        try (InputStream in = getContentResolver().openInputStream(zip); ZipInputStream z = new ZipInputStream(in)) {
            byte[] buf = new byte[65536];
            for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                if (e.isDirectory()) continue;
                String base = new File(e.getName()).getName();  // (nothing outside the folder, whatever the zip says)
                if (base.equalsIgnoreCase("meta.json")) {
                    ByteArrayOutputStream json = new ByteArrayOutputStream();
                    for (int n; (n = z.read(buf)) > 0; ) if (json.size() < 65536) json.write(buf, 0, n);
                    try {
                        meta = new JSONObject(json.toString("UTF-8"));
                    } catch (JSONException bad) {  // (the libraries are still worth taking: named by their file)
                        Log.w(TAG, "driver import: meta.json: " + bad);
                    }
                } else if (base.endsWith(".so") && !base.startsWith(".")) {
                    try (OutputStream out = new FileOutputStream(new File(tmp, base))) {
                        for (int n; (n = z.read(buf)) > 0; ) {
                            total += n;
                            if (total > MAX_BYTES) throw new Refused("too_large", total + " bytes");
                            out.write(buf, 0, n);
                        }
                    }
                    if (!libs.contains(base)) libs.add(base);
                }
            }
        } catch (Exception e) {
            delete(tmp);
            throw e;
        }
        if (libs.isEmpty()) {
            delete(tmp);
            throw new Refused("no_library", "");
        }
        String library = meta != null ? meta.optString("libraryName", "") : "";
        if (library.isEmpty() || !libs.contains(library)) library = libs.get(0);
        if (!arm64(new File(tmp, library))) {
            delete(tmp);
            throw new Refused("not_arm64", library);
        }
        library = ownName(tmp, library);
        String shown = meta != null ? meta.optString("name", "").trim() : "";
        String version = meta != null ? meta.optString("driverVersion", "").trim() : "";
        if (shown.isEmpty()) shown = library;
        if (!version.isEmpty() && !shown.contains(version)) shown += " " + version;
        String about = "";
        if (meta != null) {
            String description = meta.optString("description", "").trim(), author = meta.optString("author", "").trim();
            about = description + (author.isEmpty() ? "" : (description.isEmpty() ? "" : " - ") + author);
        }
        String folder = shown.replaceAll("[^A-Za-z0-9._-]+", "_");
        if (folder.isEmpty() || folder.startsWith(".")) folder = "driver" + folder;
        try (FileWriter w = new FileWriter(new File(tmp, "driver.txt"))) {
            w.write(oneLine(shown) + "\n" + library + "\n" + oneLine(about) + "\n");
        }
        File to = new File(root, folder);
        delete(to);
        if (!tmp.renameTo(to)) throw new IOException("no folder");
        // (the same driver installed again and chosen: its choice names the library it has now)
        File selected = new File(root, "selected");
        List<String> chosen = lines(selected);
        if (chosen.size() >= 2 && chosen.get(0).equals(folder) && !chosen.get(1).equals(library)) {
            try (FileWriter w = new FileWriter(selected)) {
                w.write(folder + "\n" + library + "\n");
            }
        }
        Log.i(TAG, "driver import: " + shown + " in " + folder + " (" + library + ", " + libs.size() + " libraries)");
        return new String[] {folder, oneLine(shown)};
    }
}

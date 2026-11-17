package com.mh4u.port;

import android.app.Dialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * MH4U: the SDL activity that starts the native runtime (libmain.so, main in app_native/android_main.cpp).
 *
 * The player's files go in the app's external folder (Android/data/<package>/files):
 *   launch.txt   game=, code=, user= (GameFolderActivity writes it)
 *   user/        saves, extra data, mh4u.ini, load/mods
 *   log.txt      this session's log (Share log)
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "MH4U";

    /** GameFolderActivity, the entry, checked everything in this process and started the game (startGame). */
    static volatile boolean entered;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // (who started it: "com.android.shell" for adb's am start, the test runs' way with a launch.txt of their own)
        android.net.Uri referrer = getReferrer();
        String by = referrer != null ? referrer.getHost() : null;
        if (!entered && !"com.android.shell".equals(by)) {
            // A start that didn't come through the entry: Android bringing the game's task back after its process ended
            // (Recents), a controller's USB start, another app. The entry's checks must come first (the app's folder, the
            // save folder's start gate with its in-use warning, the notices, the game's files and library), and SDL is made
            // only once in a process: this one ends at once, before the game runs, and PhoenixActivity opens the entry
            super.onCreate(savedInstanceState);  // (Android wants it; with finish() here no surface comes, so no game)
            Log.i(TAG, "a start without the entry (" + getIntent().getAction() + ", by " + by + "): through the entry");
            Intent intent = new Intent(this, PhoenixActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
            startActivity(intent);
            overridePendingTransition(0, 0);
            finish();
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> Process.killProcess(Process.myPid()), 200);
            return;
        }
        Diag.install(this);  // (1.0.1: the versions seen, a Java crash's stack for Share log)
        getExternalFilesDir(null);  // (makes the folder, so the native side and adb can find it)
        if (!entered) AppHome.ensure(this);  // (a test run started with adb: the app's folder checked, as the entry does)
        SaveFolder.applyPending(this);  // (Settings -> Save folder, before the game reads the save: done by the gate already)
        SaveActivity.applyPending(this);  // (an imported save, before the game reads it: done by the gate already)
        ExtraImportActivity.applyPending(this);  // (imported event quests, before the game reads its extra data)
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        drawUnderCutout();
        pickRefreshRate();
        Diag.logToGame(this);  // (the phone and the app into log.txt, off this thread)
        Diag.watch(this);      // (and what changes while the game runs: the network, the heat, power saving)
        watching = true;
        watchRefreshRate();
    }

    private boolean watching;

    @Override
    protected void onDestroy() {
        if (watching) Diag.unwatch(this);
        if (rateListener != null) ((DisplayManager) getSystemService(DISPLAY_SERVICE)).unregisterDisplayListener(rateListener);
        rateListener = null;
        super.onDestroy();
    }

    // (1.0.1, diag) the system's memory warnings and the window's mode, as events in log.txt with the session's time
    @Override
    public void onTrimMemory(int level) {
        if (watching) Diag.event("the system trims memory: " + Diag.trimLevel(level));
        super.onTrimMemory(level);
    }

    @Override
    public void onLowMemory() {
        if (watching) Diag.event("the system is out of memory (onLowMemory)");
        super.onLowMemory();
    }

    @Override
    public void onMultiWindowModeChanged(boolean on, android.content.res.Configuration config) {
        if (watching) Diag.event(on ? "split screen / a window of its own" : "full screen again");
        super.onMultiWindowModeChanged(on, config);
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration config) {
        if (watching)
            Diag.event("the configuration changed: " + config.screenWidthDp + "x" + config.screenHeightDp + " dp, "
                       + (config.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE ? "landscape" : "portrait")
                       + ", " + config.densityDpi + " dpi");
        super.onConfigurationChanged(config);
    }

    /**
     * The game fills the screen beside a notch or a punch hole too. SDL lets the window under the cutout only from Android 11
     * (LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS, when it goes full screen); on Android 9 and 10 the window stayed beside it, and
     * in landscape the cutout's side showed a black strip (4u-native #60, an Infinix Note 8 on Android 10: 1640x720, the
     * game 1600 wide). There SHORT_EDGES does the same: in landscape the cutout is on a short edge.
     */
    private void drawUnderCutout() {
        if (Build.VERSION.SDK_INT < 28) return;
        try {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            int mode = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                                                   : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            if (lp.layoutInDisplayCutoutMode == mode) return;
            lp.layoutInDisplayCutoutMode = mode;
            getWindow().setAttributes(lp);
        } catch (Exception e) {
            Log.w(TAG, "cutout: " + e);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        pickRefreshRate();
    }

    /**
     * "Guardar y reiniciar" del menú de opciones (runtime/ui.c restart_application, por JNI desde el hilo de SDL):
     * PhoenixActivity, en su propio proceso, espera a que este termine (el código nativo hace exit(0) al volver de acá,
     * y su atexit guarda la caché de pipelines) y lanza la app de nuevo. Si no se puede, una excepción: el menú dice
     * entonces que el reinicio falló y el juego sigue.
     */
    public static void restartApp() {
        SDLActivity activity = mSingleton;
        if (activity == null) throw new IllegalStateException("restartApp: no activity");
        Intent intent = new Intent(activity, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        activity.startActivity(intent);
        Log.i(TAG, "restart: PhoenixActivity, pid " + Process.myPid());
    }

    /** Los carteles del programa (SDL_ShowMessageBox) con los colores de las ventanas del juego. */
    @Override
    protected void messageboxCreateAndShow(Bundle args) {
        final int wine = Color.rgb(38, 18, 14), cream = Color.rgb(240, 228, 200), gold = Color.rgb(226, 182, 92);
        final int bronze = Color.rgb(62, 38, 22), edge = Color.rgb(196, 150, 70);
        final Dialog dialog = new Dialog(this);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setCancelable(false);
        dialog.setOnDismissListener(d -> {
            synchronized (messageboxSelection) {
                messageboxSelection.notify();
            }
        });

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(16), dp(24), dp(18));
        GradientDrawable plate = new GradientDrawable();
        plate.setColor(wine);
        plate.setStroke(dp(2), edge);
        plate.setCornerRadius(dp(6));
        box.setBackground(plate);

        TextView title = new TextView(this);
        title.setText(args.getString("title"));
        title.setTextColor(gold);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 19);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.CENTER);
        box.addView(title);
        View rule = new View(this);
        rule.setBackgroundColor(edge);
        LinearLayout.LayoutParams ruleSize = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        ruleSize.setMargins(0, dp(10), 0, dp(14));
        box.addView(rule, ruleSize);
        TextView message = new TextView(this);
        message.setText(args.getString("message"));
        message.setTextColor(cream);
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        message.setGravity(Gravity.CENTER);
        box.addView(message);

        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, dp(16), 0, 0);
        int[] ids = args.getIntArray("buttonIds");
        String[] texts = args.getStringArray("buttonTexts");
        Button first = null;
        for (int i = 0; i < texts.length; i++) {
            final int id = ids[i];
            Button button = new Button(this);
            button.setText(texts[i]);
            button.setAllCaps(false);
            button.setTextColor(cream);
            GradientDrawable face = new GradientDrawable();
            face.setColor(bronze);
            face.setStroke(dp(2), edge);
            face.setCornerRadius(dp(6));
            button.setBackground(face);
            button.setOnClickListener(v -> {
                messageboxSelection[0] = id;
                dialog.dismiss();
            });
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(dp(120), dp(44));
            size.setMargins(dp(6), 0, dp(6), 0);
            row.addView(button, size);
            if (first == null) first = button;
        }
        box.addView(row);
        final Button main = first;
        dialog.setOnKeyListener((d, key, event) -> {  // (un joystick: A o B)
            if (main == null || (key != KeyEvent.KEYCODE_BUTTON_A && key != KeyEvent.KEYCODE_BUTTON_B
                    && key != KeyEvent.KEYCODE_ENTER && key != KeyEvent.KEYCODE_ESCAPE)) return false;
            if (event.getAction() == KeyEvent.ACTION_UP) main.performClick();
            return true;
        });
        dialog.setContentView(box);
        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setLayout((int) (getResources().getDisplayMetrics().widthPixels * .52f), WindowManager.LayoutParams.WRAP_CONTENT);
        }
        dialog.show();
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    /** El informe de entrada del log (app_native/controls.cpp, por JNI desde el hilo de SDL), como el del port de MHGU:
     *  los dispositivos de entrada como los lista Android y las teclas de mando de los que no se leen como mando. "" si
     *  nada cambió. */
    public static String inputReport(boolean changed) {
        return org.libsdl.app.SDLControllerManager.inputReport(changed);
    }

    /** Teclado y mouse conectados (app_native/controls.cpp, por JNI desde el hilo de SDL), como en el port de MHGU: 1 un
     *  teclado físico con letras, 2 un mouse, los dos sumados. Con los dos, las opciones muestran las teclas y el mouse. */
    public static int keyboardMouse() {
        return org.libsdl.app.SDLControllerManager.keyboardMouse();
    }

    /** Ajustes -> Licencias (runtime/ui.c show_licenses, por JNI desde el hilo de SDL). */
    public static void showLicenses() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, LicensesActivity.class));
    }

    /** Settings -> Import save. */
    public static void showSaveImport() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, SaveActivity.class));
    }

    /** Settings -> Import DLC and event quests (S37). */
    public static void showExtraImport() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, ExtraImportActivity.class));
    }

    /** Settings -> Shortcut with your image (S56). */
    public static void showShortcut() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, ShortcutActivity.class));
    }

    /** Settings -> Save folder. */
    public static void showSaveFolder() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, SaveFolderActivity.class));
    }

    /** Settings -> Export save. */
    public static void showSaveExport() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, SaveActivity.class).putExtra(SaveActivity.EXTRA_EXPORT, true));
    }

    /** Settings -> Graphics & Audio -> Import a driver (app_native/gpu_driver.cpp): DriverActivity, over the game. */
    public static void showDriverImport() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, DriverActivity.class));
    }

    /** Settings -> Cheats -> Import cheats (app_native/content.cpp): ImportActivity, over the game. */
    public static void showCheatImport() { showImport(ImportActivity.CHEATS); }

    /** The layout editor's Picture -> Add pictures (app_native/touch.cpp img_tap): the buttons' own pictures. */
    public static void importPictures() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, PicturesActivity.class));
    }

    /** Settings -> Mods -> Add a mod (.zip / folder) (app_native/content.cpp): ImportActivity, over the game. */
    public static void showModImport() { showImport(ImportActivity.MOD_ZIP); }
    public static void showModFolderImport() { showImport(ImportActivity.MOD_FOLDER); }

    private static void showImport(int kind) {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, ImportActivity.class).putExtra(ImportActivity.KIND, kind));
    }

    /** Ajustes -> Compartir registro. */
    public static void showShareLog() {
        SDLActivity activity = mSingleton;
        if (activity != null) activity.startActivity(new Intent(activity, LogShareActivity.class));
    }

    /** El juego se cierra por un error (runtime/platform/android.c): la pantalla para compartir el registro, en su proceso. */
    public static void crashReport() {
        SDLActivity activity = mSingleton;
        if (activity == null) throw new IllegalStateException("crashReport: no activity");
        Intent intent = new Intent(activity, LogShareActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(LogShareActivity.EXTRA_CRASH, true);
        activity.startActivity(intent);
    }

    /** LAN (runtime/net/ldn.c): sin este permiso, muchos teléfonos descartan por Wi-Fi los paquetes de búsqueda de sala. */
    public static void lanSearch(boolean on) {
        SDLActivity activity = mSingleton;
        if (activity == null) return;
        try {
            synchronized (GameActivity.class) {
                if (lanLock == null) {
                    android.net.wifi.WifiManager wifi = (android.net.wifi.WifiManager) activity.getApplicationContext().getSystemService(WIFI_SERVICE);
                    if (wifi == null) return;
                    lanLock = wifi.createMulticastLock("mh4u-lan");
                    lanLock.setReferenceCounted(false);
                }
                if (on) lanLock.acquire();
                else if (lanLock.isHeld()) lanLock.release();
                Diag.event("LAN: Wi-Fi broadcasts " + (on ? "on" : "off"));
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "LAN: " + e);
        }
    }
    private static android.net.wifi.WifiManager.MulticastLock lanLock;

    /** Settings -> Build the game code: the on-phone builder, over the game (which starts again to load what it made). */
    public static void showBuilder() {
        SDLActivity activity = mSingleton;
        if (activity != null)
            activity.startActivity(new Intent(activity, BuilderActivity.class).putExtra(BuilderActivity.EXTRA_FROM_GAME, true));
    }

    /** Ajustes -> Carpeta del juego. */
    public static void showGameFolder() {
        SDLActivity activity = mSingleton;
        if (activity != null) {
            Intent intent = new Intent(activity, GameFolderActivity.class);
            intent.putExtra(GameFolderActivity.EXTRA_CHANGE, true);
            activity.startActivity(intent);
        }
    }

    /**
     * Cuánto antes de un vsync hay que entregar un búfer para que se vea en él (Display.getPresentationDeadlineNanos):
     * el margen de los presents con hora (present_pacing.cpp). 0 si no se sabe.
     */
    public static long presentationDeadlineNs() {
        SDLActivity activity = mSingleton;
        if (activity == null) return 0;
        try {
            Display display = Build.VERSION.SDK_INT >= 30 ? activity.getDisplay() : activity.getWindowManager().getDefaultDisplay();
            return display != null ? display.getPresentationDeadlineNanos() : 0;
        } catch (Exception e) {
            return 0;
        }
    }

    private static volatile float frameCap;  // (el límite de FPS de los ajustes; 0: aún no se sabe, o sin límite)

    /** El límite de FPS cambió (runtime/ui.c): la pantalla lo sigue. */
    public static void setFrameCap(float fps) {
        frameCap = fps;
        SDLActivity activity = mSingleton;
        if (activity instanceof GameActivity) activity.runOnUiThread(((GameActivity) activity)::pickRefreshRate);
    }

    private static volatile boolean frameGen;  // ([Graphics] FrameGen: the refresh rate at twice the limit, pickRefreshRate)

    /** [Graphics] FrameGen se prendió o se apagó (android_main.cpp, settings_panel.cpp): la pantalla elige de nuevo su tasa de refresco. */
    public static void setFrameGen(boolean on) {
        if (frameGen == on) return;
        frameGen = on;
        SDLActivity activity = mSingleton;
        if (activity instanceof GameActivity) activity.runOnUiThread(((GameActivity) activity)::pickRefreshRate);
    }

    // (1.0.1) The screen's refresh rate as it changes, the system's own switches too (a Galaxy S25 Ultra's panel went
    // 60 -> 120 -> 60 Hz by itself 3 minutes into a 30 FPS run with frame generation): an event in log.txt, with the rate
    // pickRefreshRate asked for. At most 40 a session (a panel that changes its rate by itself all the time)
    private DisplayManager.DisplayListener rateListener;
    private volatile float rateAsked;
    private float rateSeen;
    private int rateEvents;

    private void watchRefreshRate() {
        try {
            final DisplayManager dm = (DisplayManager) getSystemService(DISPLAY_SERVICE);
            Display display = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (dm == null || display == null) return;
            final int id = display.getDisplayId();
            rateSeen = display.getRefreshRate();
            rateListener = new DisplayManager.DisplayListener() {
                @Override public void onDisplayAdded(int displayId) {}
                @Override public void onDisplayRemoved(int displayId) {}
                @Override public void onDisplayChanged(int displayId) {
                    if (displayId != id || rateEvents >= 40) return;
                    Display d = dm.getDisplay(id);
                    if (d == null) return;
                    float rate = d.getRefreshRate();
                    if (Math.abs(rate - rateSeen) < .5f) return;
                    rateEvents++;
                    Diag.event("the screen's refresh rate is now " + rate + " Hz, " + rateSeen + " Hz before (asked for "
                               + rateAsked + " Hz)" + (rateEvents == 40 ? " (no more of these this session)" : ""));
                    rateSeen = rate;
                }
            };
            dm.registerDisplayListener(rateListener, null);  // (on this thread, the interface's)
        } catch (Exception e) {
            Log.w(TAG, "refresh rate listener: " + e);
        }
    }

    /**
     * La tasa de refresco más baja que alcanza para el límite de FPS, nunca menos de 60: una pantalla a 120 Hz con el juego
     * a 60 gasta batería y calienta el teléfono sin mostrar nada más. Sin límite conocido, la más alta.
     * Con la generación de cuadros ([Graphics] FrameGen, 1.0.1): la más baja que sea el doble del límite o más, si la
     * pantalla tiene una (límite 60: 120 Hz; 30: 60 Hz), porque la imagen hecha entre dos del juego necesita el límite a la
     * mitad de la tasa o menos; si no tiene, la de siempre. Sin FrameGen, exactamente la de antes.
     */
    private void pickRefreshRate() {
        try {
            Display display = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (display == null) return;
            Display.Mode current = display.getMode(), best = null, fastest = current, doubled = null;
            float want = frameCap > 0 ? Math.max(frameCap, 60) - .5f : Float.MAX_VALUE;
            float twice = frameGen && frameCap > 0 ? 2 * frameCap - .5f : Float.MAX_VALUE;  // (FrameGen off: never)
            for (Display.Mode m : display.getSupportedModes()) {
                if (m.getPhysicalWidth() != current.getPhysicalWidth() || m.getPhysicalHeight() != current.getPhysicalHeight()) continue;
                if (m.getRefreshRate() > fastest.getRefreshRate()) fastest = m;
                if (m.getRefreshRate() >= want && (best == null || m.getRefreshRate() < best.getRefreshRate())) best = m;
                if (m.getRefreshRate() >= twice && (doubled == null || m.getRefreshRate() < doubled.getRefreshRate())) doubled = m;
            }
            if (best == null) best = fastest;
            if (doubled != null) best = doubled;
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            lp.preferredRefreshRate = best.getRefreshRate();
            getWindow().setAttributes(lp);
            rateAsked = best.getRefreshRate();
            String fg = !frameGen || frameCap <= 0 ? "" : doubled != null ? ", frame generation: twice the limit or more"
                                                                          : ", frame generation: no rate twice the limit";
            Diag.event("display mode " + best.getPhysicalWidth() + "x" + best.getPhysicalHeight() + " @ " + best.getRefreshRate() + " Hz"
                       + (fg.isEmpty() ? "" : " (the FPS limit " + frameCap + fg + ")"));
        } catch (Exception e) {
            Log.w(TAG, "refresh rate: " + e);
        }
    }
}

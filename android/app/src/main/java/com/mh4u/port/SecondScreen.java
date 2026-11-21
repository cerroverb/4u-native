package com.mh4u.port;

import android.app.Activity;
import android.app.Presentation;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * 1.0.2 (S38): the game's touch screen on a second display of the device, for handhelds with two screens, as the
 * emulators do it. Settings > Graphics & Audio > Screen and HUD > Layout > On the second screen ([Display] Layout=second
 * of mh4u-app.ini, app_native/second_screen.h); off unless the player picks it, and offered only while there is such a
 * display.
 *
 * Android lists that display as a presentation display (DisplayManager.DISPLAY_CATEGORY_PRESENTATION). This class tells
 * the native side when one (apart from the game's own display) comes and goes, and while the native main loop wants it
 * (setWanted) keeps a Presentation open on it: a SurfaceView whose surface the native side draws the touch screen into
 * (an EGL surface of its own, on SDL's context), and whose fingers go to the game as the touch screen's.
 *
 * The Presentation's window never takes the focus (FLAG_NOT_FOCUSABLE): a touch on the second display would otherwise
 * move the focus there, and the controller's keys with it, away from the game's window. It closes while the app is in
 * the background (onStop) and opens again when it comes back; Android closes it when its display goes away, and the
 * native side then shows the touch screen in the game's window with the layout the player had before.
 * Everything here runs on the interface thread.
 */
final class SecondScreen {
    private static final String TAG = "MH4U";

    /** A presentation display apart from the game's came (true) or the last one went (false). */
    private static native void nativeDisplay(boolean present);
    /** The Presentation's surface, w x h pixels; null when it is gone (returns once the native side let it go). */
    private static native void nativeSurface(Surface surface, int width, int height);
    /** A finger on it: 0 down, 1 moved, 2 up or cancelled; x, y in the view's pixels. */
    private static native void nativeTouch(int action, float x, float y);

    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private DisplayManager manager;
    private DisplayManager.DisplayListener listener;
    private Display display;            // the second display in use (null: none)
    private Presentation presentation;  // open on it (null: none)
    private boolean wanted;             // the native main loop wants it (Layout: On the second screen)
    private boolean started = true;     // the app is in front (onStart .. onStop)
    private boolean nativeOk = true;    // (false: libmain.so has no such methods; nothing more is tried)
    private int opens;                  // opened in the last 10 seconds (a window that keeps closing is given up)
    private long opensSince;
    private int pointer = -1;           // the finger that is the touch screen's (the first one down)

    SecondScreen(Activity activity) {
        this.activity = activity;
    }

    /** From the game activity's onCreate: the displays watched from now on (the first look logs what is there). */
    void watch() {
        try {
            manager = (DisplayManager) activity.getSystemService(Activity.DISPLAY_SERVICE);
            if (manager == null) return;
            listener = new DisplayManager.DisplayListener() {
                @Override public void onDisplayAdded(int id) { refresh(); }
                @Override public void onDisplayRemoved(int id) { refresh(); }
                @Override public void onDisplayChanged(int id) { refresh(); }
            };
            manager.registerDisplayListener(listener, handler);
            refresh();
        } catch (RuntimeException e) {
            Log.w(TAG, "second display: " + e);
        }
    }

    /** From onDestroy. */
    void stop() {
        if (manager != null && listener != null) manager.unregisterDisplayListener(listener);
        listener = null;
        close();
    }

    void setStarted(boolean on) {
        if (started == on) return;
        started = on;
        if (on) refresh();
        else close();
    }

    /** The native main loop (GameActivity.setSecondScreen): the touch screen wanted on the second display, or not. */
    void setWanted(boolean on) {
        if (wanted == on) return;
        wanted = on;
        opens = 0;
        if (on) refresh();
        else close();
    }

    /** A presentation display other than the game's own: the first Android lists, or null. */
    private Display find() {
        Display own = Build.VERSION.SDK_INT >= 30 ? activity.getDisplay() : activity.getWindowManager().getDefaultDisplay();
        for (Display d : manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (own != null && d.getDisplayId() == own.getDisplayId()) continue;
            return d;
        }
        return null;
    }

    private void refresh() {
        if (manager == null) return;
        Display found;
        try {
            found = find();
        } catch (RuntimeException e) {
            Log.w(TAG, "second display: " + e);
            return;
        }
        int was = display != null ? display.getDisplayId() : -1, now = found != null ? found.getDisplayId() : -1;
        if (was != now) {
            close();  // (the display it was on went away, or another one is the first now)
            display = found;
            if (found != null) {
                Point size = new Point();
                found.getRealSize(size);
                Diag.event("a second display: \"" + found.getName() + "\", " + size.x + "x" + size.y + ", id " + now
                           + ", flags 0x" + Integer.toHexString(found.getFlags()));
            } else {
                Diag.event("the second display is gone");
            }
            tell(found != null);
        }
        if (wanted && started && display != null && presentation == null) open();
    }

    private void tell(boolean present) {
        if (!nativeOk) return;
        try {
            nativeDisplay(present);
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
            Log.w(TAG, "second display: " + e);
        }
    }

    private void open() {
        long now = SystemClock.uptimeMillis();
        if (now - opensSince > 10000) {
            opensSince = now;
            opens = 0;
        }
        if (++opens > 3) {  // (it closed again and again by itself: the touch screen stays in the game's window)
            if (opens == 4) Diag.event("the second display's window keeps closing: not opened again for now");
            return;
        }
        try {
            final Presentation p = new Presentation(activity, display);
            SurfaceView view = new SurfaceView(p.getContext());
            view.getHolder().setFormat(PixelFormat.RGBA_8888);
            view.getHolder().addCallback(new SurfaceHolder.Callback() {
                @Override public void surfaceCreated(SurfaceHolder holder) {}
                @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                    surface(holder.getSurface(), width, height);
                }
                @Override public void surfaceDestroyed(SurfaceHolder holder) {
                    surface(null, 0, 0);
                }
            });
            view.setOnTouchListener((v, e) -> {
                touch(v, e);
                return true;
            });
            p.setContentView(view);
            Window window = p.getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                                | WindowManager.LayoutParams.FLAG_FULLSCREEN);
                hideBars(window);
            }
            p.setOnDismissListener(d -> {
                if (presentation != p) return;
                presentation = null;
                Diag.event("the second display's window closed");
                handler.post(this::refresh);  // (closed by Android, the display still there and wanted: opened again)
            });
            p.show();
            presentation = p;
            Diag.event("the touch screen's window opened on the second display (\"" + display.getName() + "\")");
        } catch (WindowManager.InvalidDisplayException e) {
            Diag.event("the second display's window could not open: " + e.getMessage());
        } catch (RuntimeException e) {
            Diag.event("the second display's window could not open: " + e);
        }
    }

    private void close() {
        Presentation p = presentation;
        presentation = null;  // (its dismiss listener does nothing more)
        if (p == null) return;
        try {
            p.dismiss();
            Diag.event("the touch screen's window on the second display closed");
        } catch (RuntimeException e) {
            Log.w(TAG, "second display: " + e);
        }
    }

    /** No status or navigation bar over it, where that display has them. */
    private static void hideBars(Window window) {
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                window.setDecorFitsSystemWindows(false);
                WindowInsetsController c = window.getInsetsController();
                if (c != null) {
                    c.hide(WindowInsets.Type.systemBars());
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "second display bars: " + e);
        }
    }

    private void surface(Surface surface, int width, int height) {
        if (!nativeOk) return;
        try {
            nativeSurface(surface, width, height);
        } catch (UnsatisfiedLinkError e) {
            nativeOk = false;
            Log.w(TAG, "second display: " + e);
        }
    }

    /** The touch screen has one finger: the first one down, until it is lifted. */
    private void touch(View v, MotionEvent e) {
        if (!nativeOk) return;
        int action;
        int index;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointer = e.getPointerId(0);
                action = 0;
                index = 0;
                break;
            case MotionEvent.ACTION_MOVE:
                index = e.findPointerIndex(pointer);
                if (pointer < 0 || index < 0) return;
                action = 1;
                break;
            case MotionEvent.ACTION_POINTER_UP:
                index = e.getActionIndex();
                if (e.getPointerId(index) != pointer) return;
                action = 2;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                index = e.findPointerIndex(pointer);
                if (pointer < 0) return;
                if (index < 0) index = 0;
                action = 2;
                break;
            default:
                return;
        }
        if (action == 2) pointer = -1;
        try {
            nativeTouch(action, e.getX(index), e.getY(index));
        } catch (UnsatisfiedLinkError err) {
            nativeOk = false;
        }
    }
}

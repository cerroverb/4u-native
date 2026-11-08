package com.mh4u.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;

/**
 * El reinicio de "Guardar y reiniciar" (GameActivity.restartApp): una actividad sin interfaz en su propio proceso
 * (":phoenix", AndroidManifest.xml). Espera a que el proceso del juego termine su exit(0) , lo mata si tarda demasiado, lanza la app otra vez y se va.
 */
public class PhoenixActivity extends Activity {
    static final String EXTRA_PID = "com.mh4u.native.PID";
    private static final String TAG = "MH4U";
    // (al menos MIN_WAIT_MS aunque /proc ya no lo muestre; más de MAX_WAIT_MS: se lo mata)
    private static final long MIN_WAIT_MS = 300, MAX_WAIT_MS = 3000, POLL_MS = 50;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private int pid;
    private long start;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        pid = getIntent().getIntExtra(EXTRA_PID, -1);
        start = SystemClock.uptimeMillis();
        handler.postDelayed(this::relaunch, MIN_WAIT_MS);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {}  // (la espera no se corta: el juego ya se está cerrando)

    private void relaunch() {
        boolean alive = pid > 0 && new File("/proc/" + pid).exists();  // (mismo uid: visible pese a hidepid)
        if (alive && SystemClock.uptimeMillis() - start < MAX_WAIT_MS) {
            handler.postDelayed(this::relaunch, POLL_MS);
            return;
        }
        if (pid > 0 && pid != Process.myPid()) {
            if (alive) Log.w(TAG, "phoenix: el proceso " + pid + " no terminó a tiempo: se lo mata");
            Process.killProcess(pid);
        }
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(launch);
            Log.i(TAG, "phoenix: app lanzada de nuevo");
        } else {
            Log.e(TAG, "phoenix: sin intent de lanzamiento");
        }
        finish();
        Runtime.getRuntime().exit(0);
    }
}

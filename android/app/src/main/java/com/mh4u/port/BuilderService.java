package com.mh4u.port;

import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Builds the game's code library on the phone (BuilderActivity starts it): the tools first (BuilderTools), then mh4n's
 * tools/android/phone_build.py, the PC pipeline itself (tools/build_game.py --native --android), run by the downloaded
 * CPython through the app's launchers (app_native/builder/toolrun.c). A foreground service with a notification and a
 * partial wake lock: it goes on with the screen off or another app in front.
 *
 * Heat and battery (watchHeat): while the phone is warm (thermal status moderate) only two compilers run; severe or
 * worse, or the battery at 15 % or less and not charging, the build waits (SIGSTOP to its processes) and goes on when the
 * phone has cooled down to light (or is charging). A thermal status the battery's temperature and the thermal headroom
 * both contradict (a cool phone) is taken as a wrong reading, and a battery at 47 degrees pauses it whatever the status.
 * A build that says nothing for a minute gets a line in its log each minute with what its processes are doing (quiet).
 * Cancelled, closed by Android or cut by a reboot, it keeps its work folder: the next run goes on
 * from the objects already compiled (phone_build.py).
 *
 * The state (Job) lives in this process; state.txt in the builder's folder says a run was going on, so a later start can
 * say it was interrupted. The log of every run is builder_log.txt in the app's folder (Share log takes it). The same
 * service as MHGU for Android's.
 */
public class BuilderService extends Service {
    static final String TAG = "MH4U";
    static final String EXTRA_GAME = "game", EXTRA_UPDATE = "update", EXTRA_MOD = "mod", EXTRA_MOD_FLAG = "mod_flag",
            EXTRA_DEST = "dest";
    static final String ACTION_CANCEL = "cancel";
    private static final String CHANNEL = "builder";
    private static final int NOTE = 46;
    static final String LIBRARY = GameFolderActivity.LIBRARY;

    /** The stages, in order; their names are phone_build.py's (@@stage), "tools" is the downloads. */
    static final String[] STAGES = {"tools", "extract", "recompile", "compile", "install"};

    /** What a run is doing, for the screen and the notification. */
    static final class Job {
        volatile String status = "idle";  // idle, running, warm, paused-hot, paused-battery, done, failed, cancelled
        volatile String stage = "tools", detail = "";
        volatile long done, total, started, stageStarted;
        volatile String errorStage = "", errorText = "", library = "";
        volatile String folder = "";  // (the game folder of the run: the screen opened from the notification shows it)
        volatile float batteryC = Float.NaN;  // (the battery's temperature at the last heat check, for the screen)
        volatile long lastLine;  // (when the build last said something: quiet)
        final ArrayDeque<String> tail = new ArrayDeque<>();
        boolean active() { return status.equals("running") || status.equals("warm") || status.startsWith("paused"); }
    }

    static final Job job = new Job();
    private static final List<Runnable> listeners = new ArrayList<>();
    private static final Handler main = new Handler(Looper.getMainLooper());

    static void listen(Runnable r) { synchronized (listeners) { listeners.add(r); } }
    static void unlisten(Runnable r) { synchronized (listeners) { listeners.remove(r); } }
    private static void changed() {
        main.post(() -> {
            List<Runnable> copy;
            synchronized (listeners) { copy = new ArrayList<>(listeners); }
            for (Runnable r : copy) r.run();
        });
    }

    private volatile Process process;
    private static volatile int pythonPid;  // (phone_build.py's @@pid: the root of the build's processes)
    private volatile boolean cancel;
    private PowerManager.WakeLock wake;
    private static volatile Thread worker;  // (static: busy() asks for it from the screen)
    /** Start over or Delete the tools at work (clear): no run starts meanwhile, and the screen's buttons wait. */
    static volatile boolean clearing;
    private long lastNote;

    static File stateFile(Context c) { return new File(BuilderTools.root(c), "state.txt"); }
    static File logFile(Context c) { return new File(AppHome.home(c), "builder_log.txt"); }
    static File work(Context c) { return new File(BuilderTools.root(c), "work"); }

    /**
     * A run, or its thread still finishing (finish() writes state.txt after the status changes), or a clear: the build's
     * folder is in use and is not to be deleted.
     */
    static boolean busy() {
        Thread w = worker;
        return job.active() || clearing || (w != null && w.isAlive());
    }

    /** The left-over files of a run being deleted are renamed to this first (one step), so a run never sees half of them. */
    static final String TRASH = "trash-";

    /**
     * Something a build left that Start over clears: its work folder, a record of a run that did not end well
     * (state.txt), a half download, or a run of this process that failed or was cancelled.
     */
    static boolean halfDone(Context c) {
        File root = BuilderTools.root(c);
        String[] work = work(c).list();
        if (work != null && work.length > 0) return true;
        String state = BuilderTools.readText(stateFile(c));
        if (state.startsWith("running") || state.startsWith("failed") || state.startsWith("cancelled")) return true;
        String[] parts = new File(root, "downloads").list();
        if (parts != null && parts.length > 0) return true;
        String[] names = root.list();
        if (names != null) for (String n : names) if (n.startsWith(TRASH)) return true;
        return job.status.equals("failed") || job.status.equals("cancelled");
    }

    /** What Start over deletes, in bytes: the work folder, the temporary one, the half downloads and old trash. */
    static long halfDoneSize(Context c) {
        File root = BuilderTools.root(c);
        long n = BuilderTools.sizeOf(work(c)) + BuilderTools.sizeOf(new File(root, "tmp")) + BuilderTools.sizeOf(new File(root, "downloads"));
        String[] names = root.list();
        if (names != null) for (String t : names) if (t.startsWith(TRASH)) n += BuilderTools.sizeOf(new File(root, t));
        return n;
    }

    /**
     * Start over (BuilderActivity, off the main thread, never while busy()): what a half-done build left is deleted, so
     * the next one starts from the beginning and no file of the old one is used again. In an order that leaves nothing
     * half valid if the app is closed in the middle: first the resume stamp (work/inputs.txt: without it phone_build.py
     * translates everything again), then the run's record (state.txt, last_failure.txt), then the work folder and the
     * temporary one, each renamed aside in one step before it is deleted (a run never sees half of one; what a rename
     * left is deleted at the next clear or run: sweep), the half downloads, and a library cut short while it was copied
     * into the game folder (`<library>.part`, `<id>.part`: phone_build.py's install renames whole files only, so the
     * library there is the old one or the new one, and stays). `tools`: the downloaded tools too (the whole folder).
     */
    static void startOver(Context c, String dest, boolean tools) {
        File root = BuilderTools.root(c);
        // (the game folder of the run state.txt records, besides the one on the screen: its half-copied library too)
        String[] state = BuilderTools.readText(stateFile(c)).split(String.valueOf((char) 10));  // (its lines: readText trimmed it)
        String stateDest = state.length >= 5 ? state[4].trim() : "";
        new File(work(c), "inputs.txt").delete();
        new File(work(c), "inputs.txt.part").delete();
        stateFile(c).delete();
        new File(root, "last_failure.txt").delete();
        if (tools) {
            deleteAside(root);
        } else {
            deleteAside(work(c));
            deleteAside(new File(root, "tmp"));
            deleteAside(new File(root, "downloads"));
        }
        sweep(c);
        for (String d : new String[] {dest, stateDest}) {
            if (d == null || d.isEmpty()) continue;
            new File(d, LIBRARY + ".part").delete();
            new File(d, LIBRARY.substring(0, LIBRARY.length() - 3) + ".id.part").delete();
        }
        Log.i(TAG, "builder: " + (tools ? "the tools and the last build deleted" : "started over: the last build's files deleted, the tools kept"));
    }

    /** A folder renamed beside itself (trash-...) and then deleted. */
    private static void deleteAside(File f) {
        if (!f.exists()) return;
        File aside = new File(f.getParentFile(), TRASH + f.getName() + "-" + System.nanoTime());
        BuilderTools.deleteTree(f.renameTo(aside) ? aside : f);
    }

    /** What an earlier clear left renamed aside (the app was closed while it deleted): deleted now. */
    static void sweep(Context c) {
        for (File dir : new File[] {c.getFilesDir(), BuilderTools.root(c)}) {
            File[] kids = dir.listFiles();
            if (kids != null) for (File k : kids) if (k.getName().startsWith(TRASH)) BuilderTools.deleteTree(k);
        }
    }

    /** The screen back to "nothing built yet" after a clear: the last run's state and its notification are gone. */
    static void forget(Context c) {
        job.status = "idle";
        job.stage = "tools";
        job.detail = job.errorStage = job.errorText = job.library = "";
        job.done = job.total = 0;
        synchronized (job.tail) { job.tail.clear(); }
        NotificationManager nm = (NotificationManager) c.getSystemService(NOTIFICATION_SERVICE);
        if (nm != null) nm.cancel(NOTE + 1);
        changed();
    }

    /** A run that was going on when the app was closed (state.txt says running): what it was building, or null. */
    static String interrupted(Context c) {
        if (job.active()) return null;
        String s = BuilderTools.readText(stateFile(c));
        return s.startsWith("running") ? s : null;
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startForegroundNote();  // (first, whatever the intent: a service started as a foreground one must say so at once)
        if (intent != null && ACTION_CANCEL.equals(intent.getAction())) {
            cancel();
            if (!job.active()) stopSelf();
            return START_NOT_STICKY;
        }
        if (intent == null || job.active() || clearing) return START_NOT_STICKY;
        final String game = intent.getStringExtra(EXTRA_GAME), update = intent.getStringExtra(EXTRA_UPDATE);
        final String mod = intent.getStringExtra(EXTRA_MOD), dest = intent.getStringExtra(EXTRA_DEST);
        final String modFlag = intent.getStringExtra(EXTRA_MOD_FLAG);
        synchronized (job.tail) { job.tail.clear(); }
        job.status = "running";
        job.stage = "tools";
        job.detail = "";
        job.done = job.total = 0;
        job.errorStage = job.errorText = job.library = "";
        job.folder = dest == null ? "" : dest;
        job.started = job.stageStarted = System.currentTimeMillis();
        cancel = false;
        changed();
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "mh4u:builder");
        wake.acquire(6 * 3600 * 1000L);
        worker = new Thread(() -> run(game, update, modFlag, mod, dest), "builder");
        worker.start();
        return START_NOT_STICKY;
    }

    @Override
    public void onTimeout(int startId, int fgsType) {  // (Android 15: a data-sync service's daily time is over; resumable)
        Log.w(TAG, "builder: the system's time limit for this service: stopping (the next run goes on from here)");
        cancel();
    }

    private void cancel() {
        cancel = true;
        Process p = process;
        if (p != null) signalTree(p, 9);
    }

    private void run(String game, String update, String modFlag, String mod, String dest) {
        try (OutputStream log = new FileOutputStream(logFile(this), false)) {
            say(log, "MH4U builder, " + new java.util.Date() + ", " + Build.MANUFACTURER + " " + Build.MODEL + ", Android "
                    + Build.VERSION.RELEASE + ", " + Runtime.getRuntime().availableProcessors() + " cores, "
                    + (totalMemory() >> 20) + " MB");
            say(log, "game " + game + "\nupdate " + update + "\nmods " + modFlag + " " + mod + "\ndest " + dest);
            say(log, "heat at the start: " + heatReading());  // (one reading in every log, a cool phone's too)
            sweep(this);  // (what a clear the app was closed in left)
            BuilderTools.writeText(stateFile(this), "running\n" + game + "\n" + update + "\n" + (mod == null ? "" : mod) + "\n" + dest);
            BuilderTools.ensure(this, new BuilderTools.Progress() {
                @Override
                public void step(String what, long done, long total) {
                    job.detail = what;
                    job.done = done;
                    job.total = total;
                    note();
                    changed();
                }

                @Override
                public boolean cancelled() { return cancel; }

                @Override
                public void log(String line) { say(log, line); }
            });
            say(log, "tools: " + BuilderTools.PYTHON.name + " " + BuilderTools.PYTHON.version + ", " + BuilderTools.ZIG.name + " "
                    + BuilderTools.ZIG.version);
            int code = build(log, game, update, modFlag, mod, dest);
            if (cancel) finish("cancelled", "", "");
            else if (code == 0 && !job.library.isEmpty()) finish("done", "", "");
            else if (code == 137 || code == 9) finish("failed", "memory", "a process was closed by Android (most likely for memory)");
            else if (job.errorStage.isEmpty()) finish("failed", job.stage, "exit " + code);
            else finish("failed", job.errorStage, job.errorText);
        } catch (BuilderTools.Cancelled e) {
            finish("cancelled", "", "");
        } catch (IOException e) {
            Log.e(TAG, "builder", e);
            String text = String.valueOf(e.getMessage());
            boolean space = text.contains("ENOSPC") || text.contains("No space");
            finish("failed", space ? "space" : job.stage.equals("tools") ? "download" : job.stage, text);
        } catch (Exception e) {
            Log.e(TAG, "builder", e);
            finish("failed", job.stage, String.valueOf(e));
        }
    }

    /** phone_build.py through libpyrun.so; its @@ lines move the stages. */
    private int build(OutputStream log, String game, String update, String modFlag, String mod, String dest)
            throws IOException, InterruptedException {
        String libs = getApplicationInfo().nativeLibraryDir;
        File scripts = BuilderTools.scripts(this), work = work(this), py = BuilderTools.python(this);
        List<String> cmd = new ArrayList<>();
        cmd.add(libs + "/libpyrun.so");
        cmd.add("-u");
        cmd.add(new File(scripts, "tools/android/phone_build.py").getAbsolutePath());
        cmd.add("--update"); cmd.add(update);
        if (game != null) { cmd.add("--game"); cmd.add(game); }
        if (mod != null) { cmd.add(modFlag != null ? modFlag : "--mod"); cmd.add(mod); }  // (--game-mods / --user-mods)
        cmd.add("--work"); cmd.add(work.getAbsolutePath());
        cmd.add("--dest"); cmd.add(dest);
        cmd.add("--cc"); cmd.add(libs + "/libzigcc.so");
        cmd.add("--jobs"); cmd.add(String.valueOf(jobs()));
        cmd.add("--clean");
        String want = GameFolderActivity.wantedId(this);
        if (!want.isEmpty()) { cmd.add("--expect-id"); cmd.add(want); }
        ProcessBuilder pb = new ProcessBuilder(cmd).redirectErrorStream(true).directory(work.getParentFile());
        Map<String, String> env = pb.environment();
        File tmp = new File(BuilderTools.root(this), "tmp");
        tmp.mkdirs();
        env.put("PYTHONHOME", py.getAbsolutePath());
        env.put("LD_LIBRARY_PATH", new File(py, "lib").getAbsolutePath());
        env.put("PYTHONUNBUFFERED", "1");
        env.put("PYTHONNOUSERSITE", "1");
        env.put("PYTHONUTF8", "1");
        env.put("HOME", BuilderTools.root(this).getAbsolutePath());
        env.put("TMPDIR", tmp.getAbsolutePath());
        env.put("BUILDER_ZIG", BuilderTools.zig(this).getAbsolutePath());
        env.put("ZIG_LIB_DIR", BuilderTools.zigLib(this).getAbsolutePath());
        env.put("ZIG_GLOBAL_CACHE_DIR", new File(work, "zig-cache").getAbsolutePath());
        env.put("ZIG_LOCAL_CACHE_DIR", new File(work, "zig-cache").getAbsolutePath());
        say(log, "run: " + String.join(" ", cmd));
        stage("extract");
        pythonPid = 0;
        job.lastLine = System.currentTimeMillis();
        process = pb.start();
        Thread watch = new Thread(() -> watchHeat(log), "builder-heat");
        watch.start();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                job.lastLine = System.currentTimeMillis();
                say(log, line);
                if (line.startsWith("@@")) at(line.substring(2));
            }
        }
        int code = process.waitFor();
        process = null;
        watch.interrupt();
        say(log, "exit " + code);
        return code;
    }

    /** One @@ line of phone_build.py. */
    private void at(String line) {
        String[] w = line.split(" ", 4);
        switch (w[0]) {
            case "stage":
                if (w.length > 1) stage(w[1]);
                break;
            case "pid":
                try { pythonPid = Integer.parseInt(w[1].trim()); } catch (RuntimeException ignored) { }
                break;
            case "progress":
                if (w.length > 3) {
                    try {
                        if (w[1].equals("recompile") || w[1].equals("compile")) {
                            if (!job.stage.equals(w[1])) stage(w[1]);
                            job.done = Long.parseLong(w[2]);
                            job.total = Long.parseLong(w[3].trim());
                        }
                    } catch (NumberFormatException ignored) {
                    }
                }
                break;
            case "error":
                job.errorStage = w.length > 1 ? w[1] : job.stage;
                job.errorText = line.substring(Math.min(line.length(), 6 + job.errorStage.length() + 1)).trim();
                break;
            case "done":
                job.library = w.length > 1 ? line.substring(5).trim() : "";
                break;
            default:
                break;
        }
        note();
        changed();
    }

    /** phone_build.py's stages, folded into the five the screen shows. */
    private void stage(String s) {
        String shown;
        switch (s) {
            case "mods": case "extract": case "version": shown = "extract"; break;
            case "recompile": case "shaders": shown = "recompile"; break;
            case "compile": case "link": shown = "compile"; break;
            case "install": shown = "install"; break;
            default: shown = job.stage;
        }
        job.detail = s;
        if (!shown.equals(job.stage)) {
            job.stage = shown;
            job.stageStarted = System.currentTimeMillis();
            job.done = job.total = 0;
        }
        changed();
    }

    private void say(OutputStream log, String line) {
        try {
            synchronized (log) {  // (the build's lines and the heat checks' come from two threads)
                log.write((line + "\n").getBytes(StandardCharsets.UTF_8));
                log.flush();
            }
        } catch (IOException ignored) {
        }
        if (!line.startsWith("@@")) {
            synchronized (job.tail) {
                job.tail.addLast(line);
                while (job.tail.size() > 60) job.tail.removeFirst();
            }
        }
    }

    private void finish(String status, String errorStage, String errorText) {
        job.status = status;
        try {  // (a run that stopped while compiling: the next one uses half as many compilers)
            File failure = new File(BuilderTools.root(this), "last_failure.txt");
            if (status.equals("failed") && (errorStage.equals("compile") || errorStage.equals("link") || errorStage.equals("memory")))
                BuilderTools.writeText(failure, "compile " + lastJobs);
            else if (status.equals("done")) failure.delete();
        } catch (IOException ignored) {
        }
        if (!errorStage.isEmpty()) {
            job.errorStage = errorStage;
            job.errorText = errorText;
        }
        try {
            BuilderTools.writeText(stateFile(this), status);
        } catch (IOException ignored) {
        }
        Log.i(TAG, "builder: " + status + (errorStage.isEmpty() ? "" : " in " + errorStage + ": " + errorText));
        if (wake != null && wake.isHeld()) wake.release();
        main.post(() -> {
            NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            stopForeground(STOP_FOREGROUND_REMOVE);
            if (nm != null && !status.equals("cancelled")) nm.notify(NOTE + 1, notification(true));
            stopSelf();
        });
        changed();
    }

    /**
     * How many compilers at once: every core, but no more than the free memory holds (a compiler takes up to about
     * 300 MB on the largest files; the translator's 1.4 GB is given back before they start: phone_build.py), and half
     * of the last run's when that one stopped while compiling (most likely Android closed a compiler for memory).
     */
    int jobs() {
        int cores = Runtime.getRuntime().availableProcessors();
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        if (am != null) am.getMemoryInfo(mi);
        long free = mi.availMem > 0 ? mi.availMem >> 20 : 1024;
        int jobs = (int) Math.max(1, Math.min(cores, (free - 300) / 300));
        String last = BuilderTools.readText(new File(BuilderTools.root(this), "last_failure.txt"));
        if (last.startsWith("compile ")) {
            try {
                jobs = Math.max(1, Math.min(jobs, Integer.parseInt(last.substring(8).trim()) / 2));
            } catch (NumberFormatException ignored) {
            }
        }
        Log.i(TAG, "builder: " + jobs + " compilers (" + cores + " cores, " + free + " MB free)");
        lastJobs = jobs;
        return jobs;
    }

    private int lastJobs;

    long totalMemory() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        if (am != null) am.getMemoryInfo(mi);
        return mi.totalMem;
    }

    // ---- heat and battery
    /** While warm (thermal status moderate) at most this many compilers run; the others wait, stopped. */
    static final int WARM_COMPILERS = 2;
    /** The battery at this temperature (degrees C) or above: too hot, whatever the thermal status says (a phone that
     *  reports none, as before Android 10, or one that never raises it); the build goes on below BATTERY_COOLED. */
    static final float BATTERY_HOT = 47f, BATTERY_COOLED = 42f;
    /** A thermal status of severe or worse with the battery below BATTERY_COOL and the thermal headroom, where the phone
     *  gives one, below HEADROOM_HOT, is taken as a wrong reading: some phones report a high status whatever their heat
     *  (a player's in a freezer paused "too hot", 2026-11-15). It counts as warm then: two compilers, the build goes on. */
    static final float BATTERY_COOL = 32f, HEADROOM_HOT = 0.85f;
    /** A build that has said nothing for this long gets a line in its log, and one more each time again (quiet). */
    static final long QUIET_MS = 60_000;

    /** A severe or worse thermal status on a phone its battery and headroom say is cool: a wrong reading. */
    static boolean wrongReading(int status, float headroom, float batteryC) {
        boolean cool = batteryC < BATTERY_COOL && !(headroom >= HEADROOM_HOT);  // (NaN: false; an unknown headroom agrees)
        return status >= PowerManager.THERMAL_STATUS_SEVERE && cool;
    }

    /**
     * The heat rule (watchHeat): what the build is to be, from what it was and one reading. Light or cooler: running.
     * Moderate ("warm"): only WARM_COMPILERS compilers run, the others stopped until it cools to none. Severe or worse:
     * the whole build waits until the phone is back to light, unless the battery and the headroom say it is cool (a
     * wrong reading, taken as warm; a paused build goes on then too); the battery at BATTERY_HOT or above waits too,
     * until below BATTERY_COOLED. 15 % of battery or less, not charging: it waits until it charges (or passes 20 %).
     */
    static String heatState(String was, int status, float headroom, float batteryC, int level, boolean charging) {
        boolean cool = batteryC < BATTERY_COOL && !(headroom >= HEADROOM_HOT);
        int thermal = wrongReading(status, headroom, batteryC) ? PowerManager.THERMAL_STATUS_MODERATE : status;
        if (batteryC >= BATTERY_HOT) thermal = Math.max(thermal, PowerManager.THERMAL_STATUS_SEVERE);
        if (was.equals("paused-hot"))  // (until back to light, or cool by its battery; and the battery below BATTERY_COOLED)
            return thermal < PowerManager.THERMAL_STATUS_SEVERE && (thermal <= PowerManager.THERMAL_STATUS_LIGHT || cool)
                    && !(batteryC >= BATTERY_COOLED) ? "running" : was;
        if (was.equals("paused-battery")) return charging || level > 20 ? "running" : was;
        if (thermal >= PowerManager.THERMAL_STATUS_SEVERE) return "paused-hot";
        if (!charging && level <= 15) return "paused-battery";
        if (thermal == PowerManager.THERMAL_STATUS_MODERATE) return "warm";
        if (was.equals("warm") && thermal == PowerManager.THERMAL_STATUS_LIGHT) return "warm";  // (until it cools to none)
        return "running";
    }

    /** The battery's temperature in an ACTION_BATTERY_CHANGED intent's tenths of a degree: NaN for none (or the 0 some phones give for none). */
    static float batteryCelsius(int tenths) {
        return tenths <= -400 || tenths >= 1500 || tenths == 0 ? Float.NaN : tenths / 10f;
    }

    /**
     * One reading of what the heat rule goes by, as its log lines say it: the thermal status, the headroom, the battery's
     * temperature, level and charging. Written at the start of every build and at each of its stages (builder_log.txt):
     * the rule's own lines come only when its state changes, so a phone that stayed cool had none (1.0.1, ph-4u).
     */
    String heatReading() {
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        int status = Build.VERSION.SDK_INT >= 29 && pm != null ? pm.getCurrentThermalStatus() : -1;
        float headroom = Float.NaN;
        if (Build.VERSION.SDK_INT >= 30 && pm != null) {
            try { headroom = pm.getThermalHeadroom(0); } catch (RuntimeException ignored) { }
        }
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        int level = battery == null ? -1 : battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) * 100
                / Math.max(1, battery.getIntExtra(BatteryManager.EXTRA_SCALE, 100));
        boolean charging = battery != null && battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
        float batteryC = batteryCelsius(battery == null ? 0 : battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0));
        return (status < 0 ? "thermal status unknown (before Android 10)" : "thermal status " + status)
                + (Float.isNaN(headroom) ? "" : String.format(Locale.ROOT, ", headroom %.2f", headroom))
                + (Float.isNaN(batteryC) ? ", battery temperature unknown" : String.format(Locale.ROOT, ", battery %.1f C", batteryC))
                + (level < 0 ? "" : ", battery " + level + "%") + (charging ? ", charging" : "")
                + "; the build's state " + job.status;
    }

    /**
     * Every 5 s: the phone's thermal status (PowerManager), its thermal headroom (Android 11 and later, NaN where the phone
     * doesn't say) and the battery, its level and its temperature, through the heat rule (heatState); every change of
     * state goes to the build's log with what was read. And a build quiet for QUIET_MS gets a line in its log (quiet).
     */
    private void watchHeat(OutputStream log) {
        // (a check that fails, an exception of the system's, must not leave the build's processes stopped for good, as a
        // paused build's would be with this thread gone: they go on, and the checks start again)
        while (process != null && !Thread.currentThread().isInterrupted()) {
            try {
                watchHeatLoop(log);
                return;
            } catch (RuntimeException e) {
                Log.w(TAG, "builder: heat check", e);
                say(log, "heat check failed (" + e + "): the build goes on");
                Process p = process;
                if (p != null) signalTree(p, 18);  // SIGCONT
                if (job.active() && !job.status.equals("running")) {
                    job.status = "running";
                    note();
                    changed();
                }
            }
        }
    }

    private void watchHeatLoop(OutputStream log) {
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        boolean saidWrong = false;
        String saidStage = "";  // (a reading at each stage: heatReading)
        long saidQuiet = 0;
        try {
            while (!Thread.currentThread().isInterrupted()) {
                Thread.sleep(5000);
                Process p = process;
                if (p == null) return;
                if (!job.stage.equals(saidStage)) {
                    saidStage = job.stage;
                    say(log, "heat at the stage " + saidStage + ": " + heatReading());
                }
                int status = Build.VERSION.SDK_INT >= 29 ? pm.getCurrentThermalStatus() : 0;
                float headroom = Float.NaN;
                if (Build.VERSION.SDK_INT >= 30) {
                    try { headroom = pm.getThermalHeadroom(0); } catch (RuntimeException ignored) { }
                }
                Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
                int level = battery == null ? 100 : battery.getIntExtra(BatteryManager.EXTRA_LEVEL, 100) * 100
                        / Math.max(1, battery.getIntExtra(BatteryManager.EXTRA_SCALE, 100));
                boolean charging = battery != null && battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
                float batteryC = batteryCelsius(battery == null ? 0 : battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0));
                job.batteryC = batteryC;
                boolean wrong = wrongReading(status, headroom, batteryC);
                String reading = "thermal status " + status
                        + (Float.isNaN(headroom) ? "" : String.format(Locale.ROOT, ", headroom %.2f", headroom))
                        + (Float.isNaN(batteryC) ? ", battery temperature unknown" : String.format(Locale.ROOT, ", battery %.1f C", batteryC))
                        + ", battery " + level + "%" + (charging ? ", charging" : "");
                if (wrong != saidWrong) {
                    saidWrong = wrong;
                    say(log, wrong ? "heat: " + reading + ": the status is taken as a wrong reading (the phone is cool): it goes on, slower"
                                   : "heat: " + reading + ": the status agrees with the phone's heat again");
                }
                String was = job.status, want = heatState(was, status, headroom, batteryC, level, charging);
                if (!want.equals(was)) {
                    Log.i(TAG, "builder: " + want + " (" + reading + ")");
                    say(log, "heat: " + want + " (" + reading + ")");
                    job.status = want;
                    note();
                    changed();
                }
                // (every tick: new compilers start all the time; a stopped one stays stopped until its turn)
                List<Integer> all = tree(p);
                java.util.Set<Integer> leaves = leaves(all);
                if (want.startsWith("paused")) {
                    for (int pid : all) android.os.Process.sendSignal(pid, 19);  // SIGSTOP, the parent first
                } else {
                    int allowed = want.equals("warm") ? WARM_COMPILERS : Integer.MAX_VALUE, n = 0;
                    for (int i = all.size() - 1; i >= 0; i--) {  // (the children first)
                        int pid = all.get(i);
                        boolean leaf = leaves.contains(pid);
                        android.os.Process.sendSignal(pid, !leaf || n++ < allowed ? 18 : 19);  // SIGCONT / SIGSTOP
                    }
                }
                long now = System.currentTimeMillis(), quiet = now - job.lastLine;
                if (quiet >= QUIET_MS && now - saidQuiet >= QUIET_MS) {
                    saidQuiet = now;
                    say(log, quiet(all, quiet));
                }
            }
        } catch (InterruptedException ignored) {
        }
    }

    /**
     * A build that has said nothing for a while (QUIET_MS): a line for its log with what each of its processes is doing,
     * so a log of one that seems stuck shows whether it works (R, its CPU time growing), waits for memory to come back
     * from the swap (D, its major page faults growing) or was stopped (T), and how much memory the phone has left.
     */
    static String quiet(List<Integer> pids, long quietMs) {
        StringBuilder s = new StringBuilder("quiet for " + quietMs / 1000 + " s:");
        for (int pid : pids)
            s.append(' ').append(procLine(pid, BuilderTools.readText(new File("/proc/" + pid + "/stat")),
                                          BuilderTools.readText(new File("/proc/" + pid + "/status")))).append(';');
        return s.append(' ').append(memLine(BuilderTools.readText(new File("/proc/meminfo")))).toString();
    }

    /** "pid 123 R, 45 s of CPU, 6 major faults, 1234 MB (56 MB swapped)" from its /proc stat and status; "pid 123 gone". */
    static String procLine(int pid, String stat, String status) {
        int close = stat.lastIndexOf(')');
        if (close < 0 || close + 2 > stat.length()) return "pid " + pid + " gone";
        String[] f = stat.substring(close + 2).split(" ");
        if (f.length < 13) return "pid " + pid + " gone";
        long majflt, ticks;
        try {
            majflt = Long.parseLong(f[9]);
            ticks = Long.parseLong(f[11]) + Long.parseLong(f[12]);  // (utime + stime, in the kernel's 100 ticks a second)
        } catch (NumberFormatException e) {
            return "pid " + pid + " gone";
        }
        return "pid " + pid + " " + f[0] + ", " + ticks / 100 + " s of CPU, " + majflt + " major faults, "
                + kb(status, "VmRSS:") / 1024 + " MB (" + kb(status, "VmSwap:") / 1024 + " MB swapped)";
    }

    /** "memory available 1234 MB, swap free 567 MB" from /proc/meminfo. */
    static String memLine(String meminfo) {
        return "memory available " + kb(meminfo, "MemAvailable:") / 1024 + " MB, swap free " + kb(meminfo, "SwapFree:") / 1024 + " MB";
    }

    /** The number of kB a /proc file gives for a name ("VmRSS:   1234 kB"), 0 when it has none. */
    static long kb(String text, String name) {
        for (String line : text.split("\n")) {
            if (!line.startsWith(name)) continue;
            String[] w = line.substring(name.length()).trim().split("\\s+");
            try {
                return Long.parseLong(w[0]);
            } catch (RuntimeException e) {
                return 0;
            }
        }
        return 0;
    }

    /** The build's processes, parents before their children: /proc, the app's own processes. */
    private static List<Integer> tree(Process p) {
        int root = pidOf(p);
        List<Integer> all = new ArrayList<>();
        if (root <= 0) return all;
        Map<Integer, List<Integer>> children = new HashMap<>();
        File[] procs = new File("/proc").listFiles();
        if (procs != null) for (File d : procs) {
            int pid;
            try { pid = Integer.parseInt(d.getName()); } catch (NumberFormatException e) { continue; }
            int parent = parentOf(pid);
            if (parent > 0) children.computeIfAbsent(parent, k -> new ArrayList<>()).add(pid);
        }
        ArrayDeque<Integer> todo = new ArrayDeque<>();
        todo.add(root);
        while (!todo.isEmpty()) {
            int pid = todo.poll();
            all.add(pid);
            List<Integer> k = children.get(pid);
            if (k != null) todo.addAll(k);
        }
        return all;
    }

    private static int parentOf(int pid) {
        String stat = BuilderTools.readText(new File("/proc/" + pid + "/stat"));
        int close = stat.lastIndexOf(')');
        if (close < 0 || close + 2 > stat.length()) return -1;
        String[] f = stat.substring(close + 2).split(" ");
        try {
            return f.length > 1 ? Integer.parseInt(f[1]) : -1;
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    /** The processes that started none (the compilers at work), the build's own first one apart. */
    private static java.util.Set<Integer> leaves(List<Integer> all) {
        java.util.Set<Integer> out = new java.util.HashSet<>(all.subList(Math.min(1, all.size()), all.size()));
        for (int pid : all) out.remove(parentOf(pid));
        return out;
    }

    /** A signal to the build's process and everything it started (the compilers). */
    private static void signalTree(Process p, int signal) {
        List<Integer> all = tree(p);
        if (all.isEmpty()) {
            if (signal == 9) p.destroyForcibly();
            return;
        }
        if (signal == 18) java.util.Collections.reverse(all);  // (stopping: the parent first; going on: the children first)
        for (int pid : all) android.os.Process.sendSignal(pid, signal);
    }

    /** The build's first process: what it said (@@pid), else what Android's Process says of itself ("Process[pid=N, ..."). */
    private static int pidOf(Process p) {
        if (pythonPid > 0) return pythonPid;
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("pid=(\\d+)").matcher(String.valueOf(p));
        return m.find() ? Integer.parseInt(m.group(1)) : -1;
    }

    // ---- the notification
    private void startForegroundNote() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm != null && Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL) == null) {
            NotificationChannel ch = new NotificationChannel(CHANNEL, Look.es() ? "Crear el código del juego" : "Build the game code",
                    NotificationManager.IMPORTANCE_LOW);
            nm.createNotificationChannel(ch);
        }
        if (Build.VERSION.SDK_INT >= 29)
            startForeground(NOTE, notification(false), Build.VERSION.SDK_INT >= 34 ? ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC : 0);
        else
            startForeground(NOTE, notification(false));
    }

    private void note() {
        long now = System.currentTimeMillis();
        if (now - lastNote < 1000) return;
        lastNote = now;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm != null && job.active()) nm.notify(NOTE, notification(false));
    }

    private Notification notification(boolean ended) {
        boolean es = Look.es();
        Intent open = new Intent(this, BuilderActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        b.setSmallIcon(R.mipmap.ic_launcher).setContentIntent(pi).setOnlyAlertOnce(true);
        if (ended) {
            b.setContentTitle(job.status.equals("done") ? (es ? "El código del juego está listo" : "The game code is ready")
                                                        : (es ? "No se pudo crear el código del juego" : "The game code couldn't be built"))
             .setContentText(job.status.equals("done") ? (es ? "Toca para jugar." : "Tap to play.")
                                                       : (es ? "Toca para ver por qué." : "Tap to see why."))
             .setAutoCancel(true);
            return b.build();
        }
        int percent = BuilderText.percent(job);
        b.setContentTitle(es ? "Creando el código del juego" : "Building the game code")
         .setContentText(BuilderText.status(job, es))
         .setOngoing(true)
         .setProgress(100, Math.max(0, percent), percent < 0);
        Intent stop = new Intent(this, BuilderService.class).setAction(ACTION_CANCEL);
        b.addAction(new Notification.Action.Builder(null, es ? "Cancelar" : "Cancel", Build.VERSION.SDK_INT >= 26
                ? PendingIntent.getForegroundService(this, 1, stop, PendingIntent.FLAG_IMMUTABLE)
                : PendingIntent.getService(this, 1, stop, PendingIntent.FLAG_IMMUTABLE)).build());
        return b.build();
    }
}

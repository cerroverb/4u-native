package com.mh4u.port;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.app.GameManager;
import android.app.usage.UsageStatsManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ConfigurationInfo;
import android.content.pm.FeatureInfo;
import android.content.pm.InstallSourceInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.content.res.Configuration;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Environment;
import android.os.PowerManager;
import android.os.Process;
import android.os.StatFs;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.Inet4Address;
import java.net.Inet6Address;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * What the log says about the phone and the app (1.0.1, as the MHGU port's Diag). The owner: "que se pida hasta de
 * sobra": one log should answer most questions, so it asks for more than seems needed, once, off the game's threads.
 *
 * - snapshot(): the app (version, signature, who installed it, its updates), the phone (build, security patch, ABIs,
 *   CPU cores and clocks, memory, storage), the screen, the power (battery, its temperature, charging, power saving, the
 *   thermal status and headroom, the standby bucket, the game mode), the network's kind (Wi-Fi or mobile, metered,
 *   IPv4 / IPv6, MTU: never its name or an address), the GPU the system reports and how the app's last processes ended
 *   (Android's own record: a crash, low memory, an ANR...). GameActivity sends it into log.txt as the game starts
 *   (nativeLine), and Share log puts it at the top of what it shares.
 * - event(): a line into log.txt with the session's time (memory trimmed, the thermal status, power saving, the network
 *   changing, the window's mode).
 * - install(): every start: the app's versions seen (an update history), and a Java crash's stack written to
 *   java_crash.txt, which Share log takes.
 * Nothing that names the player: no account, no e-mail, no location, no network name, no address, no save.
 */
final class Diag {
    private static final String TAG = "MH4U";
    // (the release key's certificate, SHA-256: a build signed with another key is someone else's)
    private static final String OFFICIAL_CERT = "8de88d5c5cf89397845ad893b5aab7e7966474ecfd66db757838c32a137cc403";
    static final String CRASH_FILE = "java_crash.txt";

    private Diag() {}

    // ---- into log.txt (app_native/diag.cpp): false while the log is not the game's yet, or no game library
    private static native boolean nativeLine(String line);
    private static native boolean nativeEvent(String text);
    private static native void nativeBattery(int tenths);  // (the minute's heat line, where sysfs can't be read)

    static boolean line(String s) {
        try {
            return nativeLine(s);
        } catch (UnsatisfiedLinkError e) {  // (no game library in this process: the share screen's)
            return false;
        }
    }

    /** A line with the session's time into log.txt ("[app] 0:12:34: text"), and to logcat; one before the log is in
     *  place waits for it (logToGame writes it then). */
    static void event(String text) {
        Log.i(TAG, text);
        boolean done;
        try {
            done = nativeEvent(text);
        } catch (UnsatisfiedLinkError e) {
            done = false;
        }
        if (!done)
            synchronized (pending) {
                if (pending.size() < 40) pending.add(text + " (" + stamp(System.currentTimeMillis()) + ", before the log)");
            }
    }
    private static final List<String> pending = new ArrayList<>();

    // ---- every start
    private static boolean installed;

    static synchronized void install(Context c) {
        if (installed) return;
        installed = true;
        final Context app = c.getApplicationContext();
        noteVersion(app);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try {
                StringWriter s = new StringWriter();
                PrintWriter w = new PrintWriter(s);
                w.println("Java crash, " + stamp(System.currentTimeMillis()) + ", " + versionName(app) + ", process "
                          + processName(app) + ", thread \"" + thread.getName() + "\":");
                error.printStackTrace(w);
                w.flush();
                File f = new File(AppHome.home(app), CRASH_FILE);
                boolean keep = f.length() < (48 << 10);  // (the last few; a long run of them starts the file again)
                try (OutputStream o = new FileOutputStream(f, keep)) {
                    o.write((s.toString() + "\n").getBytes(StandardCharsets.UTF_8));
                }
                Log.e(TAG, s.toString());
            } catch (Throwable ignored) {
            }
            if (previous != null) previous.uncaughtException(thread, error);
        });
    }

    private static void noteVersion(Context c) {
        try {
            SharedPreferences p = c.getSharedPreferences("diag", Context.MODE_PRIVATE);
            String now = versionName(c);
            if (now.equals(p.getString("last_version", ""))) return;
            String history = p.getString("versions", "");
            String entry = now + " since " + new SimpleDateFormat("yyyy-MM-dd", Locale.ROOT).format(new Date());
            history = history.isEmpty() ? entry : history + "; " + entry;
            if (history.length() > 600) history = history.substring(history.length() - 600);
            p.edit().putString("last_version", now).putString("versions", history).apply();
        } catch (Exception e) {
            Log.w(TAG, "diag: " + e);
        }
    }

    // ---- the snapshot
    /** The phone and the app now, one "[diag] ..." line each. Some reads take a few ms: never on the UI thread. */
    static List<String> snapshot(Context c) {
        List<String> out = new ArrayList<>();
        add(out, "app", () -> app(c));
        add(out, "device", Diag::device);
        add(out, "cpu", Diag::cpu);
        add(out, "memory", () -> memory(c));
        add(out, "storage", () -> storage(c));
        add(out, "screen", () -> screen(c));
        add(out, "power", () -> power(c));
        add(out, "network", () -> network(c));
        add(out, "gpu", () -> gpu(c));
        add(out, "locale", () -> "language " + Locale.getDefault().getLanguage() + ", font scale "
                                 + c.getResources().getConfiguration().fontScale);
        try {
            out.addAll(exits(c, 6));
        } catch (Throwable t) {
            out.add("[diag] exits: " + t);
        }
        return out;
    }

    private interface Part { String get() throws Exception; }

    private static void add(List<String> out, String name, Part p) {
        String s;
        try {
            s = p.get();
        } catch (Throwable t) {
            s = "(" + t.getClass().getSimpleName() + ": " + t.getMessage() + ")";
        }
        if (s != null && !s.isEmpty()) out.add("[diag] " + name + ": " + s);
    }

    static String versionName(Context c) {
        try {
            PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return pi.versionName + " (" + (Build.VERSION.SDK_INT >= 28 ? pi.getLongVersionCode() : pi.versionCode) + ")";
        } catch (Exception e) {
            return "?";
        }
    }

    private static String processName(Context c) {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am != null && am.getRunningAppProcesses() != null)
            for (ActivityManager.RunningAppProcessInfo p : am.getRunningAppProcesses())
                if (p.pid == Process.myPid()) return p.processName;
        return "?";
    }

    private static String stamp(long ms) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(ms));
    }

    private static String app(Context c) throws Exception {
        PackageManager pm = c.getPackageManager();
        String pkg = c.getPackageName();
        PackageInfo pi = pm.getPackageInfo(pkg, PackageManager.GET_SIGNING_CERTIFICATES);
        StringBuilder s = new StringBuilder(versionName(c)).append(", ").append(pkg);
        String cert = "?";
        Signature[] sigs = pi.signingInfo != null ? (pi.signingInfo.hasMultipleSigners() ? pi.signingInfo.getApkContentsSigners()
                                                                                          : pi.signingInfo.getSigningCertificateHistory())
                                                  : null;
        if (sigs != null && sigs.length > 0) cert = hex(MessageDigest.getInstance("SHA-256").digest(sigs[sigs.length - 1].toByteArray()));
        s.append("; signed with ").append(OFFICIAL_CERT.equals(cert) ? "the official key" : "ANOTHER key (" + cert.substring(0, Math.min(16, cert.length())) + "...): not an official build");
        if (Build.VERSION.SDK_INT >= 30) {
            InstallSourceInfo src = pm.getInstallSourceInfo(pkg);
            String by = src.getInstallingPackageName();
            s.append("; installed by ").append(by != null ? by : "no installer (adb, or a file manager that gives none)")
             .append(" (started from ").append(src.getInitiatingPackageName()).append(')');
        } else {
            s.append("; installed by ").append(pm.getInstallerPackageName(pkg));
        }
        s.append("; first installed ").append(stamp(pi.firstInstallTime)).append(", updated ").append(stamp(pi.lastUpdateTime));
        String history = c.getSharedPreferences("diag", Context.MODE_PRIVATE).getString("versions", "");
        if (!history.isEmpty()) s.append("; versions seen: ").append(history);
        s.append("; process up ").append((SystemClock.elapsedRealtime() - Process.getStartElapsedRealtime()) / 1000).append(" s");
        if ((c.getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) s.append("; debuggable");
        return s.toString();
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format(Locale.ROOT, "%02x", x));
        return s.toString();
    }

    private static String device() {
        StringBuilder s = new StringBuilder();
        s.append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE).append(", ").append(Build.PRODUCT)
         .append("), brand ").append(Build.BRAND).append(", board ").append(Build.BOARD).append(", hardware ").append(Build.HARDWARE);
        if (Build.VERSION.SDK_INT >= 31) s.append(", chip ").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL);
        s.append("; Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append("), security patch ")
         .append(Build.VERSION.SECURITY_PATCH).append(", build ").append(Build.DISPLAY).append(" (").append(Build.TYPE).append(')');
        s.append("; kernel ").append(System.getProperty("os.version")).append("; ABIs ").append(String.join(" ", Build.SUPPORTED_ABIS));
        return s.toString();
    }

    private static String readLine(String path) {
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            String l = r.readLine();
            return l != null ? l.trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static String cpu() {
        int n = Runtime.getRuntime().availableProcessors();
        StringBuilder s = new StringBuilder().append(n).append(" cores:");
        String lastClock = null;
        int run = 0;
        StringBuilder clusters = new StringBuilder();
        for (int i = 0; i < 32; i++) {
            String top = readLine("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
            if (top.isEmpty() && i >= n) break;
            long khz = top.matches("[0-9]+") ? Long.parseLong(top) : -1;  // (not readable, or 0: "?")
            String clock = khz > 0 ? (khz / 1000) + " MHz" : "? MHz";
            if (clock.equals(lastClock)) { run++; continue; }
            if (lastClock != null) clusters.append(' ').append(run).append(" x ").append(lastClock).append(',');
            lastClock = clock;
            run = 1;
        }
        if (lastClock != null) clusters.append(' ').append(run).append(" x ").append(lastClock);
        s.append(clusters);
        // (one line of each kind: the cores' types)
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
            java.util.LinkedHashSet<String> parts = new java.util.LinkedHashSet<>();
            String features = null;
            for (String l; (l = r.readLine()) != null; ) {
                if (l.startsWith("CPU part")) parts.add(l.substring(l.indexOf(':') + 1).trim());
                else if (features == null && l.startsWith("Features")) features = l.substring(l.indexOf(':') + 1).trim();
            }
            s.append("; parts ").append(String.join(" ", parts));
            if (features != null) s.append("; features ").append(features);
        } catch (IOException e) {
            s.append("; no /proc/cpuinfo");
        }
        return s.toString();
    }

    private static long meminfo(String key) {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/meminfo"))) {
            for (String l; (l = r.readLine()) != null; )
                if (l.startsWith(key)) return Long.parseLong(l.substring(key.length()).replaceAll("[^0-9]", "")) / 1024;
        } catch (Exception ignored) {
        }
        return -1;
    }

    private static String memory(Context c) {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return (mi.totalMem >> 20) + " MB, available " + (mi.availMem >> 20) + " MB, low at " + (mi.threshold >> 20) + " MB"
               + (mi.lowMemory ? " (LOW NOW)" : "") + "; swap " + meminfo("SwapTotal:") + " MB (" + meminfo("SwapFree:") + " free)"
               + "; app heap " + am.getMemoryClass() + "/" + am.getLargeMemoryClass() + " MB" + (am.isLowRamDevice() ? "; a low-RAM device" : "")
               + "; " + processName(c) + " uses " + Debug_pss(am) + " MB";
    }

    private static long Debug_pss(ActivityManager am) {
        try {
            android.os.Debug.MemoryInfo[] m = am.getProcessMemoryInfo(new int[] {Process.myPid()});
            return m != null && m.length > 0 ? m[0].getTotalPss() / 1024 : -1;
        } catch (Exception e) {
            return -1;
        }
    }

    private static String space(File f) {
        if (f == null || !f.exists()) return "-";
        StatFs st = new StatFs(f.getPath());
        return (st.getAvailableBytes() >> 20) + " MB free of " + (st.getTotalBytes() >> 20) + " MB";
    }

    private static String storage(Context c) {
        File home = AppHome.home(c);
        StringBuilder s = new StringBuilder("the app's folder ").append(space(home)).append(" (")
                .append(home.equals(c.getExternalFilesDir(null)) ? "external" : "private").append(')');
        s.append("; internal ").append(space(Environment.getDataDirectory()));
        String game = AppHome.read(new File(home, "game_folder.txt"));
        if (!game.isEmpty()) {
            File g = new File(game);
            s.append("; the game folder (").append(game.startsWith(home.getPath()) ? "in the app's folder" : g.getPath().startsWith("/storage/emulated") ? "shared storage" : "another volume (SD card?)")
             .append(") ").append(space(g)).append(g.canRead() ? "" : ", NOT readable");
        }
        File[] vols = c.getExternalFilesDirs(null);
        s.append("; ").append(vols != null ? vols.length : 0).append(" storage volume(s)");
        return s.toString();
    }

    private static String screen(Context c) {
        WindowManager wm = (WindowManager) c.getSystemService(Context.WINDOW_SERVICE);
        Display d = wm.getDefaultDisplay();
        DisplayMetrics m = new DisplayMetrics();
        d.getRealMetrics(m);
        StringBuilder s = new StringBuilder().append(m.widthPixels).append('x').append(m.heightPixels).append(", ").append(m.densityDpi)
                .append(" dpi (stable ").append(DisplayMetrics.DENSITY_DEVICE_STABLE).append("), ")
                .append(String.format(Locale.ROOT, "%.1f", d.getRefreshRate())).append(" Hz now; modes");
        java.util.TreeSet<String> rates = new java.util.TreeSet<>();
        for (Display.Mode mode : d.getSupportedModes())
            rates.add(mode.getPhysicalWidth() + "x" + mode.getPhysicalHeight() + "@" + Math.round(mode.getRefreshRate()));
        s.append(' ').append(String.join(" ", rates));
        if (Build.VERSION.SDK_INT >= 29) {
            DisplayCutout cut = d.getCutout();
            s.append(cut == null ? "; no cutout" : "; cutout, safe insets " + cut.getSafeInsetLeft() + "/" + cut.getSafeInsetTop() + "/"
                                                     + cut.getSafeInsetRight() + "/" + cut.getSafeInsetBottom());
        }
        Display.HdrCapabilities hdr = d.getHdrCapabilities();
        if (hdr != null && hdr.getSupportedHdrTypes().length > 0) s.append("; HDR types ").append(hdr.getSupportedHdrTypes().length);
        Configuration cf = c.getResources().getConfiguration();
        s.append("; ").append(cf.smallestScreenWidthDp).append(" dp narrowest, night mode ")
         .append((cf.uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES ? "on" : "off");
        if (c.getPackageManager().hasSystemFeature("android.hardware.type.pc")) s.append("; a PC-type device (DeX, ChromeOS...)");
        return s.toString();
    }

    private static String power(Context c) {
        StringBuilder s = new StringBuilder();
        Intent b = c.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (b != null) {
            int level = b.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = b.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            int plugged = b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0), status = b.getIntExtra(BatteryManager.EXTRA_STATUS, 0);
            int health = b.getIntExtra(BatteryManager.EXTRA_HEALTH, 0), temp = b.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
            s.append("battery ").append(scale > 0 ? level * 100 / scale : level).append(" %, ")
             .append(temp != Integer.MIN_VALUE ? String.format(Locale.ROOT, "%.1f C", temp / 10.0) : "? C").append(", ")
             .append(b.getIntExtra(BatteryManager.EXTRA_VOLTAGE, 0)).append(" mV, ")
             .append(plugged == BatteryManager.BATTERY_PLUGGED_AC ? "charging from a charger" : plugged == BatteryManager.BATTERY_PLUGGED_USB ? "on USB"
                     : plugged == BatteryManager.BATTERY_PLUGGED_WIRELESS ? "on a wireless charger" : "not plugged in")
             .append(status == BatteryManager.BATTERY_STATUS_FULL ? " (full)" : status == BatteryManager.BATTERY_STATUS_CHARGING ? " (charging)" : "")
             .append(health == BatteryManager.BATTERY_HEALTH_OVERHEAT ? ", OVERHEATED" : health == BatteryManager.BATTERY_HEALTH_GOOD ? "" : ", health " + health);
        }
        PowerManager pm = (PowerManager) c.getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            s.append("; power saving ").append(pm.isPowerSaveMode() ? "ON" : "off");
            if (Build.VERSION.SDK_INT >= 29) s.append(", thermal status ").append(pm.getCurrentThermalStatus());
            if (Build.VERSION.SDK_INT >= 30) {
                float h = pm.getThermalHeadroom(0);
                if (!Float.isNaN(h)) s.append(String.format(Locale.ROOT, ", headroom %.2f", h));
            }
            s.append(", sustained performance mode ").append(pm.isSustainedPerformanceModeSupported() ? "available" : "not available");
            s.append(", battery optimization ").append(pm.isIgnoringBatteryOptimizations(c.getPackageName()) ? "off for the app" : "on");
        }
        UsageStatsManager us = (UsageStatsManager) c.getSystemService(Context.USAGE_STATS_SERVICE);
        if (us != null) s.append(", standby bucket ").append(us.getAppStandbyBucket());
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am != null && am.isBackgroundRestricted()) s.append(", RESTRICTED in the background");
        if (Build.VERSION.SDK_INT >= 31) {
            GameManager gm = (GameManager) c.getSystemService(Context.GAME_SERVICE);
            if (gm != null) {
                int mode = gm.getGameMode();
                s.append(", game mode ").append(mode == 1 ? "standard" : mode == 2 ? "performance" : mode == 3 ? "battery" : mode == 4 ? "custom" : "unsupported");
            }
        }
        return s.toString();
    }

    private static String network(Context c) {
        ConnectivityManager cm = (ConnectivityManager) c.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) return "no connectivity service";
        Network n = cm.getActiveNetwork();
        if (n == null) return "none (offline)";
        return describe(c, cm, n, cm.getNetworkCapabilities(n), cm.getLinkProperties(n));
    }

    /** A network's kind for the log: never its name or an address. */
    static String describe(Context c, ConnectivityManager cm, Network n, NetworkCapabilities nc, LinkProperties lp) {
        StringBuilder s = new StringBuilder();
        if (nc != null) {
            List<String> t = new ArrayList<>();
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) t.add("Wi-Fi");
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) t.add("mobile data");
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) t.add("Ethernet");
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) t.add("VPN");
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH)) t.add("Bluetooth");
            if (nc.hasTransport(NetworkCapabilities.TRANSPORT_USB)) t.add("USB");
            s.append(t.isEmpty() ? "other" : String.join(" + ", t));
            s.append(nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) ? ", internet checked" : ", internet NOT checked");
            if (nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_CAPTIVE_PORTAL)) s.append(", a sign-in page");
            s.append(nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) ? ", not metered" : ", metered");
            if (!nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_ROAMING)) s.append(", roaming");
            s.append(", about ").append(nc.getLinkDownstreamBandwidthKbps() / 1000).append('/').append(nc.getLinkUpstreamBandwidthKbps() / 1000).append(" Mbps");
        }
        if (lp != null) {
            int v4 = 0, v6 = 0, v6local = 0;
            for (LinkAddress a : lp.getLinkAddresses()) {
                if (a.getAddress() instanceof Inet4Address) v4++;
                else if (a.getAddress() instanceof Inet6Address) {
                    if (a.getAddress().isLinkLocalAddress()) v6local++;
                    else v6++;
                }
            }
            s.append("; IPv4 ").append(v4 > 0 ? "yes" : "NO").append(", IPv6 ").append(v6 > 0 ? "yes" : "no");
            if (Build.VERSION.SDK_INT >= 29 && lp.getMtu() > 0) s.append(", MTU ").append(lp.getMtu());
            s.append(", private DNS ").append(lp.isPrivateDnsActive() ? "on" : "off").append(", ").append(lp.getDnsServers().size()).append(" DNS server(s)");
            if (lp.getInterfaceName() != null) s.append(", interface ").append(lp.getInterfaceName().replaceAll("[0-9]+$", "*"));
        }
        if (nc != null && nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
            try {
                WifiManager wifi = (WifiManager) c.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                WifiInfo w = wifi != null ? wifi.getConnectionInfo() : null;  // (its name and address are not read: no location permission)
                if (w != null) {
                    s.append("; Wi-Fi ").append(w.getFrequency()).append(" MHz, ").append(w.getRssi()).append(" dBm, ").append(w.getLinkSpeed()).append(" Mbps");
                    if (Build.VERSION.SDK_INT >= 30) s.append(", standard ").append(w.getWifiStandard());
                }
            } catch (Exception ignored) {
            }
        }
        return s.toString();
    }

    private static String gpu(Context c) {
        StringBuilder s = new StringBuilder();
        PackageManager pm = c.getPackageManager();
        for (FeatureInfo f : pm.getSystemAvailableFeatures()) {
            if (f.name == null) continue;
            if (f.name.equals(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION))
                s.append("Vulkan ").append(f.version >> 22).append('.').append((f.version >> 12) & 0x3ff).append('.').append(f.version & 0xfff).append(", ");
            else if (f.name.equals(PackageManager.FEATURE_VULKAN_HARDWARE_LEVEL)) s.append("level ").append(f.version).append(", ");
            else if (f.name.equals(PackageManager.FEATURE_VULKAN_DEQP_LEVEL)) s.append("dEQP ").append(Integer.toHexString(f.version)).append(", ");
        }
        ConfigurationInfo ci = ((ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE)).getDeviceConfigurationInfo();
        s.append("OpenGL ES ").append(ci.reqGlEsVersion >> 16).append('.').append(ci.reqGlEsVersion & 0xffff);
        return s.toString();
    }

    private static final String[] REASONS = {"unknown", "exited by itself", "killed by a signal", "LOW MEMORY (the system killed it)",
                                             "CRASH (Java)", "CRASH (native)", "ANR (not answering)", "failed to start",
                                             "a permission changed", "too much resource use", "the player stopped it",
                                             "the player force-stopped it", "a process it needs died", "other", "frozen",
                                             "its package changed", "the app was updated"};

    /** How the app's last processes ended (Android 11+: the system's own record), the latest first. */
    static List<String> exits(Context c, int max) {
        List<String> out = new ArrayList<>();
        if (Build.VERSION.SDK_INT < 30) return out;
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(c.getPackageName(), 0, max);
        for (ApplicationExitInfo e : list) {
            int r = e.getReason();
            String desc = e.getDescription();
            out.add("[diag] exit: " + stamp(e.getTimestamp()) + ", " + e.getProcessName() + ": " + (r >= 0 && r < REASONS.length ? REASONS[r] : "reason " + r)
                    + ", status " + e.getStatus() + ", importance " + e.getImportance() + ", memory " + (e.getPss() >> 10) + " MB (resident "
                    + (e.getRss() >> 10) + " MB)" + (desc != null && !desc.isEmpty() ? ", \"" + mask(desc) + "\"" : ""));
        }
        if (list.isEmpty()) out.add("[diag] exit: none recorded");
        return out;
    }

    /** The latest exit's own trace when it was an ANR (the threads' stacks): at most `max` bytes, "" otherwise. */
    static String lastAnrTrace(Context c, int max) {
        if (Build.VERSION.SDK_INT < 30) return "";
        try {
            ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(c.getPackageName(), 0, 1);
            if (list.isEmpty() || list.get(0).getReason() != ApplicationExitInfo.REASON_ANR) return "";
            try (InputStream in = list.get(0).getTraceInputStream()) {
                if (in == null) return "";
                byte[] buf = new byte[max];
                int n = 0;
                for (int k; n < max && (k = in.read(buf, n, max - n)) > 0; ) n += k;
                return new String(buf, 0, n, StandardCharsets.UTF_8);
            }
        } catch (Exception e) {
            return "(" + e + ")";
        }
    }

    // ---- what the log may show
    private static final Pattern IPV4 = Pattern.compile("(?<![0-9.])(\\d{1,3})\\.(\\d{1,3})\\.\\d{1,3}\\.\\d{1,3}(?![0-9.])");
    private static final Pattern USER_PATH = Pattern.compile("(?i)([A-Z]:[\\\\/]Users[\\\\/])[^\\\\/\"\\r\\n]+|(/home/)[^/\"\\r\\n]+");

    /** An IPv4's last two numbers as x (192.168.x.x), a PC user's folder name as <user>. */
    static String mask(String s) {
        Matcher m = IPV4.matcher(s);
        s = m.replaceAll("$1.$2.x.x");
        m = USER_PATH.matcher(s);
        StringBuffer b = new StringBuffer();
        while (m.find()) m.appendReplacement(b, Matcher.quoteReplacement((m.group(1) != null ? m.group(1) : m.group(2)) + "<user>"));
        m.appendTail(b);
        return b.toString();
    }

    private static final Pattern SECRET_KEY = Pattern.compile("(?i)^\\s*([^=;#\\[]*(password|passphrase|pass|secret|token|nickname|nick|name|key)[^=]*)=\\s*(.+)$");

    /** A settings file without its comments and blank lines; a name, key or password only as "(set)", addresses masked. */
    static String settingsText(File f, int max) {
        StringBuilder s = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new FileReader(f))) {
            for (String l; (l = r.readLine()) != null && s.length() < max; ) {
                String t = l.trim();
                if (t.isEmpty() || t.startsWith(";") || t.startsWith("#")) continue;
                Matcher m = SECRET_KEY.matcher(t);
                // (key bindings are keys of the keyboard, not secrets: "Key" alone in [Keybinds] rows ends in "Key")
                if (m.matches() && !t.matches("(?i)^(combo[0-9]key|chatkey)\\s*=.*")) t = m.group(1).trim() + " = (set)";
                s.append(mask(t)).append('\n');
            }
        } catch (IOException e) {
            s.append("(").append(e.getMessage()).append(")\n");
        }
        return s.toString();
    }

    /**
     * A folder's files for the log: each name with its size, a folder as its count and size (`depth` levels, `max` lines).
     * Never the game's own files (romfs, exefs: tens of thousands, and a phone's shared storage lists them slowly: the
     * emulator took over 10 s), at most 3000 files counted in a folder and 3 s in all.
     */
    static String listing(File dir, int depth, int max) {
        StringBuilder s = new StringBuilder();
        list(dir, "", depth, max, s, SystemClock.elapsedRealtime() + 3000);
        return s.toString();
    }

    private static final java.util.Set<String> GAME_FILES = new java.util.HashSet<>(java.util.Arrays.asList("romfs", "exefs", "game", "data"));

    private static void list(File dir, String indent, int depth, int max, StringBuilder s, long deadline) {
        File[] files = dir.listFiles();
        if (files == null) { s.append(indent).append("(not readable)\n"); return; }
        java.util.Arrays.sort(files, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
        for (File f : files) {
            if (s.length() > max * 80) { s.append(indent).append("...\n"); return; }
            if (f.isDirectory()) {
                if (GAME_FILES.contains(f.getName().toLowerCase(Locale.ROOT))) {
                    String[] in = f.list();
                    s.append(indent).append(f.getName()).append("/  (not walked: a game's or a mod's files, ").append(in != null ? in.length : 0).append(" entries at its top)\n");
                    continue;
                }
                long[] t = new long[2];
                boolean whole = total(f, t, 0, deadline);
                if (whole || t[0] > 0) {
                    s.append(indent).append(f.getName()).append("/  ").append(t[0]).append(whole ? "" : "+").append(" files, ").append(size(t[1]))
                     .append(whole ? "" : " (counting stopped)").append('\n');
                } else {  // (the time was up before this one: what is at its top)
                    String[] in = f.list();
                    s.append(indent).append(f.getName()).append("/  ").append(in != null ? in.length : 0).append(" entries at its top (not counted: time)\n");
                }
                if (depth > 1 && SystemClock.elapsedRealtime() < deadline) list(f, indent + "  ", depth - 1, max, s, deadline);
            } else {
                s.append(indent).append(f.getName()).append("  ").append(size(f.length())).append("  ").append(stamp(f.lastModified())).append('\n');
            }
        }
    }

    // (false: stopped at 3000 files, 12 levels or the deadline)
    private static boolean total(File dir, long[] t, int level, long deadline) {
        if (level > 12 || t[0] >= 3000 || SystemClock.elapsedRealtime() > deadline) return false;
        File[] files = dir.listFiles();
        if (files == null) return true;
        for (File f : files) {
            if (f.isDirectory()) {
                if (GAME_FILES.contains(f.getName().toLowerCase(Locale.ROOT)) || !total(f, t, level + 1, deadline)) return false;
            } else {
                t[0]++;
                t[1] += f.length();
                if (t[0] >= 3000) return false;
            }
        }
        return true;
    }

    static String size(long b) {
        return b >= 1 << 30 ? String.format(Locale.ROOT, "%.2f GB", b / 1073741824.0)
             : b >= 1 << 20 ? String.format(Locale.ROOT, "%.1f MB", b / 1048576.0) : String.format(Locale.ROOT, "%.1f KB", b / 1024.0);
    }

    // ---- while the game runs (GameActivity): the system's changes, as events in log.txt
    private static BroadcastReceiver receiver;
    private static ConnectivityManager.NetworkCallback netCallback;
    private static PowerManager.OnThermalStatusChangedListener thermal;
    private static String lastNet = "";

    static synchronized void watch(Context c) {
        final Context app = c.getApplicationContext();
        if (receiver != null) return;
        receiver = new BroadcastReceiver() {
            @Override public void onReceive(Context ctx, Intent i) {
                String a = i.getAction();
                if (a == null) return;
                switch (a) {
                    case Intent.ACTION_BATTERY_CHANGED: {
                        int t = i.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
                        try {
                            nativeBattery(t);
                        } catch (UnsatisfiedLinkError ignored) {
                        }
                        break;
                    }
                    case PowerManager.ACTION_POWER_SAVE_MODE_CHANGED:
                        PowerManager pm = (PowerManager) app.getSystemService(Context.POWER_SERVICE);
                        event("power saving " + (pm != null && pm.isPowerSaveMode() ? "ON" : "off"));
                        break;
                    case Intent.ACTION_POWER_CONNECTED: event("plugged in"); break;
                    case Intent.ACTION_POWER_DISCONNECTED: event("unplugged"); break;
                    case Intent.ACTION_BATTERY_LOW: event("the battery is low"); break;
                    case Intent.ACTION_SCREEN_OFF: event("the screen went off"); break;
                    case Intent.ACTION_SCREEN_ON: event("the screen came on"); break;
                    default: break;
                }
            }
        };
        IntentFilter f = new IntentFilter();
        f.addAction(Intent.ACTION_BATTERY_CHANGED);  // (sticky: its last one comes at once)
        f.addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED);
        f.addAction(Intent.ACTION_POWER_CONNECTED);
        f.addAction(Intent.ACTION_POWER_DISCONNECTED);
        f.addAction(Intent.ACTION_BATTERY_LOW);
        f.addAction(Intent.ACTION_SCREEN_OFF);
        f.addAction(Intent.ACTION_SCREEN_ON);
        try {
            if (Build.VERSION.SDK_INT >= 33) app.registerReceiver(receiver, f, Context.RECEIVER_NOT_EXPORTED);
            else app.registerReceiver(receiver, f);
        } catch (Exception e) {
            Log.w(TAG, "diag: " + e);
        }
        final ConnectivityManager cm = (ConnectivityManager) app.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null) {
            netCallback = new ConnectivityManager.NetworkCallback() {
                @Override public void onCapabilitiesChanged(Network n, NetworkCapabilities nc) { netEvent(app, cm, n, nc); }
                @Override public void onLost(Network n) { netNow("the network is gone (offline)"); }
            };
            try {
                cm.registerDefaultNetworkCallback(netCallback);
            } catch (Exception e) {
                Log.w(TAG, "diag: " + e);
            }
        }
        PowerManager pm = (PowerManager) app.getSystemService(Context.POWER_SERVICE);
        if (pm != null && Build.VERSION.SDK_INT >= 29) {
            thermal = status -> event("thermal status " + status + (status >= 3 ? " (the system slows the phone down hard)" : status >= 2 ? " (moderate)" : ""));
            try {
                pm.addThermalStatusListener(app.getMainExecutor(), thermal);
            } catch (Exception e) {
                Log.w(TAG, "diag: " + e);
            }
        }
    }

    private static void netEvent(Context c, ConnectivityManager cm, Network n, NetworkCapabilities nc) {
        // (the kind only: transports, checked, metered; the bandwidth estimate changes all the time and is left out)
        StringBuilder s = new StringBuilder();
        if (nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) s.append("Wi-Fi ");
        if (nc.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) s.append("mobile data ");
        if (nc.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) s.append("Ethernet ");
        if (nc.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) s.append("VPN ");
        s.append(nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) ? "(internet checked)" : "(internet not checked)");
        s.append(nc.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) ? "" : ", metered");
        netNow("the network: " + s);
    }

    private static synchronized void netNow(String s) {
        if (s.equals(lastNet)) return;
        lastNet = s;
        event(s);
    }

    static synchronized void unwatch(Context c) {
        Context app = c.getApplicationContext();
        try {
            if (receiver != null) app.unregisterReceiver(receiver);
        } catch (Exception ignored) {
        }
        try {
            ConnectivityManager cm = (ConnectivityManager) app.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm != null && netCallback != null) cm.unregisterNetworkCallback(netCallback);
        } catch (Exception ignored) {
        }
        try {
            PowerManager pm = (PowerManager) app.getSystemService(Context.POWER_SERVICE);
            if (pm != null && thermal != null && Build.VERSION.SDK_INT >= 29) pm.removeThermalStatusListener(thermal);
        } catch (Exception ignored) {
        }
        receiver = null;
        netCallback = null;
        thermal = null;
        lastNet = "";
    }

    /** The snapshot into log.txt, on a thread of its own, once the game's log is in place (it waits for it, 30 s at most). */
    static void logToGame(Context c) {
        final Context app = c.getApplicationContext();
        Thread t = new Thread(() -> {
            List<String> lines = snapshot(app);
            for (int i = 0; i < 150 && !line("[diag] the app's Java side, " + stamp(System.currentTimeMillis()) + ":"); i++)
                SystemClock.sleep(200);
            for (String l : lines) line(l);
            List<String> early;
            synchronized (pending) {
                early = new ArrayList<>(pending);
                pending.clear();
            }
            for (String p : early)
                try {
                    nativeEvent(p);
                } catch (UnsatisfiedLinkError ignored) {
                }
        }, "mh4u-diag");
        t.setPriority(Thread.MIN_PRIORITY);
        t.start();
    }

    static String trimLevel(int level) {
        switch (level) {
            case 5: return "running, moderate";
            case 10: return "running, low";
            case 15: return "running, CRITICAL";
            case 20: return "the window hidden";
            case 40: return "in the background";
            case 60: return "background, moderate";
            case 80: return "background, COMPLETE (next to be killed)";
            default: return "level " + level;
        }
    }

    // ---- Share log's file (LogShareActivity, on a thread of its own)
    // (the most of each log: its start - the phone, the GPU, the settings - and its end - what led to an error; a log of
    // 4.9 MB had its start cut before 1.0.1. The whole stays well under Discord's upload limit, and past BUNDLE_ZIP the
    // file goes zipped)
    private static final int HEAD_NOW = 1 << 20, TAIL_NOW = 3 << 20, HEAD_PREV = 512 << 10, TAIL_PREV = 1536 << 10;
    private static final int HEAD_PREV2 = 192 << 10, TAIL_PREV2 = 512 << 10, BUNDLE_ZIP = 6 << 20;

    /** Everything Share log sends, in the cache folder: mh4u-log.txt, or mh4u-log.zip with it inside when it is big. */
    static File bundle(Context c, String header) {
        File home = AppHome.home(c), txt = LogProvider.report(c, false), zip = LogProvider.report(c, true);
        zip.delete();
        File user = userDir(home);
        try (OutputStream out = new java.io.BufferedOutputStream(new FileOutputStream(txt), 1 << 16)) {
            w(out, header);
            w(out, "\n===== the phone and the app now (Share log's own look, " + stamp(System.currentTimeMillis()) + ") =====\n");
            for (String l : snapshot(c)) w(out, l + "\n");
            w(out, "[diag] code library: " + codeLibrary(c, home) + "\n");
            File app = new File(home, "mh4u-app.ini");
            w(out, "\n===== the app's options (mh4u-app.ini without its comments; names and passwords hidden) =====\n");
            w(out, app.isFile() ? settingsText(app, 64 << 10) : "(none yet)\n");
            File game = new File(user, "mh4u.ini");
            w(out, "\n===== the port's game options (mh4u.ini without its comments) =====\n");
            w(out, game.isFile() ? settingsText(game, 64 << 10) : "(none yet)\n");
            File launch = new File(home, "launch.txt");  // (test runs: env= and their folders)
            if (launch.isFile()) {
                w(out, "\n===== launch.txt (the start's folders and variables) =====\n");
                for (String l : readAll(launch, 8 << 10).split("\n")) {
                    int eq = l.indexOf('=');
                    String k = eq > 0 ? l.substring(0, eq).trim() : l.trim();
                    // (the folders as their kind only: user and game folders can be anywhere on the phone)
                    w(out, (k.equals("env") ? mask(l) : eq > 0 ? k + "=" + (l.substring(eq + 1).trim().isEmpty() ? "" : "(a folder)") : mask(l)) + "\n");
                }
            }
            w(out, "\n===== mods, cheats, texture packs and pictures (names and sizes) =====\n");
            String folder = AppHome.read(new File(home, "game_folder.txt"));
            File[][] places = {{new File(user, "load/mods"), null}, {new File(user, "cheats"), null}, {new File(user, "load/textures"), null},
                               {folder.isEmpty() ? null : new File(folder, "textures"), null}, {new File(home, "pictures"), null}};
            String[] names = {"the user folder's load/mods", "the user folder's cheats", "the user folder's load/textures",
                              "the game folder's textures", "the app's pictures"};
            for (int i = 0; i < places.length; i++) {
                File d = places[i][0];
                if (d == null || !d.isDirectory()) continue;
                w(out, "-- " + names[i] + "/\n" + listing(d, i == 0 ? 3 : 1, 300));
                for (String f : new String[] {"load_order.txt", "load_order.ok"}) {
                    File order = new File(d, f);
                    if (order.isFile()) w(out, "   " + f + ":\n" + indent(readAll(order, 8 << 10)));
                }
            }
            w(out, "\n===== the app's folder (names and sizes) =====\n" + listing(home, 1, 200));
            File crash = new File(home, CRASH_FILE);
            if (crash.isFile()) section(out, crash, "Java crashes (" + CRASH_FILE + ")", 0, 48 << 10);
            section(out, new File(home, "log.txt"), "this session", HEAD_NOW, TAIL_NOW);
            section(out, new File(home, "log_prev.txt"), "the session before", HEAD_PREV, TAIL_PREV);
            File prev2 = new File(home, "log_prev2.txt");
            if (prev2.isFile()) section(out, prev2, "two sessions before", HEAD_PREV2, TAIL_PREV2);
            File imported = new File(home, "save_import.txt");  // (Import save's last try: where from, which game, what happened)
            if (imported.isFile()) section(out, imported, "the last save import", 0, 64 << 10);
            File built = new File(home, "builder_log.txt");  // (the phone builder's last run: its stages, heat and battery)
            if (built.isFile()) section(out, built, "the last code build", 64 << 10, 192 << 10);
            List<String> sys = systemLog(c, 300, false);
            w(out, "\n===== the system log: this app's warnings and errors (logcat, the last " + sys.size() + ") =====\n");
            for (String l : sys) w(out, l + "\n");
            String anr = lastAnrTrace(c, 96 << 10);
            if (!anr.isEmpty()) w(out, "\n===== the last exit's ANR trace (Android's, the first 96 KB) =====\n" + anr + "\n");
            w(out, "\n===== the end =====\n");
        } catch (IOException e) {
            Log.e(TAG, "share log: " + e);
        }
        if (txt.length() <= BUNDLE_ZIP) return txt;
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new FileOutputStream(zip));
             InputStream in = new FileInputStream(txt)) {
            z.setLevel(9);
            z.putNextEntry(new java.util.zip.ZipEntry(txt.getName()));
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) z.write(buf, 0, n);
            z.closeEntry();
        } catch (IOException e) {
            Log.e(TAG, "share log zip: " + e);
            return txt;
        }
        return zip;
    }

    /** The user folder the game ran with: launch.txt's user=, else files/user (as LogShareActivity.modsDir). */
    static File userDir(File home) {
        File user = new File(home, "user");
        for (String l : readAll(new File(home, "launch.txt"), 8 << 10).split("\n"))
            if (l.startsWith("user=") && l.length() > 5) user = new File(l.substring(5).trim());
        return user;
    }

    /** The code library the game loads (the app's private copy) and its id against the one this version wants. */
    private static String codeLibrary(Context c, File home) {
        File lib = new File(c.getFilesDir(), GameFolderActivity.LIBRARY), src = new File(home, GameFolderActivity.LIBRARY);
        String want = GameFolderActivity.wantedId(c);
        StringBuilder s = new StringBuilder();
        for (File f : new File[] {lib, src}) {
            String which = f == lib ? "the copy the game loads" : "the one in the app's folder";
            if (!f.isFile()) { s.append(which).append(": none; "); continue; }
            String id = AppHome.read(new File(f.getParentFile(), GameFolderActivity.ID));
            s.append(which).append(": ").append(size(f.length())).append(" of ").append(stamp(f.lastModified())).append(", id ")
             .append(id.isEmpty() ? "(no .id beside it)" : id).append("; ");
        }
        s.append("this version wants ").append(want.isEmpty() ? "(no id in the APK)" : want);
        return s.toString();
    }

    /** If bundle fails (it never should): the header and the logs' ends, as Share log was before 1.0.1. */
    static File plain(Context c, String header, Throwable why) {
        File home = AppHome.home(c), txt = LogProvider.report(c, false);
        try (OutputStream out = new FileOutputStream(txt)) {
            w(out, header + "(the full report failed: " + why + ")\n");
            section(out, new File(home, "log.txt"), "this session", 0, 2 << 20);
            section(out, new File(home, "log_prev.txt"), "the session before", 0, 2 << 20);
        } catch (IOException e) {
            Log.e(TAG, "share log: " + e);
        }
        return txt;
    }

    private static void w(OutputStream out, String s) throws IOException { out.write(s.getBytes(StandardCharsets.UTF_8)); }

    private static String indent(String s) { return "     " + s.replace("\n", "\n     ").replaceAll("\\s+$", "") + "\n"; }

    static String readAll(File f, int max) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), max)];
            int n = 0;
            for (int k; n < b.length && (k = in.read(b, n, b.length - n)) > 0; ) n += k;
            return new String(b, 0, n, StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "(" + e.getMessage() + ")";
        }
    }

    /** A file under its title: all of it, or its first `head` and last `tail` bytes with what was left out said between. */
    static void section(OutputStream out, File f, String title, int head, int tail) throws IOException {
        long len = f.length();
        w(out, "\n===== " + title + " (" + f.getName() + ", " + len + " bytes" + (f.exists() ? ", " + stamp(f.lastModified()) : ", none") + ") =====\n");
        if (!f.exists()) return;
        try (java.io.RandomAccessFile in = new java.io.RandomAccessFile(f, "r")) {
            if (len <= (long) head + tail) {
                copy(in, out, 0, len);
                return;
            }
            long headEnd = lineEnd(in, head, len), tailStart = lineEnd(in, len - tail, len);
            copy(in, out, 0, headEnd);
            w(out, "\n..... " + (tailStart - headEnd) + " bytes of the middle left out here (the start and the end are what tell) .....\n");
            copy(in, out, tailStart, len);
        }
    }

    // (the position just past the first newline at or after `at`, so neither part starts or ends inside a line)
    private static long lineEnd(java.io.RandomAccessFile in, long at, long len) throws IOException {
        in.seek(at);
        byte[] b = new byte[4096];
        for (long p = at; p < len; ) {
            int n = in.read(b, 0, (int) Math.min(b.length, len - p));
            if (n <= 0) break;
            for (int i = 0; i < n; i++) if (b[i] == '\n') return p + i + 1;
            p += n;
        }
        return at;
    }

    private static void copy(java.io.RandomAccessFile in, OutputStream out, long from, long to) throws IOException {
        in.seek(from);
        byte[] b = new byte[1 << 16];
        for (long p = from; p < to; ) {
            int n = in.read(b, 0, (int) Math.min(b.length, to - p));
            if (n <= 0) break;
            out.write(b, 0, n);
            p += n;
        }
    }

    /**
     * The system log's lines of this app that its own log.txt doesn't have (an app reads only its own): warnings,
     * errors and fatal ones of any tag but the game's own lines (log.txt has those), and the allocator's at any level
     * (scudo says why it aborts in an info line). After a crash they say what the game's report can't: why the
     * allocator or the C++ library stopped it, the GPU driver's own words. The last `max`.
     */
    static List<String> systemLog(Context c, int max, boolean errorsOnly) {
        java.util.ArrayDeque<String> lines = new java.util.ArrayDeque<>();
        java.lang.Process logcat = null;
        try {
            logcat = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "20000").redirectErrorStream(true).start();
            try (BufferedReader r = new BufferedReader(new java.io.InputStreamReader(logcat.getInputStream(), StandardCharsets.UTF_8))) {
                for (String l; (l = r.readLine()) != null; ) {
                    // "MM-DD HH:MM:SS.mmm  PID  TID L TAG: message"
                    String[] f = l.trim().split("\\s+", 6);
                    if (f.length < 6) continue;
                    boolean bad = f[4].equals("E") || f[4].equals("F") || (!errorsOnly && f[4].equals("W"));
                    if (!bad && !f[5].startsWith("scudo")) continue;
                    lines.addLast(mask(l.length() > 400 ? l.substring(0, 400) : l));
                    if (lines.size() > max) lines.removeFirst();
                }
            }
        } catch (IOException e) {
            lines.addLast("(logcat: " + e + ")");
        } finally {
            if (logcat != null) logcat.destroy();
        }
        return new ArrayList<>(lines);
    }
}

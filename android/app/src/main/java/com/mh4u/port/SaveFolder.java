package com.mh4u.port;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.security.SecureRandom;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Settings -> Save folder: where the save is kept. By default the game's own folder (the user folder's sdmc/, in the
 * app's folder); or a folder the player chooses, for example one a sync app shares with their other devices. The same
 * screens, texts and rules as MHGU for Android's (and its PC version's):
 *
 *   save_folder.txt          the chosen folder: the one that holds sdmc/ (the save, the guild cards, the event quests
 *                            and the Palico, in Azahar's layout, as Export save makes it); none: the game's own.
 *                            app_native/android_main.cpp gives the runtime its sdmc/ (--saves)
 *   save_folder_pending.txt  folder=<path or nothing: the game's own>, keep=folder|device: a change chosen while the
 *                            game ran, applied before it starts again (applyPending), as an imported save is
 *   save_folder_state.txt    the folder's save as last seen: an empty folder that had one is a problem
 *   device.txt               id=, name=: this device, for the in-use marker
 *   <folder>/.mh4u-in-use    id=, name=, time=: a device has the game open with that save (android_main.cpp keeps it
 *                            fresh every few minutes and removes it at a clean exit)
 *
 * The console's config (its unique ID, the MAC address), mods and mh4u.ini stay in the user folder: two devices that
 * share a save keep their own. While the folder can't be reached the runtime opens no save there and makes no new one
 * (mh4n's --saves): the game says it can't read it. Nothing is overwritten without asking; the save not chosen goes to
 * the app's own save_backups/<date>, never into the shared folder.
 */
final class SaveFolder {
    private static final String TAG = "MH4U";
    static final String SUB = "MH4U save", MARKER = ".mh4u-in-use";
    static final String ZEROS = "00000000000000000000000000000000";
    static final String[] TITLES = {"00126300", "00126100"};
    static final String[] FILES = {"system", "user1", "user2", "user3"};
    static final long FRESH_S = 15 * 60;  // (the marker is renewed every 5 minutes: older than this, nobody has it open)

    private SaveFolder() {}

    static File home(Context c) { return AppHome.home(c); }
    /** The game's own: the user folder (it holds sdmc/). */
    static File local(Context c) { return new File(home(c), "user"); }
    static File backups(Context c) { return new File(home(c), "save_backups"); }
    private static File file(Context c, String name) { return new File(home(c), name); }

    static File sdmc(File root) { return new File(root, "sdmc/Nintendo 3DS/" + ZEROS + "/" + ZEROS); }
    static File dataDir(File root, String t) { return new File(sdmc(root), "title/00040000/" + t + "/data/00000001"); }
    static File metaFile(File root, String t) { return new File(sdmc(root), "title/00040000/" + t + "/data/00000001.metadata"); }
    static File extDir(File root, String t) {
        return new File(sdmc(root), "extdata/00000000/" + (t.equals("00126100") ? "00001261" : "00001263"));
    }

    static File chosen(Context c) {
        String p = AppHome.read(file(c, "save_folder.txt"));
        return p.isEmpty() ? null : new File(p);
    }
    static File active(Context c) {
        File f = chosen(c);
        return f != null ? f : local(c);
    }

    // ---- a save and what is in it
    static final class Info {
        boolean valid;
        long time;
        int hunters;
    }

    static Info describe(File root) {
        Info i = new Info();
        for (String t : TITLES) {
            File d = dataDir(root, t), system = new File(d, "system");
            if (system.length() != 512) continue;
            int hunters = 0;
            long time = system.lastModified();
            for (int u = 1; u <= 3; u++) {
                File f = new File(d, "user" + u);
                if (f.length() > 0) { hunters++; time = Math.max(time, f.lastModified()); }
            }
            i.valid = true;
            i.hunters += hunters;
            i.time = Math.max(i.time, time);
        }
        return i;
    }

    static String when(long time) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.ROOT).format(new Date(time));
    }

    /** "2 hunters · saved 2026-11-09 18:30", or "no save". */
    static String line(Info i, boolean es) {
        if (!i.valid) return es ? "sin partida" : "no save";
        String who = es ? (i.hunters == 1 ? "1 cazador" : i.hunters + " cazadores") : (i.hunters == 1 ? "1 hunter" : i.hunters + " hunters");
        return who + (es ? " · guardada el " : " · saved ") + when(i.time);
    }

    // ---- choosing a folder
    /** A folder with the save in it as it is (an Export folder, another device's); else its "MH4U save" folder. */
    static File resolve(File picked) {
        if (describe(picked).valid) return picked;
        File sub = new File(picked, SUB);
        if (sub.isDirectory()) return sub;
        if (picked.getName().equals(SUB)) return picked;
        return sub;
    }

    static boolean holdsSave(File dir) {
        return describe(dir).valid || describe(new File(dir, SUB)).valid;
    }

    static String refusal(Context c, File picked, boolean es) {
        String p = picked.getAbsolutePath();
        if (p.contains("/Android/data/") || p.endsWith("/Android/data") || p.contains("/Android/obb"))
            return es ? "Esa carpeta es de una app y Android la mantiene cerrada. Elige otra."
                      : "That folder belongs to an app, and Android keeps it closed. Choose another one.";
        String own = home(c).getAbsolutePath();
        if (p.equals(own) || p.startsWith(own + "/"))
            return es ? "Esa es la carpeta propia del juego. Elige otra, o usa \"Usar la carpeta propia del juego\"."
                      : "That is the game's own folder. Choose another one, or use \"Use the game's own folder\".";
        if (!picked.isDirectory() || AppHome.probe(picked) != 0)
            return es ? "El juego no puede escribir en esa carpeta. Elige una en la memoria del teléfono."
                      : "The game can't write in that folder. Choose one in the phone's storage.";
        return null;
    }

    static boolean hasAccess(Context c) {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return c.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
            && c.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    static boolean setPending(Context c, File folder, boolean keepFolder) {
        return AppHome.writeText(file(c, "save_folder_pending.txt"),
                "folder=" + (folder == null ? "" : folder.getAbsolutePath()) + "\nkeep=" + (keepFolder ? "folder" : "device") + "\n");
    }

    private static Map<String, String> keys(File f) {
        Map<String, String> m = new LinkedHashMap<>();
        for (String l : AppHome.read(f).split("\n")) {
            int eq = l.indexOf('=');
            if (eq > 0) m.put(l.substring(0, eq).trim(), l.substring(eq + 1).trim());
        }
        return m;
    }

    /** As MHGU's: the change chosen last time, before the game runs. null: done or nothing to do; else why not. */
    static synchronized String applyPending(Context c) {
        File pend = file(c, "save_folder_pending.txt");
        if (!pend.isFile()) return null;
        boolean es = Look.es();
        Map<String, String> k = keys(pend);
        String path = k.containsKey("folder") ? k.get("folder") : "";
        boolean keepFolder = "folder".equals(k.get("keep"));
        File now = chosen(c), from = active(c);
        String error = null;
        try {
            if (!path.isEmpty()) {
                File to = new File(path);
                if (!to.isDirectory() && !to.mkdirs()) throw new IOException(es ? "no se pudo crear " + to : "couldn't make " + to);
                if (AppHome.probe(to) != 0) throw new IOException(es ? "no se puede escribir en " + to : "can't write in " + to);
                Info there = describe(to), here = describe(from);
                if (!to.equals(from)) {
                    if (!keepFolder || !there.valid) {
                        if (there.valid) backup(c, to, "folder");
                        if (here.valid) copySave(from, to);
                    } else if (here.valid) {
                        backup(c, from, "device");
                    }
                }
                File sd = new File(to, "sdmc");
                if (!sd.isDirectory() && !sd.mkdirs()) throw new IOException("couldn't make " + sd);  // (the runtime's root)
                AppHome.write(file(c, "save_folder.txt"), to.getAbsolutePath());
                if (now != null && !now.equals(to)) unmark(c, now);
                Log.i(TAG, "save folder: " + to + (keepFolder ? " (its save kept)" : " (this device's save copied there)"));
            } else if (now != null) {
                File own = local(c);
                Info there = describe(now), here = describe(own);
                if (keepFolder && there.valid) {
                    if (here.valid) backup(c, own, "device");
                    copySave(now, own);
                }
                if (!file(c, "save_folder.txt").delete() && file(c, "save_folder.txt").exists())
                    throw new IOException("save_folder.txt");
                unmark(c, now);
                Log.i(TAG, "save folder: back to the game's own" + (keepFolder ? " (the folder's save copied)" : ""));
            }
            file(c, "save_folder_state.txt").delete();
        } catch (IOException e) {
            Log.e(TAG, "save folder: " + e.getMessage());
            error = e.getMessage();
        }
        pend.delete();
        return error;
    }

    /**
     * The save of one root (the folder that holds sdmc/) into another, for each title it has: its save folder, its
     * format record and its extra data are copied first into <to>/.mh4u-incoming (each file checked), then each takes
     * the old one's place by renames, so the destination never holds a mix of two saves.
     */
    static void copySave(File from, File to) throws IOException {
        File incoming = new File(to, ".mh4u-incoming"), old = new File(to, ".mh4u-old");
        deleteTree(incoming);
        deleteTree(old);
        List<String> titles = new ArrayList<>();
        for (String t : TITLES) {
            if (!new File(dataDir(from, t), "system").isFile()) continue;
            titles.add(t);
            copyTree(dataDir(from, t), new File(incoming, t + "/data"));
            if (metaFile(from, t).isFile()) copyChecked(metaFile(from, t), new File(incoming, t + "/meta"));
            if (extDir(from, t).isDirectory()) copyTree(extDir(from, t), new File(incoming, t + "/extdata"));
        }
        for (String t : titles) {
            swap(new File(incoming, t + "/data"), dataDir(to, t), new File(old, t + "/data"));
            File meta = new File(incoming, t + "/meta");
            if (meta.isFile()) swap(meta, metaFile(to, t), new File(old, t + "/meta"));
            File ext = new File(incoming, t + "/extdata");
            if (ext.isDirectory()) swap(ext, extDir(to, t), new File(old, t + "/extdata"));
        }
        deleteTree(incoming);
        deleteTree(old);
    }

    private static void swap(File staged, File place, File aside) throws IOException {
        File parent = place.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("couldn't make " + parent);
        if (place.exists()) {
            aside.getParentFile().mkdirs();
            if (!place.renameTo(aside)) throw new IOException("couldn't move " + place + " aside");
        }
        if (!staged.renameTo(place)) {
            if (aside.exists()) aside.renameTo(place);  // (the old one back)
            throw new IOException("couldn't put " + place + " in place");
        }
    }

    static void copyTree(File from, File to) throws IOException {
        if (from.isDirectory()) {
            if (!to.isDirectory() && !to.mkdirs()) throw new IOException("couldn't make " + to);
            File[] list = from.listFiles();
            if (list == null) throw new IOException("can't read " + from);
            for (File f : list) copyTree(f, new File(to, f.getName()));
            return;
        }
        copyChecked(from, to);
    }

    static void copyChecked(File src, File dst) throws IOException {
        File parent = dst.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("couldn't make " + parent);
        File tmp = new File(dst.getParentFile(), dst.getName() + ".mh4u-copy");
        tmp.delete();
        AppHome.copy(src, tmp);
        if (!tmp.renameTo(dst) && !(dst.delete() && tmp.renameTo(dst))) {
            tmp.delete();
            throw new IOException("couldn't replace " + dst);
        }
        dst.setLastModified(src.lastModified());
    }

    static void deleteTree(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File k : list) deleteTree(k);
        f.delete();
    }

    /** A copy of the save of root in the app's own save_backups/<date>-<what>/<title>/{data, extdata, metadata}. */
    static File backup(Context c, File root, String what) throws IOException {
        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
        File keep = new File(backups(c), stamp + "-" + what);
        for (int n = 2; keep.exists(); n++) keep = new File(backups(c), stamp + "-" + what + "-" + n);
        for (String t : TITLES) {
            if (!dataDir(root, t).isDirectory()) continue;
            copyTree(dataDir(root, t), new File(keep, t + "/data"));
            if (metaFile(root, t).isFile()) copyChecked(metaFile(root, t), new File(keep, t + "/00000001.metadata"));
            if (extDir(root, t).isDirectory()) copyTree(extDir(root, t), new File(keep, t + "/extdata"));
        }
        Log.i(TAG, "save folder: the save of " + root + " kept in " + keep);
        return keep;
    }

    // ---- the start gate (as MHGU's; a save that went to this device can't happen here: the runtime opens none)
    static final int NOT_FOUND = 1, NO_PERMISSION = 2, EMPTY = 3, CONFLICT = 4, IN_USE = 6;

    static final class Problem {
        int kind;
        File folder;
        List<File> copies = new ArrayList<>();  // CONFLICT: the sync app's other versions of the save's files
        String device;
        long since;
    }

    static Problem check(Context c, boolean ignoreInUse) {
        File f = chosen(c);
        if (f == null) return null;
        Problem p = new Problem();
        p.folder = f;
        if (!f.isDirectory() || !f.canWrite()) {
            p.kind = hasAccess(c) ? NOT_FOUND : NO_PERMISSION;
            return p;
        }
        p.copies = conflicts(f);
        if (!p.copies.isEmpty()) {
            p.kind = CONFLICT;
            return p;
        }
        Info i = describe(f);
        if (!i.valid) {
            if (!AppHome.read(file(c, "save_folder_state.txt")).isEmpty()) {
                p.kind = EMPTY;
                return p;
            }
            return null;
        }
        if (!ignoreInUse) {
            Map<String, String> m = keys(new File(f, MARKER));
            long time = parse(m.get("time"));
            long now = System.currentTimeMillis() / 1000;
            if (time > 0 && now - time < FRESH_S && now >= time - 60 && m.containsKey("id") && !m.get("id").equals(deviceId(c))) {
                p.kind = IN_USE;
                p.device = m.containsKey("name") ? m.get("name") : "?";
                p.since = time * 1000;
                return p;
            }
        }
        return null;
    }

    private static long parse(String s) {
        try {
            return s == null ? 0 : Long.parseLong(s.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /** The other versions of the save's files a sync app left beside them ("user1.sync-conflict-...", "system (1)"...). */
    static List<File> conflicts(File root) {
        List<File> out = new ArrayList<>();
        for (String t : TITLES) {
            File[] kids = dataDir(root, t).listFiles(File::isFile);
            if (kids == null) continue;
            Arrays.sort(kids, (a, b) -> a.getName().compareTo(b.getName()));
            for (File k : kids) if (base(k.getName()) != null) out.add(k);
        }
        return out;
    }

    /** The save file a conflict copy is a version of ("user1" for "user1 (1)"), or null for a save file itself. */
    static String base(String name) {
        if (name.endsWith(".mh4u-copy")) return null;
        for (String f : FILES) if (name.startsWith(f) && name.length() > f.length()) return f;
        return null;
    }

    /**
     * CONFLICT. other: each save file takes its newest other version; else the files stay as they are. Either way
     * everything replaced or left over is backed up here first, then the other versions are removed there.
     */
    static void settleConflict(Context c, File root, boolean other) throws IOException {
        String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
        File keep = new File(backups(c), stamp + "-conflict");
        for (String t : TITLES) {
            File d = dataDir(root, t);
            File[] kids = d.listFiles(File::isFile);
            if (kids == null) continue;
            Map<String, File> newest = new LinkedHashMap<>();
            for (File k : kids) {
                String b = base(k.getName());
                if (b == null) continue;
                copyChecked(k, new File(keep, t + "/" + k.getName()));
                File best = newest.get(b);
                if (best == null || k.lastModified() > best.lastModified()) newest.put(b, k);
            }
            if (other)
                for (Map.Entry<String, File> e : newest.entrySet()) {
                    File place = new File(d, e.getKey());
                    if (place.isFile()) copyChecked(place, new File(keep, t + "/" + e.getKey()));
                    copyChecked(e.getValue(), place);
                }
            for (File k : kids) if (base(k.getName()) != null && !k.delete()) Log.w(TAG, "save folder: couldn't remove " + k);
        }
        Log.i(TAG, "save folder: " + (other ? "the other versions taken" : "the save kept as it was") + ", the rest in " + keep);
    }

    /** The newest time of the save's own files, and of the other versions. */
    static long[] conflictTimes(File root) {
        long mine = 0, other = 0;
        for (String t : TITLES) {
            File[] kids = dataDir(root, t).listFiles(File::isFile);
            if (kids == null) continue;
            for (File k : kids) {
                if (base(k.getName()) != null) other = Math.max(other, k.lastModified());
                else if (Arrays.asList(FILES).contains(k.getName())) mine = Math.max(mine, k.lastModified());
            }
        }
        return new long[] {mine, other};
    }

    /** This device's in-use mark in a folder it no longer uses (another device's stays). */
    static void unmark(Context c, File folder) {
        File m = new File(folder, MARKER);
        if (deviceId(c).equals(keys(m).get("id"))) m.delete();
    }

    static void started(Context c) {
        File f = chosen(c);
        if (f == null) return;
        Info i = describe(f);
        if (i.valid) AppHome.writeText(file(c, "save_folder_state.txt"), "time=" + i.time + "\nhunters=" + i.hunters + "\n");
        AppHome.writeText(new File(f, MARKER), "id=" + deviceId(c) + "\nname=" + deviceName(c) + "\ntime="
                + System.currentTimeMillis() / 1000 + "\n");
    }

    static void forget(Context c) {
        file(c, "save_folder.txt").delete();
        file(c, "save_folder_state.txt").delete();
    }

    // ---- this device (device.txt, shared with app_native/android_main.cpp)
    private static Map<String, String> device(Context c) {
        File f = file(c, "device.txt");
        Map<String, String> m = keys(f);
        if (!m.containsKey("id") || m.get("id").isEmpty()) {
            byte[] b = new byte[8];
            new SecureRandom().nextBytes(b);
            StringBuilder hex = new StringBuilder();
            for (byte x : b) hex.append(String.format(Locale.ROOT, "%02x", x));
            m.put("id", hex.toString());
            m.put("name", Build.MODEL == null || Build.MODEL.isEmpty() ? "Android" : Build.MODEL);
            AppHome.writeText(f, "id=" + m.get("id") + "\nname=" + m.get("name") + "\n");
        }
        return m;
    }
    static String deviceId(Context c) { return device(c).get("id"); }
    static String deviceName(Context c) {
        String n = device(c).get("name");
        return n == null || n.isEmpty() ? Build.MODEL : n;
    }
}

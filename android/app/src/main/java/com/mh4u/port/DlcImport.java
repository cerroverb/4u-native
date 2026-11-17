package com.mh4u.port;

import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * Settings -> Import DLC and event quests (1.0.1): the player's own add-on content (DLC) titles. MH4U asks the console
 * which contents of its add-on content title (0004008C00126300 for the USA game) are installed and reads each one's
 * RomFS; the runtime (mh4n native/src/services/fs/dlc_titles.cpp) answers from the titles it finds in the game's own
 * folder, in Azahar's layout:
 *
 *   user/sdmc/Nintendo 3DS/<32 zeros>/<32 zeros>/title/0004008c/<low>/content/00000000.tmd
 *                                                                     content/00000000/<content id>.app (DLC)
 *
 * They come from the player's own setup: the DLC installed in Azahar (its user folder, its sdmc folder, or the title
 * folder itself), copied as it is; or a decrypted .cia of their own, installed here as Azahar installs one (its title
 * metadata and its contents; the ticket is not needed). The app includes none, downloads none and decrypts nothing:
 * an encrypted .cia or content, one Azahar compressed, or one it encrypted for its own console is refused with a
 * message that says what is needed. The checks are the runtime's: a content is readable when it is an NCCH with the
 * NoCrypto flag and a RomFS.
 */
final class DlcImport {
    private static final String TAG = "MH4U";
    static final long TID_HIGH_DLC = 0x0004008CL;
    private static final String ZEROS = SaveFolder.ZEROS;

    /** A DLC title found: a title folder (installed) or a .cia. */
    static final class Title {
        long titleId;
        int version;
        File folder, cia;
        final List<Content> contents = new ArrayList<>();
        long bytes;           // what is copied
        String problem;       // null: it can be imported
        int readable;

        String id() { return String.format(Locale.ROOT, "%016X", titleId); }
        String low() { return String.format(Locale.ROOT, "%08x", titleId & 0xffffffffL); }
        String from() { return (cia != null ? cia : folder).getAbsolutePath(); }
    }

    static final class Content {
        long id, size, offset;  // offset: in the .cia (-1: not in it)
        int index, type;
        File app;               // the installed .app (null for a .cia)
        String state;           // null: readable; else why not (a key of why())
    }

    interface Progress { void at(long done, long total); }

    private final boolean es;

    DlcImport(boolean es) { this.es = es; }

    private String t(String spanish, String english) { return es ? spanish : english; }

    // ---- bytes
    private static long be32(byte[] b, int o) {
        return (b[o] & 0xFFL) << 24 | (b[o + 1] & 0xFFL) << 16 | (b[o + 2] & 0xFFL) << 8 | (b[o + 3] & 0xFFL);
    }
    private static int be16(byte[] b, int o) { return (b[o] & 0xFF) << 8 | (b[o + 1] & 0xFF); }
    private static long be64(byte[] b, int o) { return be32(b, o) << 32 | be32(b, o + 4); }
    private static long le32(byte[] b, int o) {
        return (b[o] & 0xFFL) | (b[o + 1] & 0xFFL) << 8 | (b[o + 2] & 0xFFL) << 16 | (b[o + 3] & 0xFFL) << 24;
    }
    private static long align(long x, long a) { return (x + a - 1) / a * a; }
    private static boolean magic(byte[] b, int o, String m) {
        for (int i = 0; i < 4; i++) if (b[o + i] != m.charAt(i)) return false;
        return true;
    }
    private static byte[] read(RandomAccessFile f, long at, int size) throws IOException {
        if (at < 0 || size < 0 || at + size > f.length()) throw new IOException("short");
        byte[] b = new byte[size];
        f.seek(at);
        f.readFully(b);
        return b;
    }

    /** The title metadata's fields and chunk records (Azahar's TitleMetadata::Load); false if it is not one. */
    private static boolean parseTmd(byte[] tmd, Title title) {
        if (tmd.length < 4) return false;
        int sig;
        switch ((int) be32(tmd, 0)) {
            case 0x10000: case 0x10003: sig = 0x200; break;
            case 0x10001: case 0x10004: sig = 0x100; break;
            case 0x10002: case 0x10005: sig = 0x3C; break;
            default: return false;
        }
        int body = (int) align(sig + 4, 0x40);
        if (tmd.length < body + 0x9C4) return false;
        int count = be16(tmd, body + 0x9E);
        if (tmd.length < body + 0x9C4 + count * 0x30) return false;
        title.titleId = be64(tmd, body + 0x4C);
        title.version = be16(tmd, body + 0x9C);
        title.contents.clear();
        for (int i = 0; i < count; i++) {
            int c = body + 0x9C4 + i * 0x30;
            Content k = new Content();
            k.id = be32(tmd, c);
            k.index = be16(tmd, c + 4);
            k.type = be16(tmd, c + 6);
            k.size = be64(tmd, c + 8);
            title.contents.add(k);
        }
        return true;
    }

    /** DLC keeps its .app files in content/00000000/ (Azahar's GetTitleContentPath: content 1 is optional). */
    private static boolean inSubfolder(Title t) {
        return t.contents.size() > 1 && (t.contents.get(1).type & 0x4000) != 0;
    }

    /** null if the NCCH at `at` can be read by the runtime, else why not: encrypted, compressed, notncch, noromfs. */
    private static String ncchState(RandomAccessFile f, long at) throws IOException {
        byte[] h = read(f, at, 0x200);
        if (magic(h, 0, "Z3DS")) return "compressed";
        if (!magic(h, 0x100, "NCCH")) return "notncch";
        if ((h[0x188 + 7] & 0x04) == 0) return "encrypted";
        long unit = 0x200L << (h[0x188 + 6] & 0xFF);
        long size = le32(h, 0x1B4) * unit;
        if (size <= 0x1000) return "noromfs";
        if (at + le32(h, 0x1B0) * unit + size > f.length()) return "short";
        return null;
    }

    // ---- reading what the player has
    /** An installed title folder (.../0004008c/<low>/, with content/<8 hex>.tmd); null if it isn't one. */
    static Title readInstalled(File folder) {
        File content = new File(folder, "content");
        String[] names = content.list();
        if (names == null) return null;
        String tmd = null;
        for (String n : names)
            if (n.length() == 12 && n.toLowerCase(Locale.ROOT).endsWith(".tmd") && n.substring(0, 8).matches("[0-9a-fA-F]{8}")
                && (tmd == null || Long.parseLong(n.substring(0, 8), 16) < Long.parseLong(tmd.substring(0, 8), 16)))
                tmd = n;
        if (tmd == null) return null;
        Title t = new Title();
        t.folder = folder;
        File tmdFile = new File(content, tmd);
        try (RandomAccessFile f = new RandomAccessFile(tmdFile, "r")) {
            if (f.length() > (1 << 20) || !parseTmd(read(f, 0, (int) f.length()), t)) return null;
        } catch (IOException e) {
            return null;
        }
        if ((t.titleId >>> 32) != TID_HIGH_DLC) return null;
        t.bytes = tmdFile.length();
        boolean sub = inSubfolder(t);
        for (Content k : t.contents) {
            String name = String.format(Locale.ROOT, "%08x.app", k.id);
            File first = new File(content, (sub ? "00000000/" : "") + name), second = new File(content, (sub ? "" : "00000000/") + name);
            k.app = first.isFile() || !second.isFile() ? first : second;
            if (!k.app.isFile()) { k.state = "missing"; continue; }
            try (RandomAccessFile f = new RandomAccessFile(k.app, "r")) {
                k.state = ncchState(f, 0);
            } catch (IOException e) {
                k.state = "short";
            }
            t.bytes += k.app.length();
        }
        return t;
    }

    /** A .cia's title (Azahar's CIAContainer: header, certificates, ticket, title metadata, the contents present); null if it isn't a DLC .cia. */
    static Title readCia(File cia) {
        try (RandomAccessFile f = new RandomAccessFile(cia, "r")) {
            if (f.length() < 0x2040) return null;
            byte[] head = read(f, 0, 0x2040);
            if (le32(head, 0) != 0x2020) return null;
            long certOff = align(0x2020, 64), tikOff = align(certOff + le32(head, 8), 64);
            long tmdOff = align(tikOff + le32(head, 12), 64);
            long tmdSize = le32(head, 16);
            if (tmdSize > (1 << 20)) return null;
            Title t = new Title();
            t.cia = cia;
            if (!parseTmd(read(f, tmdOff, (int) tmdSize), t) || (t.titleId >>> 32) != TID_HIGH_DLC) return null;
            long pos = align(tmdOff + tmdSize, 64);
            t.bytes = tmdSize;
            for (int i = 0; i < t.contents.size(); i++) {
                Content k = t.contents.get(i);
                boolean present = i < 0x2000 * 8 && (head[0x20 + (i >> 3)] & (0x80 >> (i & 7))) != 0;
                if (!present) { k.offset = -1; k.state = "missing"; continue; }
                k.offset = pos;
                pos += k.size;
                if ((k.type & 1) != 0) { k.state = "encrypted"; continue; }  // the .cia's own encryption
                if (k.offset + k.size > f.length()) { k.state = "short"; continue; }
                k.state = ncchState(f, k.offset);
                t.bytes += k.size;
            }
            return t;
        } catch (IOException e) {
            return null;
        }
    }

    /** Whether a folder looks like it holds DLC (quick: for the folder browser's ✓). */
    static boolean holds(File dir) {
        if (new File(dir, "content").isDirectory() && dir.getParentFile() != null
            && dir.getParentFile().getName().equalsIgnoreCase("0004008c")) return true;
        if (dir.getName().equalsIgnoreCase("0004008c")) return true;
        for (File base : new File[] {dir, new File(dir, "sdmc")}) {
            File n3ds = base.getName().equals("Nintendo 3DS") ? base : new File(base, "Nintendo 3DS");
            if (new File(n3ds, ZEROS + "/" + ZEROS + "/title/0004008c").isDirectory()) return true;
        }
        String[] names = dir.list();
        if (names != null)
            for (String n : names) if (n.toLowerCase(Locale.ROOT).endsWith(".cia")) return true;
        return false;
    }

    /**
     * The DLC titles in a folder chosen: the folder is a title folder, a 0004008c folder, an Azahar user or sdmc folder
     * (any id folders under Nintendo 3DS), or holds .cia files (DLC ones; the game's or its update's are not DLC).
     */
    static List<Title> find(File dir) {
        List<Title> out = new ArrayList<>();
        if (dir == null || !dir.isDirectory()) return out;
        Title here = readInstalled(dir);
        if (here != null) { out.add(here); return out; }
        List<File> dlcRoots = new ArrayList<>();
        if (dir.getName().equalsIgnoreCase("0004008c")) dlcRoots.add(dir);
        if (dir.getName().equalsIgnoreCase("title")) dlcRoots.add(new File(dir, "0004008c"));
        for (File base : new File[] {dir, new File(dir, "sdmc")}) {
            File n3ds = base.getName().equals("Nintendo 3DS") ? base : new File(base, "Nintendo 3DS");
            File[] ids0 = n3ds.listFiles(File::isDirectory);
            if (ids0 == null) continue;
            for (File id0 : ids0) {
                File[] ids1 = id0.listFiles(File::isDirectory);
                if (ids1 != null) for (File id1 : ids1) dlcRoots.add(new File(id1, "title/0004008c"));
            }
        }
        for (File root : dlcRoots) {
            File[] lows = root.listFiles(File::isDirectory);
            if (lows == null) continue;
            Arrays.sort(lows);
            for (File low : lows) {
                Title t = readInstalled(low);
                if (t != null) out.add(t);
            }
        }
        File[] cias = dir.listFiles(f -> f.isFile() && f.getName().toLowerCase(Locale.ROOT).endsWith(".cia"));
        if (cias != null) {
            Arrays.sort(cias);
            for (File c : cias) {
                Title t = readCia(c);
                if (t != null) out.add(t);
            }
        }
        // one title found twice (an installed folder and its .cia): the first one is kept
        List<Title> unique = new ArrayList<>();
        for (Title t : out) {
            boolean seen = false;
            for (Title u : unique) seen |= u.titleId == t.titleId;
            if (!seen) unique.add(t);
        }
        return unique;
    }

    // ---- checking it
    private String why(String state) {
        switch (state) {
            case "encrypted":
                return t("está cifrado. El juego solo puede leer una copia descifrada, la tuya (un .cia descifrado, o el DLC como Azahar instala un .cia descifrado). La app no descifra nada.",
                         "is encrypted. The game can only read a decrypted copy of your own (a decrypted .cia, or the DLC as Azahar installs a decrypted .cia). The app decrypts nothing.");
            case "compressed":
                return t("está comprimido por Azahar (la opción de comprimir los CIA instalados). Apaga esa opción en Azahar e instala el DLC de nuevo, o elige el .cia descifrado.",
                         "was compressed by Azahar (its option to compress installed CIA content). Turn that option off in Azahar and install the DLC again, or choose the decrypted .cia.");
            case "notncch":
                return t("no se puede leer: Azahar lo guardó cifrado para su propia consola, o la copia está dañada. Usa el .cia descifrado, el tuyo.",
                         "can't be read: Azahar stored it encrypted for its own console, or the copy is damaged. Use your own decrypted .cia.");
            case "noromfs":
                return t("no tiene datos (RomFS).", "has no data (RomFS).");
            case "short":
                return t("termina antes de tiempo (¿una copia cortada?).", "ends too early (a copy cut short?).");
            default:
                return t("no está.", "is not there.");
        }
    }

    /**
     * The checks a title has to pass, with `game` the player's game (00126300 or 00126100): sets t.problem (null: it
     * can be imported) and t.readable.
     */
    void check(Title t, String game) {
        t.readable = 0;
        String firstState = null;
        for (Content k : t.contents) {
            if (k.state == null) t.readable++;
            else if (firstState == null && !k.state.equals("missing")) firstState = k.state;
        }
        String low = t.low();
        if (!low.equals("00126300") && !low.equals("00126100"))
            t.problem = t("El DLC " + t.id() + " no es de Monster Hunter 4 Ultimate.", "The DLC " + t.id() + " is not Monster Hunter 4 Ultimate's.");
        else if (!low.equals(game))
            t.problem = t("Ese DLC es de la versión " + (low.equals("00126100") ? "europea" : "de EE. UU.") + " y tu juego es de la otra: solo sirve el de la misma región.",
                          "That DLC is for the " + (low.equals("00126100") ? "European" : "USA") + " game and yours is the other one: only the same region's works.");
        else if (t.readable == 0)
            t.problem = t("El DLC " + t.id() + " ", "The DLC " + t.id() + " ") + why(firstState != null ? firstState : "missing");
        else if (firstState != null)  // some contents can be read, some can't: one bad copy, refused as a whole
            t.problem = t("En el DLC " + t.id() + " hay contenido que ", "Part of the DLC " + t.id() + " ") + why(firstState);
        else
            t.problem = null;
    }

    // ---- bringing it in
    /** Where the runtime finds a title: the game's own folder's SD card (SaveFolder.local), in Azahar's layout. */
    static File target(File userRoot, Title t) {
        return new File(userRoot, "sdmc/Nintendo 3DS/" + ZEROS + "/" + ZEROS + "/title/0004008c/" + t.low());
    }

    /**
     * Copies (a title folder) or installs (a .cia) the title into the game's folder: written to <low>.importing first,
     * then put in place of the one that was there. Nothing is changed if it fails.
     */
    static File install(Title t, File userRoot, Progress progress) throws IOException {
        File place = target(userRoot, t);
        File work = new File(place.getParentFile(), t.low() + ".importing");
        SaveFolder.deleteTree(work);
        File content = new File(work, "content");
        if (!content.mkdirs()) throw new IOException("couldn't make " + content);
        long[] done = {0};
        boolean sub = inSubfolder(t);
        try {
            if (t.cia != null) {
                try (RandomAccessFile f = new RandomAccessFile(t.cia, "r")) {
                    byte[] head = read(f, 0, 0x2040);
                    long certOff = align(0x2020, 64), tikOff = align(certOff + le32(head, 8), 64);
                    long tmdOff = align(tikOff + le32(head, 12), 64);
                    writePart(f, tmdOff, le32(head, 16), new File(content, "00000000.tmd"), done, t.bytes, progress);
                    for (Content k : t.contents) {
                        if (k.offset < 0 || k.state != null) continue;
                        File app = new File(content, (sub ? "00000000/" : "") + String.format(Locale.ROOT, "%08x.app", k.id));
                        writePart(f, k.offset, k.size, app, done, t.bytes, progress);
                    }
                }
            } else {
                File[] tmds = new File(t.folder, "content").listFiles(f -> f.getName().toLowerCase(Locale.ROOT).endsWith(".tmd"));
                if (tmds != null)
                    for (File tmd : tmds) copy(tmd, new File(content, tmd.getName().toLowerCase(Locale.ROOT)), done, t.bytes, progress);
                for (Content k : t.contents) {
                    if (k.state != null) continue;
                    copy(k.app, new File(content, (sub ? "00000000/" : "") + String.format(Locale.ROOT, "%08x.app", k.id)), done, t.bytes, progress);
                }
            }
            File old = new File(place.getParentFile(), t.low() + ".old");
            SaveFolder.deleteTree(old);
            if (place.exists() && !place.renameTo(old)) throw new IOException("couldn't move " + place + " aside");
            if (!work.renameTo(place)) {
                old.renameTo(place);
                throw new IOException("couldn't put " + work + " in place");
            }
            SaveFolder.deleteTree(old);
        } catch (IOException e) {
            SaveFolder.deleteTree(work);
            throw e;
        }
        Log.i(TAG, "dlc import: DLC title " + t.id() + " (version " + t.version + ", " + t.readable + " of " + t.contents.size()
                   + " contents) from " + t.from() + " into " + place);
        return place;
    }

    private static void writePart(RandomAccessFile f, long at, long size, File to, long[] done, long total, Progress p) throws IOException {
        File parent = to.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("couldn't make " + parent);
        byte[] buf = new byte[1 << 16];
        f.seek(at);
        try (FileOutputStream out = new FileOutputStream(to)) {
            for (long left = size; left > 0; ) {
                int n = f.read(buf, 0, (int) Math.min(buf.length, left));
                if (n <= 0) throw new IOException("short read in " + to.getName());
                out.write(buf, 0, n);
                left -= n;
                done[0] += n;
                if (p != null) p.at(done[0], total);
            }
            out.getFD().sync();
        }
    }

    private static void copy(File from, File to, long[] done, long total, Progress p) throws IOException {
        try (RandomAccessFile f = new RandomAccessFile(from, "r")) {
            writePart(f, 0, f.length(), to, done, total, p);
        }
    }
}

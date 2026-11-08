package com.mh4u.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;

/**
 * The player's dump, read in place: a decrypted .3ds/.cci (NCSD), .cxi (NCCH) or .cia. The same rules as the
 * runtime's dump_source.cpp (mh4n native/src/services/fs), which serves the RomFS straight from these files: here
 * the small files the loader reads are written (exheader.bin, the decompressed code.bin, icon.bin) with source.txt.
 * Errors are messages a player can act on, in Spanish or English.
 */
final class Dump {
    static final long USA = 0x00126300L, EUR = 0x00126100L;

    static final class Ncch {
        long base, programId, exefsOffset, exefsSize, romfsOffset, romfsSize, ciaTitleId;
        int exheaderSize, ciaVersion;
        boolean noCrypto, cia;
    }

    static final class Problem extends Exception {
        Problem(String m) { super(m); }
    }

    private final boolean es;

    Dump(boolean es) { this.es = es; }

    private String t(String spanish, String english) { return es ? spanish : english; }

    private static long le32(byte[] b, int o) {
        return (b[o] & 0xFFL) | (b[o + 1] & 0xFFL) << 8 | (b[o + 2] & 0xFFL) << 16 | (b[o + 3] & 0xFFL) << 24;
    }
    private static long le64(byte[] b, int o) { return le32(b, o) | le32(b, o + 4) << 32; }
    private static long be32(byte[] b, int o) {
        return (b[o] & 0xFFL) << 24 | (b[o + 1] & 0xFFL) << 16 | (b[o + 2] & 0xFFL) << 8 | (b[o + 3] & 0xFFL);
    }
    private static int be16(byte[] b, int o) { return (b[o] & 0xFF) << 8 | (b[o + 1] & 0xFF); }
    private static long be64(byte[] b, int o) { return be32(b, o) << 32 | be32(b, o + 4); }
    private static long align(long x, long a) { return (x + a - 1) / a * a; }
    private static boolean magic(byte[] b, int o, String m) {
        for (int i = 0; i < 4; i++) if (b[o + i] != m.charAt(i)) return false;
        return true;
    }

    private byte[] read(RandomAccessFile f, long at, int size) throws Problem {
        try {
            if (at < 0 || at + size > f.length()) throw new Problem(t("El archivo termina antes de tiempo (¿una copia cortada?).",
                                                                      "The file ends too early (a copy cut short?)."));
            byte[] b = new byte[size];
            f.seek(at);
            f.readFully(b);
            return b;
        } catch (IOException e) {
            throw new Problem(t("No se pudo leer el archivo.", "The file could not be read."));
        }
    }

    private Ncch ncchAt(RandomAccessFile f, long base) throws Problem {
        byte[] h = read(f, base, 0x200);
        if (!magic(h, 0x100, "NCCH")) throw new Problem(t("No es un juego de 3DS.", "This is not a 3DS game."));
        Ncch n = new Ncch();
        n.base = base;
        n.programId = le64(h, 0x118);
        n.exheaderSize = (int) le32(h, 0x180);
        long unit = 0x200L << (h[0x188 + 6] & 0xFF);
        n.exefsOffset = le32(h, 0x1A0) * unit;
        n.exefsSize = le32(h, 0x1A4) * unit;
        n.romfsOffset = le32(h, 0x1B0) * unit;
        n.romfsSize = le32(h, 0x1B4) * unit;
        n.noCrypto = (h[0x188 + 7] & 0x04) != 0;
        return n;
    }

    /** The game's NCCH: an NCSD's partition 0, a CIA's content 0, or an NCCH at 0. */
    Ncch find(RandomAccessFile f) throws Problem {
        byte[] head = read(f, 0, 0x200);
        if (magic(head, 0x100, "NCSD")) {
            if (le32(head, 0x124) == 0) throw new Problem(t("La partición del juego está vacía.", "The game's partition is empty."));
            return ncchAt(f, le32(head, 0x120) * 0x200L);
        }
        if (magic(head, 0x100, "NCCH")) return ncchAt(f, 0);
        if (le32(head, 0) == 0x2020) {
            long certOff = align(le32(head, 0), 64), tikOff = align(certOff + le32(head, 8), 64);
            long tmdOff = align(tikOff + le32(head, 12), 64);
            int tmdSize = (int) le32(head, 16);
            long contentOff = align(tmdOff + tmdSize, 64);
            byte[] tmd = read(f, tmdOff, tmdSize);
            int sig = (int) be32(tmd, 0), sigLen;
            switch (sig) {
                case 0x10000: case 0x10003: sigLen = 0x200 + 0x3C; break;
                case 0x10001: case 0x10004: sigLen = 0x100 + 0x3C; break;
                case 0x10002: case 0x10005: sigLen = 0x3C + 0x40; break;
                default: throw new Problem(t("Este .cia no es uno que la app conozca.", "This .cia is not one the app knows."));
            }
            int th = 4 + sigLen, chunks = th + 0xC4 + 0x24 * 64;
            long titleId = be64(tmd, th + 0x4C);
            int version = be16(tmd, th + 0x9C), count = be16(tmd, th + 0x9E);
            long pos = contentOff;
            for (int i = 0; i < count && chunks + i * 0x30 + 0x10 <= tmd.length; i++) {
                int c = chunks + i * 0x30;
                int index = be16(tmd, c + 4), type = be16(tmd, c + 6);
                long size = be64(tmd, c + 8);
                if (index == 0) {
                    if ((type & 1) != 0)
                        throw new Problem(t("El .cia está cifrado: tiene que ser una copia descifrada, la tuya.",
                                            "The .cia is encrypted: it has to be a decrypted copy of your own."));
                    Ncch n = ncchAt(f, pos);
                    n.cia = true;
                    n.ciaTitleId = titleId;
                    n.ciaVersion = version;
                    return n;
                }
                pos += size;
            }
            throw new Problem(t("El .cia no tiene contenido del juego.", "The .cia has no game content."));
        }
        throw new Problem(t("No es un juego de 3DS (.3ds, .cci, .cxi o .cia).", "This is not a 3DS game (.3ds, .cci, .cxi or .cia)."));
    }

    byte[] exefsFile(RandomAccessFile f, Ncch n, String name) throws Problem {
        if (n.exefsSize < 0x200) throw new Problem(t("El juego no tiene ExeFS.", "The game has no ExeFS."));
        byte[] h = read(f, n.base + n.exefsOffset, 0x200);
        for (int i = 0; i < 10; i++) {
            int o = i * 16;
            int len = 0;
            while (len < 8 && h[o + len] != 0) len++;
            String entry = new String(h, o, len, StandardCharsets.US_ASCII);
            long off = le32(h, o + 8), size = le32(h, o + 12);
            if (size != 0 && entry.equals(name)) {
                if (0x200 + off + size > n.exefsSize) throw new Problem(t("El ExeFS está dañado.", "The ExeFS is damaged."));
                return read(f, n.base + n.exefsOffset + 0x200 + off, (int) size);
            }
        }
        return null;
    }

    /** Backward LZ77, as the 3DS compresses its ExeFS .code (tools/blz.py). */
    static byte[] blz(byte[] data) throws IOException {
        int size = data.length;
        if (size < 8) throw new IOException("blz");
        long topBottom = le32(data, size - 8), originalBottom = le32(data, size - 4);
        int top = (int) (topBottom & 0xFFFFFF), bottom = (int) (topBottom >>> 24);
        if (bottom < 8 || bottom > 11 || top < bottom || top > size) throw new IOException("blz footer");
        int outSize = (int) (size + originalBottom);
        byte[] out = new byte[outSize];
        System.arraycopy(data, 0, out, 0, size);
        int src = size - bottom, dst = outSize, end = size - top;
        while (src > end) {
            int flags = data[--src] & 0xFF;
            for (int i = 0; i < 8; i++) {
                if ((flags & 0x80) == 0) {
                    if (src - end < 1 || dst - end < 1) throw new IOException("blz literal");
                    out[--dst] = data[--src];
                } else {
                    if (src - end < 2) throw new IOException("blz reference");
                    src -= 2;
                    int hi = data[src + 1] & 0xFF, lo = data[src] & 0xFF;
                    int length = (hi >> 4) + 3, disp = (((hi & 0x0F) << 8) | lo) + 3;
                    if (dst - end < length || dst + disp > outSize) throw new IOException("blz reference");
                    for (int k = 0; k < length; k++) {
                        --dst;
                        out[dst] = out[dst + disp];
                    }
                }
                flags = (flags << 1) & 0xFF;
                if (src <= end) break;
            }
        }
        return out;
    }

    /** What a file is, for the folder's list: the game, its update, or nothing of ours (null). */
    String describe(File file) {
        try (RandomAccessFile f = new RandomAccessFile(file, "r")) {
            Ncch n = find(f);
            long id = n.programId;
            if ((id >>> 32) == 0x00040000L && ((id & 0xFFFFFFFFL) == USA || (id & 0xFFFFFFFFL) == EUR)) return "game";
            if ((id >>> 32) == 0x0004000EL && ((id & 0xFFFFFFFFL) == USA || (id & 0xFFFFFFFFL) == EUR)) return "update";
        } catch (Exception ignored) {
        }
        return null;
    }

    /** Writes the game folder from the game and its update; a Problem says what to fix. */
    void prepare(File base, File update, File folder) throws Problem {
        try (RandomAccessFile g = new RandomAccessFile(base, "r"); RandomAccessFile u = new RandomAccessFile(update, "r")) {
            Ncch game = find(g), patch = find(u);
            if (!game.noCrypto || !patch.noCrypto)
                throw new Problem(t("El juego o su actualización están cifrados: tienen que ser copias descifradas, las tuyas.",
                                    "The game or its update is encrypted: they have to be decrypted copies of your own."));
            long gid = game.programId, uid = patch.programId;
            if ((gid >>> 32) != 0x00040000L || ((gid & 0xFFFFFFFFL) != USA && (gid & 0xFFFFFFFFL) != EUR))
                throw new Problem(t("El juego no es Monster Hunter 4 Ultimate.", "The game is not Monster Hunter 4 Ultimate."));
            if ((uid >>> 32) != 0x0004000EL)
                throw new Problem(t("El segundo archivo no es una actualización: tiene que ser el .cia de la actualización 1.3.0.",
                                    "The second file is not an update: it has to be the 1.3.0 update's .cia."));
            if ((uid & 0xFFFFFFFFL) != (gid & 0xFFFFFFFFL))
                throw new Problem(t("La actualización no es de este juego: tienen que ser de la misma región.",
                                    "The update is not this game's: they have to be of the same region."));
            if (patch.exheaderSize == 0) throw new Problem(t("La actualización no tiene exheader.", "The update has no exheader."));
            byte[] exheader = read(u, patch.base + 0x200, 0x800);
            byte[] code = exefsFile(u, patch, ".code");
            if (code == null) throw new Problem(t("La actualización no tiene código.", "The update has no code."));
            if ((exheader[0x0D] & 1) != 0) {
                try {
                    code = blz(code);
                } catch (IOException e) {
                    throw new Problem(t("El código de la actualización está dañado.", "The update's code is damaged."));
                }
            }
            byte[] icon = exefsFile(u, patch, "icon");
            folder.mkdirs();
            write(new File(folder, "exheader.bin"), exheader);
            write(new File(folder, "code.bin"), code);
            if (icon != null) write(new File(folder, "icon.bin"), icon);
            else new File(folder, "icon.bin").delete();
            new File(folder, "romfs.bin").delete();
            new File(folder, "update_romfs.bin").delete();
            write(new File(folder, "source.txt"),
                  ("base=" + base.getAbsolutePath() + "\nupdate=" + update.getAbsolutePath() + "\n").getBytes(StandardCharsets.UTF_8));
            write(new File(folder, "info.txt"), String.format("base game     : program %016X\nupdate        : title %016X, version %d\n"
                    + "input base  : %s, %d bytes\ninput update: %s, %d bytes\n", gid, uid, patch.ciaVersion,
                    base.getAbsolutePath(), base.length(), update.getAbsolutePath(), update.length()).getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            throw new Problem(t("No se pudieron leer los archivos del juego: ", "The game's files could not be read: ") + e.getMessage());
        }
    }

    private static void write(File to, byte[] bytes) throws IOException {
        File tmp = new File(to.getParentFile(), to.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(bytes);
        }
        to.delete();
        if (!tmp.renameTo(to)) throw new IOException(to.getName());
    }
}

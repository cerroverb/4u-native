package com.mh4u.port;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Le entrega el registro armado por LogShareActivity a la app que el jugador eligió, y nada más: un solo archivo, de
 * solo lectura, sólo con el permiso que da el envío. En el proceso ":share", que sigue vivo aunque el juego se haya
 * cerrado por un error. (1.0.1: mh4u-log.txt, o mh4u-log.zip con él adentro cuando es grande: Diag.bundle)
 */
public class LogProvider extends ContentProvider {
    private static final String AUTHORITY = "com.mh4u.native.logs", NAME = "mh4u-log.txt", ZIP = "mh4u-log.zip";

    static File report(Context c) { return report(c, false); }
    static File report(Context c, boolean zip) { return new File(c.getCacheDir(), zip ? ZIP : NAME); }
    static Uri uri() { return uri(false); }
    static Uri uri(boolean zip) { return Uri.parse("content://" + AUTHORITY + "/" + (zip ? ZIP : NAME)); }

    private static boolean zip(Uri uri) { return uri != null && ZIP.equals(uri.getLastPathSegment()); }

    @Override
    public boolean onCreate() { return true; }

    @Override
    public String getType(Uri uri) { return zip(uri) ? "application/zip" : "text/plain"; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(report(getContext(), zip(uri)), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        boolean z = zip(uri);
        String[] columns = projection != null ? projection : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        Object[] row = new Object[columns.length];
        for (int i = 0; i < columns.length; i++)
            row[i] = OpenableColumns.DISPLAY_NAME.equals(columns[i]) ? (z ? ZIP : NAME)
                   : OpenableColumns.SIZE.equals(columns[i]) ? (Object) report(getContext(), z).length() : null;
        MatrixCursor c = new MatrixCursor(columns, 1);
        c.addRow(row);
        return c;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) { return null; }

    @Override
    public int delete(Uri uri, String selection, String[] args) { return 0; }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}

package com.mh4u.port;

import java.util.Locale;

/**
 * The builder's words, English and Spanish (BuilderActivity, BuilderService's notification): the stages, the state of a
 * run and what each failure means for the player. The same as MHGU for Android's, but for the files each game needs.
 */
final class BuilderText {
    private BuilderText() {}

    static String stage(String s, boolean es) {
        switch (s) {
            case "tools": return es ? "Descargar las herramientas" : "Download the tools";
            case "extract": return es ? "Leer tus archivos del juego" : "Read your game files";
            case "recompile": return es ? "Traducir el código del juego" : "Translate the game's code";
            case "compile": return es ? "Compilar" : "Compile";
            case "install": return es ? "Instalar la librería" : "Install the library";
            default: return s;
        }
    }

    /** The overall percentage (the stages weigh what they take on a phone: compiling is most of it), -1 when unknown. */
    static int percent(BuilderService.Job j) {
        double[] from = {0, 6, 10, 30, 99}, span = {6, 4, 20, 69, 1};
        int i = java.util.Arrays.asList(BuilderService.STAGES).indexOf(j.stage);
        if (i < 0) return -1;
        double part = j.total > 0 ? Math.min(1, (double) j.done / j.total) : 0;
        if (j.stage.equals("tools") && !j.detail.startsWith("download")) part = j.total > 0 ? part : .5;
        return (int) Math.round(from[i] + span[i] * part);
    }

    /** One line for the notification and the screen: what is going on now. */
    static String status(BuilderService.Job j, boolean es) {
        if (j.status.equals("paused-hot")) {  // (with the battery's temperature, where the phone gives it: what was read)
            String battery = Float.isNaN(j.batteryC) ? ""
                    : String.format(Locale.ROOT, es ? " (batería a %.0f °C)" : " (battery at %.0f °C)", j.batteryC);
            return es ? "En pausa: el teléfono está muy caliente" + battery + ". Sigue solo cuando se enfríe."
                      : "Paused: the phone is too hot" + battery + ". It goes on by itself when it cools down.";
        }
        if (j.status.equals("warm"))
            return (es ? "Más despacio: el teléfono está caliente. " : "Slower: the phone is warm. ") + now(j, es);
        if (j.status.equals("paused-battery"))
            return es ? "En pausa: queda poca batería. Conecta el cargador." : "Paused: the battery is low. Plug in the charger.";
        return now(j, es);
    }

    private static String now(BuilderService.Job j, boolean es) {
        String s = stage(j.stage, es);
        if (j.stage.equals("tools")) {
            if (j.detail.startsWith("download:"))
                return (es ? "Descargando " : "Downloading ") + toolName(j.detail.substring(9)) + ": " + (j.done >> 20) + " / "
                        + (j.total >> 20) + " MB";
            if (j.detail.startsWith("unpack:")) return (es ? "Preparando " : "Setting up ") + toolName(j.detail.substring(7));
            return s;
        }
        if (j.total > 0 && (j.stage.equals("recompile") || j.stage.equals("compile")))
            return s + ": " + j.done + " / " + j.total + (j.stage.equals("recompile") ? (es ? " funciones" : " functions")
                                                                                 : (es ? " archivos" : " files"));
        return s;
    }

    private static String toolName(String name) {
        return name.equals("python") ? "Python " + BuilderTools.PYTHON.version : name.equals("zig") ? "Zig " + BuilderTools.ZIG.version : name;
    }

    static String minutes(long ms, boolean es) {
        long m = Math.max(0, ms / 60000), s = Math.max(0, ms / 1000 % 60);
        return m > 0 ? String.format(Locale.ROOT, "%d min %02d s", m, s) : s + " s";
    }

    /** A failure in words the player can act on; the tools' own message follows it on the screen. */
    static String failure(String stage, String text, boolean es) {
        switch (stage) {
            case "space":
                return es ? "No hay espacio suficiente en el teléfono. Libera espacio (hace falta alrededor de 1 GB mientras se crea) y vuelve a intentarlo: sigue desde donde quedó."
                          : "There isn't enough free space on the phone. Free some (it needs about 1 GB while it builds) and try again: it goes on from where it stopped.";
            case "download":
                return es ? "No se pudieron descargar las herramientas. Revisa la conexión a internet y vuelve a intentarlo: la descarga sigue desde donde quedó."
                          : "The tools couldn't be downloaded. Check the internet connection and try again: the download goes on from where it stopped.";
            case "tools":
                return es ? "Las herramientas de esta versión de la app no coinciden con la app. Instala la app de nuevo desde la página oficial."
                          : "This version of the app's tools don't match the app. Install the app again from the official page.";
            case "dest":
                return es ? "No se puede escribir en la carpeta del juego. Elige una carpeta en la memoria del teléfono o en la tarjeta SD a la que el juego tenga acceso."
                          : "The game's folder can't be written. Choose a folder in the phone's storage or on an SD card the game has access to.";
            case "extract": case "version": case "mods":
                return es ? "Tus archivos del juego no son los que hacen falta: el juego y la actualización 1.3.0 de América (o 1.1.0 de Europa), descifrados, de tu propia copia. Si usas un mod de código, tiene que ser para esa versión."
                          : "Your game files aren't the ones needed: the game and its USA 1.3.0 update (or the European 1.1.0), decrypted, from your own copy. A code mod has to be for that version.";
            case "memory":
                return es ? "El teléfono se quedó sin memoria. Cierra las demás apps y vuelve a intentarlo: sigue desde donde quedó."
                          : "The phone ran out of memory. Close the other apps and try again: it goes on from where it stopped.";
            case "recompile": case "shaders": case "compile": case "link":
                return es ? "La herramienta falló al crear el código. Vuelve a intentarlo (sigue desde donde quedó); si vuelve a fallar, comparte el registro de la creación."
                          : "The tool failed while building the code. Try again (it goes on from where it stopped); if it fails again, share the build's log.";
            case "install":
                return es ? "No se pudo copiar la librería a la carpeta del juego. Revisa el espacio y el acceso a la carpeta."
                          : "The library couldn't be copied to the game's folder. Check the space and the access to the folder.";
            default:
                return es ? "La creación se detuvo. Vuelve a intentarlo: sigue desde donde quedó." : "The build stopped. Try again: it goes on from where it stopped.";
        }
    }
}

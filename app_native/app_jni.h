// Calls into the app's Java side (android_main.cpp), from the SDL thread: the interface's thread.
#pragma once

// A static void method of GameActivity without arguments: "showSaveImport", "showSaveExport", "showGameFolder",
// "showShareLog", "showLicenses", "showDriverImport", "showCheatImport", "showModImport",
// "showModFolderImport", "restartApp".
void app_call_activity(const char* method);

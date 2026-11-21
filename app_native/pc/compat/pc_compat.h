// MH4U for PC: the few POSIX calls of the shared sources that MinGW spells otherwise, included before every C++ source of
// MH4U.exe (pc.cmake: -include). The Android build never sees it.
#pragma once

#ifdef __cplusplus
#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// mkdir(path, mode): Windows' has no mode
inline int mkdir(const char* path, unsigned) { return _mkdir(path); }
// a FILE's lock (diag.cpp's log cap)
inline void flockfile(FILE* f) { _lock_file(f); }
inline void funlockfile(FILE* f) { _unlock_file(f); }
// the local time into the caller's struct (verify_files.cpp)
inline struct tm* localtime_r(const time_t* t, struct tm* out) { return localtime_s(out, t) == 0 ? out : nullptr; }
// a file written beside its place put in its place: Windows' rename never replaces a file (pc_platform.cpp)
bool pc_replace_file(const char* from, const char* to);
// strstr without regard to case (first_run.cpp's GPU names)
inline const char* strcasestr(const char* hay, const char* needle) {
    const size_t n = strlen(needle);
    for (; *hay; ++hay)
        if (_strnicmp(hay, needle, n) == 0) return hay;
    return n == 0 ? hay : nullptr;
}
#endif

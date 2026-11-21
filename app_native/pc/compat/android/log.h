// MH4U for PC: Android's log (liblog) for the shared sources that write to it beside log.txt. On PC everything they say
// goes to log.txt (stderr) already, so these say nothing more. Only the PC build sees this folder (pc.cmake).
#pragma once

enum { ANDROID_LOG_UNKNOWN, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN,
       ANDROID_LOG_ERROR, ANDROID_LOG_FATAL, ANDROID_LOG_SILENT };

#ifdef __cplusplus
#include <cstdarg>
inline int __android_log_print(int, const char*, const char*, ...) { return 0; }
inline int __android_log_vprint(int, const char*, const char*, va_list) { return 0; }
inline int __android_log_write(int, const char*, const char*) { return 0; }
#endif

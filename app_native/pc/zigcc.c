/*
 * zigcc.exe: Zig's C compiler for the PC builder (pc_builder.cpp; mh4n tools/pc_build.py gives it to tools/build_game.py
 * as --cc), the PC's counterpart of the Android app's libzigcc.so (builder/toolrun.c). build_game.py runs a compiler as one
 * program with the compiler's arguments; Zig's is `zig cc` with a target, so this program runs
 *
 *     zig.exe cc -target x86_64-windows-gnu -fno-sanitize=undefined -g0 ARGS...
 *
 * with zig.exe from MH4U_ZIG (the builder's own, which pc_builder.cpp names), else beside this program. The game's code
 * library is the same C as llvm-mingw's build of it, made by Zig's clang for the same target (MinGW-w64, UCRT). zig cc
 * turns on trapping checks of undefined behaviour unless told not to, and writes debug information: the game code relies
 * on -fwrapv and its friends, not on those checks, and its library needs no debug information. `-lm` is left out: on
 * Windows the C library has the math functions, and Zig's has no libm to link.
 *
 * Its exit code is the compiler's; the build's job object (pc_builder.cpp) ends it with the build.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

/* An argument for a Windows command line (the C runtime's rules: quotes, and the backslashes before one doubled) */
static void add_arg(wchar_t *cmd, size_t n, const wchar_t *arg) {
    size_t at = wcslen(cmd);
    if (at && at < n - 1) cmd[at++] = L' ';
    int quote = !*arg || wcspbrk(arg, L" \t\"") != NULL;
    if (quote && at < n - 1) cmd[at++] = L'"';
    for (const wchar_t *p = arg; *p && at < n - 4; p++) {
        size_t bs = 0;
        while (p[bs] == L'\\') bs++;
        if (bs && (p[bs] == L'"' || (!p[bs] && quote))) {
            for (size_t k = 0; k < bs * 2 && at < n - 4; k++) cmd[at++] = L'\\';
            p += bs - 1;
            continue;
        }
        if (*p == L'"') cmd[at++] = L'\\';
        cmd[at++] = *p;
    }
    if (quote && at < n - 1) cmd[at++] = L'"';
    cmd[at] = 0;
}

int wmain(void) {
    static wchar_t zig[MAX_PATH * 2];
    DWORD got = GetEnvironmentVariableW(L"MH4U_ZIG", zig, MAX_PATH * 2);
    if (got == 0 || got >= MAX_PATH * 2) {
        if (!GetModuleFileNameW(NULL, zig, MAX_PATH)) return 127;
        wchar_t *slash = wcsrchr(zig, L'\\');
        if (!slash) return 127;
        wcscpy(slash + 1, L"zig.exe");
    }
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 127;
    size_t need = wcslen(zig) + 128;
    for (int i = 1; i < argc; i++) need += wcslen(argv[i]) * 2 + 3;
    wchar_t *cmd = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, need * sizeof *cmd);
    if (!cmd) return 127;
    const wchar_t *front[] = {zig, L"cc", L"-target", L"x86_64-windows-gnu", L"-fno-sanitize=undefined", L"-g0"};
    for (size_t i = 0; i < sizeof front / sizeof *front; i++) add_arg(cmd, need, front[i]);
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"-lm") != 0) add_arg(cmd, need, argv[i]);
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(zig, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"zigcc: %ls can't be started (error %lu)\n", zig, GetLastError());
        return 127;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 127;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

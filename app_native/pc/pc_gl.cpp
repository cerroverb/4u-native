// MH4U for PC: the OpenGL functions past 1.1, loaded from SDL's GL context (pc_gl.h).
#include "pc_gl.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#define PC_GL_DEFINE(type, name) type pc_##name = nullptr;
PC_GL_FUNCS(PC_GL_DEFINE)
#undef PC_GL_DEFINE

namespace {
bool g_loaded = false, g_ok = false, g_memory = false;

bool Has(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    if (pc_glGetStringi != nullptr) {
        for (GLint i = 0; i < n; ++i) {
            const char* e = reinterpret_cast<const char*>(pc_glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (e != nullptr && !std::strcmp(e, name)) return true;
        }
        return false;
    }
    const char* all = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const size_t len = std::strlen(name);
    for (const char* p = all; p != nullptr && (p = std::strstr(p, name)) != nullptr; p += len)
        if ((p == all || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0')) return true;
    return false;
}
}  // namespace

bool pc_gl_load() {
    if (g_loaded) return g_ok;
    g_loaded = true;
    g_ok = true;
    std::string missing;
#define PC_GL_LOAD(type, name)                                                                     \
    pc_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                              \
    if (pc_##name == nullptr && std::strstr(PC_GL_OPTIONAL, #name) == nullptr) {                   \
        missing += " " #name;                                                                      \
        g_ok = false;                                                                              \
    }
    PC_GL_FUNCS(PC_GL_LOAD)
#undef PC_GL_LOAD
    g_memory = g_ok && Has("GL_EXT_memory_object") && Has("GL_EXT_memory_object_win32") &&
               pc_glCreateMemoryObjectsEXT != nullptr && pc_glImportMemoryWin32HandleEXT != nullptr &&
               pc_glTexStorageMem2DEXT != nullptr && pc_glMemoryObjectParameterivEXT != nullptr &&
               pc_glDeleteMemoryObjectsEXT != nullptr;
    std::fprintf(stderr, "[Info] App: OpenGL %s on %s (GLSL %s): %s; memory objects %s\n",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)), reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)),
                 g_ok ? "the functions are there" : ("missing" + missing).c_str(), g_memory ? "yes" : "no");
    return g_ok;
}

bool pc_gl_memory_objects() { return g_memory; }

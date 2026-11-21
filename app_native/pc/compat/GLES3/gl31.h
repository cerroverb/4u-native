// MH4U for PC: what frame_gen.cpp (written for OpenGL ES 3.1) includes, on the window's desktop OpenGL (pc/pc_gl.h): the
// same functions under the same names, its "#version 310 es" shaders run through the desktop GL's ES 3.1 compatibility
// (core in GL 4.5). For that file only (the PC's other GL code includes pc_gl.h): GL_VERSION reads as the ES version
// those functions and shaders amount to, and SDL's texture number is the desktop renderer's.
#pragma once

#include "pc_gl.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

inline const GLubyte* pc_es_get_string(GLenum name) {
    if (name == GL_VERSION) {
        static char text[160];
        if (text[0] == 0)
            std::snprintf(text, sizeof text, "OpenGL ES 3.2 (desktop OpenGL %s)",
                          reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return reinterpret_cast<const GLubyte*>(text);
    }
    return glGetString(name);
}
#define glGetString pc_es_get_string

#undef SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER
#define SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER

// (its quality samples' folder when MH4U_FRAMEGEN_DIR is not given: the Android app's external folder; here the current one)
inline const char* SDL_GetAndroidExternalStoragePath() { return nullptr; }

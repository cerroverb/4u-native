// MH4U for PC: the one EGL call frame_gen.cpp makes, eglGetProcAddress, as SDL's (the window's GL context is SDL's)
#pragma once
#include <SDL3/SDL.h>
#define eglGetProcAddress(name) SDL_GL_GetProcAddress(name)

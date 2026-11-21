// MH4U for PC: the OpenGL the window's picture is drawn with (SDL's OpenGL renderer: a compatibility context, 4.6 on
// today's drivers). opengl32.dll exports GL 1.1 only; the newer functions come through SDL_GL_GetProcAddress once the
// renderer's context is current (pc_gl_load), under their usual names (macros for the pointers), so that the code the
// Android app has for OpenGL ES (its programs, frame_gen.cpp) reads the same here. GL/gl.h is included without
// windows.h (its macros: near, far, min, max).
#pragma once

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif
#include <GL/gl.h>
#include <GL/glext.h>

// Loads the pointers (SDL's GL context current). False when one the screens can't do without is missing (logged).
bool pc_gl_load();
// The context has GL_EXT_memory_object_win32 (the renderer's buffers imported with no copy)
bool pc_gl_memory_objects();

#define PC_GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLGETVERTEXATTRIBIVPROC, glGetVertexAttribiv) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLTEXSTORAGE2DPROC, glTexStorage2D) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLFENCESYNCPROC, glFenceSync) \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync) \
    X(PFNGLDELETESYNCPROC, glDeleteSync) \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(PFNGLCREATEMEMORYOBJECTSEXTPROC, glCreateMemoryObjectsEXT) \
    X(PFNGLDELETEMEMORYOBJECTSEXTPROC, glDeleteMemoryObjectsEXT) \
    X(PFNGLMEMORYOBJECTPARAMETERIVEXTPROC, glMemoryObjectParameterivEXT) \
    X(PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC, glImportMemoryWin32HandleEXT) \
    X(PFNGLTEXSTORAGEMEM2DEXTPROC, glTexStorageMem2DEXT) \
    X(PFNGLGETUNSIGNEDBYTEVEXTPROC, glGetUnsignedBytevEXT) \
    X(PFNGLGETUNSIGNEDBYTEI_VEXTPROC, glGetUnsignedBytei_vEXT) \
    X(PFNGLGENQUERIESPROC, glGenQueries) \
    X(PFNGLBEGINQUERYPROC, glBeginQuery) \
    X(PFNGLENDQUERYPROC, glEndQuery) \
    X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv) \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v) \

#define PC_GL_DECLARE(type, name) extern type pc_##name;
PC_GL_FUNCS(PC_GL_DECLARE)
#undef PC_GL_DECLARE

#define glCreateShader pc_glCreateShader
#define glShaderSource pc_glShaderSource
#define glCompileShader pc_glCompileShader
#define glGetShaderiv pc_glGetShaderiv
#define glGetShaderInfoLog pc_glGetShaderInfoLog
#define glDeleteShader pc_glDeleteShader
#define glCreateProgram pc_glCreateProgram
#define glAttachShader pc_glAttachShader
#define glBindAttribLocation pc_glBindAttribLocation
#define glLinkProgram pc_glLinkProgram
#define glGetProgramiv pc_glGetProgramiv
#define glGetProgramInfoLog pc_glGetProgramInfoLog
#define glDeleteProgram pc_glDeleteProgram
#define glUseProgram pc_glUseProgram
#define glGetUniformLocation pc_glGetUniformLocation
#define glUniform1i pc_glUniform1i
#define glUniform1f pc_glUniform1f
#define glUniform2f pc_glUniform2f
#define glUniform3fv pc_glUniform3fv
#define glUniform4f pc_glUniform4f
#define glUniform4fv pc_glUniform4fv
#define glGenBuffers pc_glGenBuffers
#define glBindBuffer pc_glBindBuffer
#define glBufferData pc_glBufferData
#define glDeleteBuffers pc_glDeleteBuffers
#define glVertexAttribPointer pc_glVertexAttribPointer
#define glEnableVertexAttribArray pc_glEnableVertexAttribArray
#define glDisableVertexAttribArray pc_glDisableVertexAttribArray
#define glGetVertexAttribiv pc_glGetVertexAttribiv
#define glActiveTexture pc_glActiveTexture
#define glTexStorage2D pc_glTexStorage2D
#define glBlendFuncSeparate pc_glBlendFuncSeparate
#define glBlendEquation pc_glBlendEquation
#define glGenFramebuffers pc_glGenFramebuffers
#define glBindFramebuffer pc_glBindFramebuffer
#define glFramebufferTexture2D pc_glFramebufferTexture2D
#define glCheckFramebufferStatus pc_glCheckFramebufferStatus
#define glDeleteFramebuffers pc_glDeleteFramebuffers
#define glFenceSync pc_glFenceSync
#define glClientWaitSync pc_glClientWaitSync
#define glDeleteSync pc_glDeleteSync
#define glGetStringi pc_glGetStringi
#define glCreateMemoryObjectsEXT pc_glCreateMemoryObjectsEXT
#define glDeleteMemoryObjectsEXT pc_glDeleteMemoryObjectsEXT
#define glMemoryObjectParameterivEXT pc_glMemoryObjectParameterivEXT
#define glImportMemoryWin32HandleEXT pc_glImportMemoryWin32HandleEXT
#define glTexStorageMem2DEXT pc_glTexStorageMem2DEXT
#define glGetUnsignedBytevEXT pc_glGetUnsignedBytevEXT
#define glGetUnsignedBytei_vEXT pc_glGetUnsignedBytei_vEXT
#define glGenQueries pc_glGenQueries
#define glBeginQuery pc_glBeginQuery
#define glEndQuery pc_glEndQuery
#define glGetQueryObjectiv pc_glGetQueryObjectiv
#define glGetQueryObjectui64v pc_glGetQueryObjectui64v

// (pc_gl.cpp: which of them a context may lack)
#define PC_GL_OPTIONAL "glBeginQuery glCreateMemoryObjectsEXT glDeleteMemoryObjectsEXT glEndQuery glGenQueries glGetQueryObjectiv glGetQueryObjectui64v glGetStringi glGetUnsignedBytevEXT glGetUnsignedBytei_vEXT glImportMemoryWin32HandleEXT glMemoryObjectParameterivEXT glTexStorage2D glTexStorageMem2DEXT"

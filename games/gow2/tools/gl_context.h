// Headless OpenGL 4.5 core context via EGL (device platform when available), shared by the tools.
#pragma once
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cstdio>
#include <cstdlib>

namespace gl
{
inline bool initContext()
{
    auto queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (queryDevices && getPlatformDisplay)
    {
        EGLDeviceEXT devs[8];
        EGLint n = 0;
        queryDevices(8, devs, &n);
        for (int i = 0; i < n && dpy == EGL_NO_DISPLAY; ++i)
        {
            EGLDisplay d = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr);
            EGLint maj, min;
            if (d != EGL_NO_DISPLAY && eglInitialize(d, &maj, &min))
                dpy = d;
        }
    }
    if (dpy == EGL_NO_DISPLAY)
    {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj, min;
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min))
        {
            fprintf(stderr, "[gl] eglGetDisplay/eglInitialize failed (0x%x)\n", eglGetError());
            return false;
        }
    }
    if (!eglBindAPI(EGL_OPENGL_API))
    {
        fprintf(stderr, "[gl] eglBindAPI failed (0x%x)\n", eglGetError());
        return false;
    }
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg) || ncfg == 0)
    {
        fprintf(stderr, "[gl] eglChooseConfig failed (0x%x, n=%d)\n", eglGetError(), (int)ncfg);
        return false;
    }
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    if (surf == EGL_NO_SURFACE) fprintf(stderr, "[gl] eglCreatePbufferSurface failed (0x%x); trying a surfaceless context\n", eglGetError());
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "[gl] eglCreateContext failed (0x%x)\n", eglGetError());
        return false;
    }
    if (eglMakeCurrent(dpy, surf, surf, ctx) != EGL_TRUE && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) != EGL_TRUE)
    {
        fprintf(stderr, "[gl] eglMakeCurrent failed (0x%x)\n", eglGetError());
        return false;
    }
    return true;
}

inline GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "shader compile error:\n%s\n", log);
        exit(1);
    }
    return s;
}

inline GLuint link(GLuint a, GLuint b = 0)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, a);
    if (b) glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "program link error:\n%s\n", log);
        exit(1);
    }
    return p;
}

inline GLuint computeProgram(const char *src) { return link(compile(GL_COMPUTE_SHADER, src)); }
} // namespace gl

/* Real upstream SDL3 + actual driver + EGL boundary double (no GPU).
 * Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted from integration/SDL2/test_contract.c. */
#include <SDL3/SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ps5_opengl_display.h>

static int display_token, config_token, surface_token, context_token;
#define DISPLAY ((EGLDisplay)&display_token)
#define CONFIG ((EGLConfig)&config_token)
#define SURFACE ((EGLSurface)&surface_token)
#define CONTEXT ((EGLContext)&context_token)
static int initialized, surface_live, context_live, current, swaps, creates, destroys;
static EGLint context_version, context_profile, context_flags, config_depth, config_stencil;
static const char *failure;
static EGLint error_code;
static int fail(const char *name)
{
    if (!failure || strcmp(name, failure)) return 0;
    failure = NULL; error_code = EGL_BAD_ALLOC; return 1;
}
EGLDisplay eglGetDisplay(EGLNativeDisplayType id)
{ assert(id == EGL_DEFAULT_DISPLAY); return fail("display") ? EGL_NO_DISPLAY : DISPLAY; }
EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor)
{
    assert(d == DISPLAY && !initialized);
    if (fail("initialize")) return EGL_FALSE;
    initialized = 1;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return EGL_TRUE;
}
EGLBoolean eglTerminate(EGLDisplay d)
{
    assert(d == DISPLAY && initialized && !surface_live && !context_live && !current);
    initialized = 0; return EGL_TRUE;
}
EGLint eglGetError(void) { EGLint e = error_code; error_code = EGL_SUCCESS; return e; }
EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *a, EGLConfig *c, EGLint size, EGLint *n)
{
    assert(d == DISPLAY && initialized && size == 1);
    assert(a[0] == EGL_SURFACE_TYPE && a[1] == EGL_WINDOW_BIT);
    assert(a[2] == EGL_RENDERABLE_TYPE && a[3] == EGL_OPENGL_BIT);
    assert(a[12] == EGL_DEPTH_SIZE && a[14] == EGL_STENCIL_SIZE && a[16] == EGL_NONE);
    if (fail("config")) return EGL_FALSE;
    config_depth = a[13]; config_stencil = a[15];
    *c = CONFIG; *n = fail("no-config") ? 0 : 1; return EGL_TRUE;
}
EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *a)
{
    assert(d == DISPLAY && c == CONFIG && !w && !a && !surface_live);
    if (fail("surface")) return EGL_NO_SURFACE;
    surface_live = 1; ++creates; return SURFACE;
}
EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint a, EGLint *v)
{
    assert(d == DISPLAY && s == SURFACE && surface_live);
    if (fail("query")) return EGL_FALSE;
    assert(a == EGL_WIDTH || a == EGL_HEIGHT);
    *v = a == EGL_WIDTH ? PS5_OPENGL_NATIVE_WIDTH : PS5_OPENGL_NATIVE_HEIGHT;
    if ((a == EGL_WIDTH && fail("width")) || (a == EGL_HEIGHT && fail("height"))) *v -= 1;
    return EGL_TRUE;
}
EGLBoolean eglBindAPI(EGLenum api)
{ assert(api == EGL_OPENGL_API); return !fail("bind"); }
EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *a)
{
    assert(d == DISPLAY && c == CONFIG && !share && !context_live);
    assert(a[0] == EGL_CONTEXT_MAJOR_VERSION_KHR && a[2] == EGL_CONTEXT_MINOR_VERSION_KHR);
    assert(a[4] == EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR);
    assert(a[5] == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR ||
           a[5] == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR);
    /* Default flags must not add an attribute: that is the hardware-qualified list. */
    assert(a[6] == EGL_NONE || (a[6] == EGL_CONTEXT_FLAGS_KHR && a[7] && a[8] == EGL_NONE));
    if (fail("context")) return EGL_NO_CONTEXT;
    context_version = a[1] * 10 + a[3]; context_profile = a[5];
    context_flags = a[6] == EGL_NONE ? 0 : a[7];
    context_live = 1; return CONTEXT;
}
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c)
{
    assert(d == DISPLAY && initialized);
    if (fail("current")) return EGL_FALSE;
    if (c) assert(c == CONTEXT && context_live && surface_live && draw == SURFACE && read == SURFACE);
    else assert(!draw && !read);
    current = c != EGL_NO_CONTEXT; return EGL_TRUE;
}
EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c)
{
    assert(d == DISPLAY && c == CONTEXT && context_live);
    if (current) { error_code = EGL_BAD_ACCESS; return EGL_FALSE; }
    context_live = 0; return EGL_TRUE;
}
EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s)
{
    assert(d == DISPLAY && s == SURFACE && surface_live && !current);
    if (fail("destroy-surface")) return EGL_FALSE;
    surface_live = 0; ++destroys; return EGL_TRUE;
}
EGLBoolean eglSwapInterval(EGLDisplay d, EGLint i)
{ assert(d == DISPLAY && current && (i == 0 || i == 1)); return !fail("interval"); }
EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s)
{ assert(d == DISPLAY && s == SURFACE && current); if (fail("swap")) return EGL_FALSE; ++swaps; return EGL_TRUE; }

static GLenum APIENTRY mock_error(void) { assert(current); return GL_NO_ERROR; }
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (fail("proc")) return NULL;
#define PROC(n, f) if (!strcmp(name, n)) return (__eglMustCastToProperFunctionPointerType)f
    PROC("glGetError", mock_error);
#undef PROC
    return NULL;
}

static void request(int major, int minor, int profile)
{
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major));
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor));
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile));
}
/* The milestone target: OpenGL 4.3 Core. */
static void attributes(void) { request(4, 3, SDL_GL_CONTEXT_PROFILE_CORE); }
/* The bridge forwards exactly the requested version, profile and flags to EGL. */
static void accepted(SDL_Window *w, int major, int minor, int profile, EGLint egl_profile, int flags)
{
    SDL_GLContext c;
    request(major, minor, profile);
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, flags));
    c = SDL_GL_CreateContext(w); assert(c && current);
    assert(context_version == major * 10 + minor && context_profile == egl_profile);
    assert(context_flags == flags); /* SDL and EGL_KHR_create_context share these two bit values. */
    assert(SDL_GL_DestroyContext(c) && !context_live && !current);
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0));
}
static void rejected(SDL_Window *w, int major, int minor, int profile)
{
    request(major, minor, profile);
    assert(!SDL_GL_CreateContext(w) && !context_live);
    assert(strstr(SDL_GetError(), "3.3 to 4.6 Core, or Compatibility up to 4.6"));
}
static void rejected_attribute(SDL_Window *w, SDL_GLAttr attribute, int value, int restore)
{
    attributes();
    assert(SDL_GL_SetAttribute(attribute, value));
    assert(!SDL_GL_CreateContext(w) && !context_live);
    assert(SDL_GL_SetAttribute(attribute, restore));
}
static SDL_Window *window(void)
{ return SDL_CreateWindow("contract", PS5_OPENGL_NATIVE_WIDTH, PS5_OPENGL_NATIVE_HEIGHT, SDL_WINDOW_OPENGL); }
static void rejected_window(SDL_GLAttr attribute, int value, int restore)
{
    assert(SDL_GL_SetAttribute(attribute, value));
    assert(!window() && !surface_live);
    assert(SDL_GL_SetAttribute(attribute, restore));
}
static void native_size(SDL_Window *w)
{
    int width = 0, height = 0;
    assert(SDL_GetWindowSize(w, &width, &height));
    assert(width == PS5_OPENGL_NATIVE_WIDTH && height == PS5_OPENGL_NATIVE_HEIGHT);
    width = height = 0;
    assert(SDL_GetWindowSizeInPixels(w, &width, &height));
    assert(width == PS5_OPENGL_NATIVE_WIDTH && height == PS5_OPENGL_NATIVE_HEIGHT);
}
static void native_mode(const SDL_DisplayMode *mode)
{
    assert(mode && mode->w == PS5_OPENGL_NATIVE_WIDTH && mode->h == PS5_OPENGL_NATIVE_HEIGHT);
    assert(mode->refresh_rate == PS5_OPENGL_NATIVE_FPS && mode->format == SDL_PIXELFORMAT_ABGR8888);
    assert(mode->pixel_density == 1.0f);
}
static void clean(void)
{ assert(!initialized && !surface_live && !context_live && !current && creates == destroys); }

static int cleanup_errors;
static void SDLCALL log_output(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    if (category == SDL_LOG_CATEGORY_VIDEO && priority == SDL_LOG_PRIORITY_ERROR &&
        strstr(message, "PS5 video cleanup")) ++cleanup_errors;
    else fprintf(stderr, "SDL log: %s\n", message);
}

static int other_thread(void *data)
{
    assert(!SDL_GL_MakeCurrent(data, (SDL_GLContext)CONTEXT));
    assert(strstr(SDL_GetError(), "video thread"));
    return 0;
}

int main(void)
{
    SDL_Window *w;
    SDL_GLContext c;
    SDL_Event event;
    SDL_DisplayID *displays, display;
    SDL_DisplayMode **modes;
    SDL_Rect bounds;
    int count = 0, interval = -1, seen = 0;
    const char *init_failures[] = {"display", "initialize"};
    const char *window_failures[] = {"config", "no-config", "surface", "query", "width", "height"};
    const char *context_failures[] = {"bind", "context", "current"};
    SDL_SetLogOutputFunction(log_output, NULL);
    for (unsigned i = 0; i < SDL_arraysize(init_failures); ++i) {
        failure = init_failures[i]; assert(!SDL_Init(SDL_INIT_VIDEO)); SDL_Quit(); clean();
    }
    assert(SDL_Init(SDL_INIT_VIDEO));
    /* No hint selects it: this is the default driver of the build. */
    assert(!strcmp(SDL_GetCurrentVideoDriver(), "ps5"));
    displays = SDL_GetDisplays(&count); assert(displays && count == 1);
    display = displays[0]; SDL_free(displays);
    assert(display == SDL_GetPrimaryDisplay() && !SDL_GetDesktopDisplayMode(0));
    native_mode(SDL_GetDesktopDisplayMode(display));
    native_mode(SDL_GetCurrentDisplayMode(display));
    modes = SDL_GetFullscreenDisplayModes(display, &count); assert(modes && count == 1);
    native_mode(modes[0]); SDL_free(modes);
    assert(SDL_GetDisplayBounds(display, &bounds) && !bounds.x && !bounds.y);
    assert(bounds.w == PS5_OPENGL_NATIVE_WIDTH && bounds.h == PS5_OPENGL_NATIVE_HEIGHT);
    attributes();
    assert(!SDL_GL_LoadLibrary("libOSMesa.so"));
    /* SDL only resolves GL procedures while a GL window (or explicit load) holds the library. */
    assert(!SDL_GL_GetProcAddress("glGetError"));
    assert(!SDL_CreateWindow("software", PS5_OPENGL_NATIVE_WIDTH, PS5_OPENGL_NATIVE_HEIGHT, 0));
    assert(strstr(SDL_GetError(), "one fullscreen OpenGL window"));
    rejected_window(SDL_GL_MULTISAMPLESAMPLES, 4, 0);
    rejected_window(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1, 0);
    rejected_window(SDL_GL_DOUBLEBUFFER, 0, 1);
    for (unsigned i = 0; i < SDL_arraysize(window_failures); ++i) {
        failure = window_failures[i]; assert(!window()); assert(!surface_live);
    }
    /* The window is always the fixed fullscreen drawable, whatever a desktop consumer asks for. */
    assert(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) && SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8));
    w = SDL_CreateWindow("desktop-style", 640, 480,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    assert(w && surface_live && config_depth == 24 && config_stencil == 8);
    assert(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN);
    native_size(w);
    SDL_DestroyWindow(w); assert(!surface_live);
    w = window(); assert(w);
    native_size(w);
    assert(SDL_GetKeyboardFocus() == w);
    assert(!window() && surface_live); /* Failed second window must not destroy first. */
    assert(SDL_GL_GetProcAddress("glGetError") == (SDL_FunctionPointer)mock_error);
    assert(!SDL_GL_GetProcAddress("glMissing"));
    failure = "proc"; assert(!SDL_GL_GetProcAddress("glGetError"));
    for (unsigned i = 0; i < SDL_arraysize(context_failures); ++i) {
        failure = context_failures[i]; assert(!SDL_GL_CreateContext(w)); assert(!context_live);
    }
    rejected(w, 3, 2, SDL_GL_CONTEXT_PROFILE_CORE);
    rejected(w, 4, 7, SDL_GL_CONTEXT_PROFILE_CORE);
    rejected(w, 4, 7, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    rejected(w, 5, 0, SDL_GL_CONTEXT_PROFILE_CORE);
    rejected(w, 3, 0, SDL_GL_CONTEXT_PROFILE_ES);
    rejected(w, 0, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    rejected_attribute(w, SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG, 0);
    rejected_attribute(w, SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_RESET_ISOLATION_FLAG, 0);
    rejected_attribute(w, SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1, 0);
    rejected_attribute(w, SDL_GL_CONTEXT_NO_ERROR, 1, 0);
    rejected_attribute(w, SDL_GL_CONTEXT_RESET_NOTIFICATION, SDL_GL_CONTEXT_RESET_LOSE_CONTEXT,
                       SDL_GL_CONTEXT_RESET_NO_NOTIFICATION);
    rejected_attribute(w, SDL_GL_CONTEXT_RELEASE_BEHAVIOR, SDL_GL_CONTEXT_RELEASE_BEHAVIOR_NONE,
                       SDL_GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH);
    accepted(w, 3, 3, SDL_GL_CONTEXT_PROFILE_CORE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, 0);
    accepted(w, 4, 6, SDL_GL_CONTEXT_PROFILE_CORE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, 0);
    accepted(w, 4, 3, SDL_GL_CONTEXT_PROFILE_CORE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
             SDL_GL_CONTEXT_DEBUG_FLAG);
    accepted(w, 4, 3, SDL_GL_CONTEXT_PROFILE_CORE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
             SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    accepted(w, 4, 6, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY,
             EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, 0);
    accepted(w, 3, 2, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY,
             EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, 0);
    accepted(w, 2, 1, 0, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR, 0);
    attributes();
    c = SDL_GL_CreateContext(w); assert(c && current);
    assert(context_version == 43 && context_profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);
    assert(!SDL_GL_CreateContext(w));
    assert(SDL_GL_GetCurrentContext() == c && SDL_GL_GetCurrentWindow() == w);
    assert(SDL_GL_MakeCurrent(NULL, NULL) && !current);
    assert(SDL_GL_MakeCurrent(w, c) && current);
    {
        SDL_Thread *thread = SDL_CreateThread(other_thread, "contract", w);
        assert(thread); SDL_WaitThread(thread, NULL);
        assert(SDL_GL_GetCurrentContext() == c && current);
    }
    assert(SDL_GL_GetSwapInterval(&interval) && interval == 1);
    assert(!SDL_GL_SetSwapInterval(-1) && !SDL_GL_SetSwapInterval(2));
    assert(SDL_GL_SetSwapInterval(0) && SDL_GL_GetSwapInterval(&interval) && interval == 0);
    failure = "interval"; assert(!SDL_GL_SetSwapInterval(1));
    assert(SDL_GL_GetSwapInterval(&interval) && interval == 0);
    assert(SDL_GL_SwapWindow(w) && swaps == 1);
    failure = "swap"; assert(!SDL_GL_SwapWindow(w));
    assert(strstr(SDL_GetError(), "eglSwapBuffers") && swaps == 1);
    /* No resize hook: SDL reports the request as unsupported and the size is unchanged. */
    assert(!SDL_SetWindowSize(w, 640, 480));
    native_size(w);
    SDL_zero(event); event.type = SDL_EVENT_USER; event.user.code = 26;
    assert(SDL_PushEvent(&event));
    while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_USER && event.user.code == 26) seen = 1;
    assert(seen);
    failure = "current"; assert(!SDL_GL_DestroyContext(c));
    assert(context_live && current && SDL_GL_GetCurrentContext() == c);
    /* SDL contexts outlive windows: the drawable goes, the context is rebound to the next one. */
    SDL_DestroyWindow(w);
    assert(!surface_live && context_live && !current && !SDL_GL_GetCurrentContext());
    w = window(); assert(w && surface_live);
    assert(SDL_GL_MakeCurrent(w, c) && current && SDL_GL_SwapWindow(w) && swaps == 2);
    assert(SDL_GL_DestroyContext(c) && !SDL_GL_GetCurrentContext() && !context_live);
    SDL_DestroyWindow(w); SDL_Quit(); clean();
    /* SDL_Quit must also clean a consumer that forgot explicit deletes. */
    assert(SDL_Init(SDL_INIT_VIDEO)); attributes(); w = window(); assert(w);
    assert(SDL_GL_CreateContext(w)); SDL_Quit(); clean();
    assert(!cleanup_errors);
    /* A cleanup EGL refuses is reported and the drawable stays owned until SDL_Quit. */
    assert(SDL_Init(SDL_INIT_VIDEO)); attributes(); w = window(); assert(w);
    failure = "destroy-surface"; SDL_DestroyWindow(w);
    assert(cleanup_errors == 1 && surface_live);
    assert(!window() && surface_live);
    SDL_Quit(); clean(); assert(cleanup_errors == 1);
    printf("SDL3 PS5 host contract: PASS %dx%d nominal=%dHz (no GPU or PS5 hardware)\n",
           PS5_OPENGL_NATIVE_WIDTH, PS5_OPENGL_NATIVE_HEIGHT, PS5_OPENGL_NATIVE_FPS);
    return 0;
}

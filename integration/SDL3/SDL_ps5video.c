/* PS5 OpenGL - SDL3 video driver for the public EGL contract.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted from integration/SDL2/SDL_ps5g19.c. Built inside SDL3; public SDL APIs
 * and the upstream event queue are unchanged.
 */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include "video/SDL_sysvideo.h"
#include "events/SDL_keyboard_c.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ps5_opengl_display.h>

/* One fixed-profile fullscreen window and one context, both on the video thread.
 * ps5-opengl owns scanout and presentation; no private presentation API is used.
 * SDL's own EGL layer is not used: it expects a dynamically loaded EGL. */
struct SDL_VideoData {
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    SDL_Window *window;
    int interval;
};

static bool egl_error(const char *operation)
{
    return SDL_SetError("PS5 %s: EGL error 0x%04x", operation, (unsigned)eglGetError());
}

static bool on_thread(void)
{
    return SDL_OnVideoThread() || SDL_SetError("PS5 video requires the video thread");
}

static bool video_init(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    SDL_DisplayID id;
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ABGR8888;
    mode.w = PS5_OPENGL_NATIVE_WIDTH;
    mode.h = PS5_OPENGL_NATIVE_HEIGHT;
    mode.refresh_rate = (float)PS5_OPENGL_NATIVE_FPS;
    data->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (data->display == EGL_NO_DISPLAY) return egl_error("eglGetDisplay");
    if (!eglInitialize(data->display, NULL, NULL)) {
        data->display = EGL_NO_DISPLAY;
        return egl_error("eglInitialize");
    }
    id = SDL_AddBasicVideoDisplay(&mode);
    if (!id) return false;
    return SDL_AddFullscreenDisplayMode(SDL_GetVideoDisplay(id), &mode);
}

static bool load_library(SDL_VideoDevice *_this, const char *path)
{
    (void)_this;
    return !path || SDL_SetError("PS5 OpenGL is statically linked; use a NULL GL library path");
}

static SDL_FunctionPointer get_proc(SDL_VideoDevice *_this, const char *name)
{
    SDL_FunctionPointer proc;
    (void)_this;
    if (!name || !*name) {
        SDL_SetError("PS5 video requires a GL procedure name");
        return NULL;
    }
    proc = (SDL_FunctionPointer)eglGetProcAddress(name);
    if (!proc) SDL_SetError("PS5 OpenGL has no GL procedure %s", name);
    return proc;
}

static bool create_window(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    SDL_VideoData *data = _this->internal;
    EGLint count = 0, width = 0, height = 0;
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, _this->gl_config.red_size,
        EGL_GREEN_SIZE, _this->gl_config.green_size,
        EGL_BLUE_SIZE, _this->gl_config.blue_size,
        EGL_ALPHA_SIZE, _this->gl_config.alpha_size,
        EGL_DEPTH_SIZE, _this->gl_config.depth_size,
        EGL_STENCIL_SIZE, _this->gl_config.stencil_size, EGL_NONE
    };
    (void)props;
    if (!on_thread()) return false;
    /* SDL sizes the window to the display (VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY), so the
     * requested size and SDL_WINDOW_RESIZABLE are not errors here. */
    if (data->window || data->surface || !(window->flags & SDL_WINDOW_OPENGL))
        return SDL_SetError("PS5 video requires one fullscreen OpenGL window");
    if (_this->gl_config.stereo || _this->gl_config.multisamplebuffers ||
        _this->gl_config.multisamplesamples || _this->gl_config.floatbuffers ||
        _this->gl_config.framebuffer_srgb_capable > 0 || _this->gl_config.accum_red_size ||
        _this->gl_config.accum_green_size || _this->gl_config.accum_blue_size ||
        _this->gl_config.accum_alpha_size || _this->gl_config.buffer_size > 32 ||
        !_this->gl_config.double_buffer)
        return SDL_SetError("PS5 video supports double-buffered RGBA8 without MSAA, stereo, accumulation or sRGB");
    if (!eglChooseConfig(data->display, attributes, &data->config, 1, &count))
        return egl_error("eglChooseConfig");
    if (count != 1) return SDL_SetError("PS5 video has no matching EGL config");
    data->surface = eglCreateWindowSurface(data->display, data->config, (EGLNativeWindowType)0, NULL);
    if (!data->surface) return egl_error("eglCreateWindowSurface");
    /* Establish ownership before queries, so SDL's failed-create cleanup works. */
    data->window = window;
    if (!eglQuerySurface(data->display, data->surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(data->display, data->surface, EGL_HEIGHT, &height))
        return egl_error("eglQuerySurface");
    if (width != window->w || height != window->h)
        return SDL_SetError("PS5 EGL drawable %dx%d does not match the %dx%d window",
                            width, height, window->w, window->h);
    data->interval = 1;
    SDL_SetKeyboardFocus(window); /* SDL also uses focus to gate joystick events. */
    return true;
}

static bool make_current(SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context)
{
    SDL_VideoData *data = _this->internal;
    EGLSurface surface = context ? data->surface : EGL_NO_SURFACE;
    if (!on_thread()) return false;
    if (context && ((EGLContext)context != data->context || window != data->window))
        return SDL_SetError("PS5 video context/window mismatch");
    if (!eglMakeCurrent(data->display, surface, surface, (EGLContext)context))
        return egl_error("eglMakeCurrent");
    return true;
}

static bool destroy_context(SDL_VideoDevice *_this, SDL_GLContext context)
{
    SDL_VideoData *data = _this->internal;
    if (!on_thread()) return false;
    if ((EGLContext)context != data->context) return SDL_SetError("PS5 video: unknown context");
    /* SDL_GL_DestroyContext detaches first and updates SDL's TLS. If that
     * failed, EGL rejects destruction; keep both ownership records intact. */
    if (!eglDestroyContext(data->display, data->context)) return egl_error("eglDestroyContext");
    data->context = EGL_NO_CONTEXT;
    return true;
}

static SDL_GLContext create_context(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_VideoData *data = _this->internal;
    const int major = _this->gl_config.major_version, minor = _this->gl_config.minor_version;
    const int version = major * 10 + minor;
    const int core = _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE;
    /* SDL's default mask (0) asks for the desktop default, which is Compatibility. */
    const int compatibility = !_this->gl_config.profile_mask ||
        _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const int flags = _this->gl_config.flags;
    /* The flag list is appended only when requested, so a default request reaches
     * EGL with the same attributes as the hardware-qualified SDL2 driver. */
    const EGLint attributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, major, EGL_CONTEXT_MINOR_VERSION_KHR, minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, core ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR :
                                                    EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
        flags ? EGL_CONTEXT_FLAGS_KHR : EGL_NONE,
        ((flags & SDL_GL_CONTEXT_DEBUG_FLAG) ? EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR : 0) |
        ((flags & SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) ? EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR : 0),
        EGL_NONE
    };
    if (!on_thread()) return NULL;
    /* Core 3.3 to 4.6 is the conformance-tested path. Compatibility contexts are passed
     * through to EGL as requested; see integration/SDL2/README.md for what is covered. */
    if (data->context || window != data->window ||
        (!core && !compatibility) || major < 1 || minor < 0 || minor > 6 || version > 46 ||
        (core && version < 33) ||
        (flags & ~(SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG)) ||
        _this->gl_config.share_with_current_context || _this->gl_config.no_error ||
        _this->gl_config.reset_notification != SDL_GL_CONTEXT_RESET_NO_NOTIFICATION ||
        _this->gl_config.release_behavior != SDL_GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH) {
        SDL_SetError("PS5 video requires one unshared OpenGL context with only debug or "
                     "forward-compatible flags: 3.3 to 4.6 Core, or Compatibility up to 4.6");
        return NULL;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        egl_error("eglBindAPI");
        return NULL;
    }
    data->context = eglCreateContext(data->display, data->config, EGL_NO_CONTEXT, attributes);
    if (!data->context) {
        egl_error("eglCreateContext");
        return NULL;
    }
    /* SDL assumes a created context is current. */
    if (!make_current(_this, window, (SDL_GLContext)data->context)) {
        destroy_context(_this, (SDL_GLContext)data->context);
        return NULL;
    }
    return (SDL_GLContext)data->context;
}

static bool set_interval(SDL_VideoDevice *_this, int interval)
{
    SDL_VideoData *data = _this->internal;
    if (!on_thread()) return false;
    if (interval != 0 && interval != 1) return SDL_SetError("PS5 swap interval must be 0 or 1");
    if (!eglSwapInterval(data->display, interval)) return egl_error("eglSwapInterval");
    data->interval = interval;
    return true;
}

static bool get_interval(SDL_VideoDevice *_this, int *interval)
{
    *interval = _this->internal->interval;
    return true;
}

static bool swap_window(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_VideoData *data = _this->internal;
    if (!on_thread()) return false;
    if (window != data->window) return SDL_SetError("PS5 video: unknown window");
    return eglSwapBuffers(data->display, data->surface) || egl_error("eglSwapBuffers");
}

/* SDL's destroy and quit hooks cannot fail, and SDL3 leaves benign errors in its
 * error string, so a refused cleanup is logged where the consumer can see it. */
static void log_cleanup_error(void)
{
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "PS5 video cleanup: %s", SDL_GetError());
}

static bool release_window(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    if (!on_thread()) return false;
    data->window = NULL;
    /* SDL contexts outlive windows; release the drawable, keep the context. */
    if (!make_current(_this, NULL, NULL)) return false;
    if (!eglDestroySurface(data->display, data->surface)) return egl_error("eglDestroySurface");
    data->surface = EGL_NO_SURFACE;
    return true;
}

static void destroy_window(SDL_VideoDevice *_this, SDL_Window *window)
{
    if (_this->internal->window != window) return; /* Includes rejected second-window creation. */
    if (!release_window(_this)) log_cleanup_error();
}

static bool release_display(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    if (!data->display) return true;
    if (!on_thread()) return false;
    /* Never release resources still owned by EGL. */
    if (data->context && !destroy_context(_this, (SDL_GLContext)data->context)) return false;
    if (data->surface) {
        if (!eglDestroySurface(data->display, data->surface)) return egl_error("eglDestroySurface(quit)");
        data->surface = EGL_NO_SURFACE;
    }
    if (!eglTerminate(data->display)) return egl_error("eglTerminate");
    data->display = EGL_NO_DISPLAY;
    return true;
}

static void video_quit(SDL_VideoDevice *_this)
{
    if (!release_display(_this)) log_cleanup_error();
}

static void free_device(SDL_VideoDevice *_this)
{
    SDL_free(_this->internal);
    SDL_free(_this);
}

/* Required SDL callback; there is no window-system event source on PS5. */
static void pump_events(SDL_VideoDevice *_this) { (void)_this; }

static SDL_VideoDevice *create_device(void)
{
    SDL_VideoDevice *device = SDL_calloc(1, sizeof(*device));
    if (!device) return NULL;
    device->internal = SDL_calloc(1, sizeof(*device->internal));
    if (!device->internal) { SDL_free(device); return NULL; }
    device->VideoInit = video_init;
    device->VideoQuit = video_quit;
    device->PumpEvents = pump_events;
    device->CreateSDLWindow = create_window;
    device->DestroyWindow = destroy_window;
    device->GL_LoadLibrary = load_library;
    device->GL_GetProcAddress = get_proc;
    device->GL_CreateContext = create_context;
    device->GL_MakeCurrent = make_current;
    device->GL_DestroyContext = destroy_context;
    device->GL_SetSwapInterval = set_interval;
    device->GL_GetSwapInterval = get_interval;
    device->GL_SwapWindow = swap_window;
    device->free = free_device;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap PS5_bootstrap = { "ps5", "PS5 OpenGL EGL", create_device, NULL, false };

#endif /* SDL_VIDEO_DRIVER_PS5 */

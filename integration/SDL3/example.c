/* Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted from integration/SDL2/example.c. */
#define SDL_MAIN_HANDLED /* The native runtime calls main() directly. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GL/glcorearb.h>
#include <stdio.h>

/* SDL3 leaves benign messages in SDL_GetError() after successful calls, so
 * teardown is judged by return values and by error-priority log messages. */
static int log_errors;
static void SDLCALL log_output(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) ++log_errors;
    SDL_GetDefaultLogOutputFunction()(userdata, category, priority, message);
}

/* Ordinary SDL3 consumer. The platform driver is linked into SDL3, not here. */
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext context = NULL;
    const SDL_DisplayMode *mode;
    const GLubyte *version;
    int status = 1, width = 0, height = 0, major = 0, minor = 0, profile = 0, running = 1;
    unsigned frames = 0, probes = 0;
    PFNGLGETINTEGERVPROC get_integer;
    PFNGLGETSTRINGPROC get_string;
    PFNGLVIEWPORTPROC viewport;
    PFNGLCLEARCOLORPROC clear_color;
    PFNGLCLEARPROC clear;
    PFNGLGETERRORPROC get_error;
    PFNGLREADPIXELSPROC read_pixels;
    (void)argc; (void)argv;
    log_errors = 0;
    SDL_SetLogOutputFunction(log_output, NULL);
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) goto done;
    mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (!mode) goto done;
    if (mode->w <= 0 || mode->h <= 0 || mode->refresh_rate <= 0.0f) {
        SDL_SetError("Invalid desktop display mode"); goto done;
    }
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1)) goto done;
    window = SDL_CreateWindow("SDL3 / PS5 OpenGL", mode->w, mode->h, SDL_WINDOW_OPENGL);
    if (!window) goto done;
    context = SDL_GL_CreateContext(window);
    if (!context || !SDL_GL_MakeCurrent(window, context)) goto done;
    get_integer = (PFNGLGETINTEGERVPROC)SDL_GL_GetProcAddress("glGetIntegerv");
    get_string = (PFNGLGETSTRINGPROC)SDL_GL_GetProcAddress("glGetString");
    viewport = (PFNGLVIEWPORTPROC)SDL_GL_GetProcAddress("glViewport");
    clear_color = (PFNGLCLEARCOLORPROC)SDL_GL_GetProcAddress("glClearColor");
    clear = (PFNGLCLEARPROC)SDL_GL_GetProcAddress("glClear");
    get_error = (PFNGLGETERRORPROC)SDL_GL_GetProcAddress("glGetError");
    read_pixels = (PFNGLREADPIXELSPROC)SDL_GL_GetProcAddress("glReadPixels");
    if (!get_integer || !get_string || !viewport || !clear_color || !clear || !get_error || !read_pixels) goto done;
    get_integer(GL_MAJOR_VERSION, &major); get_integer(GL_MINOR_VERSION, &minor);
    get_integer(GL_CONTEXT_PROFILE_MASK, &profile);
    version = get_string(GL_VERSION);
    if (major < 4 || (major == 4 && minor < 3) || !(profile & GL_CONTEXT_CORE_PROFILE_BIT) || !version) {
        SDL_SetError("OpenGL 4.3 Core is required; got %d.%d with profile mask 0x%x", major, minor,
                     (unsigned)profile);
        goto done;
    }
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) goto done;
    if (width != mode->w || height != mode->h) {
        SDL_SetError("Drawable does not match desktop display mode"); goto done;
    }
    if (!SDL_GL_SetSwapInterval(1)) goto done;
    viewport(0, 0, width, height);
    printf("[sdl3-ps5] GL=%s version=%d.%d core=1 drawable=%dx%d nominal_refresh=%.0fHz (not negotiated HDMI)\n",
        (const char *)version, major, minor, width, height, (double)mode->refresh_rate);
    while (running && frames < 180) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) running = 0;
        }
        if (!running) break;
        clear_color((float)frames / 180.0f, 0.15f, 0.4f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        if (get_error() != GL_NO_ERROR) { SDL_SetError("GL frame failed"); goto done; }
        if (frames == 0 || frames == 179) {
            GLubyte pixel[4] = {0};
            const unsigned expected[4] = { (frames * 255u + 90u) / 180u, 38, 102, 255 };
            int match = 1;
            read_pixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (get_error() != GL_NO_ERROR) { SDL_SetError("GL center readback failed"); goto done; }
            for (unsigned channel = 0; channel < 4; ++channel) {
                int delta = (int)pixel[channel] - (int)expected[channel];
                if (delta < -1 || delta > 1) match = 0;
            }
            printf("[sdl3-ps5] probe frame=%u rgba=%u,%u,%u,%u expected=%u,%u,%u,%u pass=%d\n",
                frames, (unsigned)pixel[0], (unsigned)pixel[1], (unsigned)pixel[2], (unsigned)pixel[3],
                expected[0], expected[1], expected[2], expected[3], match);
            if (!match) { SDL_SetError("GL center pixel mismatch"); goto done; }
            ++probes;
        }
        if (!SDL_GL_SwapWindow(window)) goto done;
        ++frames;
    }
    status = 0;
done:
    if (status) fprintf(stderr, "[sdl3-ps5] failed: %s\n", SDL_GetError());
    if (context) {
        const bool detached = SDL_GL_MakeCurrent(NULL, NULL);
        if (!SDL_GL_DestroyContext(context) || !detached) {
            fprintf(stderr, "[sdl3-ps5] cleanup: %s\n", SDL_GetError()); status = 1;
        }
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    if (log_errors) { fprintf(stderr, "[sdl3-ps5] %d error log message(s)\n", log_errors); status = 1; }
    printf("[sdl3-ps5] frames=%u probes=%u status=%d\n", frames, probes, status);
    return status;
}

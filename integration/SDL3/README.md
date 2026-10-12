# SDL3 on PS5 OpenGL

Work in progress: native PS5 SDL3 video support on the public EGL/OpenGL SDK.
Nothing here has run on PS5 hardware yet. [PORTING.md](../../PORTING.md) defines
the milestone; [integration/SDL2](../SDL2/README.md) is the architectural reference.

## Source pin

SDL is built from an unmodified snapshot of upstream `release-3.4.10`, patched in
the build directory. The pin is recorded in [sdl-pin.json](sdl-pin.json):

| | |
|---|---|
| Repository | `https://github.com/libsdl-org/SDL.git` |
| Tag | `release-3.4.10` (lightweight; it names the commit directly) |
| Commit | `8e37db5e797b6167f3a00d697d816a684bd259c7` |
| Tree | `39cb31c9154ed4b6975c49211343fb39ba648e66` |

```sh
python3 integration/SDL3/fetch-sdl.py     # third_party/SDL, verified against the pin
```

The pin lives here rather than in the root `dependencies.json` because that file
feeds the compiler-cache identity and the SDK bundle's frozen-profile checks,
which stay untouched until the SDL3 test passes on hardware.

### Comparison with OpenGOAL's vendored SDL

This is the version OpenGOAL vendors: `../jak-project/vendor.yaml` names
`release-3.4.10` and lists no local modifications. Checked read-only on
2026-10-12 against `../jak-project/third-party/SDL` at jak-project
`a71577936f7770ed678fc5d542ae3ba0717e003f` (SDL last updated by `b9d3f50ae`):

```sh
diff -rq --strip-trailing-cr --exclude=.git third_party/SDL ../jak-project/third-party/SDL
```

- **No content differences.** Nothing exists only in the vendored copy.
- 37 files differ only in line endings. Upstream stores them with CRLF and
  jak-project's `* text=auto` normalised them to LF: 27 Visual Studio project
  files, `.bat`/`.masm`/`.csv` files, one GitHub action, `test/win32/sdlprocdump.c`
  and `src/misc/macos/SDL_sysurl.m`. None is compiled for PS5 or for the host test.
- 14 upstream files are absent from the vendored copy: seven
  `src/render/gpu/shaders/*.hlsl` sources (covered by an upstream `.gitignore` in
  that directory) and seven packaging-only `SDL3Config*.cmake` files under
  `build-scripts/pkg-support` and `Xcode/SDL/pkg-support`. None is used by the
  build (`SDL_RENDER` and `SDL_GPU` are off here).

So a patch that applies to this pin applies unchanged to OpenGOAL's copy. Repeat
the comparison whenever either side moves.

## Video driver

[SDL_ps5video.c](SDL_ps5video.c) is compiled into `SDL3-static` by the
[CMake wrapper](CMakeLists.txt) and registered as the `ps5` video driver by
[ps5-platform.patch](ps5-platform.patch). It talks to the SDK's statically linked
EGL directly; SDL's own EGL layer, renderer and GPU API are not built.

| SDL object | EGL object | Lifetime |
|---|---|---|
| video device | `EGLDisplay` | `SDL_Init` to `SDL_Quit`; terminated only after the context and surface are gone |
| the one window | the one window `EGLSurface` | `SDL_CreateWindow` to `SDL_DestroyWindow` |
| `SDL_GLContext` | the one unshared `EGLContext` | may outlive the window and be rebound to the next one |

- The device is fullscreen-only: SDL sizes every window to the fixed display
  profile from `<ps5_opengl_display.h>`, whatever size or `SDL_WINDOW_RESIZABLE`
  flag a desktop consumer passes. A second window, a non-OpenGL window, MSAA,
  sRGB, stereo and single-buffered configs are rejected.
- Contexts: 3.3 to 4.6 Core, or Compatibility up to 4.6, with only the debug and
  forward-compatible flags. Requests with no flags reach EGL with the same
  attribute list as the SDL2 driver.
- ps5-opengl owns scanout and presentation. `SDL_GL_SwapWindow` is
  `eglSwapBuffers`; the swap interval is 0 or 1.
- Window and context calls must come from the thread that initialised video.
- `SDL_GL_GetProcAddress` works while a GL window exists (SDL holds the GL
  library reference through the window); a non-NULL `SDL_GL_LoadLibrary` path is
  rejected because GL is statically linked.
- If EGL refuses a cleanup step, the handle stays recorded rather than being
  freed underneath EGL, and the refusal is logged as an SDL video error. SDL3's
  destroy and quit hooks cannot fail, and SDL3 leaves benign messages in
  `SDL_GetError()` after successful calls, so the log is the only reliable signal.

Differences from the SDL2 driver are deliberate SDL3 adaptations: SDL3's
fullscreen-only capability replaces the size-rejection logic, SDL's video-thread
record replaces the private one, and there is no `SDL_SetWindowSize` hook, so
that call reports "not supported" and leaves the size unchanged.

## Host contract

Real SDL3 and the real driver run against an EGL boundary double under
ASan/UBSan. This exercises ownership, ordering and error paths. It involves no
GPU, no PS5 toolchain and no PS5 runtime, and is not evidence of hardware
behaviour.

```sh
python3 integration/SDL3/build.py host --sdl-source third_party/SDL \
    --sdk-prefix build/sdk/ps5-opengl-gl46 --out build/sdl3-host
```

`--out` must be a new directory below `build/`. The SDK prefix is verified with
the same checks as the SDL2 lane before anything is built.

## Not done yet

The PS5 platform part of the patch (threads, timers, platform detection), the
native build, the native folder link and the hardware run. Gamepad support
follows the hardware milestone.

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

## Platform patch

Upstream SDL3 has no public PS5 platform, so [ps5-platform.patch](ps5-platform.patch)
adds a minimal one to the pinned source (42 added lines, applied in the build
directory):

- `SDL_platform_defines.h`: `__PROSPERO__` defines `SDL_PLATFORM_PS5` and removes
  `SDL_PLATFORM_UNIX` and `SDL_PLATFORM_FREEBSD`, which the PS5 compiler's
  `__unix__` and `__FreeBSD__` predefines would otherwise select.
- `SDL_dynapi.h`: the dynamic API is off; SDL is linked statically.
- `CMakeLists.txt`: a `PS5` branch with pthreads and SDL's POSIX time and timer
  backends. Everything else (audio, joystick, filesystem, fsops, loadso, locale,
  process, misc) uses SDL's existing dummy backends.
- `SDL_sysvideo.h`, `SDL_video.c`: register `PS5_bootstrap`.
- `SDL.c`: `SDL_GetPlatform()` returns `"PlayStation 5"`.

Removing the two platform macros globally is **provisional**. In the sources
this build compiles, they gate: the GTK include and `SDL_Gtk_Quit()` call, tray
polling in the event loop, a `dlsym` lookup of `environ` (now a plain
`extern`), `<sys/param.h>` in cpuinfo, `<sys/endian.h>` in `SDL_endian.h` (now
the compiler's byte-order predefines), the dlopen-note macros and the platform
name. The native build has no warnings and the linked title imports no `dl*`
symbol. The only raw `__FreeBSD__` test left in a compiled file is in SDL's
bundled dlmalloc, which is unused because the title's `malloc` is. So far this
shows no collateral damage; hardware has not confirmed it. If it does cause
trouble, the fallback is two narrow guards (the `SDL_Gtk_Quit()` call in `SDL.c`
and the `dlsym` branch in `SDL_getenv.c`) with the macros left alone.

Two things the native link decided:

- SDL's POSIX fsops cannot be combined with its dummy filesystem backend (both
  define `SDL_SYS_GetCurrentDirectory`), so fsops stays a dummy too. Filesystem
  work is deferred with the rest of OpenGOAL's filesystem needs.
- Configure probes only compile for this target, so they accept anything the
  SDK headers declare. The wrapper overrides the ones the folder link or the
  platform rejects: `wcsnlen`, `wcslcpy` and `wcslcat` (SDL's implementations
  are used) and `posix_spawn` (SDL's dummy process backend is used).

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

## Example

[example.c](example.c) is an ordinary SDL3 consumer and the intended first
hardware test: `SDL_Init`, one window, an OpenGL 4.3 Core context,
`SDL_GL_MakeCurrent`, `SDL_GL_GetProcAddress`, 180 frames of clear and
`SDL_GL_SwapWindow` with `SDL_PollEvent`, two centre-pixel readbacks, then
ordered teardown. It refuses a context below 4.3 or without the Core profile
bit, and fails if any error-priority SDL log message appears, which is how a
refused teardown becomes visible. A passing run prints:

```
[sdl3-ps5] GL=<version string> version=4.3 core=1 drawable=1920x1080 nominal_refresh=60Hz (not negotiated HDMI)
[sdl3-ps5] probe frame=0 rgba=0,38,102,255 expected=0,38,102,255 pass=1
[sdl3-ps5] probe frame=179 rgba=254,38,102,255 expected=254,38,102,255 pass=1
[sdl3-ps5] frames=180 probes=2 status=0
```

The host contract links the same object and drives it through its success and
failure paths against the EGL/GL double.

## Native build and folder

Nothing below runs on a console. It needs the pinned native-app boilerplate
(`native_boilerplate.revision` in `dependencies.json`) with its payload SDK set
up, and `clang-18`.

```sh
template=/path/to/ps5-native-app-boilerplate    # at the pinned revision
sdk=build/sdk/ps5-opengl-gl46

python3 integration/SDL3/build.py native --sdl-source third_party/SDL \
    --sdk-prefix "$sdk" --out build/sdl3-native \
    --payload-sdk "$template/.deps/native/ps5-payload-sdk" \
    --compiler-wrapper "$template/tooling/prospero-clang18"

python3 integration/SDL3/folder.py --native-build build/sdl3-native \
    --template "$template" --sdk-prefix "$sdk" --out build/sdl3-folder
```

`build.py native` produces `cmake/sdl/libSDL3.a` and the example object.
`folder.py` follows the SDL2 lane: it copies the template and its payload SDK
into the new stage, adds the unchanged `native-app/` shims, heap wrappers and
linker script, links with the template's own `tools/build.sh`, and runs
`tools/verify-native-test-app.sh`. It refuses a template that is not at the
pinned revision and never rebuilds the runtime libc: `runtime/libc.prx` must
match the template's `libc.prx.sha256`. For a template checkout that has not
built its own, pass a matching file with `--libc-prx`, for example the one in an
existing `build/native-app/PPSA99005/runtime/`.

Results:

```
build/sdl3-folder/dist/PPSA99005/     the title folder (eboot.bin, sce_module, sce_sys)
build/sdl3-folder/candidate.json      SDL, template, SDK and per-file SHA-256 identities
build/sdl3-folder/selected-test.txt   egl_public_core43_sdl3.o
```

The title ID is the shared `PPSA99005`, so deploying it replaces whichever
PS5 OpenGL test is installed. The folder link is also the check that every libc
symbol SDL uses is really exported: the template's linker rejects symbols that
only its weak header stubs provide. Compared with the hardware-proven ImGui demo
the title imports 50 more symbols (math, stdio, `sem_*`, `pthread_rwlock_*`,
thread scheduling, `sigaction`, `environ`), all from `libkernel` or
`libSceLibcInternal`. Whether they behave on a console is what the hardware run
decides.

## Hardware run

Not done. Deploy `build/sdl3-folder/dist/PPSA99005` the same way as the other
`PPSA99005` gates ([Testing](../../docs/testing.md)), launch it, and collect
`/download0/ps5-opengl.log`. Acceptance is the four `[sdl3-ps5]` lines shown
under [Example](#example) with a version of 4.3 or higher, followed by

```
[ps5-opengl-native] gate completed status=0
```

Until then the receipts and manifests, the SDK bundle, the root `Makefile` and
CI targets, and licence notices for SDL3 are deliberately untouched. Gamepad
support follows the hardware milestone.

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

# Native PS5 OpenGOAL Port

## Current Phase

Implement native PS5 SDL3 support using ps5-opengl.

OpenGOAL itself is currently read-only during this phase.

## Proven Foundation

The following have already been proven on real hardware:

- Native PS5 homebrew builds successfully.
- Native PS5 homebrew packages, installs, and launches successfully.
- ps5-opengl builds successfully.
- The ps5-opengl OpenGL/ImGui demo renders successfully on the actual PS5.
- Known upstream ps5-opengl baseline:
  00ea837b6954290275dc06db0fbd852b008a49f5

## Repository Layout

../ps5-opengl
  Active SDL3 PS5 integration work.

../jak-project
  OpenGOAL fork. Treat as read-only until the SDL3 milestone is complete.

## Architecture

Target architecture:

OpenGOAL
  -> SDL3
  -> native PS5 SDL3 backend
  -> EGL/OpenGL
  -> ps5-opengl
  -> PS5 GPU

This must remain a native PS5 application.

Do not introduce Linux, Wine, virtualization, emulation, or another runtime
compatibility layer on the PS5.

## SDL3 First Milestone

Create a native PS5 SDL3 test application capable of:

- SDL_Init
- SDL_CreateWindow
- SDL_GL_CreateContext
- SDL_GL_MakeCurrent
- SDL_GL_GetProcAddress
- SDL_GL_SwapWindow
- SDL_PollEvent
- clean shutdown

The OpenGL context must support OpenGL 4.3 Core or better.

After this works on actual PS5 hardware, add gamepad support.

## Primary Reference

Use integration/SDL2 as the architectural reference for the existing
ps5-opengl PS5/SDL bridge.

Reuse PS5-specific architecture and behavior where appropriate, but adapt it
correctly to SDL3 rather than mechanically translating SDL2 code.

## Initial Non-Goals

Do not prioritize:

- multiple windows
- desktop window resizing
- clipboard integration
- IME
- desktop mouse behavior
- screenshots
- runtime fullscreen switching
- OpenGOAL audio
- OpenGOAL filesystem changes
- OpenGOAL runtime/JIT changes

## Development Rules

- Inspect before modifying.
- Keep PS5-specific changes isolated where practical.
- Prefer integration/SDL3 for SDL-specific PS5 work.
- Avoid changing core ps5-opengl code unless necessary.
- Do not modify ../jak-project during the SDL3 phase.
- Make small, reviewable commits.
- Preserve upstream compatibility.
- Do not force-push.
- Do not rewrite shared Git history.
- Do not delete known-good build artifacts without explicit approval.
- Run relevant tests after modifications.
- Do not weaken production validation merely to make a host-side test pass.

## Upstream Strategy

origin:
  DallinVetter/ps5-opengl

upstream:
  blackbearreloaded/ps5-opengl

main must remain close to upstream.

env/orbstack contains environment-specific build/test fixes.

feature/sdl3-ps5 contains actual SDL3 PS5 development.

If upstream later implements SDL3 support, compare their implementation with
ours and prefer upstream code where practical.

## Additional Implementation Guardrails

### SDL platform detection
Treat globally undefining SDL_PLATFORM_UNIX / SDL_PLATFORM_FREEBSD as experimental.
If that causes broad SDL3 regressions, prefer narrow guards around unsupported
facilities like GTK integration, dlopen/dlsym, or unavailable runtime services.

### SDL source pin
Before implementation begins, resolve and record the exact SDL 3.4.10 revision
used by OpenGOAL. Compare against ../jak-project/third-party/SDL while keeping
../jak-project read-only.

### OpenGOAL isolation
Do not modify ../jak-project during the SDL3 hardware milestone.

Future OpenGOAL issues such as gladLoadGL/libGL behavior, SDL integration,
fullscreen APIs, audio, filesystem, or runtime/JIT work are deferred.

### Scope discipline
Do not modify the root Makefile, CI configuration, release metadata, packaging
metadata, licenses/notices, or unrelated infrastructure for the first SDL3
hardware milestone.

### Production correctness
Do not weaken PS5 runtime validation to make host-side tests pass.
Fix or isolate host-only test behavior instead.

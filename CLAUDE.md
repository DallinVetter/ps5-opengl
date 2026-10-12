Read PORTING.md before doing any work.

PORTING.md defines the architecture, current phase, repository roles,
milestones, and restrictions.

During the current phase:
- ../jak-project is read-only.
- Inspect integration/SDL2 before designing SDL3 support.
- Inspect relevant SDL3 internals before editing.
- Explain the proposed implementation plan before substantial modifications.
- Keep changes narrow and reviewable.

Additional guardrails:

- Resolve and record the exact SDL 3.4.10 revision before implementation.
- Treat global SDL_PLATFORM_UNIX / SDL_PLATFORM_FREEBSD removal as provisional.
- Prefer narrow platform guards if the global approach causes collateral issues.
- Do not modify ../jak-project during this milestone.
- Do not solve future OpenGOAL problems during SDL3 bring-up.
- Do not modify root Makefiles, CI, release metadata, or unrelated infrastructure
  until the SDL3 test succeeds on real PS5 hardware.
- Never weaken production PS5 validation merely to satisfy host-side tests.

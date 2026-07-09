# Future session handoff

This repo is on `master`. All implementation commits from this session have been pushed to `origin/master`.

Current pushed HEAD:

- `1c475eb Set CMake policy for headless script tests`

The local working tree should remain clean except for this handoff file, which is intentionally untracked/unstaged.

## What was done

- Added and updated planning/audit documentation:
  - `docs/PORTING.md` covers the porting strategy for Win64/ARM64, Linux amd64/arm64, and macOS arm64.
  - `docs/CODEBASE_AUDIT.md` captures the project state, security concerns, logic/functionality gaps, and porting risks.

- Reimplemented the Windows build entrypoint:
  - Added `build-win.ps1`.
  - Integrated the Windows build flow with CMake/platform options and DirectX SDK handling.

- Built out the cross-platform CMake/CI foundation:
  - Added platform CMake fragments under `scripts/platform/{win32,linux,macos}`.
  - Added/updated target/source organization for MP, SP, and dedicated builds.
  - Added CI workflows for portable tests and Windows x86 builds.
  - Added smoke-test support scaffolding.

- Added a headless dedicated server source profile:
  - Introduced dedicated/headless source selection in `scripts/dedi/dedi_sources.cmake`.
  - Split client-only Win32 sources away from headless dedicated builds.
  - Added assertions/tests to keep known client-only sources out of the headless profile.

- Added portability/security regression tests:
  - Huffman bounds and pointer-width tests.
  - Disk32 pointer-token bounds tests.
  - Headless source profile validation.
  - Headless client/media include-debt scanner with allowlist tracking.

- Changed the default FPS:
  - `com_maxfps` now defaults to `250`.
  - Commit: `12d23e8 Raise default com_maxfps to 250`

- Reduced headless dedicated coupling across the codebase:
  - Guarded or removed many client/media/render/sound/devgui includes from dedicated/headless paths.
  - Added no-op or guarded behavior for debug drawing, stat monitor materials, load overlays, reflection probe setup, sound alias/runtime paths, renderer lifecycle/timing hooks, FX cleanup, DevGui frame update, renderer worker waits, and client message reads.
  - Current tracked remaining debt: `33` direct client/media includes in the headless dedicated source profile, recorded in `tests/headless_include_debt.allow`.

- Fixed portability-specific compile issues:
  - Win32 handle-width truncation.
  - x87/SnapFloat assembly limited to x86.
  - Legacy MMX skinning gated.
  - FFT complex type moved into `fft.h`.
  - Thread context enum split away from renderer backend coupling.
  - Various stale includes removed or replaced with forward declarations for headless-friendly compilation.

- Fixed CI script-mode CMake behavior:
  - Previous Linux CI failed because the CMake script tests used `IN_LIST` without setting policy level `CMP0057`.
  - Commit `1c475eb` adds `cmake_minimum_required(VERSION 3.16)` to the two script-mode headless tests.

## Validation done locally

- `ctest --test-dir /tmp/kisakcod-tests-continue -C Release --output-on-failure`
  - Result: 4/4 tests passed.

- Headless include debt scan:
  - Command:
    - `cmake -DSOURCE_ROOT=/home/jmulesa/kisakcod -DALLOWLIST=/home/jmulesa/kisakcod/tests/headless_include_debt.allow -P /home/jmulesa/kisakcod/tests/headless_include_debt_test.cmake`
  - Result:
    - `-- Tracked 33 direct client/media includes in the headless dedicated source profile`

- `git diff --check`
  - Result: clean.

## CI status at handoff time

Latest GitHub Actions run after the final pushed fix:

- Run ID: `28992130859`
- URL: `https://github.com/jm2/kisakcod/actions/runs/28992130859`
- Commit: `1c475eb Set CMake policy for headless script tests`
- Final conclusion: `failure`

Passing jobs:

- Portable tests / Linux amd64
- Portable tests / Linux arm64
- Portable tests / macOS arm64
- Portable tests / Windows amd64
- Portable tests / Windows arm64

Failed jobs:

- Windows x86 / Debug
- Windows x86 / Release

Windows x86 Debug failed with these compiler error groups:

- `src/bgame/bg_pmove.cpp`: `SURF_TYPECOUNT` undeclared.
- `src/game/g_weapon.cpp`: `traceOffsets` undeclared.
- `src/physics/phys_coll_cylinderbrush.cpp`: `Vec4Copy` and `Vec3MadMad` not found.
- `src/qcommon/cm_load.cpp`: `THREAD_CONTEXT_TRACE_COUNT`, `THREAD_CONTEXT_MAIN`, `THREAD_CONTEXT_BACKEND`, and `THREAD_CONTEXT_COUNT` undeclared.
- `src/qcommon/cm_load_obj.cpp`: `DynEnt_LoadEntities` not found.
- `src/qcommon/cm_mesh.cpp`: `Vec3MadMad` not found.
- `src/win32/win_syscon.cpp`: `Con_GetTextCopy` not found.

Windows x86 Release failed with these compiler error groups:

- `src/game/g_weapon.cpp`: `traceOffsets` undeclared.
- `src/physics/phys_coll_cylinderbrush.cpp`: `Vec4Copy` and `Vec3MadMad` not found.
- `src/qcommon/cm_load.cpp`: `THREAD_CONTEXT_TRACE_COUNT`, `THREAD_CONTEXT_MAIN`, `THREAD_CONTEXT_BACKEND`, and `THREAD_CONTEXT_COUNT` undeclared.
- `src/qcommon/cm_load_obj.cpp`: `DynEnt_LoadEntities` not found.
- `src/qcommon/cm_mesh.cpp`: `Vec3MadMad` not found.
- `src/win32/win_syscon.cpp`: `Con_GetTextCopy` not found.

Previous failed run:

- Run ID: `28989793188`
- Previous Linux failures were only the CMake script-mode `IN_LIST` / `CMP0057` policy issue.
- Previous Windows x86 failures included `traceOffsets` and `SURF_TYPECOUNT`; the final run shows those are still present and additional Windows x86 compile errors are now visible.

## Recommended next session plan

1. Fix the Windows x86 Debug/Release compile errors from CI run `28992130859`.
   - Start with missing declarations/includes for `traceOffsets`, `SURF_TYPECOUNT`, vector helpers, thread-context enums, `DynEnt_LoadEntities`, and `Con_GetTextCopy`.
   - Re-run at least the Windows x86 CI jobs after fixing them.

2. Move CI from portable tests toward real platform builds:
   - Linux amd64 dedicated build.
   - Linux arm64 dedicated build.
   - macOS arm64 dedicated/headless build.
   - Win64 and Windows ARM64 builds where feasible.

3. Continue burning down the 33-item headless include-debt allowlist:
   - Move shared declarations into neutral headers.
   - Split client/render/sound-only types away from shared server code.
   - Replace client-media includes with forward declarations or dedicated-safe interfaces.

4. Replace guarded stubs/no-ops with real platform abstractions:
   - filesystem/profile paths,
   - networking,
   - console/syscon,
   - threading/timing,
   - dynamic libraries,
   - dedicated process lifecycle.

5. Add runtime smoke tests once dedicated binaries are buildable on non-Windows:
   - launch dedicated,
   - load minimal config,
   - bind local port,
   - run a few server frames,
   - clean shutdown.

6. Expand security hardening:
   - message parsing bounds checks,
   - asset loading validation,
   - pointer-token validation,
   - unsafe format/string/path handling,
   - network packet validation and fuzzable entry points.

7. Decide macOS scope:
   - Headless dedicated server is the practical near-term goal.
   - Full client/rendering on macOS is much larger because the renderer is Direct3D-era Windows-centric.

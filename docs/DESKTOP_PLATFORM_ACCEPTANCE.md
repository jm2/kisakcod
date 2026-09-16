# Desktop platform behavior and clean-install acceptance (MP clients)

**Status:** definition stage complete at the recorded SHA below; the required
tests, native runtime evidence and commercial-reference evidence remain
unproven. This document is documentation only. It changes no production
behavior, no input or gameplay default, no dvar, and it does not certify retail
compatibility.

**Tracking:** [issue #135](https://github.com/jm2/kisakcod/issues/135)
(roadmap item A13) / bead `ki-5lqu`. Issue #135 retains its full GitHub
acceptance checklist; this stage is a bounded definition artifact and does not
close it. Related: [issue #122](https://github.com/jm2/kisakcod/issues/122)
(commercial compatibility parent), [issue #130](https://github.com/jm2/kisakcod/issues/130)
(native headless server), [issue #134](https://github.com/jm2/kisakcod/issues/134)
(hosted sanitizers / Win32 runtime coverage), [issue #137](https://github.com/jm2/kisakcod/issues/137)
(release provenance).

**Recorded source SHA:** `2babfed8adb17a5b3c80db288890992ec51ec49c`
(`2babfed8`, the `origin/master` tip when this stage started; merge of fork
PR #106). Every file and line citation below was read from that exact checkout.
The tree wins over this document if they drift.

**Governance:** this work is governed by [#122](https://github.com/jm2/kisakcod/issues/122)
and [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). Perfect
interoperation with unmodified original commercial 1.7 and unmodified Steam
commercial 1.8 is mandatory. No commercial reference manifest is pinned, no
original-peer session has been run, and no client platform gate below is
satisfied by this document.

**Evidence class:** direct source inspection at the recorded SHA. No executable
was run for this document, no display/input device was exercised, no
clean-install image was built, and no licensed content or original binary was
used. Nothing here is a runtime result.

---

## 1. Purpose and acceptance boundary

The port plan names window/input and filesystem services as Phase 2 platform
work ([PORTING.md](PORTING.md) §"Phase 2 — Native Linux"), and
[ROADMAP_EXPANSION_PROPOSAL.md](ROADMAP_EXPANSION_PROPOSAL.md) A13 asks for
end-user platform behavior. What is missing is the *user-visible* acceptance:
correct platform primitives do not by themselves demonstrate retail-equivalent
command generation, usable data paths, focus behavior or clean startup.

This document supplies that missing layer:

1. a source-grounded statement of what the client platform surface does today
   at `2babfed8`;
2. normative requirements for each #135 acceptance item;
3. an explicit minimum OS/toolchain/graphics matrix; and
4. a test/acceptance matrix with exact procedures, platforms and required
   evidence.

It deliberately does **not** claim any item is met. Rows whose prerequisites
(such as a native Linux/macOS client composition or licensed reference
sessions) do not yet exist are marked **blocked** with the blocking dependency.

## 2. Scope and non-goals

**In scope (MP client unless noted):** writable config/cache/log paths and
read-only retail data discovery; case-sensitive filename handling and safe path
normalization; non-US console keys and text input; clipboard; relative mouse;
focus loss/regain; logical/framebuffer sizing; resize/fullscreen/monitor
changes; suspend/resume; absent-device and initialization-failure handling;
cleanup/restart; useful diagnostics on clean machines; explicit minimum
requirements.

**In scope for the headless dedicated server only where it shares the surface:**
filesystem paths/normalization, console/stdio behavior, process lifecycle,
startup diagnostics.

**Out of scope, explicitly deferred:** game controllers/rumble (beyond the
existing Win32 DirectInput path), HDR and other new display features, audio
device behavior (A10), renderer feature behavior (A09/M8), retail-content and
mod packaging (A11/A15), single-player save/load (SP is deferred), and any
incompatible modernization. These must not be smuggled into the SDL migration
or the platform layer; each needs its own acceptance.

## 3. Current-state source audit at `2babfed8`

### 3.1 Role composition

- The only runnable client today is the **Windows x86 MP client**; the Win32
  dedicated server also builds, including the dependency-free
  `KISAK_DEDI_HEADLESS` profile. `README.md` §"Current build support" states
  Win64, Windows ARM64, Linux amd64/arm64 and macOS arm64 are active targets
  but **not yet runnable engine builds**.
- `docs/PORTING.md` §"Phase 2" records that the Linux/macOS **client** source
  sets remain empty and gated; the native headless server does not need client
  media but does need its platform closure.
- `src/_platform/` holds backend files for `win32`, `posix`, and `macos`
  (console, event, filesystem, memory, process, socket, sync, thread, time,
  plus `macos/sys_mach_crash.cpp`). These are the platform-service layer; there
  is **no SDL anywhere in the tree** (`grep -rn SDL src tests CMakeLists.txt`
  returns nothing at the recorded SHA).

### 3.2 Window, event pump, focus, sizing

All of this is hand-rolled Win32, not a portable abstraction:

| Concern | Current implementation | Notes |
|---|---|---|
| Window class / creation | `Win_RegisterClass` and `CreateWindowExA` in `src/win32/win_main.cpp` (~98, ~688) | Win32-only; `WinMain` entry |
| Message pump / WndProc | `src/win32/win_wndproc.cpp` | Handles `WM_ACTIVATE` (~461), `WM_SETFOCUS`/`WM_KILLFOCUS` (~464/~469), `WM_MOVE` (~438), `WM_DISPLAYCHANGE` (~348), `WM_POWERBROADCAST` (~337), `WM_CLOSE`/`WM_DESTROY` |
| Activate/minimize | `VID_AppActivate` (~250) sets `g_wv.isMinimized` and `g_wv.activeApp`, then `IN_Activate` | Focus regains re-activate input |
| Screen metrics | `GetSystemMetrics` in `win_input.cpp` (~121) and `win_main.cpp` (~690) | Physical screen metrics, not logical/DPI-aware |
| Fullscreen / restart | `r_fullscreen` dvar + `Cbuf_AddText("vid_restart")` (`win_wndproc.cpp` ~397, ~426); `vid_xpos`/`vid_ypos` (~453) | Win32 display mode; no monitor enumeration/re-selection |
| DPI awareness | **none** | No `SetProcessDpiAware*`, no `WM_DPICHANGED`, no logical-vs-framebuffer scaling seam |

There is no logical/framebuffer size abstraction and no portable window/event
seam: `HWND` couples windowing to the D3D device, sound and input, exactly as
[PORTING.md](PORTING.md) §"Platform layer (win32/)" warns.

### 3.3 Input, clipboard and usercmd generation

- **Mouse:** `IN_ActivateWin32Mouse` / `IN_Win32Mouse` / `IN_MouseMove`
  (`src/win32/win_input.cpp` ~117, ~162, ~398) use `GetCursorPos`,
  `SetCursorPos` to the window centre, and `ClipCursor` to the window rect
  (~136, ~139, ~151) to produce a relative delta. `IN_RecenterMouse` and
  `IN_SetCursorPos` exist (~397, ~551). There is a DirectInput controller path
  (`win_input.cpp` ~198–201) for external controllers.
- **Keyboard/text:** `WM_KEYDOWN`/`WM_SYSKEYDOWN` produce key events
  (`win_wndproc.cpp` ~367–386); `WM_CHAR` produces character events (~382).
  There is **no IME, no keyboard-layout-aware translation** (`ToUnicode`,
  `GetKeyboardLayout`, `WM_IME_*`, `ImmGetContext` do not appear), so non-US
  console keys and dead-key/text composition are unproven and likely
  incomplete.
- **Clipboard:** `Sys_GetClipboardData` / `Sys_SetClipboardData` are Win32-only
  (`src/win32/win_main.cpp` ~481, ~508; declared in `win_local.h` ~146). Text
  fields call them (`src/ui/ui_component.cpp` ~1984, ~2020, ~3008, ~4069;
  `src/client/cl_keys.cpp` ~699). There is no POSIX/macOS clipboard backend.
- **Usercmd path (retail semantics live here):** `CL_MouseEvent`
  (`src/client_mp/cl_input.cpp` ~74) feeds `CL_Input` (~908) and
  `CL_CreateCmd` (~391), which calls `CL_FinishMove` (~798) to fill
  `usercmd_s`; `CL_WritePacket` (~141) emits commands. Mouse look applies
  `m_yaw`/`m_pitch` (registered in `cl_main_mp.cpp` ~3761/~3764),
  `sensitivity` (~3734), `cl_mouseAccel` (~3737) and the cgame FOV sensitivity
  scale (`cl_input.cpp` ~648–700), and normalizes input by `frame_msec`
  (~483–504, ~652–698). Any SDL migration must preserve this arithmetic,
  including angle quantization and msec accounting, rather than replacing it
  with a new input-to-camera path.

### 3.4 Filesystem, writable paths and path normalization

- **Base/home paths:** `FS_RegisterDvars` (`src/universal/com_files.cpp` ~1465)
  registers `fs_cdpath = Sys_DefaultCDPath()` (which returns `""` in
  `src/universal/win_common.cpp` ~435), `fs_basepath = Sys_Cwd()` (~1477), and
  `fs_homepath` defaulting to `fs_basepath` (~1488–1489). `fs_game` is
  constrained to `""` or a `mods/` subdirectory by `FS_GameDirDomainFunc`.
- `Sys_DefaultInstallPath` (`win_common.cpp` ~440) resolves the executable's
  parent directory (or CWD under a debugger) via
  `Sys_FileSystemGetExecutablePath`.
- **Consequence for clean install:** config/saves/logs are written under the
  working directory / install directory. There is no per-user writable
  config/cache/log separation and no explicit read-only retail-data source
  distinct from the write location. This is a concrete, named gap for #135 and
  is what P1 below requires.
- **OS path assembly:** `FS_BuildOSPath` / `FS_BuildOSPathForThread`
  (`com_files.cpp` ~569/~574) assemble `base/game/qpath`, enforce an OS path
  length bound (~598), then `FS_ReplaceSeparators` (~540) emits engine-style
  separators; `FS_ConvertPath` (~1434) changes case/separators for lookups.
- **Portable path semantics:** `src/qcommon/sys_filesystem.h` provides the
  portable, tested surface: `Sys_FileSystemCompareEnginePaths` /
  `Sys_FileSystemEnginePathsEqual` (ASCII case-insensitive, `\` and `:` fold to
  `/`), `Sys_FileSystemHasExtension`, `Sys_FileSystemSortPathPointers`,
  `Sys_FileSystemCreateDirectory`, `Sys_FileSystemReadFile` (no-follow, ".."
  rejected, bounded), `Sys_FileSystemListDirectory[Filtered]` (real entries,
  stable case-insensitive ordering), and `Sys_FileSystemRemoveTree` (never
  traverses links/reparse points). The win32 backend additionally rejects
  Win32-invalid bytes, normalization aliases and reserved DOS device
  components on referenced-file admission.
- **Existing coverage:** `tests/platform_filesystem_tests.cpp`
  (`TestFilteredCollectionAndPathHelpers` ~702 and the remove-tree suites)
  exercises separator/case folding, filter matching and link/reparse refusal.
  This is real coverage of the portable helpers, but it does **not** cover the
  user-visible config/cache/log layout, read-only retail discovery, or a
  case-sensitive host filesystem end to end.

### 3.5 Diagnostics, device lifecycle and existing portable coverage

- `src/win32/win_syscon.cpp` provides the Win32 system console; `_platform`
  console backends provide portable length-based stdout/stderr and line input
  (merged via PR #58), with tests in `tests/platform_console_tests.cpp` and
  `platform_console_source_test.cmake`.
- `tests/platform_service_contract_tests.cpp`,
  `platform_service_runtime_tests.cpp`, `platform_process_tests.cpp`,
  `platform_memory_tests.cpp`, `platform_socket_tests.cpp`, and
  `platform_crash_tests.cpp` cover platform-service contracts. `platform_*`
  tests are listed in `tests/CMakeLists.txt` and run in the portable CI matrix
  (`.github/workflows/ci.yml`, "Portable tests" job).
- There is **no** window/input/focus/clipboard/resize/suspend test harness and
  no clean-install or absent-device harness. Device-absence and initialization
  failure handling for display/audio/input is therefore unimplemented as
  acceptance, even where code has partial error paths.

## 4. Normative requirements

`MUST` marks a condition for #135 acceptance; `SHOULD` is a quality bar that
may be waived only with a recorded reason. Each requirement maps to test IDs in
§6. All requirements preserve the mandatory retail contract: no requirement may
change default input, gameplay, wire bytes or user-visible retail behavior.

### P1 — Writable paths and read-only retail discovery

- **P1.1** The client MUST separate, by role: a **read-only retail data root**
  (discovered, never written during normal play) and a **per-user writable
  root** for config, cache, logs, and (SP, when resumed) saves.
- **P1.2** The writable root MUST use the platform convention:
  Windows `%APPDATA%`/`%LOCALAPPDATA%` (roaming for config, local for
  cache/logs), Linux `$XDG_CONFIG_HOME`/`$XDG_CACHE_HOME`/`$XDG_STATE_HOME`
  (falling back to `~/.config`, `~/.cache`, `~/.local/state`), macOS
  `~/Library/Application Support` and `~/Library/Logs`.
- **P1.3** The retail data root MUST be selectable (install path, `-basepath`,
  or equivalent) and MUST be validated read-only for engine-managed writes;
  failure to discover it MUST produce an actionable diagnostic, not a crash or
  a silent write into the install directory.
- **P1.4** Existing `fs_homepath`/`fs_basepath` behavior MUST be preserved as
  an override so retail-compatible configurations and existing mods continue
  to resolve paths; the new defaults MUST NOT break explicit dvar/paths.

### P2 — Case-sensitive filenames and safe path normalization

- **P2.1** On case-sensitive hosts (Linux) asset lookup MUST NOT rely on
  case-insensitive matching succeeding; the on-disk case of retail/mod assets
  is authoritative, and lookups MUST try exact case before any folded fallback.
- **P2.2** Path normalization MUST fold `\` and `:` to `/` and compare ASCII
  case-insensitively only where the engine's engine-path semantics require it
  (`Sys_FileSystem*EnginePaths*`), and MUST reject `..`, absolute-path
  injection, NUL/control bytes, Win32-invalid bytes, normalization aliases and
  reserved DOS device names on externally supplied paths.
- **P2.3** Path length MUST be bounded and fail closed with a diagnostic rather
  than truncating into a different file (`FS_BuildOSPath` bound).
- **P2.4** Directory enumeration used by asset discovery MUST exclude symlinks
  and reparse points, as the current portable list/remove-tree contracts do.

### P3 — Input: non-US keys/text, clipboard, relative mouse

- **P3.1** Non-US console keys and text input MUST be tested for at least
  German, French and Japanese layouts, covering dead keys, AltGr,
  compose/IME text fields, and the `WM_CHAR`/text-entry path. Retail console
  key semantics that must not change are enumerated in §7.
- **P3.2** Clipboard get/set MUST work on every shipped client platform for
  text fields, with bounded size and no locale-dependent truncation.
- **P3.3** Relative mouse MUST produce the same per-frame delta for the same
  physical motion as the current Win32 path under the same `sensitivity`,
  `m_yaw`, `m_pitch`, `cl_mouseAccel`, and FOV scale; cursor recentering,
  clipping and foreground gating MUST be preserved.

### P4 — Focus, sizing, display changes, suspend/resume

- **P4.1** Focus loss MUST release mouse capture/relative mode and MUST NOT
  emit spurious usercmds; focus regain MUST re-acquire input predictably.
- **P4.2** Logical and framebuffer sizes MUST be representable separately with
  a defined mapping (DPI scale) so the UI and mouse deltas are correct on
  scaled displays.
- **P4.3** Windowed/edge-resize, fullscreen toggle, and monitor add/remove or
  resolution change MUST preserve a valid swapchain/target and reproduce the
  existing `vid_restart`/`r_fullscreen` semantics.
- **P4.4** Suspend/resume MUST stop and restart timing and audio/render loops
  without wall-clock jumps entering usercmd timing; on POSIX this replaces the
  Win32 `SuspendThread`/`ResumeThread` handshake with condition variables.

### P5 — Retail usercmd and movement preservation

- **P5.1** Input-to-usercmd timing, angle quantization, and movement behavior
  MUST match the current Win32 baseline for identical input traces. No default
  input or gameplay change may be introduced by the SDL/platform migration.
- **P5.2** Any SDL or platform input implementation MUST feed the existing
  `CL_MouseEvent`/`CL_Input`/`CL_CreateCmd`/`CL_FinishMove` path and MUST NOT
  bypass, rescale or reorder it.
- **P5.3** The commercial reference comparison for #127/A05 (§6, DP-CMD-01)
  is the external oracle; fork-to-fork traces are supplemental only.

### P6 — Absent devices, failures, cleanup/restart, diagnostics

- **P6.1** An absent or failed display/input/audio device MUST produce a
  bounded, human-readable diagnostic and a defined fallback or clean exit —
  never an unbounded loop, null dereference or silent no-op.
- **P6.2** Initialization failure MUST leave no half-initialized global state;
  cleanup MUST be idempotent and restart MUST work without process restart
  where the design allows.
- **P6.3** Clean-machine startup MUST be validated with no config present, with
  read-only install/data directories, and with a missing/malformed config file.

### P7 — Minimum requirements

- **P7.1** The project MUST publish explicit minimum OS, toolchain, graphics
  API/driver and display requirements per shipped target, consistent with the
  platform selection in [PORTING.md](PORTING.md) (native Vulkan RHI; MoltenVK
  on macOS; Linux amd64 release target).

## 5. Minimum supported requirements (proposed, explicit)

These are the candidate floors to be validated by the tests in §6; they are
proposed here so #135 has an explicit, reviewable target rather than an
implicit one.

| Target | Minimum OS | Toolchain | Graphics | Display/input | Notes |
|---|---|---|---|---|---|
| Windows x86/amd64 client | Windows 10 22H2 (build 19045) or later | MSVC v143 (VS 2022), CMake ≥ 3.16, Windows SDK 10.0.22621 | Vulkan 1.1 driver, or D3D9 migration reference | 1024×768 minimum; keyboard+mouse required | 32-bit x86 is the compatibility reference |
| Linux amd64 client | Ubuntu 22.04 / glibc 2.35 LTS class | GCC ≥ 12 or Clang ≥ 15, CMake ≥ 3.16 | Vulkan 1.1 loader + driver, SDL3 windowing | X11 or Wayland; 1024×768 minimum | release target |
| Linux amd64 headless server | Ubuntu 22.04 class | GCC ≥ 12 or Clang ≥ 15 | none | none (console/stdio) | priority role |
| macOS arm64 client | macOS 13 (Ventura) or later | AppleClang 15 / Xcode 15, CMake ≥ 3.16 | Metal via MoltenVK; Vulkan 1.1 feature set | 1024×768 minimum | signed/notarized app; x86_64 slice not required |
| macOS arm64 headless server | macOS 13 or later | AppleClang 15 / Xcode 15 | none | none | |
| Windows ARM64 | TBD, follows Windows 10 floor | MSVC v143 ARM64 | Vulkan 1.1 where available | keyboard+mouse | Phase 3 |

Open validation items for this table: exact Vulkan feature/extension floor,
whether 1024×768 is the real minimum for the retail UI, and the Linux display
server support statement. Rows are **proposed** until the corresponding test
evidence exists.

## 6. Clean-install acceptance matrix

Each row is a required test. "Status" is `planned` (harness absent at the
recorded SHA) or `partial` (related portable coverage exists but does not yet
satisfy the row). No row is `pass`.

| ID | Requirement | Procedure / harness | Platforms | Required evidence | Status |
|---|---|---|---|---|---|
| DP-FS-01 | P1.1–P1.4 writable vs read-only layout | Launch with no config; assert config/cache/log created under the per-user root and retail data read from the read-only root; assert no engine write under install/data | Win, Linux, macOS | New `platform_paths_tests` + clean-install image log | planned |
| DP-FS-02 | P2.1 case-sensitive lookup | On a case-sensitive host, place a mixed-case asset and require exact-case resolution; assert folded fallback only where specified | Linux | Linux test with retail-shaped fixture | partial |
| DP-FS-03 | P2.2 normalization/rejection | Extend `tests/platform_filesystem_tests.cpp` (`TestFilteredCollectionAndPathHelpers`) with `..`, absolute injection, NUL/control, Win32-invalid, DOS-device and alias cases through the engine-path helpers | Win, Linux, macOS | CTest output at exact head | partial |
| DP-FS-04 | P2.3 path-length bound | Build an over-length engine path and assert fail-closed with diagnostic, no truncation | Win, Linux, macOS | CTest output | partial |
| DP-FS-05 | P2.4 no-follow enumeration | Existing remove-tree/list link/reparse cases plus an asset-discovery walk | Win, Linux, macOS | CTest output | partial |
| DP-IN-01 | P3.1 non-US keys/text | Scripted layout matrix (de/fr/ja) through the window/input seam: dead keys, AltGr, text field, IME | Win, Linux, macOS | Input harness trace | planned |
| DP-IN-02 | P3.2 clipboard | Get/set round-trip for ASCII, non-ASCII, overlong and empty text in text fields | Win, Linux, macOS | Harness output | planned |
| DP-IN-03 | P3.3 relative mouse | Feed a fixed physical-motion trace and compare per-frame deltas against the Win32 baseline under fixed dvars | Win, Linux, macOS | Delta trace diff | planned |
| DP-WIN-01 | P4.1 focus loss/regain | Toggle focus/minimize; assert capture release, no spurious usercmds, predictable re-acquire | Win, Linux, macOS | Event + usercmd trace | planned |
| DP-WIN-02 | P4.2 logical/framebuffer mapping | Render at 100%/150%/200% DPI and assert correct UI scale and mouse mapping | Win, Linux (X11/Wayland), macOS | Screenshot + mapping test | planned |
| DP-WIN-03 | P4.3 resize/fullscreen/monitor change | Windowed resize, fullscreen toggle, monitor add/remove, resolution change; assert valid target and preserved semantics | Win, Linux, macOS | Manual + automated harness | planned |
| DP-WIN-04 | P4.4 suspend/resume | OS suspend/resume; assert timing/audio/render restart with no usercmd wall-clock jump | Win, Linux, macOS | Trace + wall-clock assertions | planned |
| DP-CMD-01 | P5.1–P5.3 usercmd preservation | Same input trace through the migration; compare against the Win32 baseline AND the #127/A05 commercial reference fixtures | Win, Linux, macOS | Trace diff + #127 fixtures | blocked on #127/#122 |
| DP-DEV-01 | P6.1 absent device | Start with no display/input/audio device or with a forced init failure; assert bounded diagnostic and fallback/clean exit | Win, Linux, macOS | Harness output + exit code | planned |
| DP-DEV-02 | P6.2 cleanup/restart | Fail init midway, clean up, restart; assert idempotent cleanup and no leaked global state | Win, Linux, macOS | Harness output | planned |
| DP-DEV-03 | P6.3 clean-machine startup | Fresh image, no config, read-only data dir, malformed config; assert actionable diagnostics | Win, Linux, macOS | Image run log | planned |
| DP-REQ-01 | P7.1 minimum requirements | Publish §5 and validate each floor on the minimum configuration (or record a measured reason) | All targets | Requirements doc + measured evidence | planned |

## 7. Retail usercmd invariants that must not change

The following are the concrete behaviors to pin in DP-IN-03 and DP-CMD-01.
They are read from the current `src/client_mp/cl_input.cpp` path and are *not*
authorized to change in the platform migration:

- Mouse look applies, in order: acceleration (`cl_mouseAccel` × per-frame rate
  + `sensitivity`), the cgame FOV sensitivity scale, then `m_yaw` (yaw) and
  `m_pitch` (pitch).
- Per-frame input is normalized by `frame_msec` (bounded so a zero frame is not
  divided by).
- Angle deltas are quantized into `usercmd_s` and written by `CL_WritePacket`
  with the existing msec accounting.
- Key/button state, down/up msec, and command creation during connection follow
  `CL_CreateCmd`/`CL_FinishMove` unchanged.
- Any controller/gamepad behavior is the existing path; new controller features
  are out of scope and must be separately gated.

## 8. Deferred / separately scoped

- Controllers/rumble beyond the existing Win32 path, HDR, and other new display
  features: out of scope, separate issues required.
- Audio device behavior, voice capture/playback: A10.
- Renderer feature parity, shader conversion: A09/M8.
- Retail-content/mod matrix and packaging: A11/A15.
- Single-player save/load and its path layout: deferred with SP.
- Wine/CrossOver: optional deployment workaround, not acceptance.

## 9. Dependencies, blockers and evidence rules

- **Commercial compatibility** is required by #122 and cannot be substituted by
  fork-to-fork or CoD4x tests. DP-CMD-01 stays blocked until #127 supplies
  reference fixtures and a licensed reference session is available.
- **Native composition**: DP-FS-01 and all DP-IN/DP-WIN rows require a native
  Linux/macOS client composition (A08/A09 and the client platform exits) that
  does not exist at the recorded SHA. They are planned against the eventual
  seam, not retrofitted to Win32.
- **CI coverage**: the applicable Win32 runtime tests and hosted sanitizers are
  A12. New harnesses must be enrolled so they run where they are applicable;
  excluding a failing platform is not acceptance.
- **Evidence rule**: a row passes only with exact head SHA, command, platform,
  configuration, and result. Hosted check success or bot review alone does not
  satisfy a row, and a missing licensed reference stays explicit.

## 10. Change control

- This document is the acceptance baseline for #135. Changes to a requirement,
  a minimum-requirement row, or a test ID require a recorded reason and must
  keep #135 open.
- The SDL migration must land behind the seam described here; do not reclassify
  an unimplemented window/input/filesystem behavior as "done" because a
  primitive compiles or a portable helper test passes.
- When the first native client is runnable, promote each `planned` row to a
  concrete harness and record results here or in its tracking bead.

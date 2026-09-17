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
startup diagnostics. Shared-surface requirements are evidenced **per role**:
client results and dedicated-server results are independent evidence and
neither certifies the other. Profile/UI-only operations are client-role tests
and are never forced onto headless roles; a headless role's evidence covers
the writable paths and clean-start diagnostics that role actually produces.

**Out of scope, explicitly deferred:** game controllers/rumble (beyond the
existing Win32 DirectInput path), HDR and other new display features, audio
device behavior including absent/failed devices, permission changes and
suspend/resume lifecycle (A10, [#132](https://github.com/jm2/kisakcod/issues/132)),
renderer feature behavior (A09/M8), retail-content and mod packaging (A11/A15),
single-player save/load (SP is deferred), and any incompatible modernization.
These must not be smuggled into the SDL migration or the platform layer; each
needs its own acceptance.

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

`WM_POWERBROADCAST` is handled only to filter APM messages
(`win_wndproc.cpp` ~337–341, returning early for `wParam > 1`); it does not
drive any thread suspend/resume handshake. The render/database pause path is
engine-event driven (see §3.4 and P4.4).

### 3.3 Input, clipboard and usercmd generation

- **Mouse:** `IN_ActivateWin32Mouse` / `IN_Win32Mouse` / `IN_MouseMove`
  (`src/win32/win_input.cpp` ~117, ~162, ~398) use `GetCursorPos`,
  `SetCursorPos` to the window centre, and `ClipCursor` to the window rect
  (~136, ~139, ~151) to produce a relative delta. `IN_RecenterMouse` and
  `IN_SetCursorPos` exist (~397, ~551). There is a DirectInput controller path
  (`win_input.cpp` ~198–201) for external controllers.
- **Keyboard/text:** `WM_KEYDOWN`/`WM_SYSKEYDOWN` produce physical key events
  through `MapKey` (`win_wndproc.cpp` ~367–379). The message pump calls
  `TranslateMessage` immediately before `DispatchMessageA`
  (`src/win32/win_main.cpp` ~136–137, and the secondary pump ~398–399), and the
  `WM_CHAR` it generates is queued as a text event (`win_wndproc.cpp` ~382–383).
  The OS message-translation path **is** therefore wired: text input is not
  absent, it arrives as `WM_CHAR` code units the platform translated from the
  keystroke. What is missing is layout-aware handling beyond that default path:
  `ToUnicode`, `GetKeyboardLayout`, `WM_IME_*` and `ImmGetContext` do not
  appear anywhere in the tree, and the client window is ANSI
  (`RegisterClassA`/`CreateWindowExA`, `win_main.cpp` ~688, ~695), so text is
  limited to the ANSI `WM_CHAR` path with no IME composition and no Unicode
  `WM_UNICHAR`. Three things must stay distinct when judging this: physical key
  mapping and console keys (present, via `MapKey`/`SE_KEY`); OS-translated ANSI
  text (present, via `TranslateMessage`/`WM_CHAR`); and non-US layout, dead-key,
  AltGr and IME composition (no evidence, likely incomplete). No non-US runtime
  evidence was captured for this stage.
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
- **Read root vs write location:** an explicit separation already exists in
  the engine, but only by dvar override. Engine-managed writes build their OS
  path from `fs_homepath`, not the read root — `FS_FOpenTextFileWrite`
  (~766), `FS_FOpenFileAppend` (~794) and `FS_Delete` (~1167) all use
  `fs_homepath` + `fs_gamedir`. Reads instead add base and home as separate
  search paths in `FS_Startup` (~2226 onward), adding the home root only when
  it differs from `fs_basepath` (~2234, ~2255, ~2269, ~2280). Setting
  `fs_homepath` to a writable location distinct from a read-only `fs_basepath`
  is a supported configuration today.
- **Remaining base-path writers (player profiles):** not every engine-managed
  write is routed through `fs_homepath`. `Com_NewPlayerProfile` and
  `Com_DeletePlayerProfile` (`src/qcommon/com_playerprofile.cpp` ~197/~163) both
  build their OS path with
  `FS_BuildOSPath(fs_basepath, "players", <profilePath>, osPath)` (~210/~171)
  and then call `FS_CreatePath` (~211) or `Sys_RemoveDirTree` (~172). With a
  distinct writable `fs_homepath` and a read-only install/retail root, normal
  profile creation or deletion therefore still targets the base (install) tree
  and can fail or attempt to modify it. The save/reload cycle has the mirror
  gap: profile/config/stat writes go through
  `FS_FOpenFileWriteToDirForThread` (`src/universal/com_files.cpp` ~3170),
  which builds its path from `fs_homepath`, but `FS_Startup` (~2226–2240)
  enrolls the `players` directory only under `fs_basepath` (~2232) and never
  enrolls `players` under a distinct `fs_homepath` — so a profile saved under
  the writable root is not reachable through the search paths on the next
  start, and `Com_SetInitialPlayerProfile`
  (`src/qcommon/com_playerprofile.cpp` ~145, reading `profiles/active.txt` via
  `FS_ReadFile` at ~151) cannot reload it. These are remaining gaps that the
  `fs_homepath` override does not cover; P1.5 and DP-FS-07 require them to be
  documented and covered with separated writable/read-only roots across the
  full save, restart and reload cycle, so missing home `players` search
  enrollment cannot pass.
- **Consequence for clean install:** the *default* is collocated, not separated.
  `fs_basepath` defaults to `Sys_Cwd()` (~1477) and `fs_homepath`
  defaults to `fs_basepath` (~1488–1489); no `Sys_DefaultHomePath`-style
  per-user path is wired in. Out of the box, config/saves/logs are written
  under the working directory / install directory, with no automatic per-user
  config/cache/log layout and no *discovered* read-only retail root. The gap
  for #135 is the missing automatic per-user layout and automatic retail-root
  discovery, not the absence of any read/write separation; P1 below requires
  it.
- **OS path assembly:** `FS_BuildOSPath` / `FS_BuildOSPathForThread`
  (`com_files.cpp` ~569/~574) assemble `base/game/qpath`, enforce an OS path
  length bound (~598), then `FS_ReplaceSeparators` (~540) emits Win32-style
  separators and `FS_ConvertPath` (~1434) folds `\` and `:` to `/` for lookups;
  neither changes case.
- **Portable path semantics:** `src/qcommon/sys_filesystem.h` exposes two
  deliberately distinct surfaces. The **normalization/compare** helpers —
  `Sys_FileSystemCompareEnginePaths` (~208) / `Sys_FileSystemEnginePathsEqual`
  (~236) (ASCII case-insensitive, `\` and `:` fold to `/`),
  `Sys_FileSystemSortPathPointers` (~243) and `Sys_FileSystemMatchesPathFilter`
  (~261) — normalize and order otherwise arbitrary bytes and have **no
  rejection result**; they are not, and must not be cited as, the unsafe-path
  gate. The **path-accepting operations** instead validate the caller-supplied
  path before doing any filesystem work: `Sys_FileSystemCreateDirectory`,
  `Sys_FileSystemReadFile` (no-follow, bounded),
  `Sys_FileSystemListDirectory[Filtered]` (real entries, stable
  case-insensitive ordering) and `Sys_FileSystemRemoveTree` (never traverses
  links/reparse points). These are general filesystem APIs: their validation
  rejects the backend-specific invalid component sets below but deliberately
  accepts well-formed absolute paths and platform-valid names, because callers
  pass configured roots, build paths and test/temp roots. It is therefore
  **not** a rooted engine-relative input gate; the caller that joins an
  untrusted `qpath` to a trusted root is the separate rooted boundary in P2.2b,
  which the audited tree does not implement. On win32 it is
  `HasUnsafeRawComponent`
  (`src/_platform/win32/sys_filesystem.cpp` ~152, invoked at ~507, ~560, ~664
  and ~1924), rejecting `..`, control and Win32-invalid characters
  (`<`/`>`/`"`/`|`/`*`, bare `?`/`:`), trailing dot or space, reserved DOS
  device base names (`CON`/`PRN`/`AUX`/`NUL`/`CONIN$`/`CONOUT$`/`COM1-9`/
  `LPT1-9`) and component-count overflow. On POSIX/macOS it is `SplitSafePath`
  (`src/_platform/posix/sys_filesystem.cpp` ~124, invoked at ~163, ~250, ~297
  and via `ParseRemoveTreePath` ~941), rejecting invalid UTF-8, `..` and
  component-count overflow. The two validators are **not equivalent**: DOS
  device names and the Win32 byte rules are win32-only, while UTF-8 validity
  is POSIX-only, and neither rejects a well-formed absolute path. Of the
  path-accepting operations, `Sys_FileSystemCreateDirectory` (via `Sys_Mkdir`,
  `src/universal/win_common.cpp` ~21) and
  `Sys_FileSystemListDirectory[Filtered]` (via `Sys_ListFiles`, ~330/~371) are
  the ones currently wired to
  production callers; `Sys_FileSystemReadFile` and `Sys_FileSystemRemoveTree`
  share the same validator but are driven today by tests and fuzz, and POSIX
  `Sys_RemoveDirTree` is still a stub (`win_common.cpp` ~68–75).
- **Open-path follow behavior:** the no-follow guarantee above belongs to the
  portable operations (`Sys_FileSystemReadFile`, `Sys_FileSystemRemoveTree`).
  The engine's actual loose-file lookup instead opens the assembled OS path with
  `FS_FileOpenReadBinary` (`com_files.cpp` ~1006 and callers such as ~956,
  ~1013), which is a plain `fopen`-class open and follows an in-root
  symlink/reparse component. Lexical `qpath` validation therefore cannot keep a
  resolved *open* under the trusted root; P2.2b/DP-FS-06 must additionally
  require a rooted no-follow open whose resolved target stays under the root.
- **Existing coverage:** `tests/platform_filesystem_tests.cpp`
  (`TestFilteredCollectionAndPathHelpers` ~702) exercises *normalization and
  ordering* only — separator/case folding, filter matching and sort order on
  the helpers — so it is real coverage but **not** a rejection test. Rejection
  assertions that do exist (`Sys_FileSystemReadFile`/`Sys_FileSystemRemoveTree`
  with `..`, invalid UTF-8, links and reparse points, ~882/~933 and
  ~1197–1201) target the portable operations rather than the win32 component
  validator through a production entry point; no test drives an unsafe path
  through `Sys_Mkdir`/`Sys_ListFiles`. None of this covers the user-visible
  config/cache/log layout, read-only retail discovery, or a case-sensitive host
  filesystem end to end.

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
  failure handling for display and input (audio is A10) is therefore
  unimplemented as acceptance, even where code has partial error paths.

## 4. Normative requirements

`MUST` marks a condition for #135 acceptance; `SHOULD` is a quality bar that
may be waived only with a recorded reason. Each requirement maps to test IDs in
§6. All requirements preserve the mandatory retail contract: no requirement may
change default input, gameplay, wire bytes or user-visible retail behavior.

### P1 — Writable paths and read-only retail discovery

- **P1.1** Each shipped role — the MP client, and the headless dedicated
  server for the filesystem surface it shares per §2 — MUST separate: a
  **read-only retail data root** (discovered, never written during normal
  play) and a **per-user writable root** for config, cache, logs, and, for
  the client only, (SP, when resumed) saves. The separation MUST be
  evidenced independently per role: client results MUST NOT certify the
  dedicated server's writable paths or clean start, and vice versa.
- **P1.2** The writable root MUST use the platform convention:
  Windows `%APPDATA%`/`%LOCALAPPDATA%` (roaming for config, local for
  cache/logs; if either variable is unset, empty, or set to a **relative**
  path, the value is unusable as a root and the affected role roots
  MUST resolve through the Windows Known Folder API — `SHGetKnownFolderPath`
  with `FOLDERID_RoamingAppData` for config and `FOLDERID_LocalAppData` for
  cache/logs — rather than by re-deriving the default `%USERPROFILE%\AppData`
  layout and rather than by resolving the variable's value against the
  current working directory, so a policy-redirected AppData location keeps
  working, the per-role split is preserved even in sessions where
  `%USERPROFILE%` itself is unavailable, and a CWD-relative root is never
  accepted; if the applicable Known Folder lookup also fails, resolution
  MUST fail closed with an actionable diagnostic and MUST NOT fall back to
  writing under the install or retail data tree),
  Linux `$XDG_CONFIG_HOME`/`$XDG_CACHE_HOME`/`$XDG_STATE_HOME`
  (falling back to `~/.config`, `~/.cache`, `~/.local/state`; per the
  [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/)
  each variable is honored only when it names an **absolute** path: an
  **empty** value MUST be treated as unset (per-role default), a
  **relative** value MUST be ignored as invalid (per-role default;
  CWD-relative resolution MUST NOT be accepted), and if no usable root can
  be resolved (for example no determinable home directory), resolution MUST
  fail closed with an actionable diagnostic and MUST NOT fall back to
  writing under the install or retail data tree), macOS
  config/state under `~/Library/Application Support`, cache under
  `~/Library/Caches`, and logs under `~/Library/Logs`.
- **P1.3** The retail data root MUST be selectable (install path, `-basepath`,
  or equivalent) and MUST be validated read-only for engine-managed writes;
  failure to discover it MUST produce an actionable diagnostic, not a crash or
  a silent write into the install directory.
- **P1.4** Existing `fs_homepath`/`fs_basepath` behavior MUST be preserved as
  an override so retail-compatible configurations and existing mods continue
  to resolve paths; the new defaults MUST NOT break explicit dvar/paths.
- **P1.5** Player-profile creation (`Com_NewPlayerProfile`), deletion
  (`Com_DeletePlayerProfile`), and selection persistence — saving the selected
  profile (`profiles/active.txt` plus the profile's `config_mp.cfg`/stats),
  restarting, and re-selecting the same profile — MUST operate on and be
  served from the writable per-user root, not from `fs_basepath`. At the
  recorded SHA creation and deletion build `players/<profile>` from
  `fs_basepath` (`com_playerprofile.cpp` ~210/~171) and then create or remove
  it there; the active-profile write instead lands under `fs_homepath`
  (`Com_ChangePlayerProfile` ~648 → `FS_FOpenFileWriteToDirForThread`,
  `com_files.cpp` ~3170); and `Com_SetInitialPlayerProfile` re-reads
  `profiles/active.txt` through the search paths (~145) even though
  `FS_Startup` enrolls the `players` directory only under `fs_basepath`
  (`com_files.cpp` ~2232; the `fs_homepath` block ~2238–2244 adds
  devraw/raw only). These remaining base-path writers and the missing home
  `players` search enrollment MUST NOT be treated as satisfied by the
  `fs_homepath` override; DP-FS-07 covers the ordered
  create/select/save/restart/reload/delete lifecycle with separated
  writable and read-only roots. Player profiles are a client-role surface:
  dedicated-server roles have no player profiles, and this requirement and
  DP-FS-07 are not asserted on headless roles.

### P2 — Case-sensitive filenames and safe path normalization

- **P2.1** On case-sensitive hosts (Linux) asset lookup MUST NOT rely on
  case-insensitive matching succeeding; the on-disk case of retail/mod assets
  is authoritative. The current engine already behaves this way: it builds the
  OS path with `FS_BuildOSPathForThread` and opens it directly
  (`FS_FileOpenReadBinary`, `src/universal/com_files.cpp` ~1006), so the host
  filesystem's case rules decide every lookup. There is **no explicit folded
  probe today**, so any case-insensitive fallback is new behavior.
- **P2.1a (permitted fold-fallback policy).** A folded (case-insensitive) probe
  MAY be attempted only when all of the following hold: (a) the exact requested
  case was probed first and failed; (b) the lookup is a read of retail/mod
  content through an engine-relative path — never an engine-constructed write
  path (config/cache/log/save) and never an externally supplied absolute path;
  and (c) the folded probe resolves to exactly one on-disk entry. If a folded
  probe matches two or more entries that differ only by case in the same
  directory, the lookup MUST fail closed with a diagnostic and MUST NOT use
  enumeration order to choose a winner. Engine-constructed paths MUST use exact
  case. A fallback is acceptable for #135 acceptance only when a
  commercial-reference comparison (DP-CMD-01/#127) shows the unmodified
  commercial client resolving the same name; until that evidence exists the
  fallback is unproven and DP-FS-02 stays `partial`.
- **P2.2** Path normalization MUST fold `\` and `:` to `/` and compare ASCII
  case-insensitively only where engine-path semantics require it. That
  normalization/compare contract is the `Sys_FileSystem*EnginePaths*` helpers
  (`src/qcommon/sys_filesystem.h` ~208–255), which have **no rejection
  result**. Validation is a **separate production boundary**: the path-accepting
  operations validate before any filesystem work (win32 `HasUnsafeRawComponent`,
  POSIX/macOS `SplitSafePath`; see §3.4) and MUST fail closed with no partial
  effect. The enforced set is backend-specific and MUST be stated per platform
  rather than asserted as one portable rule.
- **P2.2a (general path-accepting operations).** The path-accepting operations
  are general filesystem APIs. Their legitimate inputs include well-formed
  **absolute paths** — configured roots and build paths that callers resolve
  themselves — and names that are valid on the host platform. The current win32
  validator rejects `..`, control/Win32-invalid bytes, trailing dot/space,
  reserved DOS device base names and bare `?`/`:`; POSIX/macOS reject invalid
  UTF-8, `..` and component-count overflow (see §3.4). Both accept a well-formed
  absolute path, and neither rejects a name merely because it is a DOS device
  base name on POSIX. #135 does **not** require broadening these APIs to refuse
  absolute paths, and MUST NOT impose win32 name rules on POSIX absent
  compatibility evidence; existing legitimate callers (loaders, configured
  roots, test/temp roots) MUST keep working.
- **P2.2b (rooted external-input validation, separately planned).** Untrusted
  **engine-relative** input — a `qpath`-style value supplied by a mod, network
  message, or console/command input — MUST be validated at the **rooted caller**
  that joins it to the trusted engine root, before it reaches any path-accepting
  operation: absolute segments, `..` traversal, normalization aliases and
  invalid bytes are rejected there so the resolved path stays under the intended
  root. This rooted boundary is distinct from P2.2a and is **not** implemented
  by the backend component validators audited in §3.4; #135 requires it to be
  specified and tested separately (DP-FS-06) with no production behavior change
  in this definition stage, and no retail wire/command behavior change.
  - Containment MUST be enforced on the **resolved** path and MUST consider path
    components that are themselves symlinks or reparse points. A `qpath` whose
    lexical form is clean but that traverses an in-root link/reparse component to
    a target outside the root MUST fail closed. Because the engine's loose-file
    lookup (`FS_FileOpenReadBinary`) follows such links, lexical input checks
    alone are insufficient: the rooted caller MUST use a **no-follow open**
    (open each component without traversing links/reparse points and verify the
    resolved target stays under the root). DP-FS-05 verifies enumeration only and
    does not satisfy this.
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
- **P4.4** OS suspend/resume MUST stop and restart timing and render loops
  without wall-clock jumps entering usercmd timing. This is an OS power-lifecycle
  concern and is **separate** from two independent thread contracts that MUST NOT
  be conflated with it: (a) initially-suspended thread startup, which the Win32
  backend starts with a raw `ResumeThread`
  (`src/_platform/win32/sys_thread.cpp` ~328, `Sys_ThreadStart`); and (b)
  terminal crash freezing, which uses `SuspendThread` (~463,
  `Sys_ThreadForceSuspendForCrash`). At the recorded SHA the render/database
  pause handshake is driven by engine events, not by those raw calls:
  `renderPausedEvent` and `wakeDatabaseEvent`/`resumedDatabaseEvent` are created,
  set and waited in `src/qcommon/threads.cpp` (~377, ~403–406, ~635–644,
  ~744–754), and `WM_POWERBROADCAST` (`src/win32/win_wndproc.cpp` ~337) does not
  drive the suspend/resume handshake. A POSIX client therefore replaces the OS
  power-lifecycle handling with a platform power seam; it does not need to
  replicate `ResumeThread`/`SuspendThread`, and this requirement MUST NOT
  prescribe condition variables for thread startup or crash-freeze. The
  audio-device suspend/resume handshake is A10's lifecycle acceptance
  ([#132](https://github.com/jm2/kisakcod/issues/132)); A13 owns the shared
  platform suspend/resume seam that A10's audio loop attaches to, and DP-WIN-04
  tests the seam, not the audio device.

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

- **P6.1** An absent or failed **display or input** device MUST produce a
  bounded, human-readable diagnostic and a defined fallback or clean exit —
  never an unbounded loop, null dereference or silent no-op. Absent/failed
  audio devices, audio permission changes and audio suspend/resume are A10
  acceptance ([#132](https://github.com/jm2/kisakcod/issues/132)), not #135;
  see §2 and §8.
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
| Windows x86 client | Windows 10 22H2 (build 19045) or later | MSVC v143 (VS 2022) x86, CMake ≥ 3.16, Windows SDK 10.0.22621 | Vulkan 1.1 driver (required acceptance endpoint; D3D9 migration/reference evidence is recorded separately under its own label and cannot satisfy this row) | 1024×768 minimum; keyboard+mouse required | 32-bit x86 is the compatibility reference; floor evidence MUST be measured on an x86 minimum configuration |
| Windows amd64 client | Windows 10 22H2 (build 19045) or later | MSVC v143 (VS 2022) x64, CMake ≥ 3.16, Windows SDK 10.0.22621 | Vulkan 1.1 driver (required acceptance endpoint; D3D9 migration/reference evidence is recorded separately under its own label and cannot satisfy this row) | 1024×768 minimum; keyboard+mouse required | M6 amd64 client delivery (PORTING.md); floor evidence MUST be measured independently on an amd64 minimum configuration — x86 client evidence does not certify this row |
| Windows x86 dedicated server | Windows 10 22H2 (build 19045) or later | MSVC v143 (VS 2022), CMake ≥ 3.16, Windows SDK 10.0.22621 | none | none (console/stdio; headless-capable) | existing Win32 dedicated server role, `KISAK_DEDI_HEADLESS` profile; compatibility reference server |
| Windows amd64 headless server | Windows 10 22H2 (build 19045) / Windows Server 2022 class or later | MSVC v143 (VS 2022) x64, CMake ≥ 3.16, Windows SDK 10.0.22621 | none | none (console/stdio) | M6 server role; delivery tracked separately from the M6 client (PORTING.md) |
| Linux amd64 client | Ubuntu 22.04 / glibc 2.35 LTS class | GCC ≥ 12 or Clang ≥ 15, CMake ≥ 3.16 | Vulkan 1.1 loader + driver, SDL3 windowing | X11 or Wayland; 1024×768 minimum | release target |
| Linux amd64 headless server | Ubuntu 22.04 class | GCC ≥ 12 or Clang ≥ 15 | none | none (console/stdio) | priority role |
| Linux arm64 client | Ubuntu 22.04 / glibc 2.35 LTS class (arm64) | GCC ≥ 12 or Clang ≥ 15, CMake ≥ 3.16 | Vulkan 1.1 loader + arm64 driver, SDL3 windowing | X11 or Wayland; 1024×768 minimum | shipped target (see §3.1) |
| Linux arm64 headless server | Ubuntu 22.04 class (arm64) | GCC ≥ 12 or Clang ≥ 15 | none | none (console/stdio) | shipped target (see §3.1) |
| macOS arm64 client | macOS 13 (Ventura) or later | AppleClang 15 / Xcode 15, CMake ≥ 3.16 | Metal via MoltenVK; Vulkan 1.1 feature set | 1024×768 minimum | signed/notarized app; x86_64 slice not required |
| macOS arm64 headless server | macOS 13 or later | AppleClang 15 / Xcode 15 | none | none | |
| Windows ARM64 client | **Blocked — no validated OS floor** (see note below) | MSVC v143 ARM64 (candidate only) | **Blocked — committed Vulkan 1.1 endpoint unvalidated on ARM64** | keyboard+mouse (candidate only) | Phase 3; DP-REQ-01 counts this row as blocked |
| Windows ARM64 headless server | **Blocked — no validated ARM64 OS floor** (see note below) | MSVC v143 ARM64 (candidate only) | none | none (console/stdio) | M11 server role; blocked with the ARM64 client row; requires real-hardware validation |

Open validation items for this table: exact Vulkan feature/extension floor,
whether 1024×768 is the real minimum for the retail UI, and the Linux display
server support statement. Rows are **proposed** until the corresponding test
evidence exists. Every shipped target in §3.1 has a row here — including the
separate Windows x86 and Windows amd64 client rows, Linux
arm64 client/server, the existing Windows x86 dedicated server, and the
Windows amd64/ARM64 headless server roles required by PORTING.md M6/M11 — so
DP-REQ-01's "All targets" gate has a row to validate for each and cannot be
marked complete while a delivery target is undefined. Server floors are
validated per role, and the Windows client floors are validated per
architecture: one role's or architecture's validation does not stand in for
another's. The Windows x86 and Windows amd64 client rows require a native
**Vulkan 1.1 driver** as the graphics endpoint for acceptance; D3D9 (and
dxvk-native) migration/reference evidence is recorded separately under its own
label and cannot satisfy those rows' graphics floor, because PORTING.md M8
classifies D3D9/dxvk-native as migration experiments/reference paths, not
replacement endpoints.
The Windows ARM64 rows (client and headless server) are **explicitly
blocked**, not merely proposed: neither has a validated ARM64 OS floor, and
the client's committed Vulkan 1.1 endpoint additionally has no ARM64 driver
validation evidence, so DP-REQ-01 MUST treat both as blocked and cannot pass
until concrete OS/toolchain (and, for the client, graphics) floors are
published in this table and validated on their minimum configurations.

## 6. Clean-install acceptance matrix

Each row is a required test. "Status" is `planned` (harness absent at the
recorded SHA) or `partial` (related portable coverage exists but does not yet
satisfy the row). No row is `pass`.

| ID | Requirement | Procedure / harness | Platforms | Required evidence | Status |
|---|---|---|---|---|---|
| DP-FS-01 | P1.1–P1.4 writable vs read-only layout | Launch with no config; assert each artifact lands in its **exact role-specific root** and retail data is read from the read-only root, with no engine write under install/data. Windows: config under `%APPDATA%`, cache and logs under `%LOCALAPPDATA%`; with `%APPDATA%` or `%LOCALAPPDATA%` unset, empty, or set to a **relative** path such as `relative\path`, the affected role roots MUST land where `SHGetKnownFolderPath` resolves `FOLDERID_RoamingAppData` (config) and `FOLDERID_LocalAppData` (cache/logs) with the per-role split preserved — the assertion is equality with the Known Folder API result, not with the default `%USERPROFILE%\AppData` layout and not with any CWD-relative resolution of the variable value, so a redirected session still passes and a default-layout re-derivation or CWD-relative root fails; when the applicable Known Folder lookup fails, launch MUST fail with an actionable diagnostic and no engine write under install/data. Linux: config under `$XDG_CONFIG_HOME` (fallback `~/.config`), cache under `$XDG_CACHE_HOME` (fallback `~/.cache`), state/logs under `$XDG_STATE_HOME` (fallback `~/.local/state`); for each XDG variable also assert the **empty** value (treated as unset → per-role default) and a **relative** value such as `relative/path` (ignored as invalid → per-role default), and that no such run writes under the install/data tree or resolves a CWD-relative root; when no usable root can be resolved, launch MUST fail with an actionable diagnostic and no install/data write. macOS: config/state under `~/Library/Application Support`, cache under `~/Library/Caches`, logs under `~/Library/Logs`. Repeat with each environment variable overridden, empty, set to a relative path, and unset, to assert the documented behavior for every case. Evidence is recorded independently for the **client** role and the **headless dedicated server** role on each OS: the server asserts the writable config/cache/log roots and clean-start diagnostics it actually produces, client results MUST NOT certify the dedicated server paths (or vice versa), and client-only profile/UI operations are not asserted on headless roles. A build that puts every artifact under one singular root (for example all of `%APPDATA%` or all of `$XDG_CONFIG_HOME`) MUST fail this row. | Win, Linux, macOS | New `platform_paths_tests` + clean-install image log (per role) | planned |
| DP-FS-02 | P2.1/P2.1a case-sensitive lookup | On a case-sensitive host, place a mixed-case asset and require exact-case resolution first; assert a folded fallback only under the P2.1a conditions (read-only retail/mod content, single unambiguous match) and assert fail-closed rejection on a case-only collision. A fallback not validated against a commercial reference stays unproven. | Linux | Linux test with retail-shaped fixture + commercial-reference result | partial |
| DP-FS-03 | P2.2/P2.2a backend rejection **and** positive acceptance at the general path-accepting operations | Through a **production path-accepting operation** — `Sys_FileSystemCreateDirectory` (via `Sys_Mkdir`) and `Sys_FileSystemListDirectory[Filtered]` (via `Sys_ListFiles`) — assert **per platform** both negatives and positives. Win negatives: `..`, control/Win32-invalid bytes, reserved DOS device base names, trailing dot/space, over-long individual components, and a distinct component-count overflow case (more than `kMaximumPathComponents` = 256 short components, each individually legal, exercising the count guard in win32 `HasUnsafeRawComponent` — src/_platform/win32/sys_filesystem.cpp ~223 — so the row cannot pass if only the over-long-component check survives); fail closed with no effect. Linux/macOS negatives: invalid UTF-8, `..`, component-count overflow. Positives (all platforms): a well-formed absolute path under a configured/temp root succeeds, because these are general filesystem APIs rather than engine-relative gates; on Linux/macOS a DOS device base name such as `CON` is a valid filename and MUST NOT be rejected without contrary compatibility evidence; the compare/sort helpers remain non-validating. `TestFilteredCollectionAndPathHelpers` covers normalization/ordering only and cannot satisfy this row. | Win, Linux, macOS | CTest output at exact head | partial |
| DP-FS-04 | P2.3 path-length bound | Build an over-length engine path and assert fail-closed with diagnostic, no truncation | Win, Linux, macOS | CTest output | partial |
| DP-FS-05 | P2.4 no-follow enumeration | Existing remove-tree/list link/reparse cases plus an asset-discovery walk | Win, Linux, macOS | CTest output | partial |
| DP-FS-06 | P2.2b rooted engine-relative input validation (separately planned) | Feed untrusted engine-relative `qpath` values (absolute segments, `..` traversal, `\`/`:` alias spellings, invalid bytes) through the rooted caller that joins them to the trusted engine root and assert fail-closed rejection before any path-accepting or open operation, while legitimate absolute API inputs from P2.2a still succeed. Include link/reparse coverage: a lexically clean `qpath` that traverses an in-root symlink/reparse component to a target outside the root MUST fail closed at the rooted no-follow open, and any returned handle MUST refer to a target under the root. DP-FS-05's enumeration-only exclusion does not satisfy this row. Rooted validator not implemented at the recorded SHA; no production behavior change in this definition stage and no retail wire/command change. | Win, Linux, macOS | CTest output at exact head | planned |
| DP-FS-07 | P1.5 player-profile create/select/save/restart/reload/delete lifecycle under separated roots (client role) | With a distinct writable per-user root and a read-only install/retail root, run one ordered lifecycle over the same profile; the deletion leg runs last (or against an explicitly separate deletion profile) so it cannot invalidate the persistence legs: (a) create the profile; assert the `players/<profile>` directory is created under the writable root and the read-only install tree is not touched or failed on; (b) select the profile and save — assert `profiles/active.txt` (`Com_ChangePlayerProfile` ~648 → `FS_WriteFileToDir` → `FS_FOpenFileWriteToDirForThread`, `com_files.cpp` ~3170) and the profile's `config_mp.cfg`/stats artifacts are written under the writable root with nothing written into the read-only retail tree; (c) restart the engine (fresh `FS_Startup`) and assert `Com_SetInitialPlayerProfile` (`com_playerprofile.cpp` ~145) re-selects the same profile by reading `profiles/active.txt` through the search paths and that the saved config/stats reload from the writable root; (d) delete the profile; assert the `players/<profile>` directory is removed under the writable root and the read-only install tree is not touched or failed on. At the recorded SHA create/delete target `fs_basepath` (`com_playerprofile.cpp` ~210/~171), the active-profile write targets `fs_homepath` (`com_files.cpp` ~3170), and `FS_Startup` enrolls `players` as a search path only under `fs_basepath` (~2232; the `fs_homepath` block adds devraw/raw only) — a build that leaves the writable root's `players` directory un-enrolled MUST fail the restart/reload leg and cannot pass this row. | Win, Linux, macOS (client role; dedicated servers have no player profiles) | CTest output + path trace | planned |
| DP-IN-01 | P3.1 non-US keys/text | Scripted layout matrix (de/fr/ja) through the window/input seam: dead keys, AltGr, text field, IME | Win, Linux, macOS | Input harness trace | planned |
| DP-IN-02 | P3.2 clipboard | Get/set round-trip for ASCII, non-ASCII, overlong and empty text in text fields | Win, Linux, macOS | Harness output | planned |
| DP-IN-03 | P3.3 relative mouse | Feed a fixed physical-motion trace and compare per-frame deltas against the Win32 baseline under fixed dvars | Win, Linux, macOS | Delta trace diff | planned |
| DP-WIN-01 | P4.1 focus loss/regain | Toggle focus/minimize; assert capture release, no spurious usercmds, predictable re-acquire | Win, Linux, macOS | Event + usercmd trace | planned |
| DP-WIN-02 | P4.2 logical/framebuffer mapping | Render at 100%/150%/200% DPI and assert correct UI scale and mouse mapping | Win, Linux (X11/Wayland), macOS | Screenshot + mapping test | planned |
| DP-WIN-03 | P4.3 resize/fullscreen/monitor change | Windowed resize, fullscreen toggle, monitor add/remove, resolution change; assert valid target and preserved semantics | Win, Linux, macOS | Manual + automated harness | planned |
| DP-WIN-04 | P4.4 OS suspend/resume | OS suspend/resume; assert timing/render restart through the shared platform seam with no usercmd wall-clock jump. Assert separately that the initially-suspended thread-start contract and the terminal crash-freeze contract are unaffected; `WM_POWERBROADCAST` is not expected to drive either (`ResumeThread`/`SuspendThread` are used for thread start/crash freeze, not OS power). The audio-device resume handshake is A10. | Win, Linux, macOS | Trace + wall-clock assertions | planned |
| DP-CMD-01 | P5.1–P5.3 usercmd preservation | Same input trace through the migration; compare against the Win32 baseline AND the #127/A05 commercial reference fixtures | Win, Linux, macOS | Trace diff + #127 fixtures | blocked on #127/#122 |
| DP-DEV-01 | P6.1 absent display/input device | Start with no display or input device, or with a forced display/input init failure; assert bounded diagnostic and fallback/clean exit. Absent/failed audio devices are A10 ([#132](https://github.com/jm2/kisakcod/issues/132)), not this row. | Win, Linux, macOS | Harness output + exit code | planned |
| DP-DEV-02 | P6.2 cleanup/restart | Fail init midway, clean up, restart; assert idempotent cleanup and no leaked global state | Win, Linux, macOS | Harness output | planned |
| DP-DEV-03 | P6.3 clean-machine startup | Fresh image, no config, read-only data dir, malformed config; assert actionable diagnostics. Run and record independently per role (client and headless dedicated server): a client run does not certify the server role, and headless evidence covers the diagnostics that role actually produces | Win, Linux, macOS | Image run log (per role) | planned |
| DP-REQ-01 | P7.1 minimum requirements | Publish §5 and validate each floor on the minimum configuration (or record a measured reason) for **every** row, independently per role and per architecture — explicitly including Linux arm64 client/server, the separate Windows x86 and Windows amd64 client rows (each with its own architecture-specific minimum-configuration evidence; PORTING.md M6 tracks amd64 client delivery independently; acceptance requires the native Vulkan 1.1 graphics endpoint, and any D3D9 migration/reference evidence is recorded separately under its own label and cannot satisfy those rows' graphics floor — PORTING.md M8 keeps D3D9/dxvk-native as migration experiments/reference paths, not replacement endpoints), and every Windows server row (x86 dedicated, amd64 headless, ARM64 headless); a client result never certifies the server role of the same OS (PORTING.md M6/M11 track the roles separately). The Windows ARM64 client and Windows ARM64 headless server rows are **blocked** — no validated ARM64 OS floor, and the client's committed Vulkan endpoint is additionally unvalidated on ARM64 — and this row MUST NOT pass while either stays blocked | All targets | Requirements doc + measured evidence | planned |

## 7. Retail usercmd invariants that must not change

The following are the concrete behaviors to pin in DP-IN-03 and DP-CMD-01.
They are read from the current `src/client_mp/cl_input.cpp` path and are *not*
authorized to change in the platform migration:

- Mouse look applies, in order: acceleration (`cl_mouseAccel` × per-frame rate +
  `sensitivity`), the cgame FOV sensitivity scale, then `m_yaw` (yaw) and
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
- Audio device behavior including absent/failed devices, permission changes,
  suspend/resume lifecycle, and voice capture/playback: A10
  ([#132](https://github.com/jm2/kisakcod/issues/132)). A13 supplies the shared
  platform suspend/resume seam A10 depends on but does not test the device.
- Renderer feature parity, shader conversion: A09/M8.
- Retail-content/mod matrix and packaging: A11/A15.
- Single-player save/load and its path layout: deferred with SP.
- Wine/CrossOver: optional deployment workaround, not acceptance.

## 9. Dependencies, blockers and evidence rules

- **Commercial compatibility** is required by #122 and cannot be substituted by
  fork-to-fork or CoD4x tests. DP-CMD-01 stays blocked until #127 supplies
  reference fixtures and a licensed reference session is available.
- **Native composition**: DP-FS-01, DP-FS-07 and all DP-IN/DP-WIN rows require
  a native Linux/macOS client composition (A08/A09 and the client platform
  exits) that does not exist at the recorded SHA. They are planned against the
  eventual seam, not retrofitted to Win32.
- **Audio lifecycle**: absent/failed audio devices, permission changes and
  audio suspend/resume are A10 ([#132](https://github.com/jm2/kisakcod/issues/132)).
  A13 owns the shared platform suspend/resume seam A10 attaches to, but neither
  re-scopes the other: DP-WIN-04 and DP-DEV-01 stop at the display/input
  boundary and do not certify an audio device.
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
- Recorded reason for the DP-FS-07 addition and the P1.5/P2.2b/P4.4/§5 row
  changes: final-head review of PR #148 found that the `fs_homepath` override
  did not cover `Com_NewPlayerProfile`/`Com_DeletePlayerProfile`, that DP-FS-06
  could pass with an in-root symlink/reparse escape, that DP-FS-01 did not test
  the separate P1.2 per-role roots, that the §5 table omitted Linux arm64, and
  that P4.4 conflated OS power resume with raw thread suspend/resume.
- Recorded reason for adding the macOS cache root to P1.2 and DP-FS-01:
  final-head review of PR #148 ([discussion_r4034644017](https://github.com/jm2/kisakcod/pull/148#discussion_r4034644017))
  found that P1.1 includes cache but P1.2 and the DP-FS-01 macOS clause listed
  only Application Support config/state and Library/Logs, so the role-specific
  acceptance gate never verified macOS caches despite requiring exact
  role-specific locations and excluding writes into the install tree. The
  chosen convention is `~/Library/Caches`; documentation-only, no runtime
  behavior change.
- Recorded reason for defining the Windows unset-variable contract in P1.2 and
  DP-FS-01: final-head review of PR #148
  ([discussion_r4038197364](https://github.com/jm2/kisakcod/pull/148#discussion_r4038197364))
  found that P1.2 named `%APPDATA%`/`%LOCALAPPDATA%` without unset or failure
  semantics, so DP-FS-01's required unset-variable assertions had no defined
  Windows expected result. The contract resolves an unset or empty variable to
  the profile location it normally names (`%USERPROFILE%\AppData\Roaming` for
  config, `%USERPROFILE%\AppData\Local` for cache/logs, preserving the
  per-role split) and requires fail-closed diagnostics with no install-tree
  fallback when `%USERPROFILE%` is also unavailable; documentation-only, no
  runtime behavior change. Superseded by the Known Folder contract below.
- Recorded reason for the Windows Known Folder fallback (P1.2, DP-FS-01), the
  explicit Windows ARM64 block (§5, DP-REQ-01) and the Win32 component-count
  negative (DP-FS-03): final-head review of PR #148
  ([discussion_r4038665231](https://github.com/jm2/kisakcod/pull/148#discussion_r4038665231),
  [discussion_r4038665244](https://github.com/jm2/kisakcod/pull/148#discussion_r4038665244),
  [discussion_r4038665250](https://github.com/jm2/kisakcod/pull/148#discussion_r4038665250))
  found that re-deriving `%USERPROFILE%\AppData` writes to the default layout
  instead of a policy-redirected known folder and rejects sessions the Known
  Folder API could still resolve; that the Windows ARM64 `TBD` row gave
  DP-REQ-01 no measurable floor to validate; and that DP-FS-03's Win32
  negatives omitted the component-count guard. P1.2/DP-FS-01 now resolve an
  unset or empty variable through `SHGetKnownFolderPath`
  (`FOLDERID_RoamingAppData`/`FOLDERID_LocalAppData`) and fail closed only
  when that lookup fails; the Windows ARM64 row is explicitly blocked until a
  concrete floor is published and validated; DP-FS-03 adds a distinct
  >256-components Win32 negative. Documentation-only, no runtime behavior
  change.
- Recorded reason for adding the Windows server rows to §5 and the per-role
  DP-REQ-01 gate: final-head review of PR #148
  ([discussion_r4038991132](https://github.com/jm2/kisakcod/pull/148#discussion_r4038991132))
  found that §5 held Windows client rows only while claiming every shipped
  target represented, although PORTING.md M6/M11 require Windows amd64/ARM64
  server delivery and the existing Windows x86 dedicated server also needs its
  role floor. §5 now carries Windows x86 dedicated, Windows amd64 headless and
  Windows ARM64 headless server rows — explicitly blocked where no floor
  evidence exists — and DP-REQ-01 validates each role independently.
  Documentation-only, no runtime behavior change.
- Recorded reason for extending DP-FS-07 and P1.5 to the full profile
  persistence cycle: final-head review of PR #148
  ([discussion_r4039366892](https://github.com/jm2/kisakcod/pull/148#discussion_r4039366892))
  found that DP-FS-07 covered only create/delete, while the source splits
  profile persistence across roots: `Com_ChangePlayerProfile` writes
  `profiles/active.txt` under `fs_homepath`
  (`FS_FOpenFileWriteToDirForThread`, `com_files.cpp` ~3170),
  `Com_SetInitialPlayerProfile` re-reads it through the search paths
  (`com_playerprofile.cpp` ~145), and `FS_Startup` enrolls `players` only
  under `fs_basepath` (`com_files.cpp` ~2232), so with separated roots the
  saved selection and profile config are invisible after restart until the
  writable root's `players` directory is enrolled in the search paths.
  DP-FS-07 now requires save, restart and reload of the selected
  profile/config/stats from the writable root with separated read-only
  retail roots, and a missing home `players` search enrollment fails the
  row. Documentation-only, no runtime behavior change.
- Recorded reason for splitting the Windows client row in §5: final-head
  review of PR #148
  ([discussion_r4039366906](https://github.com/jm2/kisakcod/pull/148#discussion_r4039366906))
  found that the combined `Windows x86/amd64 client` row let one
  architecture's minimum-configuration evidence stand in for both, while
  PORTING.md M6 requires amd64 client delivery in its own right. §5 now
  carries separate Windows x86 client and Windows amd64 client rows, and
  DP-REQ-01 requires architecture-specific minimum-configuration evidence
  for each. Documentation-only, no runtime behavior change.
- Recorded reason for the DP-FS-07 ordered profile lifecycle, the per-role
  client/headless evidence rules (§2, P1.1, P1.5, DP-FS-01, DP-DEV-03) and
  the XDG empty/relative-value contract (P1.2, DP-FS-01): fresh requested
  review of PR #148 at head `b1dec7f3`
  ([discussion_r4040801246](https://github.com/jm2/kisakcod/pull/148#discussion_r4040801246),
  [discussion_r4040801252](https://github.com/jm2/kisakcod/pull/148#discussion_r4040801252),
  [discussion_r4040801263](https://github.com/jm2/kisakcod/pull/148#discussion_r4040801263))
  found that DP-FS-07's step (a) created *and deleted* the profile before
  steps (b)/(c) selected, saved and reloaded that same profile, so the row
  was not runnable as written; that §2 declared headless-server
  filesystem/diagnostic coverage while P1.1/DP-FS-01 mandated and evidenced
  client behavior only; and that P1.2/DP-FS-01 defined neither empty nor
  relative XDG values, which the
  [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir/latest/)
  resolves as unset (empty) and invalid-ignored (relative), requiring
  absolute paths. DP-FS-07 now runs one ordered
  create/select/save/restart/reload/delete lifecycle (deletion last, or
  against an explicitly separate deletion profile) preserving every
  separated-root, config/stats and missing-home-`players`-enrollment
  assertion; shared-surface requirements are evidenced independently per
  role with client results never certifying dedicated-server paths and
  profile/UI-only tests scoped to client roles; and XDG variables are
  defined and tested for unset, empty, relative and valid-absolute cases
  with per-role fallback locations, no CWD-relative acceptance, no
  install-tree writes and fail-closed diagnostics when no usable root
  resolves. Documentation-only, no runtime behavior change.
- Recorded reason for the Windows relative-value contract (P1.2, DP-FS-01):
  final-head review of PR #148
  ([discussion_r4041065049](https://github.com/jm2/kisakcod/pull/148#discussion_r4041065049))
  found that P1.2 defined the Windows `%APPDATA%`/`%LOCALAPPDATA%` fallback
  only for unset or empty values while DP-FS-01 required every environment
  variable to be exercised with a relative value, leaving the Windows
  relative case without an expected result. A relative value is now treated
  as an unusable root value and ignored in favor of `SHGetKnownFolderPath`
  resolution, preserving Known Folder redirection and the per-role
  roaming/local split, and is never resolved against the current working
  directory; the same actionable fail-closed diagnostic applies when the
  Known Folder lookup also fails. This mirrors the XDG relative-value rule
  already defined for Linux; accepting a relative Windows value was rejected
  because a CWD-dependent writable root would make config/cache/log
  locations depend on the launch directory and scatter per-user artifacts.
  Documentation-only, no runtime behavior change.
- The SDL migration must land behind the seam described here; do not reclassify
  an unimplemented window/input/filesystem behavior as "done" because a
  primitive compiles or a portable helper test passes.
- When the first native client is runnable, promote each `planned` row to a
  concrete harness and record results here or in its tracking bead.

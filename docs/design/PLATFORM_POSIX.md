# POSIX headless platform (WS-4)

Gate: G1 (Linux compile and link), then G2. KPIs: K1 and K2 ([NATIVE64.md](NATIVE64.md)), and K5 below. Scope: the headless dedicated server on Linux amd64, Linux arm64 and macOS arm64. Client platform work is in [CLIENT.md](CLIENT.md).

## Source sets

| Set | TUs | Notes |
| --- | --- | --- |
| Win32 headless (`kisakcod_get_dedi_sources`) | 243 (224 C++, 19 C) | Built by the Windows x86 headless CI job |
| Linux headless | 236 | Win32 set − 7 `src/win32` − 9 `src/_platform/win32` + 9 `src/_platform/posix` |
| Linux/macOS engine sets | headless | `PLATFORM_{LINUX,MACOS}_DEDI_HEADLESS` add the `posix_*` entry, console and localization; only `KISAK_DEDI_HEADLESS` configures off Win32. CI `linux-headless` builds and smoke-runs it on amd64 and arm64 |

## Service map

Engine callers are outside `src/_platform`. A service is done only when a real target calls it (AGENTS.md rule 2); no POSIX target exists yet.

| Service | Win32 | POSIX | Engine callers | Status |
| --- | --- | --- | --- | --- |
| `Sys_Milliseconds`, `Sys_Sleep` | QPC | `clock_gettime` | ~60 files | called |
| `sys_sync` locks | critical sections | pthread | ~40 files | called |
| `sys_event` | Win32 events | condvar | `threads.cpp`, `sys_worker_gate.cpp` | called |
| `sys_thread` | `CreateThread` | pthread | `threads.cpp` | called; `Sys_ThreadJoinTimeout` unused |
| `sys_memory` | `VirtualAlloc` | `mmap` | `com_memory.cpp`, `physicalmemory.cpp` | called |
| `sys_filesystem` | Win32 | `opendir`, `/proc/self/exe`, `_NSGetExecutablePath` | `com_files.cpp`, `win_common.cpp` | called; `Sys_FileSystemReadFile` unwired |
| `Sys_Console*` | `win_syscon.cpp` | stdio service | `win_main.cpp`, `win_syscon.cpp` only | no POSIX caller |
| Stream sockets, `Sys_SocketResolveHost` | Winsock | BSD | HTTP download (`dl_main*.cpp`), `net_chan_mp.cpp` | called |
| UDP sockets (`Sys_SocketOpenUdp`, `SendTo`, `RecvFrom`, broadcast) | Winsock | BSD | `qcommon/net_local.cpp` (every headless build; the MP client keeps `win_net.cpp`) | called |
| `Sys_Process*` launch/wait/park | `CreateProcess` | `posix_spawnp` | none | unwired |
| `Sys_ProcessFreezeForCrash` | — | Mach backend (`sys_mach_crash.cpp`); Linux returns Unsupported | none | unwired |
| `src/database/shader_cache.*` | — | — | none (yet in the headless set) | unwired; client item |

## What the headless server still needs

| Item | Replaces | NOW bead |
| --- | --- | --- |
| Relaunch via `Sys_ProcessLaunch` | `Sys_QuitAndStartProcess`, `Sys_Spawn` | later |
| Portable async fast-file reads | `db_file_load.cpp` | 14 |

## Compile blockers

- **`win32/win_local.h`** is the de-facto system header. 22 headless TUs include it, and it is the first non-assert error in 20 of the 39 Linux "other" failures. **Split:** a portable `qcommon/sys_local.h` takes `sysEvent_t`, `SysInfo`, `Sys_GetPacket`, `Sys_IsLANAddress*` and `Conbuf_*`. `win_local.h` keeps `WinVars_t`, `MainWndProc`, the `IN_*` DirectInput calls, `HWND`/`HMODULE` and the winsock includes, and only `src/win32` includes it.
- **D3D include cut.** `xanim.h` now holds the gfx types by pointer (forward declarations) instead of including `gfx_d3d/r_bsp.h`, `r_gfx.h`, `r_material.h` and `r_font.h`, and `gfx_d3d/r_d3d9types.h` stands in for `<d3d9.h>` off Windows: opaque COM interfaces plus the fixed `_D3DFORMAT`/`_D3DCUBEMAP_FACES` values. `tests/headless_include_debt.allow` lists 29 direct includes (the asset and collision loaders that need complete renderer records) and cannot see transitive reach; K5 does.
- **Miles cut.** Off Windows, `sound/snd_msstypes.h` stands in for `msslib/mss.h` (opaque `_SAMPLE`, `_DIG_DRIVER`, `_STREAM`), so `snd_public.h` and `snd_local.h` no longer reach the Miles SDK, whose `mss.h` has no 64-bit Mac case. `deps/ode/common.h` takes `alloca` from `<alloca.h>` on macOS, which has no `<malloc.h>`.
- **Win32 APIs in shared files:** overlapped I/O (`db_file_load.cpp`); clipboard and `MessageBoxA` (`assertive.cpp`); `HWND`/`GetActiveWindow` (`com_playerprofile.cpp`); unguarded `<Windows.h>` (`profile.cpp`, `timing.cpp`); `<io.h>` (`com_files.cpp`); `win32/win_net.h` included by `db_registry.cpp` and `sv_init_mp.cpp`.
- **MSVC CRT names:** `ARRAYSIZE` (~11 error sites), `_strlwr`, `_isnan`, `_time64`, `_TRUNCATE`, and `basename` in `qcommon/files.cpp`, which clashes with glibc. One compat header next to `universal/msvc_printf_shim.h` (NOW bead 4).
- **Two-phase lookup:** the `KeywordHashEntry` template in `ui/ui_shared.h` called undeclared `IsValidSeed`; `-fdelayed-template-parsing` hides it, and real builds don't use that flag. The seed-collision check now lives in `ui_shared.h` as the free template `IsValidSeed` (moved out of the per-instantiation copies in `ui_shared_obj.cpp`, whose wrappers delegate to it); the half-transcribed method stubs that reached for the undeclared name are gone.
- **Byte-order helpers:** `BigShort` was declared for every target but defined only inside `q_shared.h`'s `WIN32` block, so every POSIX caller compiled and then failed to link (#231). The `Big*`/`Little*` forms are now one `constexpr` set defined for every target and keyed on `KISAK_LITTLE_ENDIAN` in `kisak_abi.h`.
- **`va_list` misuse:** 2 sites in `q_parse.cpp` and 3 in `com_playerprofile.cpp`.
- **arm64 only (fixed):** the `__rdtsc` sites in `scr_vm.cpp`, `sv_main_mp.cpp`, `common.cpp`, `profile.cpp`, `timing.cpp` and `com_profilemapload.cpp` read `Sys_CycleCounter` (`qcommon/sys_time.h`): the TSC on x86 (MSVC keeps `__rdtsc()`), `CNTVCT_EL0` on AArch64.

## Portable async fast-file reads

`db_file_load.cpp` reads zones with `OVERLAPPED`, `ReadFileEx` and an alertable `SleepEx`. The plan is a small `sys_file` async-read service (open, read-at-offset, wait, close). Win32 keeps overlapped I/O; POSIX uses `pread` on the database thread (a helper thread only if profiling asks). Engine code stops seeing `HANDLE` and completion routines.

## Toolchain policy

- **Compiler:** clang ≥ 18 with `-fms-extensions` on every non-MSVC target, and Apple clang on macOS.
- **GCC:** out until `__int32` (262 uses) and non-trivial `__declspec` (36 uses) are gone.
- **Real builds:** they don't use `-fdelayed-template-parsing`, which is for the census only.
- **Warnings:** pointer-cast warnings follow [NATIVE64.md](NATIVE64.md).

## macOS notes

- **Services:** the same POSIX files, plus the Mach crash-freeze backend (no engine caller).
- **Scope before G5:** headless only (G3); a `mac64` census leg and the #265 fixes come first ([NATIVE64](NATIVE64.md#kpis)). CrossOver runs the x86 build for testing only ([ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)).
- **Release:** signing and notarization are G6 owner items ([../NOW.md](../NOW.md)).

## Steamworks availability

- **What exists:** SDK 1.52+ ships a universal macOS library that includes arm64. SDK 1.63+ ships linuxarm64. There is no Windows ARM64 library. The vendored `deps/steamsdk` holds only the Win32 `steam_api` library.
- **Correction:** `CMakeLists.txt` defaults `KISAK_ENABLE_STEAM` off on every ARM processor, and fails configure if it's set, claiming no aarch64 library exists. That is true only for Windows ARM64, so key the rule to Windows ARM64.
- **Headless:** builds Steam-off either way. Auth policy is in [NET_STEAM18.md](NET_STEAM18.md).

## K5: D3D reach

**K5** is the number of Linux headless TUs that need the census `d3d9.h` stand-in to compile, meaning they reach `<d3d9.h>`.

- **Source:** the `native64-census` job.
- **Baseline (census, 2026-09-22):** 103 of 236.
- **Target:** 0, required for G1 on Linux.
- **Moves it:** NOW bead 2.

See also [../CHARTER.md](../CHARTER.md), [../ROADMAP.md](../ROADMAP.md), [FASTFILE_LOADER.md](FASTFILE_LOADER.md).

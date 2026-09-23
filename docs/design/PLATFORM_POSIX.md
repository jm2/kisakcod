# POSIX headless platform (WS-4)

Gate: G1 (Linux compile and link), then G2. KPIs: K1 and K2 ([NATIVE64.md](NATIVE64.md)), and K5 below. Scope: the headless dedicated server on Linux amd64, Linux arm64 and macOS arm64. Client platform work is in [CLIENT.md](CLIENT.md).

## Source sets

| Set | TUs | Notes |
| --- | --- | --- |
| Win32 headless (`kisakcod_get_dedi_sources`) | 243 (224 C++, 19 C) | Built by the Windows x86 headless CI job |
| Linux headless | 236 | Win32 set − 7 `src/win32` − 9 `src/_platform/win32` + 9 `src/_platform/posix` |
| Linux/macOS engine sets | 0 | `scripts/platform/{linux,macos}/platform.cmake` leave them empty; `CMakeLists.txt` refuses non-win32 engine targets |

## Service map

Engine callers are outside `src/_platform`. A service is done only when a real target calls it (AGENTS.md rule 2); no POSIX target exists yet.

| Service | Win32 | POSIX | Engine callers | Status |
| --- | --- | --- | --- | --- |
| `Sys_Milliseconds`, `Sys_Sleep` | QPC | `clock_gettime` | ~60 files | called |
| `sys_sync` locks | critical sections | pthread | ~40 files | called |
| `sys_event` | Win32 events | condvar | `threads.cpp`, `sys_worker_gate.cpp` | called |
| `sys_thread` | `CreateThread` | pthread | `threads.cpp` | called; `Sys_ThreadJoinTimeout` unused |
| `sys_memory` | `VirtualAlloc` | `mmap` | `com_memory.cpp`, `physicalmemory.cpp` | called |
| `sys_filesystem` | Win32 | `opendir`, `/proc/self/exe`, `_NSGetExecutablePath` | `com_files.cpp`, `win_common.cpp` | called; `Sys_FileSystemRemoveTree`, `Sys_FileSystemReadFile` unwired |
| `Sys_Console*` | `win_syscon.cpp` | stdio service | `win_main.cpp`, `win_syscon.cpp` only | no POSIX caller |
| Stream sockets, `Sys_SocketResolveHost` | Winsock | BSD | HTTP download (`dl_main*.cpp`), `net_chan_mp.cpp` | called |
| UDP sockets (`Sys_SocketOpenUdp`, `SendTo`, `RecvFrom`, broadcast) | Winsock | BSD | none: `win_net.cpp` uses raw Winsock | unwired |
| `Sys_Process*` launch/wait/park | `CreateProcess` | `posix_spawnp` | none | unwired |
| `Sys_ProcessFreezeForCrash` | — | Mach backend (`sys_mach_crash.cpp`); Linux returns Unsupported | none | unwired |
| `src/database/shader_cache.*` | — | — | none (yet in the headless set) | unwired; client item |

## What the headless server still needs

| Item | Replaces | NOW bead |
| --- | --- | --- |
| POSIX `main`, frame loop, `Sys_Init`/`Sys_Quit`/`Sys_Print`, event queue | `win_main.cpp` | 13 |
| termios console on `Sys_Console*` | `win_syscon.cpp` | 13 |
| `NET_*`/`Sys_SendPacket`/`Sys_GetPacket` on the `Sys_Socket` UDP API, one file for all platforms | `win_net.cpp` | 13 |
| Remote debug socket: stub for headless | `win_net_debug.cpp` | 13 |
| Language selection, CPU detection, Steam-off stub | `win_localize.cpp`, `win_configure.cpp`, `win_steam.cpp` | 13 |
| `Sys_RemoveDirTree` on `Sys_FileSystemRemoveTree` (returns false on POSIX today) | POSIX branch of `win_common.cpp` | 13 |
| Relaunch via `Sys_ProcessLaunch` | `Sys_QuitAndStartProcess`, `Sys_Spawn` | later |
| Portable async fast-file reads | `db_file_load.cpp` | 14 |

`universal/win_common.cpp` already compiles on Linux; it needs only the remove-tree wiring.

## Compile blockers

- **`win32/win_local.h`** is the de-facto system header. 22 headless TUs include it, and it is the first non-assert error in 20 of the 39 Linux "other" failures. **Split:** a portable `qcommon/sys_local.h` takes `sysEvent_t`, `SysInfo`, `Sys_GetPacket`, `Sys_IsLANAddress*` and `Conbuf_*`. `win_local.h` keeps `WinVars_t`, `MainWndProc`, the `IN_*` DirectInput calls, `HWND`/`HMODULE` and the winsock includes, and only `src/win32` includes it.
- **D3D include cut.** `xanim.h` includes `gfx_d3d/r_bsp.h`, `r_gfx.h`, `r_material.h` and `r_font.h`; `r_gfx.h` and `r_material.h` include `<d3d9.h>`. Nearly all headless `d3d9.h` reach runs through `xanim.h`. The fix is to forward-declare the gfx types `xanim.h` holds by pointer. `tests/headless_include_debt.allow` lists 21 direct includes and cannot see transitive reach; K5 does.
- **Win32 APIs in shared files:** overlapped I/O (`db_file_load.cpp`); clipboard and `MessageBoxA` (`assertive.cpp`); `HWND`/`GetActiveWindow` (`com_playerprofile.cpp`); unguarded `<Windows.h>` (`profile.cpp`, `timing.cpp`); `<io.h>` (`com_files.cpp`); `win32/win_net.h` included by `db_registry.cpp` and `sv_init_mp.cpp`.
- **MSVC CRT names:** `ARRAYSIZE` (~11 error sites), `_strlwr`, `_isnan`, `_time64`, `_TRUNCATE`, and `basename` in `qcommon/files.cpp`, which clashes with glibc. One compat header next to `universal/msvc_printf_shim.h` (NOW bead 4).
- **Two-phase lookup:** the unused `KeywordHashEntry` template in `ui/ui_shared.h` calls undeclared `IsValidSeed`. `-fdelayed-template-parsing` hides it. Delete the template.
- **`va_list` misuse:** 2 sites in `q_parse.cpp` and 3 in `com_playerprofile.cpp`.
- **arm64 only:** `__rdtsc`, with 8 sites in `scr_vm.cpp` and 2 in `sv_main_mp.cpp`. `common.cpp` (the `Netchan_Init` seed), `profile.cpp` and `timing.cpp` also use it, but fail earlier today. Route all of them through one `Sys_CycleCounter`.

## Portable async fast-file reads

`db_file_load.cpp` reads zones with `OVERLAPPED`, `ReadFileEx` and an alertable `SleepEx`. The plan is a small `sys_file` async-read service (open, read-at-offset, wait, close). Win32 keeps overlapped I/O; POSIX uses `pread` on the database thread (a helper thread only if profiling asks). Engine code stops seeing `HANDLE` and completion routines.

## Toolchain policy

- **Compiler:** clang ≥ 18 with `-fms-extensions` on every non-MSVC target, and Apple clang on macOS.
- **GCC:** out until `__int32` (262 uses) and non-trivial `__declspec` (36 uses) are gone.
- **Real builds:** they don't use `-fdelayed-template-parsing`, which is for the census only.
- **Warnings:** pointer-cast warnings follow [NATIVE64.md](NATIVE64.md).

## macOS notes

- **Services:** the same POSIX files, plus the Mach crash-freeze backend (no engine caller).
- **Scope before G5:** headless only (G3). CrossOver runs the x86 build for testing only ([ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)).
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

# POSIX headless platform (WS-4)

Gate: G1 (Linux compile and link), then G2. KPIs: K1 and K2 ([NATIVE64.md](NATIVE64.md)), and K5 below. Scope: the headless dedicated server on Linux amd64, Linux arm64 and macOS arm64. Client platform work is in [CLIENT.md](CLIENT.md).

## Source sets

| Set | TUs | Notes |
| --- | --- | --- |
| Win32 headless (`kisakcod_get_dedi_sources`) | 252 (234 C++, 18 C) | Built by the Windows x86 headless CI job |
| Linux headless | 249 | Win32 set − 6 `src/win32` − 10 `src/_platform/win32` + 13 `src/_platform/posix` |
| Linux/macOS engine sets | headless | `PLATFORM_{LINUX,MACOS}_DEDI_HEADLESS` add the `posix_*` entry, console and localization; only `KISAK_DEDI_HEADLESS` configures off Win32. CI `linux-headless` builds and smoke-runs it on amd64 and arm64, `macos-headless` on macOS arm64 |

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
| Quit requests (`qcommon/sys_quit.h`) | `SetConsoleCtrlHandler` (`Sys_ConsoleInstallQuitHandler`) | SIGINT, SIGTERM (`posix_syscon.cpp`) | headless frame loop (`Cbuf_AddRequestedQuit`) | called |
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

## Header rules (headless set)

G1's compile blockers are fixed; these rules keep them fixed.

- **System headers:** only `src/win32` includes `win32/win_local.h`. Portable declarations (`sysEvent_t`, `SysInfo`, `Sys_GetPacket`, `Sys_IsLANAddress*`, `Conbuf_*`) live in `qcommon/sys_local.h`.
- **Stand-ins off Windows:** `gfx_d3d/r_d3d9types.h` for `<d3d9.h>`, `sound/snd_msstypes.h` for `msslib/mss.h`, and `<alloca.h>` for `<malloc.h>` in `deps/ode` on macOS. `xanim.h` holds gfx types by pointer. `tests/headless_include_debt.allow` lists the direct client/media includes; K5 measures transitive reach.
- **Compat:** MSVC CRT names come from `universal/msvc_crt_compat.h`, byte order from `KISAK_LITTLE_ENDIAN` (`universal/kisak_abi.h`), and the cycle counter from `Sys_CycleCounter` (`qcommon/sys_time.h`). Shared files call Win32 APIs only inside `#if defined(_WIN32)` arms with a portable arm, or through a platform service.
- **Templates:** names a template uses must be declared before it, because real builds don't use `-fdelayed-template-parsing`.

## Portable async fast-file reads

`db_file_load.cpp` reads zones with `OVERLAPPED`, `ReadFileEx` and an alertable `SleepEx`. The plan is a small `sys_file` async-read service (open, read-at-offset, wait, close). Win32 keeps overlapped I/O; POSIX uses `pread` on the database thread (a helper thread only if profiling asks). Engine code stops seeing `HANDLE` and completion routines.

## Toolchain policy

- **Compiler:** clang ≥ 18 with `-fms-extensions` on every non-MSVC target, and Apple clang on macOS.
- **GCC:** out until `__int32` (262 uses) and non-trivial `__declspec` (36 uses) are gone.
- **Real builds and the census:** neither uses `-fdelayed-template-parsing`, so two-phase lookup errors surface.
- **Warnings:** pointer-cast warnings follow [NATIVE64.md](NATIVE64.md).

## macOS notes

- **Services:** the same POSIX files, plus the Mach crash-freeze backend (no engine caller).
- **Scope before G5:** headless only (G3). The target is macOS 27 with Xcode 27 (Apple clang 21). CI builds and smoke-runs it on GA `macos-26`, and the census covers SDK 27 on `xcode-27`. CrossOver runs the x86 build for testing only ([ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)).
- **Symlinks:** `FS_Startup` trusts the operator's roots (`Sys_FileSystemTrustRoot`): links above a root resolve once, with planted links in shared directories refused; below it every component opens with `O_NOFOLLOW`.
- **Release:** `release.yml` ships a `.tar.xz` and a dSYM, ad-hoc signed, not notarized ([ADR-0007](../decisions/0007-macos-notarization-deferred.md)); deployment target 13.0.

## Steamworks availability

- **What exists:** SDK 1.52+ ships a universal macOS library that includes arm64. SDK 1.63+ ships linuxarm64. There is no Windows ARM64 library. The vendored `deps/steamsdk` holds only the Win32 `steam_api` library.
- **Correction:** `CMakeLists.txt` defaults `KISAK_ENABLE_STEAM` off on every ARM processor, and fails configure if it's set, claiming no aarch64 library exists. That is true only for Windows ARM64, so key the rule to Windows ARM64.
- **Headless:** builds Steam-off either way. Auth policy is in [NET_STEAM18.md](NET_STEAM18.md).

## K5: D3D reach

**K5** is the number of Linux headless TUs that need the census `d3d9.h` stand-in to compile, meaning they reach `<d3d9.h>`.

- **Source:** the `native64-census` job.
- **Baseline (census, 2026-10-01):** 0 of 249 (met).
- **Target:** 0, required for G1 on Linux.
- **Moves it:** NOW bead 2.

See also [../CHARTER.md](../CHARTER.md), [../ROADMAP.md](../ROADMAP.md), [FASTFILE_LOADER.md](FASTFILE_LOADER.md).

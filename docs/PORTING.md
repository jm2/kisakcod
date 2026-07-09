# KisakCOD Porting Plan — Win64, Linux, macOS

**Status:** implementation in progress · **Scope:** MP client + headless dedicated server
**Basis:** whole-tree analysis of the current `master` (see `docs/CODEBASE_AUDIT.md` for the health report).

---

## Implementation status (July 8, 2026)

Target policy is fixed: preserve retail assets and wire interoperability; use a
shared Vulkan RHI (MoltenVK on macOS), OpenAL Soft, and FFmpeg; publish portable
packages for Linux; and require native CI plus licensed gameplay smoke tests.
Single-player is outside this port.

Completed foundation work:

- bounded Huffman input/output decoding and rejection at both network call sites;
- pointer-width-safe Huffman tree construction with a native Linux regression test;
- a fixed-width `disk32::PointerToken` decoder with block/span validation, used
  by the current fast-file offset fixup path and covered by portable tests;
- pointer-width-safe hunk allocator alignment/accounting, temporary allocation
  return types, parse-tree alignment, and client/server skeleton alignment;
- an experimental `KISAK_DEDI_HEADLESS` CMake source profile that excludes
  client/cgame/UI/D3D/audio/cinema/proprietary media groups, plus a CTest guard
  that prevents those paths from re-entering the headless source list;
- download-block and stats-packet runtime bounds checks;
- host platform detection, target build switches, and an explicit 64-bit ABI gate;
- platform source override plumbing for Windows, Linux, and macOS;
- corrected Win32 multi-config DirectX paths and post-build output handling;
- `build-win.ps1`, Windows CI, tagged release archives/checksums, and protected
  licensed dedicated-server smoke infrastructure.

Remaining gates, in implementation order:

1. Burn down the remaining client/render/audio symbol references that prevent
   `KISAK_DEDI_HEADLESS=ON` from becoming the default dedicated-server build.
2. Introduce fixed-width `disk32` fast-file schemas and checked conversion into
   native runtime structures.
3. Widen the script VM value representation and remove pointer-to-32-bit casts.
4. Implement platform services (threads/events, sockets, filesystem, time,
   virtual memory, console) for Windows/POSIX.
5. Introduce the Vulkan RHI, retaining D3D9 temporarily on Windows during parity
   testing; add OpenAL Soft and FFmpeg backends.
6. Add scalar/SSE2/NEON dispatch and remove x86 inline assembly/MMX.
7. Enable and gate Windows amd64/ARM64, Linux amd64/arm64, then macOS arm64
   packaging only after native build, synthetic tests, and licensed gameplay
   smoke tests pass.

| Target | Utility CI | Engine build | Packaging | Gameplay smoke |
|---|---:|---:|---:|---:|
| Windows x86 | yes | yes | zip | protected self-hosted |
| Windows amd64 | yes | ABI work gated | gated | gated |
| Windows ARM64 | yes | ABI/platform work gated | gated | gated |
| Linux amd64 | yes | POSIX/RHI work gated | gated | gated |
| Linux arm64 | yes | ABI/POSIX/RHI work gated | gated | gated |
| macOS arm64 | yes | ABI/POSIX/MoltenVK work gated | gated | gated |

---

## TL;DR

- **Does going 64-bit break network compatibility with real (closed-source) COD4? No.** The wire
  format is bit-level and bitness-independent; no pointer/`size_t`/`long` value is ever serialized,
  no networked struct is blitted wholesale onto the wire, and no checksum runs over struct memory.
  A byte-compatible 64-bit build is achievable. There is exactly **one** wire-affecting defect — the
  Huffman tree builder in `src/qcommon/huffman.cpp` uses 32-bit-only pointer arithmetic — and it is a
  small, localized fix that produces retail-identical codes. (Verified by three independent reviewers,
  all confirming, high confidence.)

- **So the Win64 phase-1 you asked for is viable on the network axis.** The blocker to a *fast* Win64
  port is unrelated to networking: this codebase is a Hex-Rays **decompile that hard-codes the 32-bit
  ABI into its data layout** (~249 `static_assert(sizeof(T)==0x..)`, the GSC script VM's 4-byte
  pointer union, the fast-file asset loader's `(uint32_t*)` pointer fixups, and pointer-truncating
  memory allocators). Win64 is therefore **not a recompile — it is a near-rewrite of memory
  management, the script VM, and the asset pipeline**, and it hits Linux/macOS 64-bit identically.

- **There is also a data-compatibility (not network) catch:** shipped COD4 assets are 32-bit
  fast-files (`.ff` zones). A 64-bit engine cannot read real game data without a load-time
  translation layer. This is the practical reason Win64 is expensive.

- **Committed sequencing** (details below): security/build foundations (Phase 0), a genuinely
  headless server plus the shared 64-bit runtime/asset/VM conversion (Phase 1), native Linux amd64
  and the Vulkan/OpenAL/FFmpeg client stack (Phase 2), Windows/Linux ARM64 (Phase 3), and macOS ARM64
  through MoltenVK (Phase 4).

- **The first confirmed remote memory-corruption paths are fixed in this change**
  (`docs/CODEBASE_AUDIT.md` → Critical). The remaining network parser assertion audit is still open.

---

## 1. The pivotal question: 64-bit vs. network compatibility

You explicitly gated the Win64 phase on *"unless there's a blocker that would break network
compatibility with the existing real closed-source COD4."* The analysis answers this decisively.

### 1.1 Why 64-bit does **not** change the bytes on the wire

The three ways a bitness change *could* alter the wire format all fail here:

| Break condition | Finding |
|---|---|
| (a) A pointer/`size_t`/`long` value is serialized | **Never happens.** `MSG_WriteBits/ReadBits` (`src/qcommon/msg_mp.cpp:100,164`) shift bit-by-bit over an `int`; `MSG_WriteLong` writes exactly 4 bytes via `*(uint32_t*)` (`:302`), short=2, byte=1. Field widths in the `netField` tables are hardcoded constants (e.g. `{NETF(eType),8,0u}`), never `sizeof`-derived. `NetField.offset` is `size_t` but is only used to index host structs; it is computed by `offsetof` (`server_mp.h:27`) so it self-adjusts and is never written to the wire. |
| (b) A whole networked struct is memcpy'd into the message buffer | **Never happens.** grep confirms no `MSG_WriteData(msg,(byte*)&struct,sizeof)` anywhere. The whole-struct memcpys (`msg_mp.cpp:1360`, `:1483`) are host-memory baseline copies between `from`/`to` state, not writes to the wire. Every networked struct is pure POD of int32/uint32/float/enum/arrays with `static_assert`s on size (`entityState`=0xF4, `playerState`=0x2F64, `clientState`=0x64, `usercmd`=0x20) — **zero** pointer/`size_t`/`long`/`double`/`int64` members — so layout is byte-identical under LLP64 (Win64) and LP64 (Linux/macOS). |
| (c) A checksum runs over networked struct memory | **Never happens.** `Com_BlockChecksum*` runs over pak-file/BSP/save byte buffers (`md4.cpp:378`), not over networked structs. `checksumFeed` is an `int` on the wire; command hashing is over command *strings*. Padding is irrelevant. |

Netchan headers are fixed width (`net_chan_mp.cpp:761-768`). Endianness is a non-issue: MSG uses
host-order little-endian stores, matching retail x86; all modern targets (x86-64, Apple Silicon) are
little-endian; the only `BigShort` is the UDP port, which is correct. C `long` never appears in the
wire path.

### 1.2 The one must-fix: the wire Huffman tree builder

The gameplay snapshot/command stream is Huffman-compressed on the wire (server encode
`sv_snapshot_mp.cpp:1326`; client decode `cl_parse_mp.cpp:485`; client encode `cl_input.cpp:282`;
server decode `sv_client_mp.cpp:1583`). The tree is built from the fixed `msg_hData[256]` table, so
the emitted codes are bitness-independent **and match retail COD4 if the tree is built correctly**.
But the builder is written 32-bit-only:

- `src/qcommon/huffman.cpp:115` `nodeCmp` does `*(uint32_t*)(*(uint32_t*)left + 12)` — it truncates a
  `nodetype*` to 32 bits and reads `weight` at byte offset **+12** (valid only when 3 pointers = 12
  bytes; on 64-bit `weight` is at +24 and the pointer is 8 bytes).
- `huffman.cpp:135,145` call `qsort(heap, 256, 4u, nodeCmp)` with element size hardcoded to **4**,
  while `heap` is an array of 8-byte `nodetype*`.

On a naive x64 build this dereferences a truncated pointer (crash) or builds a *different* tree
(every compressed packet differs → total incompatibility with real servers/clients).

**Fix (no wire change):**
```cpp
// nodeCmp:
return ((const nodetype*)left)->weight - ((const nodetype*)right)->weight;
// both qsort calls:
qsort(heap, 256, sizeof(nodetype*), nodeCmp);
```
Add a round-trip test that compresses/decompresses a known vector and byte-matches the 32-bit output.

### 1.3 Orthogonal note: protocol version

The protocol version is pinned to `1` (`sv_client_mp.cpp:613`, dvar default `sv_init_mp.cpp:671`).
This is a compile-time `int`, identical on 32/64-bit, so it does **not** affect the bitness question —
but it does **not** match retail COD4's protocol number. Interop with retail servers/clients is a
pre-existing KisakCOD-vs-retail matter independent of this port; decide separately whether KisakCOD
intends to be wire-compatible with retail or only with itself.

---

## 2. The real cost of 64-bit: a decompiled 32-bit ABI

The dominant obstacle to Win64 (and to *any* 64-bit target — Win64 LLP64 and Linux/macOS LP64 both
have 8-byte pointers) is that the source is a decompile that structurally encodes the 32-bit ABI:

1. **~249 `static_assert(sizeof(T)==0x..)` + ~200 `offsetof` asserts** lock every reconstructed struct
   to its 4-byte-pointer size. Any struct with a pointer member fails to compile the instant pointers
   become 8 bytes — e.g. `Scr_StringNode_s` (`scr_vm.h:173`, asserted `==0x8`→`0x10`),
   `function_stack_t` (`scr_vm.h:183`, `0x14`→`0x20`), `scrVmPub_t` (`0x4328`). *(Huge)*

2. **GSC script VM packs pointers into a 4-byte tagged union.** `VariableUnion`
   (`scr_variable.h:100-132`, asserted `sizeof==0x4`) holds `const float*`, `const char*`,
   `VariableStackBuffer*` alongside `uint32` handles. The entire VM (2048-entry value stack, opcode
   operands, object refs) assumes pointers fit in 32 bits. Subsystem-wide redesign, not a mechanical
   fix. *(Huge)*

3. **Fast-file (`.ff` zone) asset format is 32-bit-pointer-locked.** The loader fixes up embedded
   pointers with `DB_ConvertOffsetToPointer((uint32_t*)&field)` (`db_load.cpp:562-636`) using
   `0xFFFFFFFF` sentinels; render/asset structs are frozen by `static_assert` (e.g. `Material==80`).
   On 64-bit the asserts fail, and even removed, the loader writes only 32 bits of each 64-bit
   pointer. **Shipped COD4 assets are 32-bit zones, so a 64-bit engine cannot read real game data
   without a distinct on-disk-vs-runtime struct split and a load-time widening pass.** *(Huge)*

4. **Pointer-truncating memory management.** The hunk allocator masks addresses with
   `(uint32_t)&s_hunkData[..] & 0xFFFFF000` (`com_memory.cpp:431,435,490,…`, already flagged
   `KISAKTODO: sus int32_t cast`); skeleton-memory (`cl_main.cpp:625`, `sv_game.cpp:558`) and the
   script parse-tree (`scr_parsetree.cpp:332`) do the same. These corrupt core memory at startup on
   64-bit. **Fix: use `uintptr_t` for all pointer↔integer alignment math.** *(Medium)*

5. **~80 pointer→`int32` cast sites** walk arrays via `(int)&arr[i]` (`sentient.cpp:441`,
   `g_utils.cpp:1846`, `cm_world.cpp:1226`, …). Rewrite as typed pointer loops. *(Medium, broad)*

6. **Win64-specific compile breaks:** MMX `__m64` skinning intrinsics (`r_model_skin_sse.cpp`, 141
   intrinsics — MSVC dropped `__m64` on x64; rewrite with SSE2 `__m128i` or route to the scalar
   skinner); inline `__asm` for CPUID (`win_configure.cpp:287`), the stack-walker
   (`assertive.cpp:547`, already `// KISAKX64 // broken`), and `SnapFloatToInt` x87
   (`qcommon.h:1583`, guarded by `_WIN32` which is *also* true on Win64 — change to `_M_IX86`); the
   `SOCKET`-into-`uint32_t` truncation (`win_net.cpp:531,874`); and `SetWindowLongA` truncating the
   64-bit `WndProc` pointer (`win_syscon.cpp:231` → use `SetWindowLongPtrA`/`GWLP_WNDPROC`).

**Bottom line:** items 1–3 are the gating decision. You either keep the target 32-bit, or you commit
to a full re-layout of structs + VM + asset pipeline. This is why "Win64 first" is wire-safe but not
cheap.

---

## 3. Recommended phasing

Two independent axes of work exist, and it's important not to conflate them:

- **Bitness axis** (32→64): dominated by §2. Huge. Blocks Win64 and 64-bit Linux/macOS equally.
- **Cross-platform axis** (Windows→POSIX): dominated by the Win32/DX9/Miles/Bink surface. Large, but
  *achievable while staying 32-bit*, which sidesteps the entire bitness axis.

```
Phase 0  Foundation, security, build/test/release hygiene                    [M]
Phase 1  Headless dedicated server + disk32/runtime64/VM conversion          [XL]
Phase 2  Linux amd64 + shared Vulkan/OpenAL/FFmpeg client stack               [XL]
Phase 3  Windows ARM64 + Linux ARM64, NEON and architecture cleanup           [L]
Phase 4  macOS ARM64 via MoltenVK                                             [L–XL]
```

Wine + DXVK remains a useful deployment workaround for the existing Win32
binary, but it is not a native port deliverable and is not a release gate.

---

## Phase 0 — Foundation & hygiene *(do first; 32-bit-safe)*

None of this changes bitness or the wire; all of it de-risks everything downstream and can land on
`master` today.

1. **Fix the confirmed remote RCEs** (`docs/CODEBASE_AUDIT.md` → Critical/High): Huffman decompress
   output bounds (`sv_client_mp.cpp:1583`, `cl_parse_mp.cpp:485`), `CL_ParseDownload` size check
   (`cl_parse_mp.cpp:404`), `SV_ReceiveStats` bound (`sv_client_mp.cpp:304`), and convert the
   security-relevant no-op asserts to real runtime checks (`assertive.h:26`).
2. **Fix the `set(WIN32 …)` collision** (`common_files.cmake:558`): the source-list variable named
   `WIN32` shadows CMake's built-in boolean, so **every `if(WIN32)` in the build evaluates true on all
   platforms** — the Windows-only link/flag block is currently unconditional. Rename to `WIN32_SRC`.
   This single bug silently defeats any non-Windows CMake logic you add later.
3. **Make the platform layer selectable.** Replace `set(KISAK_PLATFORM win32)` (`CMakeLists.txt:10`)
   with detection from `CMAKE_SYSTEM_NAME`; either flesh out or delete the empty
   `scripts/platform/linux/platform.cmake` stub; remove the bogus `/intentionallbreakthisshit` flag
   and the undefined `MSVC_WARNING_DISABLES` (`platform/win32/platform.cmake:14,18`).
4. **Populate the override tree.** `src/_platform/` doesn't exist even though the override macro
   points at it and only `CLIENT_MP` is routed through `apply_platform_overrides`. Route the
   `WIN32_SRC`, `SOUND`, `GFX_D3D`, `GROUPVOICE` lists through it so per-platform files can be swapped
   in without `#ifdef` soup.
5. **Cross-compiler hygiene macros** (harmless on MSVC, unblock GCC/Clang later): a compat header that
   `#define`s `__cdecl/__stdcall/__thiscall/__fastcall` away on non-MSVC (~703 files use them), a
   `KISAK_ALIGN(n)` macro for the 38 `__declspec(align(n))` structs, and `#ifdef _MSC_VER` around
   `#pragma optimize` (`scr_yacc_structs.h`) and the `NvOptimusEnablement`/`AmdPowerXpress`
   `dllexport`s (`win_main.cpp:863`).
6. **Adopt `build-win.ps1`** (this PR) and bump CI. `build-win.ps1` already targets VS 2026
   (`"Visual Studio 18 2026"`, needs CMake ≥ 4.0), fixes the two latent `.bat` bugs (never built
   `KisakCOD-sp`; omitted `--config`), and fails fast. To move CI to VS 2026 you'll need a runner
   image that ships it (`windows-2022` has VS 2022; use `windows-latest`/`windows-2025` once VS 2026
   images are available) and to bump `mksln.bat`'s generator string to match.

**Deliverable:** same 32-bit Windows binary as today, but secure, with a build system that can grow
non-Windows/non-x86 configs without fighting the `WIN32` collision.

---

## Optional interim deployment — Linux via Wine + DXVK

The rendering analysis is unambiguous: **Wine + DXVK is by far the most realistic near-term Linux
path.** The existing 32-bit x86 exe needs **zero** source changes — DXVK translates D3D9→Vulkan, and
the 32-bit Miles/Bink/Steam DLLs run under Wine unchanged. This sidesteps *every* bitness and
abstraction blocker.

**Work:** package a Wine prefix (or a Proton/Lutris recipe), ship a `winetricks`/DXVK setup script,
document the game-file + DLL copy steps, and smoke-test MP connect + a dedicated server. This is
integration/packaging, not engine work.

**Deliverable:** "KisakCOD on Linux" for players/testers in days. Doubles as the baseline the native
port must match. macOS users can use the same stack via CrossOver (DXVK on MoltenVK) once Phase 1
resolves 64-bit, since modern macOS is 64-bit-only.

---

## Phase 1 — Headless dedicated server and native Win64 runtime

Wire-safe (§1), but gated on the 32-bit-ABI rewrite (§2). Order of operations:

1. **Huffman builder fix** (§1.2) — prerequisite for any 64-bit netcode; ~1 file.
2. **Pointer-truncation sweep** (§2 items 4–5): convert all `(uint32_t)&`/`(int)&` alignment and
   array-walk sites to `uintptr_t`/typed pointers. Mechanical but must be exhaustive — a single missed
   hunk-allocator mask corrupts memory at startup.
3. **The gating decision — struct re-layout (§2 items 1–3):** either
   - **(A) keep on-disk 32-bit, widen at runtime:** define `#pragma pack`ed mirror layouts for every
     networked/asset struct plus a load-time widening pass, so the runtime can be 64-bit while zones
     and the wire stay 32-bit. Preserves retail-asset and wire compatibility. Large, invasive, but the
     only option that reads real game data.
   - **(B) re-bake everything 64-bit:** modify the fast-file linker to emit 64-bit zones and drop the
     size asserts. Cleaner runtime, but abandons retail assets (must re-bake all content) — usually a
     non-starter for a mod-focused project.
   Recommend **(A)**.
4. **Script VM value representation (§2 item 2, detailed in §8):** widen `VariableUnion` to a native
   8-byte union rather than going handle-based. The VM is pure-runtime (never serialized to a `.ff`;
   its savegame path already decomposes values by type — `scr_readwrite.cpp`), only three of its
   members are real pointers, and the ~215 deref sites in `src/script` then compile unchanged — the
   cost is regenerating ~7 `scr_vm.h` size asserts and a 32 KB→64 KB value stack. (Handle/index
   representation is the higher-touch, higher-risk fallback, kept only if the value-stack footprint
   ever becomes a measured problem.)
5. **Win64 compile breaks (§2 item 6):** MMX→SSE2 skinner, inline-asm removal, `_WIN32`→`_M_IX86`
   guards, `SOCKET`/`SetWindowLongPtr` fixes.
6. **Build:** x64 configuration — drop `/machine:x86` (`pre_build.cmake:78`), switch generator
   platform to x64, point DXSDK at `lib/x64` (the June-2010 SDK ships x64 D3DX import libs; `d3d9.lib`
   is in the modern Windows SDK for x64, so **DX9 itself is not a Win64 blocker**), and obtain 64-bit
   deps (see §5): `steam_api64` (free, same SDK), and a plan for Bink/Miles (both 32-bit-only blobs).

**Deliverable:** a native 64-bit Windows exe, wire-compatible with 32-bit KisakCOD, still DX9. This is
XL effort and the highest-risk phase; consider gating it behind whether you actually need native x64
(vs. Wine handling Linux and 32-bit remaining fine on Windows).

---

## Phase 2 — Native Linux *(POSIX/SDL + Vulkan)*

Everything in Phase 1 **plus** the cross-platform axis. Depends on Phase 0's platform-override
plumbing.

- **Entry/window/input:** replace `WinMain` with `main()`; replace the hand-rolled Win32 window class
  + message pump and DirectInput with **SDL2/3** (window, events, relative mouse, clipboard,
  message-box). The `HWND` is the linchpin threading windowing→D3D device→sound→input, so these move
  together.
- **Threading:** `CreateThread`/Events/`Interlocked*` (209 sites) → `pthread`/`std::thread`, a POSIX
  auto/manual-reset event class, and `std::atomic<int32_t>`/`__atomic` (use fixed-width types, not
  `long`). **`SuspendThread`/`ResumeThread` have no POSIX equivalent** — the render/database suspend
  handshake and `Sys_SuspendOtherThreads` must be re-expressed with condition variables.
- **Networking:** Winsock → BSD sockets shim (`SOCKET`→`int`, `closesocket`→`close`,
  `ioctlsocket(FIONBIO)`→`fcntl`, `WSAGetLastError`→`errno`, `gethostbyname`→`getaddrinfo`,
  `FD_SET` instead of poking `fd_set` fields).
- **Filesystem/time/console:** `_findfirst`→`opendir`/`readdir`/`fnmatch`; `GetModuleFileName`→
  `/proc/self/exe`; `timeGetTime`/QPC→`clock_gettime(CLOCK_MONOTONIC)`; `VirtualAlloc`→`mmap`; the
  Win32 GUI system console → termios/ANSI (critical for the dedicated server).
- **Rendering:** either **dxvk-native/vkd3d-proton** (keep the D3D9 renderer, link against a native
  Vulkan translation) — much cheaper — or a from-scratch Vulkan/OpenGL RHI behind a new abstraction
  (331+ direct `IDirect3DDevice9` calls to reroute; effectively a renderer rewrite).
- **Audio:** rewrite `snd_mss.cpp` (54 `AIL_*` calls) against **OpenAL Soft**; gate Bink cinematics
  off or decode via **FFmpeg** (has bink/binkaudio decoders).

**Highest-value first target: the dedicated server (`dedi`).** Headless, it avoids the D3D9 *and*
audio blockers entirely — only entry point, console, threading, timing, filesystem, and sockets need
porting. **Caveat:** today `dedi` still links the full client (D3D9, Miles) — see audit
`scripts/dedi/CMakeLists.txt:40`; making it genuinely headless is a prerequisite and a worthy Phase 0/2
task on its own.

The release target is Linux amd64. A 32-bit Linux build is not part of the
supported matrix.

---

## Phase 3 — Windows ARM64 and Linux ARM64

- Remove remaining x86 inline assembly and pointer-width assumptions.
- Provide scalar reference paths plus SSE2/AVX dispatch on amd64 and NEON on ARM64.
- Build and test all portable dependencies natively for both operating systems.
- Require byte-identical network/asset fixtures and licensed gameplay smoke before packaging.

---

## Phase 4 — macOS ARM64 *(stretch, strictly downstream)*

macOS has no D3D9 and no native Vulkan (only Metal via **MoltenVK**), and modern macOS is
**64-bit-only** — so the 32-bit escape hatch does not exist and the §2 rewrite is mandatory. Two paths:

- **Pragmatic:** run the Win64 build under **CrossOver/Wine with DXVK layered on MoltenVK** (the most
  fragile of the stacks, but no native engine work).
- **Native:** everything in Phase 2 + a Metal (or Vulkan-on-MoltenVK) RHI backend + an arm64/x86_64
  universal build + macOS builds of all prebuilt deps. XL, only worth it if native macOS is a goal in
  itself.

Treat macOS as "after Win64 + Linux land."

---

## 4. Per-subsystem effort map

| Subsystem | Win64 | Native Linux | Native macOS | Notes |
|---|---|---|---|---|
| Netcode / wire format | S (Huffman only) | S | S | Wire is bitness/endian-neutral; see §1 |
| 32-bit ABI (structs/VM/fast-file) | **XL** | **XL** | **XL** | The gating item; avoidable only by staying 32-bit |
| Memory mgmt pointer truncation | M | M | M | `uintptr_t` sweep |
| Rendering (DX9) | S (relink x64) | XL native / S via dxvk | XL / S via MoltenVK | D3D9 works on Win64 as-is |
| Platform layer (win32/) | S | XL | XL | SDL + POSIX; `HWND` couples window/render/audio/input |
| Threading | S | L | L | No POSIX `SuspendThread` |
| Audio (Miles) | L (need x64 lib) | XL (OpenAL rewrite) | XL | 32-bit-only proprietary blob |
| Video (Bink) | L (need x64 lib) | M (stub or FFmpeg) | M | 32-bit-only; `#ifdef CINEMA`, non-essential |
| Steam | S (steam_api64) | S (libsteam_api.so) | S | Free from same SDK |
| Build system | S | M | M | Phase 0 unblocks all |

S=small · M=medium · L=large · XL=extra-large/rewrite.

---

## 5. Dependency matrix

| Dep | Consumed as | Win64 | Linux | macOS | License / redistribution |
|---|---|---|---|---|---|
| **Miles Sound System** (`mss32`) | 32-bit blob, 54 `AIL_*` calls | ❌ no free x64 | ❌ | ❌ | Proprietary RAD/Epic — **remove from public repo**; replace with OpenAL Soft |
| **Bink Video** (`binkw32`) | 32-bit blob, `#ifdef CINEMA` | ❌ | ❌ | ❌ | Proprietary; stub cinematics or decode via FFmpeg (LGPL) |
| **Steamworks** (`steam_api`) | 32-bit blob + headers | ✅ `steam_api64` | ✅ `libsteam_api.so` | ✅ `.dylib` | Redistributable; all in the same SDK, just not committed |
| **ODE physics** | in-tree source | ✅ | ✅ | ✅ | LGPL/BSD (pick BSD); light 64-bit type audit |
| **zlib 1.1.4** | in-tree source | ✅ | ✅ | ✅ | zlib license; **upgrade to 1.3.1** (1.1.4 has known CVEs; DEFLATE output unchanged so `.iwd`/`.ff` compat preserved) |
| **Speex 1.1.9** | in-tree source | ✅ | ✅ | ✅ | Xiph BSD; **wire-locked** — codec version is embedded in voice packets, do not swap (e.g. to Opus) or in-game voice breaks vs. real clients |

The two proprietary RAD blobs are the hard gate for every non-32-bit-Windows target and are legally
questionable to ship in a public repo. The three source-drop deps are portability-clean.

---

## 6. Risks and fixed constraints

1. **Retail wire/asset compatibility is required.** The protocol-version mismatch (§1.3) must be
   resolved with captured compatibility fixtures, and the checked fast-file translation (§2.3,
   option A) is mandatory.
2. **Native 64-bit is required.** Windows amd64/ARM64, Linux amd64/arm64, and macOS arm64 are fixed
   targets, so the disk/runtime split and VM widening cannot be deferred.
3. **Proprietary deps:** removing Miles/Bink blobs from the repo is both a legal and a portability
   action. Sequence the OpenAL migration early if native non-Windows is a real goal.
4. **The decompile is fragile:** many hot paths are flagged by the original authors as unverified
   (`KISAKTODO`, disabled asserts, "sus cast"). Any 64-bit re-layout will surface latent bugs the
   32-bit layout was accidentally hiding. Budget for it; keep the round-trip/parity tests close.

---

## 7. Milestone plan (M0–M14): dependency graph & critical path

The phases above map onto 15 milestones. The dominant fact: **one mandatory shared foundation
(M4+M5) gates all five targets** — there is no cheap first target, because even win64 on its
friendliest toolchain requires the full ABI conversion, the fast-file mirror/relocation rewrite, and
an audio-backend replacement (Miles is 32-bit-only). The good news is the pre-foundation work
(M0–M2) and the entire platform layer (M3) are **validated on the existing 32-bit Windows build**
before any struct widens.

| ID | Milestone | Effort | Depends on | Exit criterion (abridged) |
|---|---|---|---|---|
| **M0** | Build-system foundation & CI scaffolding (still 32-bit Win) | M | — | CMake produces byte-identical 32-bit mp/sp/dedi; `KISAK_TARGET_OS/ARCH` auto-detect; a Linux preset configures |
| **M1** | Cross-compiler hygiene: `kisak_abi.h`, calling-conv & atomics headers | L | M0 | MSVC x86 build unchanged; GCC/Clang syntax-parse of the new headers passes; **fixed-width `sys_atomic.h`** replaces the `long` Interlocked shim |
| **M2** | Pointer-truncation sweep + UBSan/ASan/tidy gate + Huffman fix | L | M1 | 32-bit build + map-load + demo-playback run **clean under ASan/UBSan**; Huffman table byte-identical to retail; CI tripwire fails new `(int)&`/`&0xFFFFF000` |
| **M3** | Platform-abstraction layer (`Sys_*`/threads/net/fs/time + SDL3) | XL | M0, M1 *(parallel with M2/M4/M5)* | 32-bit Windows client+dedi run on the refactored layer with input/timer/net parity; POSIX backend dir compiles under GCC/Clang |
| **M4** | 64-bit ABI conversion: runtime structs, GSC VM union, zone/hunk | XL | M2, M1 | win64 links; **dual asserts** live (ILP32 value on 32-bit AND LP64 value on 64-bit); GSC VM runs a script-heavy save/load correctly at 64-bit |
| **M5** | Fast-file split: packed 32-bit mirrors + widening relocation loader | XL | M4 | an **unmodified retail `.ff`** loads on win64 and every runtime asset hash-matches the 32-bit reference dump; 32-bit build still loads it |
| **M6** | win64 client + dedi bring-up (first native 64-bit target) | XL | M5, M4 (M3 rec.) | win64 client boots, loads a retail map, golden-image render match, **OpenAL** audio/voice; win64 dedi passes demo/replay parity |
| **M7** | linux_amd64 **dedicated server** (headless) — first cross-platform runnable | L | M3, M5, M4, M1 | linux dedi compiles under GCC+Clang, loads a map **without GFX_D3D/Miles/Bink**, runs a match, demo-parity hashes bit-identical to win64 |
| **M8** | dxvk-native render backend + SDL3 client surface (shared Linux/macOS) | XL | M6, M3, M5 | linux full client renders a retail map through **dxvk-native/Vulkan**, golden-image match to win64; retail DX9 shader bytecode runs unmodified |
| **M9** | linux_amd64 **full client** — second native target | L | M8, M7, M6 | linux client fully playable; a **win64↔linux cross-play** demo-parity test shows bit-identical movement/physics; establishes the x86-64 FP baseline |
| **M10** | ARM64 determinism & arch layer (OS-agnostic) | L | M9, M1 | an aarch64 build produces **bit-identical** movement + demo hashes to the x86-64 baseline; no `__rdtsc`/`__cpuid`/x87/`__m64`/inline-asm remain |
| **M11** | win_arm64 — first ARM target (native D3D9on12 + Win32) | M | M10, M6, M4, M5 | win_arm64 client boots on Windows-11-ARM, renders via **D3D9on12**, cross-arch demo-parity vs win64 |
| **M12** | linux_arm64 — cross-compiled Linux ARM | M | M9, M10 | runs on **real ARM hardware** (not emulated), cross-arch parity vs linux_amd64 & win64 |
| **M13** | macos_arm64 — MoltenVK + bundle/codesign/notarize (final) | L | M9, M10 | **signed & notarized `.app`** renders via MoltenVK (feature-gap fallbacks verified), cross-arch parity vs win64/linux |
| **M14** | Full 5-target CI matrix, packaging & required gates | L | M6, M9, M11, M12, M13 | all 5 targets green as required gates; ASan/UBSan required on linux_amd64; cross-arch parity harness runs in CI; artifacts published |

**Critical path:** `M0 → M1 → M2 → M4 → M5 → M6 → M8 → M9 → M10 → M13 → M14`. The long pole is the
contiguous **M4→M5→M6** block (ABI conversion → fast-file rewrite → first win64 bring-up); it cannot
be parallelized away because every later target consumes its output. **M3 runs off the critical path**
in parallel with M2/M4/M5 and only becomes blocking at M7/M8 — if it slips, it joins the critical path.

```
M0 → M1 → M2 → M4 → M5 → M6 → M8 → M9 → M10 ┬→ M11 ┐
      └──→ M3 ─────────────┘   │    │       ├→ M12 ┼→ M14
                    M7 ────────┘    │       └→ M13 ┘
      (M3 feeds M7 & M8; M7 is the linux dedi beachhead off M5)
```

**Target order & why:** (1) **win64** — cheapest beachhead: exercises the entire mandatory ABI +
fast-file crux while holding every other variable constant (same MSVC, native x64 D3D9 needs zero
render rewrite, existing Win32 layer). (2) **linux_amd64**, entered via the **headless dedicated
server** — forces GCC/Clang + POSIX + the 64-bit crux together but needs no render/audio/input
(null-RHI stub), so it's the cheapest cross-platform runnable, the ASan/UBSan gate host, and the
highest-value real artifact (Linux game servers); the full client then layers dxvk+SDL3+OpenAL.
(3) **win_arm64** — cheapest ARM: the OS routes D3D9 through D3D9on12 so render "just works," the
Win32 layer is reused, adding essentially only the ARM determinism layer. (4) **linux_arm64** — a
near-pure matrix extension of the finished linux client + the ARM layer (main new cost: cross-compile
toolchain + real-ARM CI). (5) **macos_arm64** — last: it needs the whole Linux client stack **and**
the ARM layer **and** its own novel MoltenVK feature-gap + notarization work.

---

## 8. ABI conversion — the three-layout-class strategy *(deepens §2)*

Do **not** convert file-by-file. Classify every asserted/serialized struct into one of three layout
classes and drive the conversion class-by-class. This keeps the on-disk/wire contract a live tripwire
even under a 64-bit compiler.

1. **ON-DISK / WIRE structs** (fast-file assets; networked POD) — *keep frozen at 32-bit.* Re-express
   each as a **packed mirror** type whose pointer fields become `Ptr32<T> = uint32_t`, and pin the
   mirror with the **original** `sizeof`/`offsetof` asserts (`ONDISK_SIZE`). Detect them by which
   structs are the target of a `Load_*` walker in `src/database` (assets) or appear in
   `MSG_Read/WriteBits` POD paths (wire). Networked POD stays bit-identical, preserving retail wire
   compat.
2. **RUNTIME-ONLY structs** (`scrVmPub_t` 0x4328, `XZoneMemory` 0x58, hunk bookkeeping) — *widen
   pointers to native* and **regenerate the assert under a width switch** (`RUNTIME_SIZE(T,n32,n64)`:
   assert the 32-bit value on ILP32, the 64-bit value on LP64/LLP64) so drift is still caught on both
   builds.
3. **ASSET runtime structs** — *use native 8-byte pointers* and convert the loader from **in-place
   relocation to a load-time widening/relocation pass**: read the 32-bit mirror image into the stream
   buffer, allocate the widened runtime struct from the zone, copy field-by-field, and set pointer
   fields from the packed offset via the existing block math (`block=(off-1)>>28`,
   `byteoff=(off-1)&0xFFFFFFF`). Touchpoints: `db_stream_load.cpp:45-57`, `db_stream.cpp:81-105`,
   `db_load.cpp:552-1652` (all `Load_*` + convert call sites), `db_memory.cpp`.

**GSC VM decision — widen the union, do *not* go handle-based** (refines §Phase 1 item 4): change
`codePosValue`/`vectorValue`/`stackValue` (`scr_variable.h:100-140`) to native pointers; leave
`intValue`/`floatValue`/`stringValue`/`pointerValue`/`entityOffset` at 32 bits. `VariableValue`
becomes 0x10; regenerate `function_stack_t` (0x14→0x28), `scrVmPub_t`, and the 2048-entry value
stack. The ~215 deref sites compile unchanged. This is lower-risk than handle-based because the VM is
never serialized and its savegame path already decomposes by type.

**Do the pure-bug pointer-truncation sweep first, on the 32-bit build, UBSan-gated** (M2, before any
struct widens): every `(int)&`/`(uint32_t)&` cast-of-address and `& 0xFFFFF000` page mask →
`uintptr_t` + `~(uintptr_t)0xFFF`; the ~80 pointer-as-int loops (e.g. `sentient.cpp:441`'s `i+=116`
stride, `g_utils.cpp:1846`, `cm_world.cpp:1226`) → typed pointer arithmetic so the compiler recomputes
strides. Key sites: `com_memory.cpp:431-695`, `scr_parsetree.cpp:332`, `cl_main.cpp:625`,
`sv_game.cpp:558`. **`long` note:** harmless on Win64/WinARM64 (LLP64, `long`=32) but **bites on
Linux/macOS LP64** (`long`=64) — replace type-significant `long`/`unsigned long` with fixed-width
`int32_t`/`int64_t`, especially in the atomics shim (M1's `sys_atomic.h`).

---

## 9. Correctness & parity strategy *(the safety net for the whole port)*

The decompile's 32-bit layout was *accidentally hiding* bugs; widening will surface them. These gates
are not optional — several failure modes (pointer truncation, cross-arch FP desync) pass every test on
a dev box and corrupt only in production.

- **Dual-assert tripwire:** never blindly regenerate the 249 `sizeof`/200 `offsetof` asserts to 64-bit
  values — that removes the only defense during the riskiest refactor. On-disk mirrors keep the
  *original* 32-bit asserts; runtime structs assert both widths. A CI grep **fails** on any bare
  `sizeof(...)==0x..` outside `layout_asserts.h`.
- **Sanitizer gate (M2 onward):** a Linux clang build with `-fsanitize=undefined,address`
  (pointer-overflow, alignment, cast) as a **required** gate, plus clang-tidy for reinterpret-cast /
  portability. Lands on the 32-bit build first so truncation bugs are caught before widening.
- **>4 GB allocation test:** pointer truncation only manifests when an allocation lands above 4 GB.
  Run the asset-load harness in a config that reserves a low guard region to force high addresses, so
  ASan poisons the truncation instead of it silently corrupting in production.
- **Retail-asset golden hash (gates M5→M6):** load a real retail `.ff` in CI and hash each resolved
  runtime asset's field values + pointers against a 32-bit reference dump — per-asset-type, before the
  loader is enabled globally. This is the mitigation for the single biggest quality risk on the
  critical path (the M5 relocation rewrite touches the least-testable code in the engine).
- **Cross-arch demo-parity determinism harness (gates M9→M14):** play back a recorded demo and diff
  per-frame movement/physics/entity state against a 32-bit reference, then across win64 ↔ linux ↔ ARM.
  This catches VM widening *and* FP determinism regressions before they ship as MP desyncs. Pin FP to
  round-to-nearest with **`-ffp-contract=off`** / `/fp:precise`; require the SSE2 and NEON skinning
  paths to bit-match the reference. **`KISAK_PURE` x87 bit-exactness is physically impossible on ARM**
  (no `fistp` analog) — hard-disable it off x86 and rely on the harness for parity.
- **Real ARM hardware runners are mandatory** (`ubuntu-24.04-arm`, `windows-11-arm`, `macos-15`):
  cross-compiled ARM cannot run on x64 builders, so compile-only jobs give false confidence for exactly
  the runtime-only bugs (truncation, `__m64`, memory ordering).

**Up-front blockers to resolve before the build is even exercised:** the repo is GPLv3 while
Miles/Bink/Steam/DXSDK redistributables are proprietary and 32-bit-only; macOS/ARM lose Miles and Bink
entirely, Steam ships no ARM lib, and the June-2010 DXSDK has no ARM64 import lib. Stub audio/cinematic
behind the platform layer early (so those targets link) and treat the Miles→OpenAL / Bink→FFmpeg
replacement as a dependency track that gates M6, not M4/M5.

---

*See `docs/CODEBASE_AUDIT.md` for the full security / logic / functionality / build-tooling findings
that this plan references.*

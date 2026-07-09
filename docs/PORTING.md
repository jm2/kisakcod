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
4. **Script VM value representation (§2 item 2):** store handles/indices, never raw pointers, in
   `VariableUnion`; re-audit every opcode and the stack format. Touches all of `src/script`.
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

*See `docs/CODEBASE_AUDIT.md` for the full security / logic / functionality / build-tooling findings
that this plan references.*

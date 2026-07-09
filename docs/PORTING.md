# KisakCOD Porting Plan — Win64, Linux, macOS

**Status:** implementation in progress · **Scope:** MP client + headless dedicated server
**Basis:** whole-tree analysis of the current `master` (see `docs/CODEBASE_AUDIT.md` for the health report).

---

## Implementation status (July 8, 2026)

Target policy is fixed: preserve retail assets and wire interoperability; use a
shared **native Vulkan RHI** (MoltenVK on macOS) that replaces D3D9, OpenAL Soft,
and FFmpeg; publish portable packages for Linux; and require native CI plus
licensed gameplay smoke tests.

**Committed scope is the MP client + the (headless) dedicated server. Single-player
is deferred** — SP-only serialization surfaces (save-games) and SP subsystems are
documented for completeness but are off the current critical path.

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
- **Rendering (committed: native Vulkan RHI, not a translation layer):** land a thin in-tree RHI
  (`src/gfx/kisak_rhi.h`) covering seven state groups (device/swapchain, context, pipeline collapsing
  `r_state.cpp` render+sampler state, buffers, textures, shader modules + constant binding,
  query/fence, render-target + caps/VRAM). Implement **`RhiD3D9` as a passthrough first** and reroute
  the **~400 device call sites** (308 `device->` + 79 `dx.device->`) to `rhi->` so Windows keeps
  shipping on D3D9 at every commit; put **SDL3** under the surface; then add the **Vulkan backend**
  behind the identical interface (MoltenVK gives macOS for free; the same backend serves linux
  amd64/arm64 and win-arm64). **dxvk-native is demoted to an optional *intermediate* runtime** used to
  stay demoable on Linux/macOS while the native backend is written — not the shipping endpoint. See
  the shader subsection below and §9 for the D3D9-semantics risks.
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

### Shader pipeline: DX9 bytecode → SPIR-V

The runtime consumes **precompiled DX9 (SM3) bytecode baked into fast-files**
(`CreatePixelShader`/`CreateVertexShader` over `loadDef->program`); the asset/tool path uses D3DX
(`Material_GenerateShaderString` → `D3DXCompileShader`). The native Vulkan backend needs SPIR-V, and
**there is no turnkey DX9-SM3-bytecode→SPIR-V compiler** — DXBC SM4/5 is solved (vkd3d), but DX9 uses
the older token-stream ISA. Strategy:

- **Bring-up:** an **offline re-baker** (`tools/shader_rebake`) that emits a SPIR-V-carrying shader
  load-def variant (`loadForRenderer` already multi-targets, `r_gfx.h:649,656`). The only
  production-grade DX9→SPIR-V compiler that exists is **dxvk's DXSO module** — lift it as a standalone
  offline tool (note: reusing it even offline partially reintroduces the translation code the native
  goal wants gone; writing a DX9 lifter from scratch is a multi-month subproject).
- **Long-term source of truth:** migrate the in-tree HLSL generator to **HLSL→SPIR-V via DXC** once the
  RHI is stable.

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
| Rendering (native Vulkan RHI) | XL (RHI + Vulkan rewrite, ~400 device sites) | XL native (S via dxvk-native *interim*) | XL / free via MoltenVK under same RHI | D3D9 kept only as the `RhiD3D9` passthrough for parity, not the endpoint |
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
| **M8** | native Vulkan RHI: `kisak_rhi.h` + `RhiD3D9` passthrough (reroute ~400 device sites, Windows stays green) → Vulkan backend + SDL3 surface + offline shader re-bake | XL | M6, M3, M5 | linux full client renders a retail map through the **native Vulkan backend** (re-baked SPIR-V shaders), golden-image match to win64; dxvk-native may serve as an interim backend feeding M9/M13 but is not the M8 deliverable |
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
   mirror with the **original** `sizeof`/`offsetof` asserts (`ONDISK_SIZE`). **Build this on the
   existing `src/database/db_disk32.h`** (`disk32::PointerToken` + bounds-checked `DecodeOffset`
   block/offset math), which is already the packed-mirror seed — do not stand up a parallel `Ptr32<T>`. Detect them by which
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
- **Native-Vulkan-RHI render risks:** (a) no turnkey DX9→SPIR-V compiler — DXSO reuse reintroduces a
  translation-code dependency the native goal wants gone; (b) **D3D9 semantics are baked into assets and
  engine math** (half-texel offset, Y-flip, clip-Z range, row- vs column-major, BGRA, D3DPOOL/lost-device)
  and render subtly wrong if not replicated; (c) the `SetVertex/PixelShaderConstantF` register writes →
  push-constants/UBOs must **byte-match `R_HW_SetVertexShaderConstant`** or it is silent corruption;
  (d) the 249 `sizeof` static-asserts + `GfxCmdBufPrimState`/`GfxTexture` are read by `.ff` byte-parsers
  (`db_memory.cpp`), so RHI-ifying `DxGlobals` must not break fast-file compat; (e) `db_memory.cpp`
  allocates GPU buffers during `.ff` load, so **headless/dedi needs a null-RHI stub**; (f) the
  `IDirectDraw7` VRAM query (`r_texturemem.cpp`) and D3DX have no Vulkan analog and become the RHI
  caps query (all-target, incl. Win-ARM64 under D3D9on12 — it does not provide DirectDraw7 either).

**Up-front blockers to resolve before the build is even exercised:** the repo is GPLv3 while
Miles/Bink/Steam/DXSDK redistributables are proprietary and 32-bit-only; macOS/ARM lose Miles and Bink
entirely, Steam ships no ARM lib, and the June-2010 DXSDK has no ARM64 import lib. Stub audio/cinematic
behind the platform layer early (so those targets link) and treat the Miles→OpenAL / Bink→FFmpeg
replacement as a dependency track that gates M6, not M4/M5.

---

## 10. Gaps surfaced by adversarial review

An adversarial completeness pass over §1–§9 found the following gaps and corrections. They are
tracked here so the milestone work accounts for them.

**H1 (DEFERRED — SP-only) — Save-games are an unclassified 4th serialization surface.** Because
single-player is deferred (see scope), this is **off the MP/dedi critical path** and is recorded for
when SP is picked up. `g_save.cpp` raw-dumps whole runtime structs (`gentity_s`, `gclient_s`,
`level_locals`) via `SaveMemory_SaveWrite` — e.g. `WriteClient` passes the hardcoded magic size
`46104`, keyed only by `header.saveVersion = 287`. The three-layout-class model (§8) routes these
structs to "runtime → widen," which **silently changes the on-disk save layout with no migration**, and
LLP64 vs LP64 widths make saves **non-portable across targets**. *When SP is resumed:* add a 4th layout
class "SERIALIZED-RUNTIME (save-game)" — either replace the raw memcpy with descriptor-driven
field-by-field writes (killing the `46104` magic), or declare saves version-bumped and non-portable
with a `saveVersion` + arch/width tag; add a save round-trip + cross-width load test. **Not milestone
gating for MP/dedi.**

**H2 — Steam auth: capability-gate it (and doing so fixes an existing dedi defect).**

*Why it exists (it is not retail behavior):* retail COD4 ran with no Steam. KisakCOD removed the CD-key
scheme and PunkBuster, losing its stable player identity and ban primitive, so contributor LWSS grafted
Steam in (commit `fc43d360`, 2025-06-23 — **not** the original decompile, **not** the recent porting
work) to fill exactly that hole: `SteamID64` is substituted verbatim for the old `cdkeyHash` and
becomes the server GUID (`sv_client_mp.cpp:204`, with the original `//…cdkeyHash` line commented right
above), keeping `SV_IsBannedGuid`/`SV_IsTempBannedGuid` alive as string compares; a session ticket
(`GetAuthSessionTicket`→`BeginAuthSession`) adds login-time anti-spoof auth. It is **not** an
ownership/license gate (no `UserHasLicenseForApp`/`BIsSubscribedApp` anywhere in `src/`) and **not**
matchmaking (persona name fetched but unused; master browser already dead).

*Steam is portable, with one gap.* The layer uses the standard cross-platform Steamworks API; the only
Windows include in `win_steam.cpp` is `<Windows.h>`. Valve ships `steam_api64`/`libsteam_api.so`/
`.dylib` free in the same SDK, so Steam links natively on **win64, linux-amd64, macOS-arm64** with
minimal fixup (guard refactor + `<Windows.h>` decouple + commit the per-target libs). **There is no
ARM64 Steam library** (the SDK ships `linux32/linux64/osx/win64` only), so **win-arm64 and linux-arm64
cannot link Steam** — that, not portability, is what mandates a fallback.

*Current state is a hard gate on both ends.* The client refuses to send a challenge without a real
ticket (`cl_main_mp.cpp:1056-1060`); the server rejects an empty ticket/ID (`sv_client_mp.cpp:172-175`)
and only issues `challengeResponse` if `Steam_CheckClientTicket` passes (`:210`, else *"Your Steam
Client Ticket was Invalid"*). **Critical defect:** the dedicated server uses the *client*
`SteamAPI_Init`, **not `SteamGameServer`** (none in tree), and `Steam_CheckClientTicket` returns false
when the process isn't Steam-initialized (`win_steam.cpp:238-242`) — so a **headless dedi is presently
unjoinable unless its operator is logged into a desktop Steam client that owns appid 7940**. Absurd for
a Linux server; making Steam optional *fixes* this.

*Plan (~3–4 focused days).* Introduce a `KISAK_STEAM` capability macro **decoupled from `WIN32`** and
replace the three `#ifdef WIN32 … #else #error` guards (`cl_main_mp.cpp:38-43`,
`sv_client_mp.cpp:16-21`, `cl_main_pc_mp.cpp:388-393`) with `#if KISAK_STEAM`. Add a **2-backend
identity provider** behind the four connect call sites: STEAM = real ticket + `SteamID64` (today's
path); NO-STEAM = empty ticket + a **persistent self-generated md5 `cl_guid`** — the Quake3-style
scaffolding already sits commented at the exact emit site (`cl_main_mp.cpp:1053-1054`
`//CL_BuildMd5StrFromCDKey`). Add **`sv_requireSteam` (default 0)** gating only the empty-ticket
`iassert` (`:172-175`) and `Steam_CheckClientTicket` (`:210`); leave the GUID ban path
(`:187/:195/:204`) untouched (identity-agnostic). Stub `Steam_Init`/`Steam_CheckClients`/
`Steam_OnClientDropped` to no-ops when off and guard `Steam_Init` (`win_main.cpp:825`). *Cross-play
contract:* a server picks one mode — `sv_requireSteam=1` accepts only real-ticket Steam clients;
`sv_requireSteam=0` is an open/LAN/community server that records whatever GUID arrives but validates no
ticket; a no-Steam client cannot join a require-Steam server (correct, not a regression). Format
self-gen GUIDs distinctly from 17-digit `SteamID64` so ban entries can't collide. What's genuinely lost
with Steam off — VAC-style async kick, the implicit ownership check, friends/browser (already unwired)
— is all non-connect-critical and costs zero code to disable; document it as the open-server security
posture, and keep `sv_requireSteam=1` available wherever a `steam_api` lib is committed.

**H3 — Testing strategy must be reconciled with reality; "retail parity" is a non-goal.** The
existing 5-target CI matrix builds with `KISAK_BUILD_MP/DEDICATED/SP=OFF`, so it exercises **zero game
code** (green-but-empty). "Wire parity vs retail" is infeasible/mis-framed — the protocol is pinned to
`1` (not retail) and a retail `.ff` cannot live in a GPLv3 repo. *Corrected testing plan:* the goal is
**self-consistency across the five builds**, not retail parity. Pin **x86-64 (linux_amd64 or win64) as
the golden reference**; commit a golden vector set for `Sys_SnapVector`/`PM_` movement plus a short
recorded demo; gate the three ARM legs **bit-exact** against it via **demo playback** (the wire-neutral
`cl_demo.cpp` MSG stream is the right determinism oracle). Produce the asset-load oracle from a
**self-generated `.ff`** built by the (Windows-only) asset tool in CI. Attach the **ASan/UBSan gate to
a leg that actually builds the game (linux_amd64)**, not the portable-only legs.

**M1 — MMX/SSE skinning is dead code, not a render prerequisite (correction).** §Phase-3/render
framing implied porting `r_model_skin_sse.cpp` (`__m64`) is a render blocker. It is not: the SSE call
at `r_model_skin.cpp:158` is commented out, the scalar `R_SkinXSurfaceSkinned` (`:163`) is live, and
`KISAK_ENABLE_X86_MMX_SKINNING` already defaults **OFF** on 64-bit/ARM. Treat the SSE file as dead code
to **drop** in the ARM step; NEON skinning is an optional perf item, never a bring-up blocker.

**M2 — Several "to-do" items are already done; rebase estimates on HEAD.** `KISAK_PLATFORM`
auto-detects (`CMakeLists.txt:22-32`), the `set(WIN32)` collision is fixed (renamed `PLATFORM_WIN32`),
`db_disk32.h` already seeds the packed mirror (§8), the headless-dedi split and the 5-target portable
CI matrix exist, and the M2 pointer-truncation sweep + tripwire have now landed. `platform_compat.h`
covers calling-convention/`KISAK_ALIGNAS` but is barely wired in; the **`sys_atomic.h` fixed-width
shim (209 `Interlocked`/`long` sites) and `kisak_abi.h` are the main open M1 items.**

**M3 — Windows-ARM64 D3D9on12 is "expected to work," not "just works"; `IDirectDraw7` is mis-scoped.**
`r_texturemem.cpp:14-86` queries VRAM via `IDirectDraw7` (`DirectDrawCreateEx`/`GetAvailableVidMem`),
which **D3D9on12 does not provide** any more than dxvk does — so Windows-ARM64 hits the same failure as
Linux/macOS. *Move the `IDirectDraw7` replacement into an all-target step*, and validate the D3D9on12
device-create + VRAM-query + double-`Direct3DCreate9` seam on the `windows-11-arm` runner.

**M4 — Fatal-error thread suspend needs a per-OS mechanism.** `Sys_SuspendOtherThreads`/`Sys_Error`
freeze all threads to walk stacks (`threads.cpp:168-181/326-336`); there is no portable equivalent.
*Specify:* Linux `tgkill`+`SIGRTMIN` handler that only sets a flag and parks on a semaphore; macOS
`mach thread_suspend`/`thread_get_state`. The crashing thread's stack walk must read the suspended
thread's context and **never call libc heap** (async-signal-safety), and must interlock with the ARM
step's `backtrace()`/`CaptureStackBackTrace` replacement. Add a deliberate worker-thread-assert test
on all five targets.

**L1 — Non-headless dedicated server is Windows-only.** `scripts/dedi/dedi_sources.cmake` links
Bink/GFX_D3D/Miles/Speex unless `KISAK_DEDI_HEADLESS=ON`. State that **headless is the only supported
dedi on the four non-Windows targets** (CI builds those legs with `KISAK_DEDI_HEADLESS=ON`); keep
non-headless dedi as a Windows-only legacy config.

**L2 — Localized-string assets ride the fast-file path.** `SE_GetString_LoadObj`
(`stringed_ingame.cpp:15-18`, `IsFastFileLoad()`) loads the `localizedString` asset through the same
`DB_ConvertOffsetToPointer` 32-bit-zone machinery — add it to the M5 mirror/relocation inventory. The
text `.str` path (`SE_LoadFileData` → `FS_ReadFile`) is bitness-neutral and needs no work.

---

*See `docs/CODEBASE_AUDIT.md` for the full security / logic / functionality / build-tooling findings
that this plan references.*

# KisakCOD Porting Plan — Win64, Linux, macOS

**Status:** implementation in progress · **Scope:** MP client + headless dedicated server
**Basis:** whole-tree analysis of the current `master` (see `docs/CODEBASE_AUDIT.md` for the health report).
**Live checkpoint:** [`docs/task.md`](task.md) tracks the current batch, CI evidence, debt counts, and next queue.

---

## Implementation status (July 15, 2026)

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
  return types, parse-tree alignment, and checked client/server skeleton arenas with fixed-width
  cursors/epochs, wrap invalidation, and contention coverage;
- native-width, bounded heterogeneous DObj/model-surface streams with exact arena reservations,
  worker/scene framing validation, ARM-safe signed LOD storage, and pre-publication XSurface/XModel
  skin, skeleton, LOD, material, and collision-graph validation;
- a green Windows x86 `KISAK_DEDI_HEADLESS` compile/link profile that excludes
  client/cgame/UI/D3D/audio/cinema/proprietary media groups, parses common
  fast-files through a validated null GPU/audio backend, retains its binary as
  a CI artifact, and has source/dependency guards against media re-entry;
- download-block and stats-packet runtime bounds checks;
- host platform detection, target build switches, and an explicit 64-bit ABI gate;
- platform source override plumbing for Windows, Linux, and macOS;
- target-owned engine/headless/service source selection that preserves the exact Win32 lists while
  keeping Linux/macOS lists explicitly empty and incomplete until native backends land;
- portable `sys_sync.h`/`sys_time.h`/`sys_thread.h` contracts, including fixed-width MP/SP
  critical-section IDs and opaque native thread handles wired into all five utility-CI targets;
- native Win32/POSIX monotonic-clock, sleep/yield, and recursive-critical-section backends plus a
  common fixed-width fast reader/write lock, with concurrent runtime coverage on utility targets;
- an opaque event contract with checked Win32 and POSIX manual/auto-reset implementations, and
  Windows-free public thread/context headers;
- an opaque thread lifecycle with current-thread capture, create-suspended/start, identity,
  finite/infinite join, and explicit destruction on Win32 and POSIX;
- an opaque, target-neutral renderer-worker gate with acknowledged cooperative pause/resume and
  stale-event-resistant runtime stress coverage;
- corrected Win32 multi-config DirectX paths and post-build output handling;
- `build-win.ps1`, Windows CI, tagged release archives/checksums, and separately
  protected legacy/headless licensed dedicated-server smoke definitions;
- Steam decoupled from `WIN32` behind `KISAK_ENABLE_STEAM`/`KISAK_STEAM` with a
  persistent `cl_guid` fallback and `sv_requireSteam`, fixing the unjoinable
  headless-dedi defect (see §10 H2);
- a portable callback-driven FX archive admission controller with typed, fail-closed gate values,
  phase-aware TLS ownership, deterministic cleanup/generation tests, and production integration;
- status-bearing, segment-bounded legacy `MemoryFile` RLE/zlib reads with caller-owned C-string capacity,
  exact little-endian segment headers, report-free FX integration, and TLS-owned error-unwind cleanup;
- a transactional FX effect-definition restore table backed by one bounded BSS image, with exact TLS/serial ownership,
  symmetric archive/lifecycle admission, full parse-before-registration, explicit little-endian keys, Win32-safe asset
  names, longjmp abandonment, and raw/zlib concurrency/malformed-input coverage;
- a bounded heap-owned FX effect-definition save snapshot that releases database enumeration ownership before validation
  or output, preserves valid legacy raw/zlib bytes, rejects unsafe names and invalid/conflicting Disk32 keys before the
  first write, and carries portable constrained-stack plus compiler-frame gates; source-scoped Windows x86 production
  measurement is calibrated and enforced in Debug and Release PR CI;
- a coherent FX save-snapshot publication boundary (merged in PR #21) that admits camera/time/visibility
  publishers and readers against archive exclusion, adds an external fixed-width shared/exclusive camera gate for normal
  workers without changing frozen `FxSystem`, publishes camera validity only after its payload, stages raw system and
  buffer bytes once, validates through a separately relinked heap image, derives bounded visibility selectors, and proves
  every copied effect-definition pointer belongs to the retained table before dereference or output;
- a production-integrated FX Disk32 layer with distinct strong archive-definition-key and archive-address types, exact
  legacy x86 and deterministic native64 key policies, fixed `0x1C` spatial-frame, `0x80` effect-record, `0xB0` camera, `0x10`
  sprite, `0xA60` system, and `0x47480` buffer mirrors, explicit compiler-independent bolt/sort packing, exhaustive
  owner/all-effect-handle conversion, numeric full-buffer topology and visibility-selector validation, raw aligned pool
  slots, bounded free-list allocation reconstruction, and a checked heap-owned native structural workspace. The workspace
  resolves active definition identities without dereference, explicitly constructs every native pool member, preserves
  opaque free-slot tails, relinks local pointers/selectors, rejects resolver reentry, and publishes only after scratch-backed
  allocation-graph validation. Definition-aware semantic `Ready`, exact Ready-only physics enumeration, and a portable
  transactional reader for the post-definition system/buffers/address/body tail are complete. `FX_Restore` now copies that
  Ready image into an independent mutable candidate while holding the reader's operation gate and exact stored definition
  lease, destroys the reader, validates/releases the lease, and immediately enters generation-checked archive ownership
  before reusing the existing live publication/rollback controller. The reader is 670,976 bytes on x86 / 695,640 bytes on
  native64; the candidate is 376,240 / 400,904 bytes, respectively, and both are checked heap workspaces. Ready-view
  access validates and rechecks that exact active lease under the candidate operation gate, rejecting stale, released,
  forged, reacquired, or callback-reentrant access without publishing an output. The old raw
  restore parser, restore-only width/ABI/address-relocation path, and native64 restore guard are removed. `FX_Save`, its
  native64 guard, the legacy writer, wire format, and licensed workflow remain unchanged;
- exact pointer-bearing FX fast-file Disk32 effect, element, velocity/visibility, visual/decal, trail, and impact-table
  schemas with compiler-independent size/offset and golden-byte contracts. The active widening branch extracts one
  canonical portable native effect-definition type family and implements bounded two-pass effect and impact-table
  converters. Planning freezes resolver requests, snapshots callback descriptors and source records, binds every string
  and native identity exactly once, detects callback/source mutation and partial overlap within every resolver-reported
  retained extent, and validates both legacy and widened layouts. Callback-free materialization placement-constructs
  actual runtime objects into an aligned caller-owned blob, owns copied strings/records, preserves legacy trail capacity
  semantics, and publishes only after all fallible checks. Review hardening validates retail time/count/visibility/atlas
  canonicalization, rejects trail definitions outside the runtime-supported looping range, and prevents the normalized
  visibility endpoint from indexing beyond the final adjacent sample pair. The exact effect workspace is 325,904 bytes
  on x86 and 325,928 bytes on native64; the impact workspace is 11,216 and 11,232 bytes, respectively. The stateful
  XBlock/XAsset loader, retail bytes, legacy x86 path, archive writer, and save-side guard remain unchanged pending a
  zone-owned arena and guarded production adapter;
- the M1 ABI-contract headers `kisak_abi.h` (OS/arch/pointer-width detection +
  the `ONDISK_SIZE`/`RUNTIME_SIZE` layout-freeze macros) and `sys_atomic.h` (the
  fixed-width, MSVC-byte-identical atomics shim), reconciled with
  `disk32::Ptr32<T>` and covered by a portable atomics/layout compile-check that
  rides all five CI legs (see §10 M1).

Remaining gates, in implementation order:

1. Keep the protected licensed headless startup/map/network smoke deferred and do not dispatch it until
   its `[self-hosted, kisakcod, windows, x86]` runner and `KISAKCOD_GAME_DIR` secret are provisioned;
   surface that infrastructure blocker rather than creating a permanently queued run.
2. Complete FX archive runtime closure. Live generation-checked sidecars, full-capacity rollback, exhaustive
   pure restore control, checked heap transaction/preflight scratch, and the normal archive admission gate with
   deterministic waiter/error-unwind coverage are implemented. Exact competing non-FX ODE occupancy and silent
   live creation/impact/rollback transactions are also implemented. The report-free, segment-bounded `MemoryFile`
   parser prerequisite, transactional BSS effect-definition restore lease, bounded save snapshot, and portable extracted-
   helper frame/runtime gates are complete. Windows x86 production analysis exposed and removed a 10,256-byte convenience
   wrapper; authoritative Debug and Release reports now measure `FX_Save` at 2,756 bytes, `FX_Restore` at 6,124 bytes,
   and the largest other helper at 2,064 bytes. Coherent camera/scalar/visibility snapshot publication passed all nine CI
   jobs plus exact-head Codex review in PR #21, and its sole Gemini finding is fixed and resolved. The first reader-first
   Disk32 batch now separates native definition identity from explicit archive keys, preserves exact x86 keys, assigns
   deterministic native64 keys, and proves fixed effect-record/owner-handle conversion with exhaustive portable fixtures.
   The exact `FxSystemDisk32` mirror and pure decoder are also complete, including checked `0x47480` address topology,
   visibility selectors, all-effect-handle permutation, active-ring metadata, and spotlight conversion. The exact
   `FxSystemBuffersDisk32` and nested record mirror plus bounded raw-slot free-list allocation reconstruction are also
   complete. The exact heap-owned native structural image is now complete as well: it performs active-only opaque
   definition resolution, explicit native union-member construction, local pointer/selector relinking, and complete
   allocation-graph validation before exposing a non-publishable `StructurallyValid` view. Definition-dependent payload
   activation, semantic `Ready` validation, exact body-record decoding, and transactional report-free `MemoryFile` staging
   of the post-definition archive tail are complete in the portable reader. Production restore now consumes that image
   through the exact-lease-bound mutable candidate, centralized staging cleanup, immediate lease-release-to-archive-admission
   handoff, and the existing publication/rollback controller. That production-restore checkpoint is merged. The writer and
   save guard follow later.
3. Continue fixed-width `disk32` fast-file widening. Exact FX effect/visual/trail/impact schemas and hardened pure
   transactional native converters are implemented in PR #32; local GCC/Clang, complete sanitizer, strict i386/AArch64,
   and source-contract checks are clean while replacement CI/review/merge remain. Next, add a zone-owned aligned native
   arena and guarded stateful XBlock/XAsset adapter with exact rollback, completed-object/alias registration, and lifetime
   tests before replacing any legacy loader path. Retail wire bytes remain frozen.
4. Widen the script VM value representation and remove pointer-to-32-bit casts.
5. Implement the remaining platform services (sockets, filesystem,
   virtual memory, console/process) for Windows/POSIX.
6. Introduce the Vulkan RHI, retaining D3D9 temporarily on Windows during parity
   testing; add OpenAL Soft and FFmpeg backends.
7. Add scalar/SSE2/NEON dispatch and remove x86 inline assembly/MMX.
8. Enable and gate Windows amd64/ARM64, Linux amd64/arm64, then macOS arm64
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

**H2 (IMPLEMENTED) — Steam auth: capability-gated; the existing dedi defect is fixed.**

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

*What landed (commit on `master`).* A `KISAK_ENABLE_STEAM` CMake option (default **ON** everywhere a
Steamworks lib exists, **OFF** on ARM — Valve ships no aarch64 library) defines a `KISAK_STEAM`
capability macro **decoupled from `WIN32`**. All three `#ifdef WIN32 … #else #error` blocks are gone;
every one of the eight Steam call sites is `#ifdef KISAK_STEAM`-guarded (verified programmatically) and
`win_steam.cpp` compiles to an **empty translation unit** when off; `steam_api.lib`/`steam_api.dll` are
linked/copied only when enabled. The no-Steam identity is a **persistent self-generated `cl_guid`**
(`DVAR_ARCHIVE | DVAR_USERINFO`, seeded from `Sys_MillisecondsRaw()` folded with the srand-seeded
`rand()` stream), sent as `getchallenge 0 "" "<cl_guid>"`; `CL_CDKeyValidate` becomes a no-op when off.
**`sv_requireSteam` (default 0)** was added. Server accept policy in `SV_GetChallenge`: an identity
(arg 3) is always required and always runs the ban path (`SV_IsBannedGuid`/`SV_IsTempBannedGuid`); if a
ticket (arg 2) is present **on a `KISAK_STEAM` build it must validate** (anti-spoof preserved, same
reject as before); a ticketless client is accepted unless `sv_requireSteam` is set; a **non-`KISAK_STEAM`
server ignores any presented ticket** and treats the client as identity-only, so a Windows Steam client
can still join an ARM server (**cross-play preserved**). The **headless-dedi-unjoinable defect is fixed**
— `Steam_Init` is guarded, so a dedicated server no longer needs a logged-in desktop Steam client.
`SV_DropClient` only ends a Steam session for a genuine all-digit `SteamID64` (not a hex `cl_guid`). A
`windows-x86-nosteam` CI leg compiles the fallback path (the only buildable engine target today).
*Adversarial review* (4 lenses, per-finding verification) confirmed the default Windows build is
byte-identical and found one low-severity item — the `cl_guid` RNG was strengthened in response.
*Follow-ups:* `steam_api64`/`libsteam_api.so`/`.dylib` still need committing for native Steam on
win64/linux-amd64/macOS (works today via the same SDK); format self-gen GUIDs distinctly from 17-digit
`SteamID64` if ban-namespace collisions ever matter. What's lost with Steam off — VAC-style async kick,
the implicit ownership check, friends/browser (already unwired) — is all non-connect-critical.

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
covers calling-convention/`KISAK_ALIGNAS`.

**M1 (headers landed) — `kisak_abi.h` + `sys_atomic.h` + the portable compile-check are in.**
`src/universal/kisak_abi.h` (compile-time only: OS/arch/`KISAK_PTR_BITS` from compiler predefineds +
`UINTPTR_MAX`, the `ONDISK_SIZE`/`RUNTIME_SIZE`/`*_OFFSET` layout-freeze macros, includes
`platform_compat.h`) and `src/universal/sys_atomic.h` (the fixed-width atomics shim) have landed, with
`db_disk32.h` reconciled onto `ONDISK_SIZE` + the single canonical `disk32::Ptr32<T>`, and a new
`tests/abi_atomics_tests.cpp` ctest that rides all five portable-CI legs (verified locally under g++ 16
and clang++ 22 on both ILP32 and LP64). **Atomics design decision:** the collision-free `Sys_Atomic*`
API is now the canonical boundary on every compiler. It accepts only aligned `int32_t`/`uint32_t`
words, uses unsuffixed `_Interlocked*` compiler intrinsics behind `<intrin.h>` on MSVC, and maps to
`__atomic_*` seq-cst operations on GCC/Clang. Bit-preserving conversions centralize MSVC's unavoidable
`long *` intrinsic impedance mismatch without exposing `LONG` or importing `Windows.h`. The critical
contract — `Increment`/`Decrement` return the *new* value, `FetchAdd`/`Exchange`/`CompareExchange`
return the *old*; `CompareExchange` keeps Win32 `(dest,exchange,comparand)` order — now runs on MSVC
as well as GCC/Clang, including high-bit/wrap and pointer exchange cases. The temporary non-MSVC
`Interlocked*` aliases have been removed, and the executable engine census is now **zero direct
`Interlocked` calls**. Shared fast locks, worker queues, database/script/XAnim/DObj/EffectsCore state,
and renderer reservations use exact-width storage and portable operations. *M1 open tail:* migrate the
remaining raw volatile polling, Windows `LONG` storage, platform-header coupling, and native-layout
assumptions that do not appear in the direct-call census. The bare `sizeof(T)==0x..` CI tripwire (§9)
continues to freeze and burn down M4 layout debt.

**M3 (native time/synchronization/event/thread-lifecycle services landed) — platform ownership is
explicit, but the engine remains gated.**
`src/qcommon/sys_sync.h` now owns the exact fixed-width MP/SP `CriticalSection` IDs, the 8-byte
`FastCriticalSection` contract, and the existing lock API; `src/qcommon/sys_time.h` owns the clock
and sleep/yield API. The database public header no longer imports `win_local.h` or exposes the
private `_OVERLAPPED` callback, and the platform-neutral
`Sys_SnapVector` implementation no longer lives in the Windows timing translation unit. CMake now
selects target-neutral engine, headless, and service source variables: Win32 republishes its exact
working engine lists plus native `timeGetTime`/`Sleep` and `INIT_ONCE`/`CRITICAL_SECTION` services;
Linux/macOS retain empty engine/headless lists but select `CLOCK_MONOTONIC`/`nanosleep` and
`pthread_once`/recursive-pthread services. An opaque event layer additionally maps Win32 event
objects and a POSIX condition-variable state machine with manual/auto reset, assigned-wake, poll,
timeout, and infinite-wait parity; all high-level event consumers use it, and public thread headers
no longer expose `Windows.h`, `DWORD`, or `HANDLE`. Tests freeze both profile ABIs and calling
conventions,
require one-copy source composition, reject Win32 sources in non-Windows sets, and exercise
concurrent first initialization, recursion, contention, and time progress. This is a working native
service slice, **not** a claim that a POSIX engine target builds. `FastCriticalSection` readers in
`dvar.cpp` and `db_registry.cpp` now use the same sequentially consistent helper contract as writers;
reader/writer stress tests and source guards prevent direct volatile polling from returning. That
migration also fixed the no-match read-lock leak in `DB_IsXAssetDefault`. The wider M1 Interlocked/
`LONG` inventory still needs fixed-width adoption before POSIX/ARM64 engine compilation. An opaque
thread lifecycle now adds native Win32/POSIX current-thread capture, suspended creation with a
one-shot start gate, handle identity, bounded/infinite join, and explicit destruction without
publishing `HANDLE` or `pthread_t`; runtime tests exercise ordering, identity, timeouts, completion
visibility, and repeated cleanup on all five native utility targets. A shared four-state worker gate
now supplies cooperative pause requests, parked acknowledgements, and directional resume signals;
integrated stress tests cover waiting and in-flight tasks, queued work while parked, rapid stale-event
cycles, and independent workers under ThreadSanitizer. Both renderer workers now call that gate only
at command-free boundaries; controller transitions wake their command wait and do not return from a
disable until `Parked` is published. This removes debugger-oriented `SuspendThread` from normal
operation and fixes the worker entry's incompatible function-pointer cast. Scheduling policy now
uses fixed-width result enums and backend-owned eligible-processor ordinals: Linux snapshots its
sparse allowed cpuset dynamically, macOS reports hard pinning unsupported, and Windows keeps its
native group mask private. Priority hints are likewise truthful rather than silently ignored. A
terminal-only crash-freeze call is deliberately separate and has no resume operation. High-level
orchestration now stores only opaque handles, passes pointer-safe start records through the native
trampoline, uses handle identity, and applies the backend scheduling policy. `threads.cpp` no longer
includes Windows headers or calls native threading/Interlocked APIs; SP pointer-to-int returns and
four callback casts were removed. Next, finish broader fixed-width atomics, then add filesystem/
virtual-memory, console/process, and BSD sockets.

The first follow-on atomic cleanup also moved dvar sorting off `LONG`/Interlocked and the Win32
network sleep wrapper. Two private seq-cst boolean atomics now provide sorter ownership and sorted-
array publication across concurrent read-lock holders, while the write lock invalidates publication
when a dvar is registered. Regression guards forbid raw access and require sort-before-publish-before-
release ordering.

The next fixed-width batch moved all 24 script string/vector Interlocked sites behind that boundary.
`RefString` is now one explicit aligned `uint32_t` packed word rather than an atomically modified union
aliased through non-atomic bitfields. A standalone CAS protocol combines user-bit claims with their
reference increment, retries reference-to-user transfers from the observed word, rejects 16-bit
underflow/overflow, preserves the encoded length, and gives exactly one remover the zero transition.
The generic last decrement is reserved for the hash-lock owner, so lookup cannot observe a linked
zero-count entry; shutdown clears a user and its owned reference in one CAS. Moving an owner onto an
already-present destination consumes the duplicate packed/debug reference instead of leaking it.
This fixes the prior same-user double increment, transfer leaks, lost user-bit RMW, post-free debug
counter race, and error paths that could unwind while retaining the recursive string lock.
Thirty-two-way claim/remove contention, same/disjoint user removal, transfer races, bit preservation,
and invalid bounds run under the five native utility targets and local TSan. Leak initialization now
clears the whole debug state (including `ignoreLeaks`), and vector debug indexing uses the memory-tree
stride with bounds/alignment validation. `RefVector` now spells its 16/8/8-bit header fields explicitly;
its local
16-bit lifetime remains serialized by the script VM, while only global/debug counters are atomic.
`scrVarPub_t` now freezes the vector counter at its distinct 32/64-bit runtime offsets instead of
asserting the 32-bit total size on every architecture.

The next batch moved XAnim tree overlap counters and DObj locking/lifecycle onto the same exact-width
boundary. DObj create/clone reserves construction state and publishes last, free and source cloning
own the lock, object maps publish only complete instances, and archive/unarchive copies one exact,
initialized native-width saved record inside the existing render-thread-quiesced DB window. Model
pointer/parent storage now scales with pointer width and forces an aligned buddy block for the
one-model 64-bit case; clone no longer copies a transient lock, and the prior native-size archive
formula can no longer over-read the stack. `XAnimEntry`, `XAnim_s`, `XAnimTree_s`, and `DObj_s` have
dual 32/64-bit layout contracts. Variable XAnim tables use checked `offsetof + count * sizeof(entry)`
allocation and pointer-width debug tables; SP preview buffers/clone traversal, tracked pool sizes,
and corpse-tree/metadata traversal no longer use x86 byte strides or pointer-to-int writes. This is
not a complete XAnim payload widening: `XAnimIndices`/`XAnimParts`, the 88-byte clone allocation, and
matching load-object assumptions remain frozen to the retail layout and must be split into disk32
mirrors plus a native 0x88 runtime `XAnimParts` before a 64-bit engine TU can compile.

Corrective/XAnim-DObj commit `f2159da` passed all nine CI jobs in run 29176960257. The next landed
diagnostic batch moved 12 mark-generation and local-entity overlap-counter calls onto exact-width
`Sys_Atomic*` words while preserving balanced diagnostic entry/exit accounting; commit `c400a27`
passed all nine jobs in run 29177286439. These counters detect overlap and do not claim to serialize
the underlying renderer or cgame operations.

Database I/O/recovery commit `cfd9045` replaces another 20 executable native atomic calls. A
standalone-tested `FileReadState` publishes error/byte results before completion and rejects invalid
completion sizes; `ProgressState` provides coherent snapshots, bounded fractions, rebasing, and
checked negative/overflow updates without mixed atomic/raw access. A tested `AssetRecoveryGate`
preserves the database safe point across back-to-back lost-device recoveries and rechecks before
asset use. Zone queues reserve producer ownership, publish initialized entries before wake-up,
atomically claim each batch once, and reject
replacement, capacity overflow, and loading-asset underflow. Minimum-fast-file, initialization,
loading-zone, and loading-asset state now have explicit atomic ownership, while the lost-device
recovery handshake claims and releases its safe state with yielding waits. Buffered overlapped file
opens remove the previous unbuffered sector-alignment contract that the ring allocation did not meet;
file handles, zone requests, read-buffer cursors, and vertex/index pointer-offset conversion also gain
runtime validation. The full portable suite is 18/18 locally under GCC, Clang, GCC ASan/UBSan, and
GCC TSan; all nine Windows/portable CI jobs passed in run 29177998144.

IWD/loopback commit `aa91d37` removes three more native atomic calls. Canonical IWD-handle
ownership and reference publication use fixed-width atomics, while contended readers open an
independent archive instead of copying a live unzip cursor. Runtime layout contracts and native
allocation/sort widths cover IWD, file-handle, directory, and search-path records. Checked two-pass
archive construction rejects partial traversal, long or embedded-NUL names, overflow/allocation
failure, inconsistent name extents, and invalid cached positions; shutdown now closes actual handles
and resets its IWD count. The unzip reader rejects short scalar reads, malformed EOCD/bounds,
allocation/inflate failure, and partial publication, and the unsafe `unzReOpen` API is gone. Per-
queue locks serialize loopback payload/cursor publication across wraparound, with bounded routes,
packet sizes, destinations, and unaligned marker reads. Fake-lag queueing validates release-build
inputs and allocations, preserves caller receive capacity, rejects inconsistent metadata, and
clears complete retired slots. Source guards freeze these contracts, and the full 18-test local
GCC/Clang/ASan/UBSan/TSan matrix passes. Its first Windows run exposed two MSVC-only fake-lag
`char *` to `uint8_t *` conversions; corrective commit `0a119b7` made the reinterpretation explicit,
and replacement run 29195736931 passed all nine jobs. Remaining adjacent debt is explicit:
path-based clone opens retain a compatible-replacement file-identity TOCTOU, unzip CRC enforcement is
disabled, and native-`unsigned long`/signed-int file-size records cap safe archives below 2 GiB.

Skeleton/pose commit `060e6ba` removes another three native calls and closes the adjacent arena
publication defects. A shared platform-neutral helper derives each arena's aligned base and exact
capacity from its real backing array; checked arithmetic and CAS reservation reject invalid
alignment, size overflow, corrupt/misaligned cursors, exhaustion, and out-of-range requests without
advancing the cursor or forming a pointer first. Client and server resets publish the base and empty
cursor before advancing their exact-width epoch. Before a complete 32-bit cycle reuses epoch 1, all
affected client/server DObj skeletons are cleared, preventing a dormant timestamp from accepting an
old arena pointer. A private exact-width guard serializes the entire reset publication scope, and the
clear callback runs inside the epoch CAS retry loop, so contending resetters cannot race or publish
epoch one without first invalidating the old generation; server creation also reloads the epoch after
an allocation-triggered reset and drops on an impossible allocation failure rather than reporting an
unusable matrix as ready. Once-per-epoch warning claims are atomic. Every SP/MP `cpose_t::cullIn`
access now uses a shared CAS/store/exchange/load protocol that preserves culled priority and cannot
erase a racing producer with a split load/reset. Dual 32/64-bit layout contracts, repository-wide
source guards, exact-
capacity/corrupt-cursor/rollover tests, eight-thread arena contention, and pose producer/consumer
races pass in the full 20-test GCC, Clang, Clang ASan/UBSan, and Clang TSan matrix; all nine jobs then
passed in run 29196678355. Skeleton-worker quiescence during reset remains an
external lifecycle contract; resetter serialization is enforced, but the inherited
`allowedAllocSkel` state is still unused and does not enforce worker quiescence.

The bounded renderer-reservation batch then removes all five native calls from `r_drawsurf.cpp`.
One exact-width CAS helper grants non-overlapping fixed-array ranges without overflow, overshoot, or
counter poisoning and permits exact-capacity use. FX regions and release-build inputs are checked;
scene/backend counters, resetters, merge consumers, and backend readers share one atomic boundary;
and malformed stage, code-mesh record, argument, triangle, and index extents fail closed before
backing-array access. Single-index and multi-element eight-thread contention tests bring the full
local GCC, Clang, Clang ASan/UBSan, and Clang TSan matrix to 21/21. Commit `0fddf2d` passed all nine
jobs in run 29197855220.

The renderer worker-queue batch removes another 25 native calls and closes both the lossy CAS-min
priority race and a wrapped read-cursor ABA that could execute a stale command twice. A deterministic
17-type scan replaces the hint. Short exact-width producer/consumer guards serialize payload copy
and cursor publication only; handlers stay parallel. One outstanding count covers queue submission,
execution, recursive FX generation, and full-queue inline fallback until completion. Unconditional
notifications plus predicate rechecks remove waiter-count signaling races while retaining the shared
event's existing 1 ms bounded poll. All 17 payloads use compile-time traits, native-size buffers,
dual-width layouts, aligned bounded dequeue storage, and typed dispatch; shadow-cookie, DPVS entity,
lighting-handle, and timeout-callback narrowing is removed. The 22-test GCC/Clang/ASan/UBSan/TSan
matrix includes eight-producer/eight-consumer exact-once wrap stress. Run 29199400717 passed headless
plus all five portable architectures, then exposed one
MSVC-only const mismatch in the three client builds: the widened cached-lighting pointer targets a
handle that the lighting cache mutates. `R_AddDObjToScene` now declares that pose parameter mutable;
corrective commit `33bdd81` passed all nine jobs in run 29199666846. Worker/event shutdown remains absent;
the inherited full-ring inline path may overtake older same-type work, and handler longjmp bypasses
normal completion accounting, so both remain runtime/error-unwind gates.

The DObj/model-surface batch removes the unchecked fixed stack-record overlay and retail pointer-
bearing strides. A shared native-width planner/cursor now bounds and aligns exact scene/vertex
reservations; producers preflight selected LOD, bone, record, and output extents and publish only
complete descriptors. Workers and scene walkers validate owner frame, published cursors, exact
framing, output contiguity, material/surface identity, and required bones. Fast-file completion now
validates XSurface buckets/weights/rigid coverage plus XModel skeleton parents, classifications,
exact LOD coverage, materials, and collision spans/bones. The 23-test GCC/Clang/ASan/UBSan/TSan
matrix is green, and subsequent batches reduced the executable direct-`Interlocked` census to zero.
All-target run 29250761031 passes the five portable jobs and all four Windows engine variants for the
preceding filesystem/final-atomic baseline; EffectsCore runtime-hardening PR #2 merged at `036ddaf8`
after run 29277249156 passed all nine jobs. Its 30 local tests also pass under GCC, Clang,
ASan/UBSan, and TSan, plus strict AArch64 cross-compilation. EffectsCore now has exact-width runtime layouts, portable atomic
operations, cooperative iterator/GC protocols, bounded visibility publication, transactional freelists,
fixed-size allocation sidecars, atomic active counts, and native-size handle codecs. Archive restore
transactionally reconstructs all three ownership maps from bounded acyclic freelists and rejects count
mismatches; archive, draw/update/profile/sort, spawn, removal, GC, kill, and rewind paths validate their
complete traversed state before publication. A writer-intent gate, durable lifecycle marker, bounded
packed-status CAS helpers, retained owner-subtree preflight, restart-root transaction, and explicit
longjmp unwind prevent resurrection, concurrent traversal/mutation, adjacent-field carry, and abandoned
locks/gates. Explicit signed trail bytes preserve compressed basis data on Linux ARM64. PR #3 replaced
the loose-file missing-effect raw clone with a checked native-width typed alias and merged at
`facbfb12` after run 29279924536 passed all nine jobs; its 31-test GCC/Clang/sanitizer matrix and strict
AArch64 syntax check were also green. PR #4 added a private noncopyable native-body
sidecar with full-width generation tokens, semantic validation, lifetime-bound ownership-preserving
staged publication/rollback, and a fallible pre-freelist pool callback without changing `FxElem`'s
0x28-byte layout. Its 32-test GCC/Clang/ASan/UBSan/TSan matrix and strict AArch64 syntax check are
green; both automated review fixes landed and run 29286377602 passed all nine jobs before merge at
`3c542f20`. PR #11 later completed spawn/draw/free/reset/archive integration; the full-capacity recipe
transaction is summarized below. Its prerequisite was allocation-failure-safe ODE body/user-data/model-
collision construction. PR #7 closes the audited exhaustion paths with
a portable resource-pair transaction used for body/user-data and primary/optional-transform acquisition,
a checked fresh body-plus-model API that destroys every partial resource before failure is observable,
and FX archive staging that publishes only complete collision bodies. Allocation precedes center-of-mass
mutation, the legacy wrapper retains deterministic space ordering, fallback heap bodies use matching
`new`/`delete`, and adjacent brush-pointer/wake-timestamp truncations are removed. Review also widened
native ODE brush class data from the x86-only 16 bytes to an aligned 16/24-byte payload with exact
x86/64-bit layout and shared-slot-fit assertions. Archive restore now keeps the physics lock across staged
body construction, FX graph publication, validation, and commit or rollback; the archive gate plus a
post-acquisition allocator drain barrier makes its brief nested publication lock safe. Function-scoped
source contracts enforce callback rollback, allocation-before-COM mutation, and that continuous archive
interval. The portable transaction test injects failures into the shared primitive (not the full ODE
pools); the 36-test GCC/Clang/ASan/UBSan/TSan matrix is green, with focused storage tests also clean under
x86-32/AArch64 compilation. It merged at `580b93bb` after run **29291013134 passed all nine jobs**.
The current M4 batch removes the generic allocator's `uint32_t` freelist serialization while preserving
the retail 8-byte x86 `pooldata_t` layout. A base/stride/count descriptor accompanies every operation;
native-width links use `memcpy`, bounded exact-slot validation covers invalid extents and overflow, and
checked queries return status plus value without aliasable output pointers. Pre-review commit `202cce76`
passed all nine jobs in run **29293356200**. Gemini review then found that hot allocation/free still walked
the complete inactive chain and that the `PhysGlob` tracking-size guard was runtime-only. The review
fix keeps ownership and link provenance in an external per-slot shadow control: `Pool_Alloc`, `Pool_Free`,
and `Pool_GetFreeCount` are O(1), validate active metadata plus the touched node/link, and reject
foreign/interior active pointers, duplicate frees, and count divergence transactionally. An O(1) hot
operation cannot inspect arbitrary dormant tail bytes, so the explicit bounded `Pool_ValidateFull` owns
short/long-chain and cycle detection at FX archive-capacity and ODE leak-diagnostic boundaries. All body,
geom, userdata, FX-capacity, and ODE leak callers provide their real extents and shared shadow controls.
This also repairs the previously hidden native64 `PhysObjUserData` overlap:
the record is 0x70 bytes on x86 but 0x78 on native64, so its pool no longer advances by the hard-coded x86
stride. ODE geom backing remains explicitly transform-aligned. The review-fix 39-test GCC, Clang,
ASan/UBSan, and TSan suites are green, as are strict x86-32 and AArch64 allocator compile/link checks.
Native
engine source sets still do not compile these production callers, so Windows x86 engine CI remains their
compile gate while all five portable jobs exercise the allocator contract. PR #9 merged at `8ce11763`
after replacement run **29300663478 passed all nine jobs** and all review threads were resolved. PR #11
then merged generation-checked native-body ownership through live FX spawn/draw/free/reset/shutdown and
legacy-x86 archive replacement at `da273589`; run **29335570405 passed all nine jobs**. PR #12 merged the
full-capacity transaction at `a9994b6b` after run **29355001881 passed all nine jobs**. It captures exact
rollback recipes and complete silent ODE topology, retires only the old FX bodies required by the 512-body
global ceiling, reconstructs them on failed publication, preserves
archive iterator exclusion across both graph images, and atomically drains all three sidecars before a
canonical safe-empty reset. Unexpected cleanup failure after ownership transfer fail-stops before admission
can reopen. Its portable matrix is **42/42** under GCC, Clang, ASan/UBSan, and TSan, with strict x86-32 and
AArch64 sidecar/capacity compile-link checks; Windows x86 CI remains the production translation-unit gate.
Status-bearing resource-pair rollback now retains and reports a primary resource when fresh cleanup refuses;
both archive call sites fail-stop rather than losing that ownership, and diagnostic rollback holds recursive
PHYSICS exclusion across body/geometry destruction. The restore branch tree now lives in a pure,
engine-type-free controller with explicit success, recoverable-failure, and unsafe-failure results. Its
portable executable fixture injects every result at every primary, live-graph recovery, snapshot-recovery,
commit-cleanup, and safe-empty operation; it verifies exact ordering, invalid-callback fail-closure, immediate
unsafe termination, and the single desired-publication success outcome. The production adapter retains archive
and PHYSICS exclusion around the complete synchronous controller call and translates an unsafe outcome to a
fail-stop before admission or scratch ownership can be released. Checked heap lifetimes now own rollback/control,
both transaction sidecars, ownership images, retirement planning, and pool-graph validation; a short-lived checked
heap image also covers malformed graph preflight before archive admission. Safe outcomes leave PHYSICS, end archive
admission, destroy scratch, and then free referenced buffers, while unsafe outcomes use the explicit `[[noreturn]]`
platform fatal boundary without cleanup. Wrapper/scratch parity, failure preservation/reuse, full capacity, and
source cleanup order are covered by the **44/44** GCC/Clang/ASan/UBSan/TSan matrix plus strict x86-32/AArch64
compile/link fixtures. PR #16 merged the portable normal-admission controller, production adapter, and strengthened
source contracts at `5455c778` after all nine jobs passed. Typed `Open`, `Pending`, and `Exclusive` gate values reject
unknown encodings, while durable `Pending`,
`PendingExclusive`, `Acquired`, and
`ExclusiveGateOnly` TLS phases preserve exact ownership through waiter cancellation, promotion rollback, partial
release retry, and error abandonment. Deterministic tests cover waiters, cancellation, rollback, every partial
release/error-abandon path, lifecycle generations, and corrupt-state validation. Begin rejects cooperative,
kill-exclusive, sort-exclusive, effect-lock, self, and otherwise non-idle ownership; cleanup releases iterator
ownership before reopening the gate, and corrupt nominally `Idle` state reaches fail-stop validation. The
prechecked kill race accepts only known `Open`/`Pending` values, pool rebuild/validation requires exact `Acquired`
proof to bypass admission, and safe-empty reset performs checked generation refresh. The separate reset/init/
shutdown two-gate lifecycle protocol is unchanged. Local validation is **45/45** under GCC, Clang, ASan/UBSan
(leak detection disabled), and TSan; strict x86-32 and AArch64 controller compile/link checks and all three focused
source scripts pass. Two independent audits found and fixed three concrete fail-closed issues and found no
remaining PR-scope issue. PR #17 completed the next prerequisite: an
exact 512-body/512-userdata/2,048-geom competing-occupancy fixture and intrinsically silent production ODE
create/model-collision/inertial/bullet-impact/destroy transactions. CG SP/MP, DynEnt, and live FX retain PHYSICS through
rollback or ownership publication, then report outside the lock; unrecoverable cleanup and ambiguous sidecar ownership
fail-stop only after unlocking. FX structurally validates its complete sidecar from a bounded BSS workspace before body
allocation and reuses that workspace for binding. Exact legacy mass/COM/list order, bullet arithmetic, the DynEnt
DYNENT-parameter/FX-body world split, and RNG consumption are pinned by source and executable contracts. Two independent
final audits approve the x86 scope, and all **47/47** tests pass under GCC, Clang, ASan+UBSan, and TSan. PR #17 merged
at `288c2b78` after replacement run **29382870200 passed all nine jobs**.

PR #18 completed the no-longjmp archive-input prerequisite. Production
`MemoryFile` now exposes report-free status-bearing data and bounded-C-string reads over the actual legacy RLE/zlib
decoder. Explicit little-endian headers and bounded segment discovery reject malformed length chains and prevent a read
from crossing into its successor; failure poisons the reader and releases the singleton decoder, while invalid arguments
do not mutate outputs. `FX_ReadArchiveDataNoDrop` uses this boundary directly. A TLS owner sidecar lets both global
`Com_Error` longjmp paths abandon same-thread inflate/deflate state before unwinding, remains active in headless builds,
and prevents a foreign error path from touching the global codec. Runtime tests cover handcrafted codec parity, raw and
compressed round trips, corruption/truncation, exact C-string capacities, partial writes, sticky overflow, foreign-thread
isolation, semantic-error abandonment, and immediate read/write reuse. The complete **48/48** GCC, Clang, ASan+UBSan,
and TSan suites pass; strict x86-32 and AArch64 compile/link pass; and two independent audits report no blocker.

PR #19 completed the effect-table boundary at `885ec28a`. It removes the uninitialized truncated-key path and
8,196-byte x86 / 16,392-byte native64 stack table in favor of a bounded BSS lease. Parsing is silent and complete before
registration; exact pointer-cookie/TLS/serial ownership plus Active-to-Closing cleanup makes stale, nested, foreign, and
longjmp abandonment non-destructive. Archive/lifecycle gate handshakes exclude concurrent mutation, pool ownership
validates before staged effect-pointer fixup, and the captured generation gates later archive admission. Asset names reject
traversal, Win32-invalid bytes, normalization aliases, and reserved DOS device components. Its raw/zlib fixtures cover
1,024 records, late 0--3-byte key truncation, duplicate/registration policy, reentry, abandonment, stale/foreign ownership,
reuse, and contention; all nine CI jobs passed in run **29387860025**.

PR #29 closed the live-publication pointer-provenance seam and squash-merged as `559cad41`.
Desired and rollback visibility roles are captured as one bounded, zero-invalid selector pair, rebound only to their own
staged buffers, and round-tripped before controller admission. Publication resolves fresh destination pointers, copies
buffers before the system image under archive/allocator exclusion, relinks the system, assigns read then write roles, and
round-trips the result before admitting the graph. Rollback capture derives and rebinds its pair inside one coherent
`FX_ALLOC` interval; safe-empty recovery remains canonical read-zero/write-one. The complete local GCC/Clang and sanitizer
matrices, strict x86-32/AArch64 compilation, Clang analysis, and focused source/security contracts are green. Exact
implementation/review-fix head `0fbee229` passed all nine CI jobs in run **29445375084** after both actionable Gemini
null-context findings were fixed, answered, and resolved. Final documentation head `4fdc0ba7` passed all nine jobs again
in run **29446277872** before merge. At that historical merge, production wire I/O, both native64 guards, and the writer
remained unchanged. PR #30 then merged the non-publishing reader prerequisite, and the current branch has now switched
production restore to it; only the save-side guard and writer remain.

Overall porting progress is approximately **49%** (plausible range **46–54%**). The foundation/checklist view is about
**61%**, the shared foundation is **95%+** mature, and target delivery remains **0/5**.
Bounded save-side definition capture and portable x86/native64 stack/runtime ceilings are implemented. Source-scoped
Windows x86 Debug and Release production reports now enforce 2,756-byte `FX_Save`, 6,124-byte `FX_Restore`, and
2,064-byte maximum-other frames after replacing the discovered 10,256-byte helper with checked heap scratch. Coherent
camera/scalar/visibility publication, copied-image validation, visibility selectors, and staged effect-definition
membership passed all nine jobs in PR #21 run **29397910131** at implementation head `7895f7a9`; Codex found no major
issue at that exact commit and the sole Gemini finding was fixed and resolved; final documentation run **29414351528**
also passed all nine jobs before squash merge `0f878ff4`. The reader-first Disk32 FX key/effect-record, fixed system, and
fixed buffer/raw-free-list batches, heap-owned structural native conversion, and definition-aware semantic `Ready`
finalization are implemented and locally validated. PR #30 left them non-production; the current branch now supplies the
production native64 restore reader/candidate path, while a portable production writer remains outstanding. The shared
semantic oracle uses callback-free preflight, representation-preserving union activation, bounded physics descriptors,
and failure-to-Empty publication gating. The production physics collector now delegates to that oracle through a bounded
sink, and restore retains definition ownership through both semantic passes before generation-checked archive admission.
The merged portable-reader checkpoint passes **66/66** GCC and Clang suites plus **65/65** ASan+UBSan and TSan suites,
strict GCC/Clang x86-32 and AArch64 compilation, Clang analysis, source/security/ABI contracts, and independent audits; the
exact PR #30 checkpoint head `42d1c4bb` also passed all nine five-target/measured-Windows jobs in run **29449586954**.
Gemini reviewed that head with no comments or additional feedback, and the thread-aware query is empty. Final documentation
head `6ce201f4` passed all nine jobs again in run **29450294896**, and PR #30 squash-merged as `7cbe7070`. The merged
sequence implements a 670,976-byte x86 / 695,640-byte native64 heap-owned,
non-publishing workspace with a lightweight `BodyState` header, exact `BodyStateDisk32` decode, Ready-only physics
enumeration, exact definition-lease validation, and transactional raw/zlib staging of the complete legacy
system/buffers/address/body tail. Partial reads, callback prefixes, stale leases, and failed views remain hidden behind a
final `Ready` gate; the reader validates but never acquires or releases the caller-retained lease, and retry uses
fresh/repositioned input. Generic `Phys_ObjSave` now zero-initializes the complete record and
`Phys_GetStateFromBody` assigns the whole `underwater` word, closing the three-byte stack disclosure while the reader keeps
legacy low-byte compatibility. The active `agent/fx-archive-disk32-production-restore` branch completes the restore-side
integration in four commits: coverage `78a14fbc`, source contracts `0f689d9b`, the independent mutable candidate
`8d94e7c5`, and the production switch `e1174d33`. Candidate construction holds the reader gate across validation and copy,
binds the exact private lease identity, revalidates graph metadata/semantics/physics, and remaps all candidate-local
pointers before publishing `Ready`. Production centrally owns and cleans both checked heap workspaces plus physics,
rollback, and controller staging; it destroys the reader, validates and releases the lease, then immediately calls
generation-checked `FX_BeginArchive` before the existing publication/rollback/safe-empty controller consumes copied asset
identities. Ready-view access independently requires and revalidates the exact active lease, including its full serial,
while the operation gate rejects callback reentry; failure preserves the caller's output. The reader is 670,976 bytes on
x86 / 695,640 bytes on native64 and the candidate is 376,240 / 400,904 bytes.
The old raw restore parser, restore width/ABI/address guard and relocation path, and native64 restore guard are gone. The
save guard, legacy writer, wire format, and licensed workflow are unchanged. The complete local GCC suite is **67/67**
green and the complete Clang suite is **67/67** green; the complete ASan+UBSan and TSan suites are **66/66** green with
only the compiler-generated stack-usage test omitted under instrumentation. Strict i386 compilation/linking passes although sandbox execution is blocked
by `SIGSYS`; AArch64 linking, Clang analysis, all focused source/security contracts, stack/ABI checks, and independent
audits pass. Initial PR #31 head `cfc3454a` passed all nine required jobs in run **29452814892**, including all four
measured Windows x86 engine variants. Review hardening binds Ready views to the exact still-active lease and tests
same-owner lease reacquisition; Gemini's valid const-correctness cleanup is applied, while its null-cleanup and claimed
`BodyState`-padding reports are contradicted by pinned tests/layout contracts and have evidence-backed replies. All four
threads are resolved, and exact review-fix/documentation head `21dae5ca` passed all nine required jobs in run
**29453934377**. PR #31 squash-merged as `1a966369` from final documentation head `9fb7dafd`; post-merge run
**29454579529** also passed all nine jobs. The active branch now implements exact FX fast-file Disk32
definition/visual/trail/impact schemas plus separate report-free transactional effect-definition and impact-table
planner/materializers without touching the stateful production loader. Canonical native objects, frozen resolver
transactions, full source/name provenance, bounded journals, callback-free publication, retained-extent overlap rejection,
and adversarial mutation/alias tests are included. Current implementation/test head through `1153eefe` passes GCC and
Clang
**71/71**, ASan+UBSan and TSan **70/70**, a focused MemorySanitizer run, strict i386 compilation/linking, strict AArch64
compilation/linking, and the updated source contract. The sandbox blocks the i386 executable with `SIGSYS`, so Windows x86
CI remains the runtime authority. Prior CI run **29462535215** at `0f376a92` passed five jobs and exposed test-only MSVC
warning-as-error failures in the other four; their fixed-width/representation-safe corrections plus the semantic/runtime
hardening above are now present. Replacement run **29464935543** passed seven jobs, including both portable Windows targets
and the no-Steam/headless Windows x86 variants; measured Debug/Release exposed only one redundant test-fixture alignment,
fixed by `1153eefe`. Codex found no major issue at review head `e5b755a4`. Final replacement CI/merge are the current gate;
a zone-owned native arena and guarded XBlock/XAsset adapter are next. Writer replacement follows later after exact x86
full-image equivalence.
A checked
whole-segment compressed-finalization boundary remains a
later integrity item
because FX reads mid-segment and SND intentionally skips/copies segments. Remaining FX work is checked writer/save-guard
retirement, broader completed-object/fast-file conversion,
and that later segment-finalization boundary. A separate hard M4 blocker
remains: MP `cpose_t::physObjId` and
`BreakablePiece::physObjId` still truncate ODE pointers into `int32_t`; native-width storage or a token/sidecar is required
before any native64 engine runtime can be enabled. The
unbounded/alignment-unsafe `Buf_Read<T>` primitive instead has 114 consumers in XAnim/XModel and needs
a separate transactional `current/end` cursor migration. Detailed live blockers and sequencing remain in
`docs/task.md` and `docs/CODEBASE_AUDIT.md`.

**M3 — Windows-ARM64 D3D9on12 is "expected to work," not "just works"; `IDirectDraw7` is mis-scoped.**
`r_texturemem.cpp:14-86` queries VRAM via `IDirectDraw7` (`DirectDrawCreateEx`/`GetAvailableVidMem`),
which **D3D9on12 does not provide** any more than dxvk does — so Windows-ARM64 hits the same failure as
Linux/macOS. *Move the `IDirectDraw7` replacement into an all-target step*, and validate the D3D9on12
device-create + VRAM-query + double-`Direct3DCreate9` seam on the `windows-11-arm` runner.

**M4 — Fatal-error thread freeze is isolated but still needs POSIX mechanisms.**
`Sys_FreezeOtherThreadsForCrash` is now called only by `Sys_Error`; the abandoned SP executable-
handoff command no longer freezes threads or tears down D3D. Windows uses the terminal-only checked
backend operation, with no resume API. Linux still needs a preinstalled `tgkill`+`SIGRTMIN` handler
that only records context and parks on a futex/semaphore; macOS needs `mach thread_suspend` plus
`thread_get_state`. The crashing thread's stack walk must read suspended contexts and **never call
libc heap** from the signal handler, and must interlock with the ARM `backtrace()`/stack-capture work.
Add a deliberate worker-thread-assert test on all five targets.

**L1 — Non-headless dedicated server is Windows-only.** `scripts/dedi/dedi_sources.cmake` links
Bink/GFX_D3D/Miles/Speex unless `KISAK_DEDI_HEADLESS=ON`. State that **headless is the only supported
dedi on the four non-Windows targets**; future engine CI must build those legs with
`KISAK_DEDI_HEADLESS=ON`, while the current five target legs are utility tests only. Keep
non-headless dedi as a Windows-only legacy config.

**L2 — Localized-string assets ride the fast-file path.** `SE_GetString_LoadObj`
(`stringed_ingame.cpp:15-18`, `IsFastFileLoad()`) loads the `localizedString` asset through the same
fast-file machinery. Its asset pointer now uses typed alias provenance and its block-4 XStrings use
exact registered start/extent provenance, but the packed mirror/runtime widening still belongs in
the M5 inventory. The text `.str` path (`SE_LoadFileData` → `FS_ReadFile`) is bitness-neutral and
needs no work.

---

*See `docs/CODEBASE_AUDIT.md` for the full security / logic / functionality / build-tooling findings
that this plan references.*

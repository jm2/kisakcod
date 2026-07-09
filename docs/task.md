# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: checked derived-count arithmetic and relocation provenance after
  completing the first fast-file/BSP boundary-hardening pass.
- Last completed batch: checked generated array byte counts, unconditional stream
  and BSP spans, actual asynchronous read results, short/corrupt compressed input,
  bounded asset/script-string metadata, and overflow-safe fail-closed zone allocations.
- Portable validation: 9/9 tests pass locally, including checked database arithmetic
  and security source invariants. These tests do not execute the Windows loader.
- Windows validation: CI run 29058084282 passed x86 Debug, Release, no-Steam,
  and all five portable target jobs on 2026-07-09. The current loader batch still
  requires its own Windows CI run after push.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 builds; five native utility-test runners; engine runtime smoke and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, and portable compile tests exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 37 pointer fixes, tripwire, remote-input hardening, and the first loader/BSP boundary pass landed; sanitizers, derived-count arithmetic, and relocation provenance remain. |
| M3 platform services | Not started beyond CMake plumbing | No POSIX implementation or populated `src/_platform` tree. |
| M4 runtime 64-bit ABI | Seed only | Runtime structures and script VM remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed only | `disk32::PointerToken` exists; checked packed-mirror/widening loader does not. |
| M6-M14 target deliverables | Not started | No non-Windows or 64-bit engine target builds yet. |

## Target matrix

| Target | Engine status |
|---|---|
| Windows x86 | MP and legacy dedicated compile in Debug/Release; gameplay smoke still pending. |
| Windows amd64 | Utility tests only; engine gated by ABI/asset/dependency work. |
| Windows ARM64 | Utility tests only; engine gated by ABI, ARM, renderer, and dependency work. |
| Linux amd64 | Utility tests only; engine configuration intentionally blocked pending POSIX/headless work. |
| Linux arm64 | Utility tests only; same blockers plus ARM determinism/dependencies. |
| macOS arm64 | Utility tests only; same blockers plus SDL/Vulkan/MoltenVK application integration. |

## Immediate queue

1. Replace pre-overflowed fast-file products, sums, shifts, and increments with checked derived-count helpers.
2. Integrate or discard the unfinished bare-hex-sizeof tripwire left by the previous implementation session.
3. Design M5 typed relocation/alias validation, then add malformed production-path loader/BSP harnesses.
4. Add a Windows x86 headless compile/link CI leg and fix its unresolved client-symbol dependencies.
5. Finish M1 fixed-width atomics integration, then begin the M3 headless platform-services interface.

## Known release blockers

- Headless source composition is not compile/link-tested and retains 33 allowlisted client/media includes.
- Fast-file loading still has unchecked derived count expressions and lacks a
  production-path malformed-input test harness.
- Database-thread `Com_Error` handling is still process-fatal, so malformed
  fast-file rejection remains a denial-of-service boundary until zone rollback
  and recoverable database-thread error propagation are implemented.
- Fast-file offset aliases are not provenance-tracked, and typed relocations only
  prove that one destination byte is in-bounds; full relocation-table validation
  remains an M5 requirement.
- Top-level `ASSET_TYPE_XMODELPIECES` loading remains unsupported until its asset
  registration and shared-inline alias semantics are implemented and verified.
- SteamGameServer is not implemented; legacy non-headless dedicated Steam auth still uses the desktop client API.
- Master-server discovery and HTTP redirect downloads remain nonfunctional.
- Miles/Bink and zlib 1.1.4 must be removed or replaced before portable releases.
- Licensed gameplay smoke and release packaging workflows have not run.

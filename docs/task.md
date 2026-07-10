# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: replace the unfinished bare-sizeof scanner with an exact M1/M4 ABI
  debt ledger, then return to relocation provenance.
- Last completed batch: checked 39 generated loader count formulas across animation,
  models, water, effects, clipmaps, raw files, string tables, light grids, and DPVS;
  products, sums, differences, and ceiling division now reject negative/overflowed data.
- Portable validation: 9/9 tests pass locally, including checked database arithmetic
  and security source invariants. These tests do not execute the Windows loader.
- Windows validation: loader CI run 29058600306 passed x86 Debug, Release,
  no-Steam, and all five portable target jobs on 2026-07-09. The derived-count
  batch still requires its own Windows CI run after push.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 builds; five native utility-test runners; engine runtime smoke and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, and portable compile tests exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 37 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, and generated derived counts landed; sanitizers and relocation provenance remain. |
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

1. Replace the unfinished bare-hex-only scanner with a hex/decimal, expression-keyed ABI debt ledger.
2. Design M5 typed relocation/alias validation, then add malformed production-path loader/BSP harnesses.
3. Add a Windows x86 headless compile/link CI leg and fix its unresolved client-symbol dependencies.
4. Finish M1 fixed-width atomics integration and continue pointer-debt removal.
5. Begin the M3 headless platform-services interface.

## Known release blockers

- Headless source composition is not compile/link-tested and retains 33 allowlisted client/media includes.
- Fast-file loading lacks a production-path malformed-input test harness and
  typed relocation/alias provenance.
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

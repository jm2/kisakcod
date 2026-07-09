# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: security hardening of fast-file/BSP parsing before continuing M1/M2.
- Last completed batch: remote file-list/IWD bounds, network-facing format strings,
  Steam/GUID identity separation, authenticated Steam defaults, and Steam-free
  headless target configuration.
- Portable validation: 8/8 tests passed locally after the completed security batch.
- Windows validation: the 2026-07-09 run passed all five portable jobs but failed
  the three x86 engine builds on two MSVC-only syntax issues; the focused fixes
  are committed and the rerun is pending.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 builds; five native utility-test runners; engine runtime smoke and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, and portable compile tests exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 37 pointer fixes, tripwire, and first remote-input hardening batches landed; sanitizers and remaining pointer debt remain. |
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

1. Finish unconditional fast-file/BSP stream, count, and span validation with portable arithmetic tests.
2. Integrate or discard the unfinished bare-hex-sizeof tripwire left by the previous implementation session.
3. Add a Windows x86 headless compile/link CI leg and fix its unresolved client-symbol dependencies.
4. Finish M1 fixed-width atomics integration, then resume the M2 pointer-debt burndown.
5. Begin M3 with a minimal platform-services interface for headless process, console, time, filesystem, threads, and sockets.

## Known release blockers

- Headless source composition is not compile/link-tested and retains 33 allowlisted client/media includes.
- Fast-file loading still contains assert-only validation and unchecked generated array-size expressions.
- Fast-file offset aliases are not provenance-tracked, and typed relocations only
  prove that one destination byte is in-bounds; full relocation-table validation
  remains an M5 requirement.
- Top-level `ASSET_TYPE_XMODELPIECES` loading remains unsupported until its asset
  registration and shared-inline alias semantics are implemented and verified.
- SteamGameServer is not implemented; legacy non-headless dedicated Steam auth still uses the desktop client API.
- Master-server discovery and HTTP redirect downloads remain nonfunctional.
- Miles/Bink and zlib 1.1.4 must be removed or replaced before portable releases.
- Licensed gameplay smoke and release packaging workflows have not run.

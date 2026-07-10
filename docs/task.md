# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: extend exact completed-object provenance from material vertex declarations
  to shared techniques, vertex/pixel shaders, and texture arrays, then run cross-material
  table/state validation. Complete world AABB topology validation and the remaining 27
  legacy direct references follow.
- Last completed batch: inline material vertex declarations register their exact aligned
  block-4 start before their 100-byte disk32 extent is loaded, and publish only after
  structural validation and the renderer build pass. Direct references now require that
  exact completed start, object kind, schema extent, and a fully materialized span;
  pending/forward, interior, wrong-kind, wrong-schema, wrong-block, and unmaterialized
  references are rejected. The fixed serialized extent is explicitly separated from
  future native 64-bit `sizeof`, and identical declarations at distinct inline locations
  retain distinct serialized identities.
- Portable validation: 12/12 tests pass locally. The production relocation registry is
  also strict-warning clean under GCC/Clang and GCC ILP32 syntax checking; ASan/UBSan
  pass locally with leak detection disabled because LeakSanitizer cannot run under the
  command-runner ptrace environment. Portable tests do not execute the Windows stream
  adapter or media ownership paths.
- Windows validation: material-structure CI run 29067593151 passed x86 Debug, Release,
  no-Steam, and all five portable target jobs on 2026-07-09. The completed-declaration
  batch requires its own Windows CI run after push.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 builds; five native utility-test runners; engine runtime smoke and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, portable compile tests, and an exact 259-site ABI debt ledger exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 37 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, 23/50 bounded direct references, and pre-use material structure/argument validation landed; production-path fuzz fixtures and 27 direct relocations remain. |
| M3 platform services | Not started beyond CMake plumbing | No POSIX implementation or populated `src/_platform` tree. |
| M4 runtime 64-bit ABI | Seed only | Runtime structures and script VM remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed plus provenance registries | `disk32::PointerToken`, a native-width typed alias/completed-slot side table, 19 full-span raw/POD fields, exact registered direct strings, and one exact completed material object type exist; packed mirrors, 27 direct offsets, broader completed-object relocation, and runtime widening remain. |
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

1. Extend exact completion provenance to material techniques, vertex/pixel shaders, and
   texture arrays; add bounded cross-material table/state semantics, then implement the
   linear-time world AABB topology validator and resume the remaining 27
   raw/completed-object relocations.
2. Add a Windows x86 headless compile/link CI leg and fix its unresolved client-symbol dependencies.
3. Finish M1 fixed-width atomics integration and continue pointer-debt removal.
4. Classify and burn down the 255 direct and four formula-based ABI layout assertions.
5. Begin the M3 headless platform-services interface.

## Known release blockers

- Headless source composition is not compile/link-tested and retains 33 allowlisted client/media includes.
- Fast-file loading lacks a production-path malformed-input test harness and
  completed-object/type provenance for direct offsets.
- Inline material declarations, techniques, passes, and arguments receive pre-use
  structural validation, and shared vertex declarations now require exact completed-object
  provenance. Shared techniques, shaders, and texture arrays among the 27 remaining legacy
  offsets can still bypass corresponding checks. Add exact completion provenance, then
  require sorted texture/constant tables, bounded named-hash membership,
  `stateBitsEntry + passCount` spans, and safe remapped-technique relationships.
  Material `cameraRegion`, `sortKey`, and other derived
  runtime fields also need bounds or recomputation before asset publication.
- Material vertex declarations own one mutable set of up to 16 COM handles behind a shared
  `isLoaded` flag, without reference counts. Independently removing one technique set can
  release handles still referenced by another; verify or repair cross-technique-set lifetime
  ownership. D3D creation assumes fatal errors never return and has no partial-build rollback,
  while repeated identical inline declarations bypass load-object deduplication and can
  amplify COM allocations; add rollback plus a per-zone declaration/resource budget or deduplication.
- World AABB model indices are bounded, but serialized child offsets/counts, surface
  ranges, acyclic topology, and aggregate validation/runtime work are not. Validate each
  owning cell's complete flat tree before renderer traversal and cache or budget repeated
  validation of shared direct index lists.
- Database-thread `Com_Error` handling is still process-fatal, so malformed
  fast-file rejection remains a denial-of-service boundary until zone rollback
  and recoverable database-thread error propagation are implemented.
- Weapon registration still protects the 128-entry `bg_weaponDefs` table only with a
  release-disabled assertion after incrementing the index. Single-player's 128-entry
  editable accuracy-graph registry has the same assertion-only overflow pattern.
- Twenty-seven explicitly labeled legacy direct fast-file relocations still prove only
  one destination byte is in-bounds. Nineteen raw/POD fields, three string/holder fields,
  and one completed material object type now enforce bounded materialization or exact typed
  completion, but broader object/type provenance plus runtime widening remain M5 requirements.
  Already-materialized and registered-start enforcement intentionally rejects
  raw/string/completed-object forward references;
  compatibility still needs retail fast-file fixtures. Alias and direct provenance also
  need production-path Windows stream/media fixtures.
- The script-string hash uses only length for strings of 256 bytes or more. A malicious
  fast-file string list can therefore force quadratic same-length collision chains;
  changing it needs a determinism review plus a content hash/work-budget regression test.
- Top-level `ASSET_TYPE_XMODELPIECES` loading remains unsupported until its asset
  registration and shared-inline alias semantics are implemented and verified.
- SteamGameServer is not implemented; legacy non-headless dedicated Steam auth still uses the desktop client API.
- Master-server discovery and HTTP redirect downloads remain nonfunctional.
- Miles/Bink and zlib 1.1.4 must be removed or replaced before portable releases.
- Licensed gameplay smoke and release packaging workflows have not run.

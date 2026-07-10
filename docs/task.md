# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: resume the remaining 11 legacy direct references, then add a Windows x86
  headless compile/link CI leg.
- Last completed batch: shared sound files, speaker maps, sound-alias arrays, weapon bounce-sound
  tables, world sun lights, and string tables now register their exact serialized block-4 start,
  fix every nested field, validate a fixed disk32 schema, and publish only after completion.
  String-table completion deliberately precedes asset canonicalization so direct references retain
  legacy serialized-object identity. This batch also rejects invalid sound union tags, malformed
  speaker-map dimensions/levels, incomplete aliases, unsafe falloff-curve knot counts/graphs,
  nameless or dimensionally inconsistent string tables, and non-finite/invalid sun lights.
- Portable validation: 12/12 tests pass locally. The production relocation registry is
  also strict-warning clean under GCC/Clang and GCC ILP32 syntax checking; ASan/UBSan
  pass locally with leak detection disabled because LeakSanitizer cannot run under the
  command-runner ptrace environment. Portable tests do not execute the Windows stream
  adapter or media ownership paths.
- Windows validation: AABB warning repair run 29096212752 and bounded-direct-reference run
  29096704210 both passed all eight jobs: five portable targets and three Windows x86 engine
  builds. The exact completed-object batch requires its own run after push.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 builds; five native utility-test runners; engine runtime smoke and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, portable compile tests, an exact 259-site ABI debt ledger, and native-width database enumeration contexts exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 37 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, 39/50 bounded direct references, pre-publication material/sound/world graph and state validation, bounded runtime material consumers, and complete graphics-world AABB topology validation landed; production-path fuzz fixtures and 11 direct relocations remain. |
| M3 platform services | Not started beyond CMake plumbing | No POSIX implementation or populated `src/_platform` tree. |
| M4 runtime 64-bit ABI | Seed only | Runtime structures and script VM remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed plus provenance registries | `disk32::PointerToken`, a native-width typed alias/completed-slot side table, 23 full-span raw/POD fields, one bounded completed script-string-handle array, exact registered direct strings/holders, and 12 exact completed object types exist; packed mirrors, 11 direct offsets, broader completed-object relocation, and runtime widening remain. |
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

1. Resume the remaining 11 graph/composite relocations. Add deferred typed fixups for authentic
   forward references and derived adjacency extents rather than weakening materialization checks.
2. Add a Windows x86 headless compile/link CI leg and fix its unresolved client-symbol dependencies.
3. Finish M1 fixed-width atomics integration and continue pointer-debt removal.
4. Classify and burn down the 255 direct and four formula-based ABI layout assertions.
5. Begin the M3 headless platform-services interface.

## Known release blockers

- Headless source composition is not compile/link-tested and retains 33 allowlisted client/media includes.
- Fast-file loading lacks a production-path malformed-input test harness and
  completed-object/type provenance for direct offsets.
- Inline material declarations, techniques, passes, and arguments receive pre-use
  structural validation, and shared vertex declarations, techniques, both shader stages,
  nested water, and texture tables now require exact completed-object provenance. Complete
  graphs additionally enforce ordered tables, bounded named-hash membership,
  `stateBitsEntry + passCount` spans, decode-safe render states, and structurally safe current
  and future remaps. Runtime named lookups independently fail within their table if a later
  dynamic remap introduces a material-specific hash. Material `cameraRegion`, `sortKey`, and other derived
  runtime fields also need bounds or recomputation before asset publication.
- Techniques can be shared across technique sets, while their pass shaders and declarations
  store mutable renderer handles without technique-set ownership or reference counts.
  Independently removing one set can release handles still referenced through another;
  declaration `isLoaded` guards only double release, not live ownership. Verify or repair
  cross-technique-set lifetime ownership. Shader creation now scrubs serialized state,
  checks HRESULT, and rolls back partial outputs, but vertex-declaration construction still
  assumes fatal errors never return and has no partial-build rollback. Repeated identical
  inline declarations and shaders bypass load-object deduplication and can
  amplify COM allocations; add rollback plus a per-zone declaration/resource budget or deduplication.
- The shader loader bounds DX9 token traversal and stage/model envelopes but does not
  validate operand/register semantics; untrusted fast-files still expose the D3D driver's
  semantic validator. Require signed/trusted production assets or a complete validated
  translation path plus malformed-bytecode fuzzing before accepting arbitrary content.
- Graphics-world AABB topology and surface coverage are now validated end-to-end, but exact
  shared direct static-model index spans can still amplify repeated validation work; cache or
  budget identical spans. The separate clipmap `CollisionAabbTree` still needs material,
  child/partition, leaf-owner, finite-bounds, acyclic-topology, and depth validation before its
  recursive collision/physics consumers can treat untrusted maps as safe.
- Database-thread `Com_Error` handling is still process-fatal, so malformed
  fast-file rejection remains a denial-of-service boundary until zone rollback
  and recoverable database-thread error propagation are implemented.
- Fast-file enumeration still invokes arbitrary consumers while holding the database read
  count. Known image failures now defer their drop until after enumeration, but remaining
  callbacks must not longjmp; snapshot enumeration or add a general deferred-error mechanism
  before treating callbacks as recoverable failure boundaries.
- Weapon registration still protects the 128-entry `bg_weaponDefs` table only with a
  release-disabled assertion after incrementing the index. Single-player's 128-entry
  editable accuracy-graph registry has the same assertion-only overflow pattern.
- Eleven explicitly labeled legacy direct fast-file relocations still prove only
  one destination byte is in-bounds. Twenty-three raw/POD fields, three string/holder fields,
  twelve exact completed object types, and one completed-but-not-yet-typed bone-name array now
  enforce bounded materialization or exact typed completion, but broader graph/type provenance
  plus runtime widening remain M5 requirements. Physics brush-side plane, path-tree, and portal-cell
  tokens include known or plausible forward references and therefore need a deferred-fixup design
  before bounded resolution.
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

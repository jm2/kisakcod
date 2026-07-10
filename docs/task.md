# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: rerun all Windows x86 engine legs after repairing the second headless compiler layer
  and the normal-build declarations that had previously arrived transitively through client headers;
  then iterate on the first linker boundary before beginning the Linux platform-services boundary.
- Last completed batch: the second headless compile remediation gives shared declarations and math
  helpers explicit owners, removes Win32 scalar/intrinsic leakage from shared networking code, and
  keeps normal client command/voice dependencies explicit. The common lifecycle now preserves the
  server, database, filesystem, script, packet, and fake-lag paths while compiling client, renderer,
  sound, input, and UI work out of the headless profile. Headless startup directly loads the four
  authoritative MP fast-file zones plus an optional mod without loading `ui_mp`, fixes the dedicated
  dvar at mode 2, and retains bounded pre-console diagnostics. Console command queuing and event-copy
  bounds were also repaired. Five client/media include-debt entries were retired, leaving 29 tracked.
- Portable validation: 13/13 tests pass locally under GCC, Clang, and GCC ASan/UBSan, with leak
  detection disabled because LeakSanitizer cannot run under the command-runner ptrace environment.
  The production relocation registry is also strict-warning clean under GCC/Clang and GCC ILP32
  syntax checking. Portable tests do not execute the Windows stream adapter or media ownership paths.
- Windows validation: AABB warning repair run 29096212752, bounded-direct-reference run
  29096704210, and exact shared-object run 29098235207 each passed all eight jobs: five portable
  targets and three Windows x86 engine builds. Model-pieces run 29099309312 also passed all eight
  jobs. Surface run 29100892076 passed all five portable jobs but exposed an early-declaration
  error in the Windows-only surface validator. Physics/repair run 29102757297 then passed all eight
  jobs, confirming the parameterized model-bone-count repair and exact physics graph batch. Clipmap
  brush-graph run 29105491437, portal/cell run 29108651064, and path-data/tree run 29110801804
  also passed all eight jobs. Asset-admission run 29111550531 then passed all eight jobs with the
  new 13-test portable suite. Headless-gate run 29116266353 passed all eight established jobs and
  configured the new dependency-free target successfully, then failed during compilation because
  server-owned MP layouts and several shared constants still lived behind excluded client/cgame
  headers. First-remediation run 29120376492 passed all five portable jobs, but all four Windows
  engine jobs failed: the three established builds exposed normal-client declarations that had
  arrived transitively through removed headers, while the headless job reached a second layer of
  shared math, profiling, console, lifecycle, and preprocessor defects. The current batch maps every
  non-cascade diagnostic from those jobs to a direct fix; its MSVC rerun is the next gate.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 client/legacy-dedicated builds, a Release headless-dedicated compile/link gate, and five native utility-test runners exist; the headless target configures without client SDK setup and its first two compiler layers are repaired locally, while the next MSVC/link rerun, runtime smoke, and release workflows remain unexercised. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, portable compile tests, an exact 259-site ABI debt ledger, and native-width database enumeration contexts exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 43 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, all 50 direct references bounded, pre-publication material/sound/world/model/surface/physics/clipmap-brush/portal/path graph and state validation, build-mode-specific asset admission, bounded runtime material/collision consumers, and complete graphics-world AABB topology validation landed; production-path fuzz fixtures remain. |
| M3 platform services | Not started beyond CMake plumbing | No POSIX implementation or populated `src/_platform` tree. |
| M4 runtime 64-bit ABI | Seed only | Runtime structures and script VM remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed plus provenance registries | `disk32::PointerToken`, a native-width typed alias/completed-slot side table, all legacy direct references migrated to bounded resolution, 23 full-span raw/POD fields, one bounded completed script-string-handle array, exact registered direct strings/holders, graph-validated clipmap brush, portal/cell, and path-tree spans, and 18 exact completed object types exist; packed mirrors, broader completed-object relocation, and runtime widening remain. |
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

1. Rerun the Windows x86 headless compile/link CI leg, fix its next compile/link layer, and burn down unresolved client/media dependencies.
2. Begin the M3 headless platform-services interface.
3. Finish M1 fixed-width atomics integration and continue pointer-debt removal.
4. Classify and burn down the 255 direct and four formula-based ABI layout assertions.
5. Add production fast-file fixtures/fuzzing and assess stronger SP overlap/chain hierarchy semantics.

## Known release blockers

- Headless source composition configures successfully and has progressed through two compiler layers.
  All diagnostics from run 29120376492 are repaired locally and await CI confirmation; 29
  client/media includes remain allowlisted. The next expected boundary is linking: `scrDebuggerGlob`
  still embeds polymorphic UI objects whose vtables and core methods live in the excluded UI source
  group. A deliberate script-debugger core/state split is required rather than linking graphical UI
  code back into the server. Headless script-created console channels currently retain the default
  script channel because the client console filter graph is absent; extract a shared channel registry
  if per-channel filtering is required for dedicated administration.
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
- Rigid model-surface collision topology is validated, but deformed-surface blend bucket totals,
  bone-offset records, and weight semantics still need a pre-publication pass. Collision-tree
  scales intentionally allow positive infinity because the legacy source builder divides by zero
  on planar axes; fix and fixture-test degenerate-axis encoding before requiring finite scales.
- Model physics brushes and geometry lists now have exact completed provenance and source-builder
  bounds, and authentic forward side-plane references are deferred until their target array is
  materialized. Retail fast-file fixtures still need to confirm inline-plane encoding, and primitive
  orientation orthonormality plus positive/semi-definite mass properties remain compatibility-sensitive
  semantic checks rather than enforced invariants.
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
- All 50 direct fast-file references now use bounded resolution or validated owning-array fixups.
  Twenty-three raw/POD fields, three string/holder fields,
  eighteen exact completed object types, graph-validated clipmap brush spans, and one
  completed-but-not-yet-typed bone-name array now
  enforce bounded materialization or exact typed completion, but broader graph/type provenance
  plus runtime widening remain M5 requirements. Path-tree forward/back offsets now resolve only
  after the complete flat owner array materializes. The current IW3 asset writer confirms that the
  counted array is registered before reusable children become direct offsets; a retail SP fixture
  remains desirable as an end-to-end compatibility regression rather than an inline-format decision.
  Already-materialized and registered-start enforcement intentionally rejects
  raw/string/completed-object forward references;
  compatibility still needs retail fast-file fixtures. Alias and direct provenance also
  need production-path Windows stream/media fixtures.
- The script-string hash uses only length for strings of 256 bytes or more. A malicious
  fast-file string list can therefore force quadratic same-length collision chains;
  changing it needs a determinism review plus a content hash/work-budget regression test.
- Top-level `ASSET_TYPE_XMODELPIECES` loading remains unsupported until its redirectable asset
  registration and shared-inline alias semantics are implemented and verified; the exact embedded
  model-pieces provenance used by dynamic entities deliberately does not claim that support.
- SteamGameServer is not implemented; legacy non-headless dedicated Steam auth still uses the desktop client API.
- Master-server discovery and HTTP redirect downloads remain nonfunctional.
- Miles/Bink and zlib 1.1.4 must be removed or replaced before portable releases.
- Licensed gameplay smoke and release packaging workflows have not run.

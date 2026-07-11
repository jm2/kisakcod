# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Branch: `master`
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: migrate remaining engine creation/identity/priority/affinity consumers onto opaque
  thread handles, quarantine crash-only freezing, then continue fixed-width atomic adoption without
  relaxing the POSIX engine gate.
- Last completed batch: both renderer workers now use the cooperative gate at command-free safe
  points. Initial activation starts the native thread once; runtime dvar transitions request a pause,
  wake the command wait, and block until the worker publishes `Parked`. The old normal-operation
  `Sys_SuspendThread` path is gone, the wait counter is balanced before parking, and `R_WorkerThread`
  now has the exact fixed-width callback signature instead of relying on an incompatible cast.
- Previous worker-gate batch: an opaque `SysWorkerGateHandle` implements acknowledged cooperative
  renderer-worker pausing without native suspension. A four-state atomic protocol and separate
  directional auto-reset events prevent lost/stale wakeups. Integrated tests cover initial start,
  waiting and in-flight pauses, queued work while parked, duplicate transitions, 128 rapid cycles,
  two-worker independence, and orderly thread/gate teardown; repeated Clang and TSan runs are clean.
- Previous thread-lifecycle batch: an opaque `SysThreadHandle` contract selects checked Win32
  and POSIX backends. It captures the current thread, creates callbacks behind a one-shot start
  gate, compares current-thread identity without exposing `HANDLE`/`pthread_t`, supports bounded
  and infinite joins, and releases captured or joined handles explicitly. Runtime tests cover
  suspended-start ordering, parent/child and four-way identity isolation, timeout and completion
  visibility, destruction/nulling, and 32 repeated lifecycles on every native utility target.
- Previous event batch: an opaque `SysEventHandle` contract selects checked Win32 and POSIX
  event backends. The POSIX condition-variable state machine preserves sticky manual-reset signals,
  one-waiter auto reset, assigned wakes across Reset, zero/finite/infinite waits, and steady-clock
  timeouts. All 21 MP/SP event creations retain their original modes, raw event APIs are gone from
  `threads.cpp`, and the public thread/context headers no longer expose Windows types. Runtime tests
  cover auto/manual reset, multi-waiter behavior, timeout/poll, infinite wait, and destruction.
- Previous synchronization batch: `FastCriticalSection` exposes shared, fixed-width read/write
  helpers;
  all eight dvar and six database manual reader acquisitions use them, and source guards forbid
  direct counter polling. Concurrent readers/writers are stress-tested under TSan. The migration
  also repaired a real `DB_IsXAssetDefault` no-match read-lock leak that survived when its
  "unreachable" assertion returned instead of terminating the process.
- Previous M3 batch: the first native service backends implement wrap-compatible
  monotonic clocks, yield/sleep, recursive critical sections, and the common fast write lock.
  Windows uses `timeGetTime`/`Sleep`, `INIT_ONCE`, and `CRITICAL_SECTION`; Linux/macOS use
  `CLOCK_MONOTONIC`, EINTR-safe `nanosleep`, `pthread_once`, and recursive pthread mutexes. A
  race-safe runtime test covers concurrent first initialization, recursive entry, contention,
  fast-lock balance, and timing on the selected host backend.
- Contract/source batch: `sys_sync.h` owns the exact MP/SP critical-section IDs and fixed-width
  `FastCriticalSection` ABI, while `sys_time.h` owns the clock declarations without importing
  Win32. The database public header no longer imports `win_local.h` or exposes `_OVERLAPPED`.
  Platform CMake files publish explicit engine/headless/service source sets; Windows retains its
  exact working composition, while Linux/macOS keep empty, incomplete engine/headless sets and
  cannot inherit Win32 files. MP/SP contract tests and source-composition invariants cover the seam.
- Previous completed batch: the Windows x86 headless target now links with a database-scoped null
  GPU/audio backend while retaining validated CPU asset graphs and exact progress/ownership rules.
  The runtime follow-up honors `fs_basepath` for core and mod fast-files, preserves inherited output
  handles, suppresses GUI console/error paths, exits fatally with a nonzero status, retains the linked
  binary as a CI artifact, and adds a separately protected headless map/`getstatus` smoke alongside
  the legacy dedicated smoke. The smoke now requires the requested map in the status response, and
  its self-hosted jobs run only from `master` and check out the immutable dispatched SHA. Twenty-one
  client/media includes remain.
- Portable validation: 16/16 tests pass locally under GCC, Clang, and GCC ASan/UBSan, with leak
  detection disabled because LeakSanitizer cannot run under the command-runner ptrace environment.
  The integrated platform-service runtime also passes under GCC ThreadSanitizer.
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
  shared math, profiling, console, lifecycle, and preprocessor defects. Second-remediation run
  29121929895 passed all eight established jobs, proving the normal Release, Debug, and no-Steam
  repairs, and the headless target compiled every translation unit before failing at link with 106
  unresolved externals. The local script-debugger boundary removes the largest UI/vtable family;
  database GPU/audio realization and smaller collision, dynamic-entity, sound-alias, and load-object
  adapters as the remaining confirmed linker seams. Script-boundary run 29127753640 then passed all
  eight established jobs and reduced the headless link failure from 106 to 45 unresolved externals;
  every remaining symbol belongs to those exact database and small-adapter families now covered by
  the local null-resource batch. Null-resource run 29128702142 then passed all nine jobs: five
  portable targets, three established Windows x86 builds, and the first complete Windows x86
  headless dedicated compile and link. Runtime-hardening run 29129630878 subsequently passed all
  nine jobs and retained the hardened headless artifact. Platform-contract run 29130295757 also
  passed all nine jobs, including the two new ABI contract tests on all five portable targets.
  Native-service run 29131012290 then passed all nine jobs, executing the time/synchronization
  runtime backend on Windows amd64/ARM64, Linux amd64/arm64, and macOS arm64 while preserving all
  Windows x86 engine links and the retained headless artifact.
  Atomic-reader run 29134203963 likewise passed all nine jobs after the dvar/database migration and
  lock-leak repair. Event-service run 29134703222 then passed all nine jobs after the opaque event
  consumer migration, including both Windows architectures and all three POSIX utility targets.
  Thread-lifecycle run 29135266993 also passed all nine jobs, executing create/start/identity/join/
  destroy coverage on every native utility target while preserving all four Windows x86 links.
  The observed linker debt is now 106 -> 45 -> 0.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 client/legacy-dedicated builds, a green Release headless-dedicated compile/link gate, retained headless artifact, protected legacy/headless gameplay-smoke definitions, and five native utility-test runners exist. The licensed headless smoke has not run, and release workflows remain Windows x86-only. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, `sys_atomic.h`, portable compile tests, an exact 259-site ABI debt ledger, and native-width database enumeration contexts exist; engine atomics/platform integration remains. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 43 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, all 50 direct references bounded, pre-publication material/sound/world/model/surface/physics/clipmap-brush/portal/path graph and state validation, build-mode-specific asset admission, bounded runtime material/collision consumers, and complete graphics-world AABB topology validation landed; production-path fuzz fixtures remain. |
| M3 platform services | In progress: time/sync/event/thread lifecycle native | Portable contracts and target-owned source sets select tested native Win32/POSIX clock, sleep/yield, recursive/reader-write lock, opaque event/thread lifecycle, and a target-neutral cooperative worker gate now used by renderer workers. Public contracts are Windows/pthread-free. Linux/macOS engine/headless sets remain empty and engine-gated; remaining creation/identity plus priority/affinity, filesystem, process/console, and socket backends remain. |
| M4 runtime 64-bit ABI | Seed only | Runtime structures and script VM remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed plus provenance registries | `disk32::PointerToken`, a native-width typed alias/completed-slot side table, all legacy direct references migrated to bounded resolution, 23 full-span raw/POD fields, one bounded completed script-string-handle array, exact registered direct strings/holders, graph-validated clipmap brush, portal/cell, and path-tree spans, and 18 exact completed object types exist; packed mirrors, broader completed-object relocation, and runtime widening remain. |
| M6-M14 target deliverables | Not started | No non-Windows or 64-bit engine target builds yet. |

## Target matrix

| Target | Engine status |
|---|---|
| Windows x86 | MP and legacy dedicated compile in Debug/Release; dependency-free headless dedicated also links in Release; licensed gameplay smoke still pending. |
| Windows amd64 | Utility tests only; engine gated by ABI/asset/dependency work. |
| Windows ARM64 | Utility tests only; engine gated by ABI, ARM, renderer, and dependency work. |
| Linux amd64 | Utility tests only; engine configuration intentionally blocked pending POSIX/headless work. |
| Linux arm64 | Utility tests only; same blockers plus ARM determinism/dependencies. |
| macOS arm64 | Utility tests only; same blockers plus SDL/Vulkan/MoltenVK application integration. |

## Immediate queue

1. Validate the protected licensed-content headless startup/map/`getstatus` workflow on its runner.
2. Migrate high-level engine creation/identity to `SysThreadHandle`, add truthful priority and
   ordinal processor-policy backends, and quarantine terminal crash-only freezing.
3. Finish the broader M1 fixed-width atomic call-site migration, including dvar sorting and
   remaining `LONG`/volatile counters that are not part of `FastCriticalSection`.
4. Continue M1/M5 ABI cleanup and production fast-file fixtures/fuzzing.

## Known release blockers

- Headless source composition now configures, compiles, and links. Runs 29121929895, 29127753640,
  and 29128702142 reduced unresolved symbols from 106 to 45 to zero while keeping all established
  jobs green. The binary is not release-ready until the protected licensed-content startup/map-load
  smoke succeeds; the local runtime batch fixes its known base-path, redirected-output, GUI-error,
  and exit-status blockers, but that protected workflow has not yet run. Twenty-one client/media
  includes remain allowlisted. Headless script-created console channels currently retain the
  default script channel because the client console filter graph is absent; extract a shared channel
  registry if per-channel filtering is required for dedicated administration.
- The exact headless closure still contains 182 C++ and 19 C translation units. Twenty-six files
  directly include 36 Windows-only headers, and the remaining service surface is concentrated in
  thread/atomics, filesystem/database I/O, sockets, console/process, and virtual memory.
  The extracted source sets are intentionally incomplete on Linux/macOS; do not relax the engine
  gate until real POSIX backends populate them.
- Native time and recursive critical-section services are selected and runtime-tested. The custom
  fast lock now routes every dvar/database reader and writer counter operation through shared
  sequentially consistent helpers, with source guards and reader/writer stress coverage. Broader
  engine atomics still contain Windows `LONG`, direct volatile polling, and Windows-header coupling;
  finish that M1 migration before compiling a non-Windows engine target.
- Native event services are selected and all event consumers use the opaque handle. A native
  opaque thread lifecycle is also selected and runtime-tested, but high-level creation/identity has
  not yet migrated from Windows-owned `threads.cpp`. Renderer workers now park through the tested
  cooperative gate, so normal-operation native suspension has been removed. Priority/affinity policy
  is unported. Fatal-error freezing remains separate and requires the Linux signal park and macOS
  Mach mechanisms specified in the porting plan.
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

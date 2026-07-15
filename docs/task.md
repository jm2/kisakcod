# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Active branch: `agent/memfile-silent-read`; branch point: merged ODE occupancy checkpoint
  `288c2b78`; upstream-integration baseline: `2b759db`.
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Active work: move `FX_RestoreEffectDefTable` behind a bounded lifecycle-owned BSS lease. Its prerequisite no-longjmp
  input boundary is complete on this branch: status-bearing `MemoryFile` data and caller-bounded C-string readers decode
  the production legacy RLE/zlib stream without assertions, prints, or drops; validate exact segment lengths and
  little-endian headers; reject cross-segment, malformed, and truncated input; preserve legacy partial-output and
  sticky-overflow behavior; and reset the process-global codec on failure. `FX_ReadArchiveDataNoDrop` now uses that primitive
  directly instead of temporarily suppressing reports around the legacy reader. TLS records the exact stream owner, and
  both global `Com_Error` longjmp paths abandon same-thread inflate/deflate state outside the client-only cleanup guard;
  foreign threads cannot release it, mismatch cleanup cannot corrupt a newer owner's RLE caches, and raw/compressed read,
  compressed-write, idempotence, immediate-reuse, and foreign-thread behavior are executable contracts. The complete
  **48/48** suite passes under GCC, Clang, ASan+UBSan, and TSan; strict x86-32 and AArch64 compile/link pass; and two
  independent final audits approve the slice. Checked whole-segment compressed finalization remains a later integrity
  API because SND intentionally skips/copies segments and FX performs multiple mid-segment reads. The next transaction
  must parse all exact little-endian names/keys before registration, eliminate the uninitialized truncated-key path,
  publish only complete entries, replace the 1,024-entry stack table (8,196 bytes on x86; 16,392 on native64), release
  scratch before `FX_BeginArchive` or PHYSICS admission, and let error abandonment reclaim the lease without diagnostics.
- PR #18 is open. Initial run **29385108870** exposed one macOS arm64-only build seam: the bundled zlib header suppresses
  its `Byte` typedef whenever modern AppleClang defines the legacy `TARGET_OS_MAC` marker, although modern Darwin no
  longer makes that classic-Mac typedef visible. The focused correction keeps the classic compiler exception while making
  zlib self-contained on Apple Mach targets. All ten bundled C translation units compile under simulated Darwin macros;
  focused GCC/Clang/ASan+UBSan/TSan execution and strict x86-32/AArch64 compile-link pass. Replacement CI is pending.
- ODE occupancy runtime on this branch is otherwise complete. The engine-free physics batch controller rejects invalid,
  duplicate, overlapping-output, and unknown-status inputs before callbacks; preflights every selected retirement or
  reconstruction before mutation; and reports the exact successful commit prefix. Production FX archive retirement and
  reconstruction now delegate to it while retaining recoverable wrapper validation, caller-owned PHYSICS, prefix recovery,
  and centralized fail-stop ownership for ambiguous duplicate bindings. A real exact-size fixture fills 512 body and 512
  user-data slots plus 2,048 geom slots with 500/500/2,024 competing non-FX resources and 12/12/24 FX resources. It proves
  full and mixed-demand retirement/reconstruction, deterministic minimum plans, recoverable failure after a committed
  prefix, transactional capacity/selection rejection, cleanup-refusal ownership, exact non-FX identity preservation,
  and pool conservation. The **47/47** GCC suite passes; focused GCC/Clang/ASan/UBSan/TSan execution and strict x86-32/
  AArch64 compile-link also pass. Independent integration review found no functional issue; a wording-only source-test
  correction now distinguishes prohibited heap/Z allocation from intentional native fixed-pool reconstruction.
- The production ODE transaction-hardening follow-on is complete in commits `f9a7e24`, `c10dcc25`, and `8d3ba0a`.
  Exact body/user-data/geom/joint topology validation, silent allocation and joint-aware destruction, complete
  body-plus-model construction, inertial/center-of-mass publication, and bullet-impact prepare/commit now remain
  intrinsically report-free while the caller owns PHYSICS. CG SP/MP, DynEnt pieces, and live FX keep one ownership
  transaction through initial impact, binding, rollback, and publication; resource subtype diagnostics occur only after
  the outermost leave, and cleanup or ambiguous duplicate ownership fail-stops only after unlocking. DynEnt retains the
  legacy DYNENT timestep applied to an FX-world body and consumes its three RNG values only after successful creation.
  FX performs full sidecar validation from a bounded BSS workspace before native allocation and reuses it for binding.
  Two independent final audits approved the frozen x86 scope. All **47/47** tests pass under GCC, Clang, ASan+UBSan,
  and TSan; strict lower-ODE Clang syntax with warnings-as-errors also passes. PR #17 squash-merged as `288c2b78`
  after replacement run **29382870200 passed all nine jobs**. The replacement fixed one missing MSVC helper
  declaration and one omitted lambda reference capture, then added an exact 512-slot guard before fixed-workspace
  indexing. Gemini's nullable-physics-preset comment was resolved from the immediately dominating null-return guard;
  its workspace-bound comment produced the exact fail-closed descriptor check. Both review threads were resolved and
  a final independent audit approved commit `488314be` without another finding.
- Archive-gate checkpoint: PR #16 merged as `5455c778` after CI run **29374832707 passed all nine jobs**. Gemini's
  two comments were resolved as false positives: the generation lookup is intentionally null-safe and the requested
  standard headers were already explicit. Codex reviewed the exact merge head `5c3a96a0cd` and found no major issue.
  Normal FX archive admission now uses a portable callback-driven controller with typed
  `Open`, `Pending`, and `Exclusive` gate values; unknown encodings fail closed. Durable TLS records exact
  `Pending`, `PendingExclusive`, `Acquired`, and `ExclusiveGateOnly` ownership so waiter cancellation, promotion
  rollback, partial release retry, and error abandonment cannot discard a still-owned resource. Deterministic
  tests cover retry/cancellation, owner identity, lifecycle generation and checked refresh, rollback, every cleanup
  phase, corrupt-state validation, and iterator-before-gate reopening. Production admission rejects cooperative,
  kill-exclusive, sort-exclusive, effect-lock, self, and non-idle TLS ownership. The prechecked kill writer accepts
  only the known `Open`/`Pending` race, pool rebuild/validation bypass admission only with exact `Acquired` proof,
  safe-empty reset refreshes its generation through the checked controller API, and a corrupt nominally idle state
  reaches fail-stop validation instead of reopening admission. The distinct reset/init/shutdown two-gate lifecycle
  protocol is unchanged. Local validation is **45/45** under GCC, Clang, ASan/UBSan (leak detection disabled), and
  TSan; strict x86-32 and AArch64 controller compile/link plus all three focused source scripts pass. Two independent
  audits found and verified three concrete fail-closed corrections and found no remaining PR-scope issue.
- Next: implement the transactional effect-definition-table BSS lease, then add measured stack/runtime gates.
- Restore-workspace checkpoint: PR #15 merged as `1ea12d76` after final CI run **29364493294 passed all nine
  jobs**; duplicate merge-push run **29365086642** also passed. This checkpoint completed checked heap-backed FX
  archive restore scratch. One explicitly constructed,
  noncopyable transaction workspace now owns the rollback system/control state, staged and rollback body sidecars,
  ownership/token images, retirement candidates/planner, and pool-graph scratch. A short-lived checked heap image
  also covers malformed restored-graph preflight before the transaction workspace exists, so no attacker-reachable
  restore validation retains the former 14–23 KiB convenience-wrapper frame. Size narrowing, max-alignment,
  placement construction, destructor-before-free ordering, scratch/wrapper parity, failure output preservation,
  and reuse after failure are executable portable contracts. Safe outcomes leave PHYSICS, end archive admission,
  destroy the workspace, then release referenced buffers; `UnsafeFailure` uses the now-contractual `[[noreturn]]`
  platform fatal boundary without destructing/freeing scratch, releasing PHYSICS, or reopening admission. Source
  contracts pin those orders and all production `WithScratch` routes. The four local **44/44** GCC, Clang,
  ASan/UBSan, and TSan suites pass, as do strict x86-32 and AArch64 compile/link checks for sidecar, capacity,
  pool-graph, workspace-lifetime, and restore-controller fixtures. Independent lifetime, sidecar, and pool reviews
  found and verified fixes for the early preflight wrapper and fatal-boundary contract, then reported no remaining
  ownership, cleanup, lock-order, or scratch-reuse defect. The prior measured restore peaks were approximately
  **58–66 KiB x86 / 105–121 KiB native64**; structural census projects roughly **15 KiB / 23 KiB** after this
  change, but an actual production frame gate remains pending because `fx_archive.cpp` is not yet portable outside
  the Windows engine build. The early 8,196-byte x86 effect-definition table remains stack-backed because
  registration can longjmp before cleanup; moving it requires a separate no-leak parsing boundary. The archive-gate
  and portable ODE occupancy follow-ons are now complete; the remaining sequence begins with the effect-table boundary
  and measured per-function stack gates.
  PR #15's initial run **29361544758** passed the Linux amd64/arm64 and macOS arm64 portable suites plus the
  headless Windows x86 engine build, but both MSVC portable jobs rejected the intentionally over-aligned workspace
  fixture with C4324 under `/WX`. The exact-payload fixture correction passed all nine jobs in replacement run
  **29363682092**. Final independent review then found that the workspace support variable eagerly formed
  `alignof(T)` for unsupported non-object or incomplete template arguments; the guarded support trait now rejects
  void, function, incomplete, array, throwing-lifetime, and over-aligned types through the constraint instead of a
  hard substitution error. Focused GCC, Clang, ASan/UBSan, and TSan execution plus strict x86-32 and AArch64
  compile/link passed on the final review fix; final run **29364493294** repeated the complete nine-job gate before
  the PR was squash-merged.
  PR #14 merged the executable restore controller as `39432a29` after run **29359061795 passed all nine CI
  jobs** and Gemini plus independent review reported no findings. Its portable controller covers all operation
  failures and the production adapter retains archive/PHYSICS ownership through desired, original, safe-empty,
  or unsafe terminal outcomes. Local validation was **43/43** under GCC, Clang, ASan/UBSan, and TSan with strict
  x86-32 and AArch64 controller compile/link checks.
  PR #13 merged the status-bearing resource cleanup prerequisite as `48906d26` after run
  **29356956952 passed all nine CI jobs** and Gemini reported no findings. The generic body/user-data and
  primary-geom/transform transaction retains explicit primary ownership when rollback refuses and returns a
  dedicated cleanup-failure result. Both production acquisition paths translate it to
  `PhysBodyModelCreateStatus::CleanupFailed`; both FX reconstruction callers fail-stop, and diagnostic body
  and geometry rollback holds recursive PHYSICS exclusion across each complete legacy destructor call.
  Local validation was **42/42** under GCC, Clang, ASan/UBSan, and TSan, with strict x86-32 and AArch64
  resource-pair compilation. Production `phys_ode.cpp` remains covered by the four Windows x86 engine jobs
  until a narrow portable ODE runtime translation unit exists.
  PR #11 merged the live generation-checked sidecar baseline as `da273589` after replacement run
  **29335570405 passed all nine CI jobs**; PR #9's native physics-pool prerequisite merged at `8ce11763`
  after run **29300663478 passed all nine jobs**.
  PR #12 merged the recoverable full-capacity transaction as `a9994b6b` after replacement run
  **29355001881 passed all nine CI jobs** and all review threads were resolved. Restore captures exact
  body/model/topology recipes, retires only generation-checked old FX bodies needed at ODE's 512-body
  ceiling, reconstructs them on failed publication, preserves iterator/archive exclusion, and drains three
  disjoint sidecars before a canonical safe-empty fallback. Automated review added fixed snapshot-capacity
  bounds and one recursive PHYSICS interval around fresh body/user-data destruction; the claimed nullable
  canonical FX buffer members were a false positive because those members are embedded arrays.
  The licensed-content smoke is deferred and must not be dispatched: it requires a self-hosted
  `[self-hosted, kisakcod, windows, x86]` runner and the `KISAKCOD_GAME_DIR` secret, neither of which is
  currently provisioned. Surface that infrastructure blocker instead of triggering the workflow.
- Progress estimate: approximately **38% complete by engineering effort** (plausible range 34–43%).
  The foundation/checklist view is about 50–55% and the shared foundation is roughly 88–93% mature,
  but none of the five requested 64-bit/non-Windows engine targets builds yet; target delivery is 0/5.
- Upstream integration: merged PR #1 at `2b759db`, incorporating upstream `master` through `8a0f14f`
  (nine commits; upstream was not ahead at merge time) while preserving the port's pointer-width and
  security changes. It restores several primarily SP features plus real shared renderer, XAnim, and
  unzip fixes; the accidental
  `src/staged_changes.patch` artifact is excluded. Pre-review run **29204498860 passed all nine CI
  jobs**. Gemini/Codex review found fixed-width vehicle save-stream and null-dereference defects; the
  review follow-up fixed the substantiated findings and recorded the two false positives (pathnode's
  no-match branch already returned, and `CG_Missile` consistently declares/uses its uppercase local).
  A full delta audit additionally repaired the restored SP script-file API's one-slot ownership,
  pointer-width handle, signed-size, append-status, bounds, cleanup, and variadic-format defects, then
  applied the same one-slot ownership, bounds, and signed-allocation protections to the active MP API.
  Source-level regression guards cover these engine-only paths. Review-fix commit `6d604c1` and merge
  candidate run **29205843697 passed all nine CI jobs**, including all four Windows engine variants.
- Current EffectsCore width batch: the core runtime aggregates now live in portable
  `EffectsCore/fx_runtime.h` with exact 32/64-bit size, alignment, standard-layout, and trivial-copy
  contracts. Nineteen atomic words and `FxCmd::spawnLock` use explicit 32-bit storage, and all 61
  native atomic calls across the six EffectsCore users preserve their old/new-value semantics through
  `Sys_Atomic*`. The iterator admission/exclusive-release and garbage-collection request paths now use
  fixed-width atomic helpers with cooperative wait yielding. The remaining renderer reservations and
  counters have also migrated, so the executable census is now **zero direct `Interlocked` calls**. Native
  conversion uses checked `sizeof` allocation/copy math and alignment, typed XModel physics pointers,
  complete 12-pointer curve copies, bounded paired trail indices, correct single-decal deep copies,
  native impact-table allocations, and a native `FxProfileEntry` sort stride. A shared checked blob
  cursor now gives every pointer-bearing converted payload its native alignment, zeroes padding, rejects
  signed-size/capacity overflow transactionally, and verifies planner/writer parity. Disk32 work remains
  for `fx_archive` and fast-file effect/impact-table reads; the loose-file missing-effect fallback is
  native-width-safe, and the merged physics sidecar is now wired through live and legacy-x86 archive
  ownership paths. Freelist allocation/free
  now validates heads, links, strides, counts, and ownership before mutation, clears payloads before
  publication, maintains atomic active counts, and leaves failed mutations transactional. Native-size
  effect handle codecs remove the x86-only `0x8000` ceiling and reject malformed/misaligned handles.
  Fixed-size allocation sidecars reject arbitrary non-head duplicate frees in O(1), prevent short
  freelist cycles, and are rebuilt transactionally from bounded acyclic archive lists while requiring
  archived active counts to match. Reset, allocate, free, and restore all share that ownership state.
  Archive effect-ring/table fixup and class-1 physics walks, sorting, draw, update, profile, bulk removal,
  garbage collection, retrigger, trail, and spawn paths now bound their rings/chains and validate pool
  allocation plus definition indices before access or mutation; malformed GC handles drop only after
  iterator ownership is released. Public kill/kill-definition/kill-all/through paths use a writer-intent
  gate, retain and lock a validated owner subtree, and keep allocation sidecars consistent with a durable
  lifecycle marker so killed externally referenced effects cannot be resurrected or respawn children.
  Rewind retains restart roots, kills and collects under that same exclusive transaction, atomically
  downgrades to a cooperative reader, and holds admission closed until every root is rearmed; explicit
  longjmp cleanup releases reservations, retains, locks, writer/restart gates, and iterator ownership.
  The legacy 32-bit save/restore path validates system/camera/visibility scalars, runtime semantics,
  pool ownership, and physics before publication, stages the replacement graph, rolls back a failed
  commit, and deliberately rejects 64-bit use until the Disk32 converter exists.
  Packed reference, owned-effect, pending-loop, and lock transitions use bounded CAS operations instead
  of arithmetic that can carry into adjacent fields. Trail byte fields have explicit signedness,
  preserving compressed negative basis vectors on Linux ARM64. The visibility blocker protocol validates
  finite packed inputs, uses all 256 slots, publishes payloads before counts, and bounds corrupt readers.
  PR #2 pre-review run **29275713249 passed all nine CI jobs** at `e6b10da`, including all three ARM64
  portable targets and all four Windows x86 engine variants. Codex review then found that partial
  updates intentionally pass a staged trail copy into element retirement: the correction retains the
  real allocated trail owner for validation and publishes both staged and live endpoints in the same
  allocator transaction. PR #2 merged at `036ddaf8` after replacement run **29277249156 passed all
  nine CI jobs**; local GCC, Clang, ASan/UBSan, and TSan validation was 30/30. PR #3 then replaced
  the loose-file missing-effect fallback's raw `_DWORD` clone and literal x86 rebasing with a checked,
  typed native-width alias that owns its name, shares only a fully validated immutable element span,
  and fails transactionally on size, alignment, capacity, or overlap errors. PR #3 merged at
  `facbfb12` after run **29279924536 passed all nine CI jobs**; Gemini reported no review findings,
  and the 31-test local GCC/Clang/ASan/UBSan/TSan plus strict AArch64 matrix was green. PR #4 added a
  private noncopyable native `dxBody *` registry keyed by element
  slot, full-width generation tokens with bit-preserving legacy-field codecs, structural and semantic
  validation, and source/revision/lifetime-bound staged publication/rollback that rejects duplicate
  ownership, mismatched generations, replay, reconstructed registries, and stale transaction
  provenance. A fallible pool pre-publication
  callback lets the integration detach external ownership
  before a slot reaches the freelist while leaving rejected frees transactional. Gemini review then
  bounded validated ownership scans by their proven active counts; Codex review made callback vetoes
  nonfatal in all production pool-free wrappers. PR #4 merged at `3c542f20` after replacement run
  **29286377602 passed all nine CI jobs**. Its expanded **32/32** portable matrix passes under GCC,
  Clang, ASan/UBSan, and TSan plus strict x86-32 and AArch64 compilation. PR #11 routes
  production spawn/draw/free/reset/init/shutdown and legacy archive paths through that primitive while
  preserving the frozen element ABI. PR #12 adds recipe-based full-capacity restore: ODE's
  global 512-body ceiling is handled by silently validating exact resource demand and global topology,
  retiring only owned old FX bodies needed for capacity, and reconstructing every retired body before an
  old-graph rollback. Desired, rollback, and safe-empty publication keep archive iterator exclusion intact;
  unexpected cleanup failure terminates before reopening admission. PR #15 moved its bounded transaction and
  validation scratch to checked heap lifetimes without weakening fail-stop ownership, and the current branch now
  supplies executable normal archive-gate control and production integration. The current ODE occupancy follow-on adds
  exact fixed-pool competition plus intrinsically silent live creation/impact/rollback transactions. Remaining
  FX-specific work is real 64-bit Disk32 archive/fast-file conversion, camera/scalar snapshot publication, the
  no-longjmp effect-table parsing boundary, and measured stack/runtime gates.
  Audit of that transaction exposed prerequisite ODE exhaustion defects: `dBodyCreate` and user-data allocation
  dereference null, while primary/transform geometry failures can publish incomplete collision bodies.
  PR #7 checks body exhaustion before world publication, pairs the dormant heap
  fallback's `new`/`delete`, and uses one tested opaque resource-pair transaction for both body/user-data
  and primary-geom/optional-transform acquisition. A checked fresh body-plus-model API owns whole-body
  rollback and the archive restore path uses it before encoding or publishing a body. Allocation happens
  before center-of-mass mutation, completed outer geoms retain legacy simple-space ordering, malformed
  mass/model geometry is failure-observable, and two adjacent 64-bit defects were removed: brush state no
  longer truncates pointers through an x86 union overlay, and wake timestamps no longer cast a space
  pointer to `int`. Review then closed two further production gaps: ODE user geoms now reserve and align
  the complete native `BrushInfo` payload (16 bytes on x86, 24 on amd64/ARM64) while proving it still fits
  the shared transform-sized pool slot, and archive restore retains `CRITSECT_PHYSICS` continuously from
  replaced-body capture through graph publication and old/new body commit or rollback. The archive gate's
  post-acquisition `FX_ALLOC` snapshot is an explicit drain barrier, so the narrowly scoped
  PHYSICS-to-FX_ALLOC publication cannot invert a live allocator lock. Function-scoped source contracts
  bind callback rollback, allocation-before-COM mutation, and the complete archive lock interval. The
  utility suite is **36/36 locally green** under GCC, Clang, ASan/UBSan (leak detection disabled under the
  ptrace runner), and TSan; focused storage validation also passes x86-32 and AArch64 compilation. PR #7
  merged at `580b93bb` after all nine jobs passed in run **29291013134** and automated plus independent
  review found no remaining defects. PR #9 closes the next hard 64-bit blocker without
  changing the 8-byte x86 metadata: every link is stored and loaded at native pointer width through
  `memcpy`, and a non-owning base/stride/count descriptor supplies each operation's exact extent. Checked
  queries return status and value together, so caller output cannot alias and rewrite live pool storage
  or metadata. Pre-review commit `202cce76` passed all nine jobs in run **29293356200**. Gemini review then
  identified that `Pool_Alloc` and `Pool_Free` still traversed the complete inactive list on every hot
  mutation, and that the `PhysGlob` tracking-size guard was runtime-only. The review fix keeps
  `pooldata_t` at its retail 8-byte x86 layout while adding external per-slot shadow ownership/link state:
  `Pool_Alloc`, `Pool_Free`, and `Pool_GetFreeCount` now validate the active metadata and touched link in
  O(1), reject foreign/interior active nodes, duplicate frees, and active-count divergence before mutation,
  and preserve transactional failure. Dormant deep links cannot be inspected by an O(1) operation;
  explicit bounded `Pool_ValidateFull` performs the complete short/long-chain and cycle audit at archive
  capacity and ODE leak-diagnostic boundaries. All three production pools use typed or explicit extents;
  `PhysObjUserData` now uses
  its actual 0x70/0x78 stride instead of overlapping native64 records, ODE diagnostics use checked index
  and next accessors, and FX archive capacity performs the full validation and fails closed on corrupt
  state. The review-fix strict **39/39** GCC, Clang, ASan/UBSan, and TSan suites pass, as do x86-32 and
  AArch64 compile/link checks. Those portable tests compile the allocator contract;
  Windows x86 engine CI remains the production-callsite compile gate until native engine source sets
  exist, while all five portable runtime jobs remain the cross-target allocator gate. PR #9 merged at
  `8ce11763` after replacement run **29300663478 passed all nine jobs** and all review threads were resolved.
  The global
  32-bit frozen token field necessarily permits generation reuse after 2^32 advances, and release
  shutdown must explicitly drain/finalize the registry because destructor assertions are diagnostic.
  The global unbounded/alignment-unsafe `Buf_Read<T>` cursor has
  114 consumers in XAnim/XModel load-object code and is a distinct security/ARM batch, not an
  EffectsCore reader.
- Current model draw-stream batch: a shared checked dword-offset resolver validates the published surface
  arena, overflow, native alignment, full record extent, and tag before access. Static scene traversal
  handles hidden and rigid records without raw pointer stepping; all five draw-XModel and seven scoped
  tessellation consumers use typed tag-aware resolution and bound every `gfxEntIndex`. Both frontend
  BModel traversals validate the complete placement-owned record sequence and canonical world surfaces;
  the backend validates each tagged record and consumes malformed draw entries so it cannot spin.
- Current virtual-memory batch: `sys_memory.h` exposes native-page `size_t` reserve/commit/decommit/
  release services with Win32 and POSIX backends. Both track live reservation extents under a lock and
  reject invalid ranges; POSIX decommit remaps anonymous `PROT_NONE` pages so recommit is guaranteed
  zero-filled on Linux and macOS. Zone, hunk, and physical-memory consumers no longer call Win32
  allocation APIs or assume 4 KiB pages, removing one pointer-truncation allowlist site. Runtime tests
  cover partial ranges, overflow, zero-on-recommit, exact-once release, and concurrency. MSVC exposed
  one `max`-macro collision and one incorrectly named FX visual-union source; corrective commits
  `8dbc36b` and `5a72e5e` fixed both, and run **29214406641 passed all nine CI jobs**.
- Current filesystem batch: portable Win32/POSIX services provide UTF-8 directory creation,
  dynamically sized current-directory lookup, executable-path lookup, and bounded deterministic
  directory enumeration. Enumeration rejects symlink/reparse/special entries, applies optional filters
  before result limits, preserves native pointer widths in sort/list storage, and reports incomplete
  direct or recursive results instead of silently hiding matches. Directory creation holds and
  validates every ancestor without following POSIX symlinks or Win32 reparse points; Windows uses wide
  long-path APIs and rejects invalid/DOS-device components. Common wrappers no longer truncate at 256
  bytes and preserve POSIX, drive, extended-drive, and UNC roots. Recursive deletion deliberately remains
  Win32 debt pending handle-relative enumeration/deletion. The integrated utility suite passes **30/30**
  locally under GCC, Clang, ASan/UBSan, and TSan. Run **29215880727** passed every engine job and all three
  POSIX utility jobs, but the filesystem contract failed without diagnostics on Windows amd64/ARM64;
  diagnostic run **29216713463** isolated that failure to a test forcing the process current directory
  beyond the default Windows `MAX_PATH` policy, before enumeration began. The corrected fixture still
  verifies dynamic current-directory sizing below that policy boundary on Windows, retains the >320-byte
  POSIX case, uses native wide file APIs, and emits granular Win32 diagnostics. Replacement run
  **29250761031 passed all nine jobs**, including Windows amd64/ARM64 portable runtime tests and all four
  Windows engine variants.
- Current DObj/model-surface batch: the inherited fixed 3,600-byte stack overlay and retail
  4/24/56-byte pointer-bearing stream assumptions are gone. A shared checked planner/cursor uses
  native 4/8, 24/40, and 56/72-byte records, aligned exact-capacity CAS reservations, placement
  construction, and explicit word/surface counts. DObj skinning now preflights selected LODs, bone
  spans, hidden/rigid/skinned record bytes, output vertices, surface and vertex arenas, and second-pass
  equivalence before publishing the scene descriptor or worker command. Worker and scene consumers
  validate exact framing, owner frame, published reservation cursors, contiguous output ranges,
  surface identity, material coverage, and shifted required-bone sets before access. Scene cull state,
  surface/temp cursors, BModel records, and all touched publication paths use exact-width atomics and
  native layouts; `int8_t` LOD storage preserves `-1` on ARM targets. Fast-file XSurface/XModel
  completion now validates skin buckets, weight records/sums, rigid coverage, part bits, skeleton
  parents, hit-location classifications, exact LOD/cache bases and allocation, finite/canonical
  vertex/base-pose/bone/model data, materials, and collision counts/spans/bounds/bone indices/contents.
  Runtime DObj model/LOD/bone-info accessors duplicate the safety-critical bounds for load-object and
  debug paths. Corrective construction now prepares all fallible DObj create/clone resources before
  pool reservation or object locking, validates duplicate maps and signed LODs, and publishes through
  an assignment-only commit; ordinary failure paths discard their owned plan. Local validation is
  **23/23** under GCC, Clang, ASan/UBSan (with leak detection disabled for the ptrace runner), and
  TSan. That batch removed seven executable native calls and, with the subsequent EffectsCore work,
  the census at that checkpoint fell to **9 direct `Interlocked` calls in 4 engine translation units**;
  the later renderer migration reduced the current executable census to zero. Run
  **29201767094** passed all five portable
  jobs but failed the four Windows engine jobs on two undefined yields, one missing cull-state include,
  and a non-constexpr layout assertion. Corrective commit `89a6122` repaired all four seams: run
  **29203597111** passed all four Windows engine jobs, including the three client builds that are the
  only jobs compiling `gfx_d3d`, but its two MSVC portable-test jobs failed on fifteen identical C4267
  `size_t`-to-`uint32_t` narrowings in the new `db_validation_tests.cpp`, which only `/W4 /WX` MSVC
  diagnoses. Commit `78d72b1` applies the repository's existing `static_cast<std::uint32_t>` count
  idiom at all fifteen sites; replacement run **29203923350 passed all nine jobs**. Residuals: a failed
  later vertex reservation can consume an already reserved surface slice for that frame;
  `XAnimResetAnimMap`
  mutates a shared tree during pre-reservation preparation, so a theoretically racing reservation
  failure can leave the requested mapping without a published DObj (current callers are serialized); and the load-object
  `Buf_Read` family remains unbounded and alignment-unsafe, requiring a cursor threaded through the
  model/surface/physics parsers rather than another local assertion.
- Completed worker-queue batch: all 25 native atomic calls and mixed raw queue accesses in
  `r_workercmds.cpp` now use exact-width bounded helpers. The lossy minimum-type hint is removed in
  favor of a deterministic 17-type scan. Short per-queue producer and consumer guards cover only
  reserve/copy/publication and claim/copy/cursor movement, closing the inherited wrapped-cursor ABA
  duplicate-execution race while handlers remain parallel. One `outstandingCount` owns queued,
  executing, recursively submitted, and full-queue inline work through completion, so type-specific
  and wait-all predicates are linearizable; publication/completion notification is unconditional.
  All 17 command types have compile-time payload traits, native `sizeof`-derived descriptors and
  32/64-bit layout contracts. Typed dispatch removes truncated shadow-cookie/DPVS pointers, delayed
  pointer-buffer mismatches, x86 word decoding, three lighting-handle pointer round trips, and the GPU
  timeout callback cast. Descriptors initialize before backend startup; dequeue scratch is aligned and
  bounded; impossible release-build transitions fail closed. Local validation is 22/22 under GCC,
  Clang, Clang ASan/UBSan, and Clang TSan, including eight-producer/eight-consumer exact-once wrap
  stress. The batch reduced the live census from 102 calls/14 TUs to 77 calls/13 TUs. Run
  29199400717 passed the headless build and all five portable architectures but exposed one shared
  MSVC const mismatch in the three client builds. Corrective commit `33bdd81` makes the cached-lighting
  pose truthfully mutable, and replacement run **29199666846 passed all nine jobs**. The shared manual
  event retains its inherited 1 ms bounded poll, and
  worker shutdown/reinitialization remains process-lifetime debt. The inherited full-ring inline
  fallback may overtake older same-type queued work, and a handler-level `Com_Error`/longjmp can
  bypass normal completion accounting; preserve those as explicit runtime-test/error-unwind work.
- Completed renderer-reservation batch: the five `r_drawsurf.cpp` native atomics now use a tested,
  exact-width bounded CAS reservation helper. This repairs the code-mesh argument allocator's split
  load/exchange lost-update race, prevents failed draw-surface/code-mesh/mark-mesh reservations from
  poisoning their counters, permits exact-capacity use, validates release-build region/input bounds,
  and removes the first four LP64-widening renderer counter declarations. Reset, merge, and backend
  accesses share the atomic boundary; stage, record, argument, triangle, and index extents fail closed
  before backing-array access. Local validation was 21/21 under GCC, Clang, Clang ASan/UBSan, and
  Clang TSan, including contended single- and multi-element reservations. The batch reduces the live
  census from 107 calls/15 TUs to 102 calls/14 TUs. Commit `0fddf2d` passed all nine jobs in run
  29197855220.
- Completed skeleton/pose batch: a shared, platform-neutral skeleton-arena helper derives exact
  aligned capacity from each backing array, rejects invalid alignments, arithmetic overflow,
  corrupt/misaligned cursors, oversized requests, and exhaustion without poisoning the cursor, and
  reserves non-overlapping offsets with a checked fixed-width CAS. Client/server resets publish the
  arena base and empty cursor before advancing their exact-width epoch; a full 32-bit wrap clears all
  affected client/server DObj skeletons before epoch 1 is reused, preventing a dormant timestamp from
  reviving a stale arena pointer. A private fixed-width reset guard serializes the complete base,
  cursor, invalidation, and epoch publication scope; the invalidation callback also lives inside the
  epoch CAS retry loop, so contending resetters cannot race or publish epoch 1 without first clearing
  the old generation. Server creation reloads the epoch after an allocation-triggered reset,
  exhaustion diagnostics are claimed atomically once per epoch, and invalid requests drop cleanly
  instead of forming out-of-range pointers, looping forever, or reporting an unusable matrix as
  ready. All SP/MP `cpose_t::cullIn` producers and consumers now use a priority-preserving
  CAS/store/exchange protocol, so a late used mark cannot downgrade a culled request and a consumer
  cannot erase a racing publication. Dual 32/64-bit layout
  contracts, contention/rollover tests, and repository-wide source guards freeze the migration. This
  removes three more native calls, leaving **107 direct `Interlocked` calls in 15 engine translation
  units**. Local validation is 20/20 under GCC, Clang, Clang ASan/UBSan, and Clang TSan. Commit
  `060e6ba` passed all nine jobs in run 29196678355. Worker quiescence during reset remains an external
  lifecycle contract; resetter serialization is enforced, but the inherited `allowedAllocSkel` flag
  is still unused and does not enforce worker quiescence.
- Completed IWD/loopback batch: canonical IWD-handle ownership and reference publication use exact
  fixed-width atomics; contended readers open independent archives instead of copying a live unzip
  cursor; selected entries, handle bounds, native record sizes, and shutdown cleanup are validated.
  IWD discovery now uses pointer-sized sorting and native search-path allocations. Both central-
  directory passes reject partial traversal, long/embedded-NUL names, arithmetic/allocation failure,
  changed name extents, and invalid positions. The legacy unzip layer rejects short scalar reads,
  invalid EOCD/bounds, allocation/inflate failures, and partial state, and its unsafe `unzReOpen` API
  is removed. Per-queue locks now protect loopback payload/cursor publication across wraparound;
  packet routes, lengths, destinations, and marker reads are bounded. Fake-lag queueing validates
  release-build inputs and allocation results, preserves the caller's receive capacity, rejects
  inconsistent metadata, and fully clears retired slots. Commit `aa91d37` initially exposed two
  MSVC-only fake-lag `char *` to `uint8_t *` conversion errors; focused corrective commit `0a119b7`
  replaced them with explicit pointer reinterpretation, and all nine jobs then passed in replacement
  run 29195736931.
- Last completed database batch: standalone-tested atomic protocols use fixed-width
  `FileReadState`/`ProgressState` records plus an `AssetRecoveryGate`; they publish results coherently,
  serialize back-to-back lost-device recovery against database asset use, reject invalid read sizes
  and progress underflow/overflow, and calculate bounded progress snapshots without mixed atomic/raw
  access. The database queue now publishes initialized entries before wake-up, claims each batch
  exactly once, reserves producer ownership, and rejects replacement, capacity overflow, and asset-
  count underflow. Atomic minimum-fast-file, initialization, loading-zone, and loading-asset states
  close prior cross-thread races; the lost-device recovery handshake now has a tested safe-state
  claim/recheck protocol and yielding waits. Buffered overlapped reads remove the unfulfilled
  unbuffered sector-alignment contract, and file-handle, zone input, vertex/index pointer-offset, and
  read-buffer bounds receive runtime validation. This batch replaced 20 executable native atomic
  calls; commit `cfd9045` passed all nine CI jobs in run 29177998144.
- Previous diagnostic batch: mark-generation and local-entity overlap counters now use exact
  fixed-width `Sys_Atomic*` words, keep every increment paired with a decrement, and have source guards
  against reintroducing native atomics. This landed 12-call migration is diagnostic only and does not
  claim to serialize the underlying renderer/cgame work.
- Previous XAnim/DObj batch: XAnim tree overlap counters and the DObj lock/lifecycle protocol now use
  exact 32-bit `Sys_Atomic*` words without Windows headers. Create/clone construction is reserved and
  published last, free/clone source access is serialized, object maps publish only complete DObjs,
  contended locks yield, and archive/unarchive preserves an exact initialized native-width overlay
  inside its existing externally quiesced DB window. DObj model storage now sizes and aligns native
  pointer arrays, clone no longer copies a live lock, and the old 64-bit archive over-read is gone.
  Exact 32/64-bit layouts cover DObj, XAnim tree/table entries, and saved records. Variable-sized
  XAnim tables use checked `offsetof + count * sizeof(entry)` allocation, pointer-width debug tables,
  and native allocator alignment. SP preview DObj buffers/clone traversal and corpse tree/metadata
  traversal no longer use x86 byte strides or pointer-to-int writes. Source guards freeze the new
  layouts, lifecycle ordering, allocation formulas, publication order, and absence of raw/native
  atomic access.
- Previous script-lifetime batch: script string and vector lifetime accounting no longer imports Windows
  atomics or types. A tested packed-word CAS helper makes same-user claims, duplicate transfers,
  reference changes, and user-bit moves indivisible while preserving the encoded length; it rejects
  underflow/overflow and assigns exactly one owner to the zero transition. The last generic decrement
  is published while holding the hash lock, shutdown clears a user with its owned reference in one
  CAS, and transfers collapse an already-owned destination instead of leaking a reference. Debug
  accounting completes before a freed string slot can be recycled, error paths release the recursive
  lock before dropping, leak initialization resets `ignoreLeaks`,
  vector debug indices are validated, and exact 32/64-bit runtime layouts replace two raw size
  assertions. The vector object's 16-bit reference field remains explicitly script-VM serialized;
  only its global/debug accounting is atomic.
- Previous atomic-boundary batch: `sys_atomic.h` now provides collision-free, fixed-width
  `Sys_Atomic*` operations on every compiler. MSVC uses `<intrin.h>` without importing `Windows.h`,
  with the sole `int32_t`/`uint32_t` to native `long` cast and bit-preserving conversion centralized;
  GCC/Clang use seq-cst `__atomic` operations. All native utility runners now execute return-order,
  wrap/high-bit, load/store, CAS, and pointer-exchange semantics. The shared fast lock is the first
  canonical consumer and no longer contains platform branches, native casts, or Windows names.
- Previous batch: dvar sorting no longer depends on `LONG`, Interlocked, Windows headers, or
  the Win32 networking sleep wrapper. Private seq-cst atomic flags now publish sorted state and
  arbitrate the one-sorter/multiple-waiter protocol; registration invalidation is atomic, waiting
  uses `Sys_Sleep`, and source guards reject raw flag access or reordered publication.
- Previous high-level thread batch: thread orchestration uses private `SysThreadHandle` storage,
  pointer-safe per-context start records, exact CDECL callback types, backend identity checks, and
  ordinal scheduling policy. `threads.cpp` no longer includes Windows headers or calls native thread/
  Interlocked APIs; renderer handoff/count and SP timeout state use explicit seq-cst atomics. All
  initial starts route through the one-shot lifecycle API, SP handle-to-int conversions and four
  callback casts are gone, and old four-CPU affinity globals/`Win_InitThreads` were removed. Forced
  suspension is named and callable only as terminal crash handling; the unfinished SP executable
  handoff now reports unavailable before synchronization or renderer teardown.
- Previous scheduling-policy batch: the opaque thread service owns truthful scheduling policy. Fixed-width
  enums distinguish applied, unsupported, permission-denied, and unavailable results; priority uses
  exact Win32 levels and a conservative POSIX policy, while processor affinity is expressed as an
  ordinal into a backend-owned eligible set rather than a public mask. Linux snapshots a dynamically
  sized sparse cpuset for pin/restore, macOS reports hard pinning unsupported, and Windows keeps its
  current-group `DWORD_PTR` masks private. Terminal crash-only suspension has a separate status API
  with no resume counterpart. ABI and runtime tests cover every status/function safely.
- Previous renderer-consumer batch: both renderer workers use the cooperative gate at command-free safe
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
- Portable validation: the current 40-test utility suite passes GCC, Clang, ASan/UBSan, and TSan. The
  expanded generation-sidecar/pool transaction fixture passes strict x86-32 and AArch64 compile/link;
  resource-pair and native user-geom tests retain their strict standalone coverage. Scoped source contracts
  exercise live spawn/draw/free/lifecycle ordering, native-construction bounds, canonical archive tokens,
  two-domain archive commit/rollback, and the complete physics/archive lock boundaries. Leak
  detection remains disabled because LeakSanitizer cannot run under the command-runner ptrace environment.
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
  Worker-gate run 29135580627, renderer-consumer run 29135757396, and scheduling-policy run
  29135831489 each passed all nine jobs in sequence. High-level opaque-thread run 29136380656 and
  dvar-atomic run 29136530880 then each passed all nine jobs as well. Fixed-width atomic-boundary run
  29136863094 also passed all nine jobs, including the MSVC intrinsic backend on amd64 and ARM64.
  Script-lifetime run 29137883793 passed Linux amd64/arm64 and macOS arm64, but exposed three
  Windows-engine `ARRAYSIZE` compile errors and an MSVC `/W4 /WX` interaction with the test-only
  over-aligned script layout. This batch replaces those three uses with `sizeof`, keeps Windows64
  layout compilation under a local C4324 suppression, and splits portable build/test CI steps;
  corrective XAnim/DObj run 29176960257 passed all nine jobs. Diagnostic-overlap commit `c400a27`
  then passed all nine jobs in run 29177286439. Database I/O/recovery commit `cfd9045` subsequently
  passed all nine jobs in run 29177998144. IWD/unzip/loopback commit `aa91d37` then exposed two
  MSVC-only fake-lag pointer-conversion errors; corrective commit `0a119b7` resolved them, and
  replacement run 29195736931 passed all nine jobs. Skeleton/pose commit `060e6ba` then passed all
  nine jobs in run 29196678355. Renderer-reservation commit `0fddf2d` then passed all nine jobs in run
  29197855220. Worker-queue commit `9772706` passed headless and all five portable jobs in run
  29199400717, while all three client builds found the same MSVC const mismatch in
  `R_AddDObjToScene`; focused correction `33bdd81` then passed all nine jobs in replacement run
  29199666846. DObj/model-surface run 29201767094 then passed all five portable jobs but failed all
  four Windows engine jobs at compile time. Corrective commit `89a6122` cleared every one of those
  seams: run 29203597111 passed all four Windows engine jobs and the three POSIX portable jobs, but
  both MSVC portable-test jobs failed on fifteen C4267 narrowings confined to the new
  `db_validation_tests.cpp`. Corrective commit `78d72b1` casts those counts, and replacement run
  29203923350 passed all nine jobs. Upstream integration commit `6c86e83` subsequently passed all nine
  jobs in run 29204498860 before review follow-ups.
  The observed linker debt is now 106 -> 45 -> 0.

## Milestone status

| Milestone | Status | Current evidence / next gate |
|---|---|---|
| M0 build/CI foundation | Partial | Windows x86 client/legacy-dedicated builds, a green Release headless-dedicated compile/link gate, retained headless artifact, protected legacy/headless gameplay-smoke definitions, and five native utility-test runners exist. The licensed headless smoke has not run, and release workflows remain Windows x86-only. |
| M1 compiler/ABI hygiene | Partial | `platform_compat.h`, `kisak_abi.h`, the cross-compiler `Sys_Atomic*` boundary, portable compile/contention tests, an exact ABI debt ledger, native-width database enumeration/IWD search contexts, fixed-width fast locks, native dvar/script/XAnim/DObj/database/IWD/loopback/skeleton/pose/EffectsCore, bounded renderer/model-surface reservations, and a typed fixed-width worker queue exist. The executable engine has zero direct `Interlocked` calls; remaining work is broader raw-width/layout debt and platform integration. |
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 46 pointer fixes, tripwire, remote-input hardening, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, all 50 direct references bounded, pre-publication material/sound/world/model/surface/physics/clipmap-brush/portal/path/FX graph and state validation, build-mode-specific asset admission, bounded runtime material/collision consumers, complete graphics-world AABB topology validation, bounded XSurface/XModel skin/skeleton/collision contracts, transactional FX pool/handle ownership validation, allocation-safe ODE body/user-data/model-collision construction, and a bounded transactional native-width physics pool allocator have landed or are in the current reviewed batch; production-path fuzz fixtures and the load-object bounded cursor remain. |
| M3 platform services | In progress: thread, memory, and filesystem enumeration integrated | Portable contracts and target-owned source sets select tested native Win32/POSIX clock, sleep/yield, recursive/reader-write lock, opaque event/thread lifecycle, processor/priority policy, virtual-memory lifecycle, UTF-8 mkdir/cwd/executable paths, bounded directory enumeration, and a cooperative worker gate used by renderer workers. Linux/macOS engine/headless sets remain empty and engine-gated; handle-relative recursive deletion, POSIX crash freezing, process/console, and socket backends remain. |
| M4 runtime 64-bit ABI | First runtime families in progress | XAnim tree/table, DObj runtime/saved layouts, allocations, preview buffers, SP corpse pointers, EffectsCore effect/pool handle codecs, ODE user-geometry storage, and the generic physics pool allocator are native-width exact. MP `cpose_t::physObjId` and `BreakablePiece::physObjId` still store ODE pointers in `int32_t` and are a hard native64 blocker; XAnimParts/XAnimIndices, the script VM, most runtime structures, and asset payloads also remain 32-bit-layout-bound. |
| M5 disk32 widening loader | Seed plus provenance registries | `disk32::PointerToken`, a native-width typed alias/completed-slot side table, all legacy direct references migrated to bounded resolution, 23 full-span raw/POD fields, one bounded completed script-string-handle array, exact registered direct strings/holders, graph-validated clipmap brush, portal/cell, and path-tree spans, and 18 exact completed object types exist; packed mirrors, broader completed-object relocation, and runtime widening remain. |
| M6-M14 target deliverables | Not started | No non-Windows or 64-bit engine target builds yet. |

## Target matrix

| Target | Engine status |
|---|---|
| Windows x86 | MP and legacy dedicated compile in Debug/Release; dependency-free headless dedicated also links in Release; licensed gameplay smoke is deferred because its runner and secret are not provisioned. |
| Windows amd64 | Utility tests only; engine gated by ABI/asset/dependency work. |
| Windows ARM64 | Utility tests only; engine gated by ABI, ARM, renderer, and dependency work. |
| Linux amd64 | Utility tests only; engine configuration intentionally blocked pending POSIX/headless work. |
| Linux arm64 | Utility tests only; same blockers plus ARM determinism/dependencies. |
| macOS arm64 | Utility tests only; same blockers plus SDL/Vulkan/MoltenVK application integration. |

## Immediate queue

1. Replace `FX_RestoreEffectDefTable`'s fallible stack parser with a bounded no-longjmp transaction. Add
   `MemFile_TryReadDataNoReport` (and the narrow CString cursor built on it), decode each key explicitly as little-endian,
   reject truncation/capacity/name failures before registration or table publication, and hold the 1,024-entry candidate
   in a checked BSS lease whose error-abandon path cannot leak. Release that lease before `FX_BeginArchive`, PHYSICS, or
   any reporting call. Then measure the Windows x86 production frame and enforce source-scoped MSVC plus portable
   extracted-TU stack limits; follow with a save-side definition snapshot outside database read ownership.
2. Add packed FX savegame Disk32 schemas, native handle remapping, opaque effect-definition keys,
   and byte-level malformed/round-trip fixtures; retain the legacy x86 writer until equivalence is
   proven. Fast-file `FxEffectDef` widening remains a separate nested-payload batch.
3. Replace the 114 XAnim/XModel `Buf_Read<T>` and adjacent raw/string reads with a transactional
   `current/end` cursor plus count, bone, weight, triangle, and string bounds.
4. Keep the licensed-content smoke deferred and do not dispatch it while its required self-hosted runner
   and `KISAKCOD_GAME_DIR` secret are absent. Implement the designed handle-relative recursive deletion
   service without symlink/reparse traversal instead; surface the smoke infrastructure blocker if asked.
5. Extract standard-stream console services, then process/event services and Linux signal-park plus
   macOS Mach crash freezing behind the already isolated terminal API.
6. Continue M1/M5 ABI cleanup and production fast-file fixtures/fuzzing.

## Known release blockers

- Headless source composition now configures, compiles, and links. Runs 29121929895, 29127753640,
  and 29128702142 reduced unresolved symbols from 106 to 45 to zero while keeping all established
  jobs green. The binary is not release-ready until the protected licensed-content startup/map-load
  smoke eventually succeeds; the local runtime batch fixes its known base-path, redirected-output,
  GUI-error, and exit-status blockers. Testing is currently deferred and must not be dispatched because
  neither its self-hosted runner nor `KISAKCOD_GAME_DIR` secret exists. Twenty-one client/media
  includes remain allowlisted. Headless script-created console channels currently retain the
  default script channel because the client console filter graph is absent; extract a shared channel
  registry if per-channel filtering is required for dedicated administration.
- The exact headless closure still contains 182 C++ and 19 C translation units. Twenty-six files
  directly include 36 Windows-only headers, and the remaining service surface is concentrated in
  thread/atomics, directory enumeration/database I/O, sockets, and console/process.
  The extracted source sets are intentionally incomplete on Linux/macOS; do not relax the engine
  gate until real POSIX backends populate them.
- Native time and recursive critical-section services are selected and runtime-tested. The custom
  fast lock now routes every dvar/database reader and writer counter operation through shared
  sequentially consistent helpers, with source guards and reader/writer stress coverage. Broader
  engine state still contains Windows `LONG`, direct volatile polling, and Windows-header coupling,
  although the executable direct-`Interlocked` census is now zero and dvar sorting plus all state in
  `threads.cpp` use portable atomics. Finish the remaining raw polling/layout migration before compiling
  a non-Windows engine target.
- The renderer worker queue now uses guarded fixed-width element cursors, one full-lifetime work
  count, typed native descriptors, aligned scratch, and unconditional notification; the racy minimum
  hint and x86 payload literals are gone. The shared manual event still uses its inherited 1 ms poll
  as a bounded fallback around external renderer/gate wakes, and worker threads/events have no
  shutdown or safe reinitialization lifecycle. Full-ring inline fallback can still overtake older
  same-type queued work, and handler `Com_Error`/longjmp bypasses completion accounting. Add a
  generation-aware event wait, error-unwind cleanup, ordering fixtures, and explicit teardown before
  embedding or restarting the renderer in-process.
- The DObj/model-surface stream is now planned, aligned, bounded, native-width, and validated from
  producer through worker and scene consumers. One bounded frame-level inefficiency remains: the
  surface arena is reserved before the independent vertex arena, so a later vertex-capacity failure
  burns that surface slice until the next frame. Static XModel draw-list `surfId` decoding also still
  trusts producer state instead of a shared bounded accessor. Fallible XAnim map construction is
  performed before DObj reservation/locking; make that tree mutation itself transactional if DObj
  creation becomes concurrently reservable. Address these without weakening the exact DObj framing contract.
- EffectsCore's former 61 native atomic calls, iterator/kill gates, garbage-collection request,
  visibility publication, freelists, active counts, ownership sidecars, and native handle codecs use
  checked portable boundaries. Restore reconstructs pool ownership only from bounded acyclic lists;
  archive, draw/update/profile/sort, spawn, removal, GC, kill, and rewind paths validate their complete
  traversed state before publication. Bounded CAS helpers prevent packed reference/owned/pending/lock
  fields from carrying into neighbors, and a durable admission marker prevents killed records with
  external references from re-entering the graph. Generation-tagged native physics ownership now covers
  live spawn/draw/free/spotlight/reset/init/shutdown and transactional legacy-x86 archive replacement;
  admission revalidation prevents old iterator jobs from crossing lifecycle generations. The merged full-capacity
  and restore-workspace batches use exact silent topology recipes, bounded retirement/reconstruction, preserved
  archive exclusion, canonical safe-empty reset, fail-stop handling after lost native ownership, and checked heap
  transaction/validation scratch including early malformed-graph preflight. PR #16 adds
  executable normal admission, durable partial-cleanup ownership, fail-closed typed values, and checked production
  integration. The current branch completes competing non-FX occupancy and the silent production ODE mutation boundary.
  Remaining blockers are camera/scalar publication, real Disk32 FX archive/fast-file conversion, the effect-table parsing
  boundary, and measured stack/runtime enforcement.
  `R_FilterXModelIntoScene` still retains an
  FX element's cached-lighting handle pointer beyond the per-effect draw lock; current renderer scheduling
  consumes it before the next FX mutation, but a generation-aware scene-owned cache is required before that
  ordering can be relaxed. The unbounded load-object cursor is tracked separately under XAnim/XModel.
- MP `cpose_t::physObjId` and `BreakablePiece::physObjId` remain frozen `int32_t` fields that publish and later recast
  ODE body pointers. That is valid only for the current x86 engine and truncates on native64; introduce a generation-
  checked token/sidecar (preferred where ABI must remain frozen) or native-width runtime storage before enabling any
  64-bit engine target. The current pointer-truncation tripwire intentionally records rather than hides this blocker.
- XAnim tree/table ownership and DObj runtime storage are native-width-safe, but the animation payload
  is not: `XAnimIndices` and `XAnimParts` still freeze the retail 32-bit layout, `XAnimClone` still
  allocates 88 bytes, and load-object code contains matching 32-bit payload assumptions. The actual
  native `XAnimParts` size is 0x88 on 64-bit. Split the disk mirror from the widened runtime payload
  before treating any 64-bit XAnim translation unit as buildable.
- XModel physics collision still treats `BrushWrapper` as a common-prefix `cbrush_t` view. Shared field
  offsets match the supported x86 ABI, but unrelated-type aliasing, weaker wrapper alignment, and
  hardcoded 80/68-byte loader records make it invalid for runtime64. Resolve it with the Disk32/native
  XModel physics schema instead of hiding the issue behind another cast.
- Native event and thread services are selected, runtime-tested, and used by high-level orchestration.
  Renderer workers park cooperatively; private opaque handles own creation, identity, priority, and
  ordinal affinity; `threads.cpp` is free of native Windows threading APIs. Fatal-error freezing is
  quarantined behind a terminal-only operation: Windows has a checked implementation, while Linux
  signal parking and macOS Mach suspension remain required before useful POSIX crash stack capture.
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
- Rigid and deformed model surfaces now validate exact vertex buckets, blend byte extents, aligned
  bone offsets, part-bit membership, bounded explicit-weight sums, rigid coverage, collision
  partitions, and collision-tree topology before publication; the worker repeats the skinning
  semantics before execution. Collision-tree scales intentionally allow positive infinity because
  the legacy source builder divides by zero on planar axes; fix and fixture-test degenerate-axis
  encoding before requiring finite scales. The load-object model/surface/physics readers still use
  the global unbounded, potentially unaligned `Buf_Read<T>` primitive; replace it with a checked
  `current/end` cursor before treating local model files as hostile-input safe or ARM-clean.
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
- Independent IWD readers still reopen an archive by pathname. Selected entry/count validation
  catches many replacements, but an on-disk path swap with a compatible directory remains a file-
  identity TOCTOU until the filesystem service can reopen the same underlying file object. Legacy
  unzip CRC verification is disabled, and its public/runtime records still use native
  `unsigned long` plus signed-int file sizes, limiting safe archives to below 2 GiB.
- Licensed gameplay smoke and release packaging workflows have not run.

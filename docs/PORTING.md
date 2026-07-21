# KisakCOD Porting Plan — Win64, Linux, macOS

**Status:** implementation in progress · **Scope:** MP client + headless dedicated server
**Basis:** whole-tree analysis of the current `master` (see `docs/CODEBASE_AUDIT.md` for the health report).
**Live checkpoint:** [`docs/task.md`](task.md) tracks the current batch, CI evidence, debt counts, and next queue.

---

## Implementation status (July 20, 2026)

Target policy is fixed: preserve retail assets and wire interoperability; use a
shared **native Vulkan RHI** (MoltenVK on macOS) that replaces D3D9, OpenAL Soft,
and FFmpeg; publish portable packages for Linux; and require native CI plus
licensed gameplay smoke tests.

**Committed scope is the MP client + the (headless) dedicated server. Single-player
is deferred** — SP-only serialization surfaces (save-games) and SP subsystems are
documented for completeness but are off the current critical path.

Completed foundation work:

- the earlier audited upstream/gameplay reconciliation checkpoints, PR #48's report-free script-string ownership
  foundation, PR #49's
  constructed lifecycle controller, PR #50's failure-atomic script-string initialization hardening, and PR #51's
  generation-keyed runtime table plus PR #52's test-fixture repair, PR #53's authenticated memory-tree validation
  lease, PR #54's exact terminal adapters, PR #55's pointer-free script-string OwnershipBatch, PR #56's physics-sidecar
  authority seal, PR #57's exact-key mutable runtime adapters, and PR #58's portable console through `9fb46bea`.
  PR #51 exact-head run
  **29628040709** and post-merge master run
  **29628132007** each passed eight of nine jobs; Windows x86 Debug alone exposed a missing test-fixture
  `MyAssertHandler` definition required by `qcommon/sys_sync.cpp`, while the production targets and other eight jobs
  passed. PR #52 supplied the established aborting fixture boundary without weakening assertions or changing production
  code and squash-merged as `e792c160`; exact final run **29628599645** and authoritative post-merge run
  **29628940419** passed all nine jobs. PR #53 exact final run **29649484692** passed all nine jobs before squash merge
  `445d436f`; authoritative post-merge master run **29649890520** also passed all nine jobs. PR #54 exact final run
  **29650796617** and authoritative post-merge run **29651211711** passed all nine jobs before and after squash merge
  `8e7fd162`. PR #55 exact-head run **29657884407** passed all nine jobs, exact-head hosted Codex review was clean,
  and the production-neutral OwnershipBatch squash-merged as `f39e0e4a`;
- the merged report-free script-string ownership foundation: dedicated recursive outer DB serialization,
  private journal callbacks, exact ordinary/database-user ownership results, full allocator-backed byte-count/hash/debug
  validation, rejection of packed-length-ambiguous earlier NULs, and failure-atomic memory-tree allocate/query/free
  operations with assertion-free typed commits and complete disjoint-partition/corruption fixtures. Typed APIs preserve
  exhaustive 65,536-bucket/free-forest validation. Production legacy queries authenticate only their allocation interval,
  while mutations authenticate all free-tree heads and the fixed-width allocation, membership, topology, and dual-count
  mirrors on every path they consume or change. Exact allocator-class recovery preserves compact non-NUL legacy binary
  records. Typed final release validates the complete bounded free list; legacy release validates its complete collision
  chain and local free-list splice, while global shutdown/transfer perform one complete linear preflight followed by a
  physical-entry mutation pass. Collision validation records and clears only entries touched by its preceding chain walk;
  a deterministic public-path counter prevents whole-table scratch resets from returning. Fixed-width score decoding
  avoids inactive-union-member reads. Comparable GCC Release
  measurements improved from roughly 135/766/1,056 ms to **2.692/1.977/4.148 ms** for 2,000 unique, 10,000 repeated, and
  200 same-length 300-byte interns; a 4,096-singleton legacy allocator probe is about **0.003 ms**, with deterministic
  counters proving no complete partition, forest, free-list, or collision-scratch scan on those legacy paths. The
  adapter has no production caller, and exactly seven raw user-4/user-8 ownership/sweep sites (including the 4 -> 8
  transfer and retirement sweep) remain outside the serializer;
- a merged constructed, production-neutral lifecycle binding from PR #49. One fixed controller acquires
  the dedicated serializer and binds an exact Loading lifecycle slot/key, journal, fixed backing span/count, private
  token serial, and owning thread through terminal return. It stages ordinary references with failure-atomic ID output,
  seals and transfers one occurrence at a time, prepares reversible `CommitReady`, then either publishes lifecycle
  `Live`, unconditionally finalizes/detaches the journal, performs infallible admission, and releases serialization last,
  or first convergently unpublishes staged IDs, publishes `Abandoning`, reverses exact journal ownership, detaches backing,
  completes lifecycle cleanup to `Empty`, and retains an authenticated `Abandoned` receipt until explicit reset. Retry,
  callback reentry, stale/swapped binding, foreign-thread exclusion, poisoning, and receipt authentication have dedicated
  runtime/source-contract coverage; the exact GCC Debug suite is **105/105** green. This is not production wiring: the
  stream, registry generation claims, PMem, arena/adapter, aliases/completed objects, real callbacks, and Live-unload
  route do not use it. All seven frozen raw ownership/sweep sites remain outside it. Merged PR #55 builds the
  pointer-free string `OwnershipBatch` on the active allocator lease, but typed production integration still must borrow
  or own exact transaction authority and enroll the legacy sites atomically;
- merged debug-initialization hardening from PR #50. Independent `SL_InitCheckLeaks` calls now retain the
  recursive script-string lock from state inspection through reset and pointer publication; duplicate calls unlock before
  diagnostics and leave live accounting untouched even without assertions. `SL_Init` rejects already-published full or
  debug-only state before allocator/hash mutation. Foreign-thread pause/resume and live-reference helper/full duplicate-
  init regressions and source-order gates pass at exact head `8080f44f`, including the complete GCC Debug suite **105/105**
  and focused GCC Release. The helper-only precursor also passed GCC assertion-disabled/`RELEASE_ASSERTS`, Clang
  ASan+UBSan, and 50 repeated concurrency runs. Gemini and the exact independent re-audit were clean; exact-head run
  **29627237107** and authoritative post-merge run **29627591759** passed all nine jobs before and after squash merge
  `eeca68ba`;
- merged generation-keyed runtime-table checkpoint from PR #51. A fixed production-owned table provides
  stable storage for all 33 physical slots while reserving slot zero and exposing only slots 1..32. Every durable entry
  owns the exact lifecycle slot, script-string ownership controller, and by-value generation key outside zone PMem.
  `DB_Init` initializes pristine storage before asset-pool mutation and fails closed at the existing fatal boundary.
  Checked lookup, generation claim, and read-only keyed views reject invalid/default slots, stale/cross-slot/ABA keys,
  partial initialization, corrupted or poisoned state, impossible lifecycle/controller phases, and callback phases without
  retained serialization. The view copies its key so stable entry addresses cannot silently upgrade stale authority.
  The complete GCC suite is **107/107** green; Clang, ASan+UBSan, strict x86 layout, AArch64 cross-compile, source, and
  diff gates pass. Independent review found and closed mutable-view authority and incomplete canonical/poison/phase/
  serializer checks, then reported the exact transplant clean. PR #51 squash-merged as `beb2925d`; its exact-head run
  **29628040709** and post-merge run **29628132007** each exposed only the same Windows x86 Debug fixture-link omission
  after eight jobs passed. PR #52 repaired the fixture and squash-merged as `e792c160`; exact final run **29628599645**
  and authoritative post-merge run **29628940419** passed all nine jobs. PR #54 merged exact-key terminal reset and
  Live-unload adapters as `8e7fd162`; production claims/consumption, load/stage/commit routing, real cleanup
  callbacks, PMem/adapter binding, and all seven raw ownership/sweep sites remain deliberately absent;
- merged PR #53 authenticated memory-tree validation-lease checkpoint. Implementation `34b91875` and contract coverage
  `2154e423` retain the recursive memory-tree lock across one serialized transaction, with distinct Complete,
  LegacyLocal, and Leased policies. Begin and finish each authenticate the full Basic+Forest+Partition state, while
  leased allocate/query/free operations retain PR #48's bounded mirror-aware touched-path validation. Overflow-safe
  serial/mutation accounting, exclusive same-thread ownership, and fail-closed poisoning reject stale, foreign, nested,
  corrupted, exhausted, or unleased access. A private admission capability reserved production construction for the
  script-string `OwnershipBatch`; merged PR #55 now supplies that batch, still without changing a production caller;
- PR #53 lifetime hardening `b193343b` closes Gemini's valid exact-head `fc496b01` stack UAF finding. The generic
  registry now stores only mirrored by-value address, serial, and Idle/Active/Poisoned/Frozen lifecycle state and never
  dereferences stored stack authority. Independent mirrored TLS identity proves the retained owner acquisition before an
  exact destructor releases it. Exact abandonment publishes process-lifetime `Frozen`, clears identity, and releases the
  authenticated lock; torn or unauthenticated destruction freezes and clears identity but retains the unproven base
  acquisition. A skipped destructor may therefore retain exclusion but cannot create a later global UAF. `Frozen`
  survives `MT_Init`, has no production reset, and report-freely rejects every typed, legacy, leased, raw, reset, query,
  validation, and reporting traversal path with output/state atomicity. Canonical unrelated/finished destruction is a
  no-op, the lease remains 16-byte standard-layout but is intentionally non-trivially destructible, and test-only thaw is
  macro-gated and authenticates retained TLS mirrors before releasing anything (`90f7e0e0`). The GCC Release suite at
  `b193343b` is **107/107** green. Follow-ups `19602b84`/`847ff969` authenticate global/local by-value identity and
  serialize foreign snapshots and lease calls across normal Finish while caller-owned storage remains live;
  `81f41b84` gives each raw mutator one locked
  reject/validate/commit interval. `847ff969` closes the remaining legacy check/use windows, bounds raw size/score/
  subtree/node/pointer inputs, restricts subtree traversal to a completely authenticated free forest, and captures one
  fixed-BSS authenticated dump image before releasing the memory-tree lock and emitting numeric IDs. Reporters,
  assertions, formatting, and script-string callbacks cannot run under the allocator lock. Exact follow-up `847ff969`
  passes focused GCC Release/`RELEASE_ASSERTS`, the production ownership fixture, Clang ASan+UBSan, 50 repeated locking
  runs, strict fixture/production i386/AArch64 compiles, source/security, and diff gates. Independent Clang MS-compat
  and clang-cl x86/x64/ARM64 excerpt checks also place the fixed snapshot/flag in BSS; hosted Windows CI remains the
  authoritative Microsoft STL/SDK integration check. Exact final run **29649484692** passed all nine hosted jobs before
  squash merge `445d436f`;
- merged PR #54 production-neutral terminal-adapter checkpoint. `d2740fb2` adds retry-safe Live-unload ownership while
  retaining exact key, lifecycle, callback identity, and the outer transaction serializer through completion;
  `7764af22` adds exact-key runtime-table Live-unload and terminal-receipt reset adapters; `dc4aee23` hardens the
  fault-test argument parser; `74002a69` rejects lifecycle generations hidden behind an empty durable table key; and
  `0eac1f2d` makes fixture parsing exact and pins the public enrollment/receipt contracts. Exact
  Abandoned/Unloaded receipts reset only controller ownership, retain lifecycle/table generation evidence until the next
  claim, and reject cross-slot, stale, ABA, swapped-callback, reentrant, malformed-phase, and corrupt-state use. These
  adapters remain unenrolled: the production loader does not yet claim a generation or route load, staging, commit,
  PMem, or cleanup through them. Review cleanup `e8d7a3f6` removes a redundant post-authentication branch without
  changing the fail-closed result. Codex cleanup `5bee8bba` rejects missing, extra, unknown, and malformed fault-runner
  arguments and enrolls three negative CTest gates;
- terminal-adapter validation passes the complete GCC Release, GCC Debug, and Clang Release suites at **117/117**; the
  focused terminal runtime/source selection at **12/12** under GCC, Clang, `RELEASE_ASSERTS`, and Clang ASan+UBSan; 50
  repetitions apiece across the ownership/retry/reentry and unsafe-boundary matrix (**400/400** invocations);
  source/security contracts; strict i386 and AArch64 compilation; and `git diff --check`. Exact final run
  **29650796617** and authoritative post-merge run **29651211711** passed all nine jobs;
- merged PR #55 production-neutral script-string OwnershipBatch checkpoint. Implementation head `e9051955` adds a fixed
  0x20 standard-layout batch that retains
  SCRIPT_STRING before its nested memory-tree lease, pays complete allocator/string validation at Begin and Finish, and
  authorizes only four report-free bounded ownership operations in between. Mirrored by-value outer/serial/nested/
  lifecycle state plus independent TLS proof replace stored stack pointers. Exact abandonment freezes and releases only
  independently proven acquisitions; torn authority remains retained fail closed. The legacy surface serializes or
  rejects during a batch, `RefString` is opaque outside `scr_stringlist.cpp`, exact reader/mutator validation precedes
  access, diagnostics occur after unlock, canonical reset fails fast callback-free, and all character folding uses
  unsigned-char inputs. `d5f6c9ac` authenticates debug ownership per live ID and gates all five nested lease entries with
  one non-forgeable exact-address capability; exact-head run **29656098733** passed all nine jobs. Final hardening
  `e9051955` removes the reproducible `OwnershipBatchAccess` and `MT_ValidationLeaseAccess` friend shims, confines state
  mutation to private members, and passes capability-authenticated state only through a stack-local anonymous-namespace
  view. The `constinit`, trivially destructible capability emits no guarded first-use or shutdown registration on native,
  i386, AArch64, or an MSVC-ABI probe. A macro-off compile target rejects ungated operations, test accessors, and private
  lifetime/state helpers. Invalid concurrent-destruction fixtures were replaced with legal owner-Finish/waiter-join
  coverage plus separate same-thread destructor abandonment. GCC Release is **117/117** green; focused
  `RELEASE_ASSERTS`, Clang ASan+UBSan, and TSan are **4/4**;
  strict i386/AArch64 production-mode API-seal and fixture compile/link, production-TU cross-compiles, source/security
  contracts, and `git diff --check` pass. The managed runner blocks the new i386 binaries with its established `SIGSYS`;
  hosted Windows remains the executable production-width authority. Two authority/lifetime audits and a portability
  audit are clean. Exact-head run **29657884407** passed all nine jobs, exact-head hosted Codex review was clean, and
  PR #55 squash-merged as `f39e0e4a`. The batch remains unenrolled and callback-free, restricted to its four typed
  operations;
- merged PR #57 exact-key mutable runtime-table adapters add ten pre/post-authenticated mutations, preserved recoverable
  status values, publish-after-authentication output,
  and no public mutable table authority. A normal positive-build macro-off seal independently denies all five private
  table/entry capabilities to a recreated same-name test helper. GCC Release passes **124/124**; the focused keyed and
  PR #56 seal selection passes **19/19** under GCC and Clang warnings-as-errors, and strict i386/AArch64 production and
  fixture compilation passes. A terminal-specific result allowlist also keeps the existing Live-unload path fail closed
  on the three journal-only recoverable values. Independent exact-diff review found that issue, verified its correction,
  and reports no remaining actionable finding. Exact-head run **29659895814** and authoritative post-merge run
  **29660281653** passed all nine jobs, exact-head Codex review was clean, and the batch squash-merged as `57e2b1a2`;
- merged PR #56 physics-sidecar authority seal closes the separate macro-off authority leak found by the
  production-friend audit. Both `SidecarTestAccess` forward/friend declarations are now test-macro gated, and an
  ordinary macro-off executable recreates the name and uses two dependent access predicates plus negative static
  assertions to prove neither private ownership nor lifecycle mutation is available. Portable builds compile and run the
  positive seal normally, while measured Windows x86 builds and selects it explicitly. Native GCC/Clang Release build and
  pass **118/118** tests; strict i386/AArch64 fixtures and seals compile, and the same seal fails both assertions against
  the old friend-bearing baseline. `[[maybe_unused]]` preserves the test-bypass field's layout while keeping AppleClang
  Release warning-clean after the friend removal. Live-FX/security source contracts and `git diff --check` pass. Exact
  final head `c2613282` passed all nine hosted jobs in run **29658932268**, exact-head Codex review was clean, and PR #56
  squash-merged as `6159275e`. Authoritative post-merge master run **29659347033** also passed all nine jobs; no other
  project-owned production friend leak was found;
- merged PR #58 adds platform-neutral length-based stdout/stderr writes, flush/redirection queries,
  and bounded allocation-free line input with Win32/POSIX backends. POSIX broken-pipe writes contain `SIGPIPE` on the
  calling thread while preserving masks/pending state; Win32 pipe flush is nonblocking and message-mode reads preserve
  `ERROR_MORE_DATA` bytes. Runtime coverage spans the 4,096-byte drain budget, embedded NUL/overlong input, partial EOF,
  default/ignored SIGPIPE, Win32 message pipes, native i386, AArch64 compile, sanitizers, and Wine. Redirected Win32
  headless input works; attached native character-console input remains an explicit follow-up. Exact-head run
  **29666269398** passed all nine jobs, exact-head Codex review was clean, the nullable fatal-message finding was fixed,
  and the batch squash-merged as `9fb46bea`. Authoritative post-merge run **29670244884** also passed all nine jobs;
- merged PR #59 canonically plans one 32-bit-offset slab holding
  the script-string journal/entries, native FX arena, Disk32 adapter workspace, and aligned arena backing. Its stable,
  noncopyable handle stays outside the slab, authenticates itself and the complete plan/pointer layout, and prevalidates
  alignment, address ranges, capacity, and overlap before placement construction. Exact teardown permits only a pristine
  or terminal detached journal, idle/non-operating adapter, and transaction-free arena bound to the planned backing and
  budget, then destroys in reverse order with idempotent terminal state. A private FX bridge and read-only lifecycle
  predicates avoid exporting mutation or generation authority. The exact `9fb46bea` replay passes GCC Release
  **131/131**, GCC i386 runtime, AArch64 GCC strict syntax for every batch translation unit and fixture, and Clang
  ASan+UBSan runtime with leak detection disabled because LSan is unavailable under ptrace; the headless/debt gate, ABI
  scanner, source invariants, and `git diff --check` pass. Exact head `8cec770d` passed all nine jobs in run
  **29671392540**, and exact-head Codex, Gemini, and independent audits were clean. The source family is build-enrolled,
  but no production loader or lifecycle caller consumes this API, no runtime generation or PMem scope is claimed, and
  retail bytes are unchanged. It squash-merged as `ff61504e`; authoritative post-merge run **29671849514** passed all
  nine jobs;
- merged PR #60 checked-PMem foundation at exact final head `0eec9b1e` provides report-free, failure-atomic `TryBegin`,
  `TryEnd`, and `TryFree` over exact typed allocation entries. It validates both complete 32-entry prim topologies before
  mutation, including the low-prim base, monotonic low/high positions, legitimate middle holes, and typed tail collapse.
  Its stable-address receipt is noncopyable, nonmovable, nontrivial, self-authenticating, single-use, and protected by a
  phase witness. External serialization, mutually disjoint control/receipt/managed-backing storage, stable name lifetime,
  and no-bypass/reinitialization constraints are explicit. The exact `ff61504e` replay passes GCC Release **134/134**;
  earlier focused GCC/Clang, Clang ASan+UBSan, Clang static analysis, strict i386/AArch64, source/API/security, and diff
  evidence all pass. Exact-head run **29673379640** passed all nine jobs; Codex reviewed exact final head `0eec9b1e`,
  Gemini reviewed identical code head `f04c63e0`, both were clean, and zero review threads remained before squash merge
  `74916b5b`.
  Authoritative post-merge run **29673608169** passed
  all nine jobs at that exact merge commit. It is build-enrolled but has no production caller. PR #68 later removed the
  legacy native64-invalid `PMem_FreeIndex`/`PMem_EndAllocInPrim` handling; the serialized global PMem boundary and
  `$init` lifecycle remain production blockers;
- merged PR #61 adds an exact lifecycle-key receipt and one reusable controller for process-wide stream and relocation
  singleton state. `b2737088` prevalidates the exact nine
  typed `XZoneMemory` spans, layout/cursor parity, disjointness, stable control storage, Loading lifecycle, and idle
  singleton before publishing Bound last. Exact invalidation drops alias/direct provenance, releases retained relocation
  capacity, scrubs all nine block views and the complete delay/stack/scalar state, clears singleton ownership, and
  publishes Invalidated last; terminal retry ordering prevents a stale generation from clearing a newer binding, while
  epoch exhaustion fails closed. `f99982c8` supplies runtime/source/security/seal and portable-host selection.
  `da868c9e` closes an independently caught production-neutrality-seal bypass across header enrollment, using or
  unqualified references, manual namespaces, and private capabilities, and rejects a misaligned typed zone identity
  before descriptor access. Follow-up `0c5049e7` closes compile-valid phase-2 line-splice and phase-3 comment-separated
  token bypasses with compiler-validated detector probes. The exact `0c5049e7` re-audit reports **PASS**; the focused
  runtime/source/security selection passes **4/4** under GCC and Clang warnings-as-errors and Clang ASan+UBSan, while the
  full GCC Release replay passes **137/137**. Native i386 runtime, strict AArch64 compilation, source/security/API-seal,
  ABI, headless/debt, and diff gates also pass. Final exact head
  `f9dfaaeb43eaaa32cd44c645e3a0e347c9bebdfc` passed all nine jobs in run **29691282387**; all four Gemini threads were
  resolved and exact-head Codex review was clean. PR #61 squash-merged as
  `32e6de4efc86823020d1a2eef2c473e013f893ba`, and authoritative post-merge run **29691725277** passed all nine jobs. No
  loader, runtime-table entry, PMem lifecycle, pending-copy ledger, coordinator, or raw ownership site consumes it;
- merged PR #62 supplies the production-neutral pending-copy ledger through core `08014141`, protocol `8d6b04f3`, runtime
  hardening `8935b5a73836bcf31a09b9e7d2d0bb920377bd08`, and final source-seal review head
  `a3c21e9db369d02f29b18f4e1208169517353513`. It provides fixed storage for 2,048 ordered by-value asset indices across
  up to eight exact-key generations, noncopyable admission receipts, stable-compacting discard, exact prepared
  completion, retry-safe by-value drain, terminal reset, and fail-closed callback/poison paths. Hardening requires
  strictly increasing descriptor serials below the next serial, rejects overlapping pristine control spans before
  authentication, keeps terminal reset independent of a newer ledger callback, and returns Busy for authenticated
  retained authority during completion/drain callbacks. Pending and stream source seals cover phase-2 splices,
  commented/manual declarations, using/function-pointer references, uncommon whitespace, raw hash/digraph/trigraph paste
  capability, macro-generated header stems, and exact protected tokens across every file below `src`; legacy ODE contact
  macros no longer paste tokens. Local evidence is full native **140/140** at exact `a3c21e9d` plus strict GCC/Clang, Clang
  ASan+UBSan, Clang analysis, i386 runtime, AArch64 strict link, native/AArch64 production seals, and final-head
  source/security/diff gates. The pending API is build-enrolled but has no production caller or
  loader/runtime-table/PMem/raw-site enrollment. Independent audit reports PASS on exact `a3c21e9d`. Deliberate
  assembler-label, dynamic-symbol, or linker-level enrollment remains outside the source seal's accidental-regression
  model and requires portable object/relocation auditing or a visibility redesign before production cutover. PR #62
  initial exact head `b4719e1a` ran **29694616671**: Linux amd64/arm64, macOS arm64, and headless Windows x86 passed;
  portable Windows amd64/ARM64 failed only because MSVC folded two empty test callbacks to one address. Final exact head
  `6a79677f` uses a distinct context identity, preserving production callback identity without relying
  on foldable function addresses. Exact-head run **29694906394** passed all nine jobs; exact-head Codex and Gemini
  reviews were clean and zero review threads remained. PR #62 squash-merged as
  `888d12e6beedd587602f18cf6763ae04cc067470`; authoritative post-merge run **29695353022** passed all nine jobs at that
  exact master commit;
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
  on x86 and 325,928 bytes on native64; the impact workspace is 11,216 and 11,232 bytes, respectively. The zone-owned
  aligned native arena and the guarded stateful zone adapter that drives those converters from the legacy wire walk are
  implemented as portable primitives with exact workspace contracts (774,216-byte x86 / 799,944-byte native64 adapter
  scratch), watermark-ratcheting LIFO arena transactions, nested impact/inline-effect conversion, and
  materialize-commit-then-publish ordering. Arena rebinding now requires an explicit unbind lifetime boundary, and
  publication returns the canonical registered root identity while keeping shallow-owned children arena-backed. The
  stateful
  XBlock/XAsset loader, retail bytes, legacy x86 path, archive writer, and save-side guard remain unchanged pending the
  production wiring and whole-zone ownership/rollback batch;
- exact portable top-level fast-file envelopes for `XAssetHeaderDisk32` (0x4), `XAssetDisk32` (0x8),
  `ScriptStringListDisk32` (0x8), and `XAssetListDisk32` (0x10), plus a pure bounded preflight/iterator layer merged in
  PR #34 as `3e9b51b0`. It enforces the 32768-asset and 65536-script-string limits, count/token parity, checked eight-byte
  record extents, raw signed type range, caller-supplied portable build admission, unaligned exact-stride reads,
  high-bit-token preservation, and failure-atomic outputs without importing native `xanim.h` layouts or changing
  production stream/PMem/zone state;
- an exact `ScriptStringTokenDisk32` (0x4) record and pure bounded script-string header/span/iterator layer merged in
  PR #35 as `3271b8d6`. It computes checked `count * 4` extents before the 65536-entry cap, enforces root and bounded-span
  presence parity, preflights the complete caller-owned array, preserves null/inline/ordinary-offset tokens verbatim,
  rejects the unsupported shared-inline sentinel, reads unaligned entries with exact-stride `memcpy`, ignores trailing
  bytes, revalidates sequential mutation before each publication, and leaves output/cursor state unchanged on failure or
  `End`. Production streams, script-string registration, PMem/zone state, and retail bytes remain unchanged;
- a pure, production-neutral generation-keyed zone-load lifecycle controller. One explicitly constructed 16-byte slot
  outside `XZone` tracks `Empty`/`Loading`/`Live`/`Abandoning` ownership through a nonzero 64-bit generation plus slot key,
  rejects stale/cross-slot/ABA use, fails closed at wrap, and preserves idempotent terminal receipts. Loading abandonment
  has the exact nine-step cancel/abort/unpublish/reset/geometry/native-storage/EndAlloc/free/registry-gate recipe, while
  committed Live unload has a distinct six-step live-owned recipe that cannot replay load-only cancel, adapter abort,
  `PMem_EndAlloc`, or loading-gate/signal work. Retry cursors, callback reentry, unsafe poisoning, corrupt-state
  validation, and internal slot release are covered without production registry/PMem/stream/adapter wiring. The static
  controller slot and callback metadata must outlive zone PMem so they survive `PMem_Free` and can publish `Empty`;
  per-generation arena/workspace/journal/backing belongs in the named PMem scope. The non-atomic API requires one external
  serializer across every transition/query/callback/destruction; cleanup callbacks are convergent ensure-postcondition
  operations. Successful loading keeps admission closed through all fallible work and `PMem_EndAlloc`, publishes `Live`
  through `TryCommit`, then performs an infallible no-drop gate/signal release before dropping that serializer. Exact
  candidate `f8efc613` passed all nine jobs in PR #36 run **29530465823** and received a clean hosted Codex review after
  its sole test-friendship finding was fixed and resolved. PR #36 squash-merged as `15469b3d`, and authoritative
  post-merge master run **29531440687** passed all nine jobs;
- a production-neutral transactional script-string journal at baseline core `70ef96fc`, hardened/integrated checkpoint
  `d158d5e9`, prepare/finalize implementation `aa295367`, boundary-hardened implementation `81ded193`, and exact
  source-contract head `83a545ad`. It binds to one exact generation/slot key, preflights the complete expected count and
  fixed caller-owned capacity before acquisition, and preserves one full nonzero
  `uint32_t` ID for every ordinary-reference
  acquisition, including duplicate IDs. Transfer records the exact `DatabaseUserClaimed` or `DuplicateReleased` result
  per occurrence. The status-bearing pre-publication API performs its final full scan and moves reversible `Transferred`
  to reversible `CommitReady`; after the matching lifecycle controller publishes `Live`, an unchecked, unconditional
  `void noexcept` finalizer only publishes `Committed`, resets flags, and detaches backing. Reverse rollback removes
  still-staged ordinary refs, removes only the targeted database-user ref for claimed entries, and skips duplicates
  already balanced by transfer. Controller
  validation is O(1); complete entry scans occur only on first seal, prepare, or begin-rollback, and one-entry operations
  validate their current entry. The non-copyable exact-0x30 journal never allocates or performs destructor cleanup,
  detaches backing at terminal finalization/rollback, rejects callback reentry as `Busy`, and makes every status-bearing
  pre-publication operation fail closed on corrupt, unknown, or unsafe state. The finalizer is deliberately protocol-only:
  it does not validate, branch, scan, invoke callbacks, or report status, and callers must invoke it only after successful
  matching prepare and `Live` publication. One global transaction serializer must be acquired before initialization and
  held continuously through terminal return; on success it remains held through prepare, `Live` publication, journal
  finalization, and the no-fail/no-drop gate/signal release. It excludes every other journal transaction, all raw
  database-user add/transfer/remove/publication, and the global database-user 4 -> 8 sweep. Overlapping journals are
  forbidden without future shared claim accounting. Production `SL_*`, loader, registry, PMem, and native-pointer wiring
  deliberately remain absent. Disk32 permits 65,536 occurrences while the current packed refcount permits only 65,535
  identical ordinary refs, so the final acquisition may reject and require exact rollback. The journal retains
  future-facing full-u32 IDs, while current runtime adapters must enforce `0 < id < 65536`. Exact code/test head
  `83a545ad` passes the complete GCC and Clang **82/82** suites, strict warning/conversion builds, ASan+UBSan, MSan,
  TSan, strict i386/AArch64 compile/link, production-TU/test-access isolation, and ABI/source contracts. The real
  lifecycle controller is linked into composed success/pre-`Live` failure tests, the finalizer is pinned to four direct
  assignments, and the 65,536-entry path passes through rollback-capable `CommitReady`. Final PR #37 head `376ce097`
  passed all nine jobs in run **29539650666**; Gemini reported no comments, hosted Codex found no major issue, and no
  review threads remained. PR #37 squash-merged as `7a9bce34`, and authoritative post-merge master run
  **29542960583** passed all nine jobs;
- PR #38 merged referenced-fast-file and SYSTEMINFO hardening as `a7c485fd`, centralizing 33 physical database zone slots
  as reserved/default
  slot 0 plus usable slots 1..32, fixes the prior 0..31 listing range, and makes referenced checksum/name formatting
  bounded and failure-atomic. SYSTEMINFO serialization distinguishes missing from present-empty keys, validates exact
  signed-decimal checksum tokens, bounds the terminating byte, preserves prior output on failure, and clears dvar flags
  only after complete publication. Remote referenced-file admission validates every checksum and path before acquiring
  strings or publishing state, rejecting traversal, tokenizer, control, quote, download-list-delimiter, NTFS ADS/drive,
  Windows wildcard/metacharacter, and DOS-device namespace injection. Server-file comparison uses native-width dvar
  string pointers and prefix lengths without legacy narrowing, exact component boundaries, and an exact
  case-insensitive `mods/` namespace. `fs_game` now requires forward-slash relative form because backslash is an
  info-string field delimiter. Exact final head `9fb4fc18` and squash commit `a7c485fd` each passed all nine jobs in runs
  **29551519068** and **29551990840**, respectively; exact-head Codex review found no major issue and no review threads
  remain;
- PR #39 merged the first curated reconciliation of the audited nine-commit upstream delta from shared base `312a9d2e`
  through `ba3c79f3`. Complete cherry-picks `526d59fb`, `9e6c5836`, and `d27803d2` fix the renderer scoped-list indices,
  zero-valued actor sight assertions, and zero-node output initialization. Only the direct blur-radius return from
  `d6b4c5e4` was adapted; its Xbox safe-area/startup changes remain excluded. A portable source contract pins all four
  corrections because three affected SP-only translation units are not compiled by the present engine CI matrix. Exact
  branch head `dd0cfee9` passed all nine jobs in run **29553056639**; Gemini and exact-head Codex review were clean, no
  review threads remained, and the PR squash-merged as `f1007fbb`. Authoritative post-merge master run
  **29553509114** passed all nine jobs;
- PR #40 merged the focused radius-damage correction from `d592fb4a`. Both unobstructed trace paths now return success
  rather than breaking out of the sample loop and falling through to failure. Its portable source contract rejects
  either `break` regression and requires the sole failure return after loop exhaustion. Exact branch head `952fa06d`
  passed all nine jobs in run **29554051839**; Gemini and exact-head Codex review were clean, no review threads remained,
  and the PR squash-merged as `dbc84ec9`. Authoritative post-merge master run **29554663451** passed all nine jobs;
- PR #41 merged the manually adapted navigation changes from `3f256654` and the navigation-only parts of `77404c61`.
  It forwards the real goal position, enables all five intended ignore filters, uses strict full-XYZ distance plus
  visibility, passes the real plane normal, preserves three-component cylinder origins, and excludes upstream's
  squared-radius-as-Z defect and unrelated API churn. Review hardening also fails closed on missing LOS goals and applies
  cylinder/plane constraints before accepting the initial/current node. Exact final head `add3b87f` passed all nine jobs
  in run **29555716499**; both substantive review findings were fixed, exact-head Codex re-review was clean, no unresolved
  threads remained, and the PR squash-merged as `38025fa5`. Authoritative post-merge master run **29556169431** passed
  all nine jobs;
- PR #42 merged the focused `b31ea047` vehicle material-timing adaptation while preserving exact disabled value `-1`;
  zero and other negative states remain valid for reverse motion. Shared SP/MP producer/consumer logic performs defined
  modular 32-bit advancement/interpolation, skips the reserved value in the direction of travel, and uses renderer default
  zero if either snapshot endpoint is disabled. Windows macro-safe numeric limits and standard NaN classification were
  added during review. Exact final head `614bbabc`, squash commit `599dbb88`, and runs **29557006806**/**29557583267**
  passed all nine jobs; exact-head Codex was clean and both Gemini threads were resolved. The legacy producer float-to-int
  range risk remains deferred;
- PR #43 merged the focused missile union/layout corrections from `d592fb4a` plus the complete SP grenade-cache alias
  correction from `77404c61`. Exact MP 0x3c/SP 0x54 layouts, typed missile access, variant-aware teams, script setters,
  item publication, and `predictLandTime` cache validity replace overlapping mover/item aliases. Exact final head
  `d33e80cc` and squash `2e9c19e7` passed all nine jobs in runs **29582100035** and **29582679571**, respectively.
  Gemini's uninitialized fallback-time finding was fixed and resolved, exact-head Codex was clean, and no threads
  remained. SP production translation units remain unbuilt while every CI job uses `KISAK_BUILD_SP=OFF`;
- PR #44 merged exactly four audited `77404c61` safe fixlets: SP friendly-fire melee suppression, removed-snapshot
  Shutdown -> Unlink -> FX/DObj teardown, fixed complete/failed objective commands, and both translation-unit-local actor
  miss caches initialized to `6969.0f`. All unrelated omnibus hunks and three rejected actor-aim behavior changes remain
  excluded. Review hardening normalized CRLF before source slicing, scoped the contract to the measured Windows x86 ctest
  selection, and exposed failed child-mutation diagnostics. Exact final head `da1ea81f` passed all nine jobs in PR run
  **29584250420**; exact-head Codex found no major issue, both Gemini threads were resolved, and no unresolved review
  threads remained. Squash `cbb8bdb0` and authoritative post-merge run **29585012405** also passed all nine jobs;
- PR #45 merged the SP target-table portability checkpoint as `d85ed087`. Its reviewed sequence combines four initial
  implementation/contract commits (`0a1e89b2`,
  `c63bb68e`, `d981527f`, `653fdfdb`) with audit hardening `7ab3e174`, review/CI hardening `d8440271`, and Apple locale
  declaration fix `b9844b2e`. It replaces
  pointer truncation and raw 28-byte walks with bounded typed 32-entry native storage. It freezes `target_t` and
  `TargetGlob` at `0x1c`/`0x384` on x86 and
  `0x20`/`0x408` on native64, removes the `game/g_targets.cpp` pointer allowlist entry, and validates every target
  configstring before publication. Checks cover ordinary live-entity identity, WORLD/NONE and level bounds, duplicate
  references, finite int-encodable offsets, exact flags, and registered material indices. Stale storage is cleared without
  dereferencing old-generation pointers, authoritative live `FL_TARGET` state is rebuilt, and shader, lock-on, and SP
  locked-weapon publication fail closed on invalid input. The audit also made regular info-string replacement bounded and
  failure-atomic without changing its legacy ABI, stages every target wire update before native mutation, and replaces
  the unbounded SP/MP material-name copies. The latest hardening also guards all reviewed script-entity lookups and uses
  native floating `from_chars` where available plus a bounded, C-locale, round-to-nearest POSIX fallback for Xcode 16.4.
  The fallback restores errno and the full floating environment, preserves finite subnormals, rejects true underflow, and
  never reads past its supplied token range. This is the 47th M2 pointer fix and adds the native-width SP target table to
  M4 evidence;
- GCC 16 and Clang 22 builds at exact target code head `b9844b2e` each pass **97/97** tests; the focused target
  runtime/source, pointer-tripwire, and PR #44 contract set passes **4/4** under each compiler. At production-identical
  head `d8440271`, focused Clang ASan+UBSan also passed with leak detection disabled because LSan cannot run under the
  sandbox's ptrace policy. Strict GCC/Clang i386 and AArch64 GCC object compilation passed at `7ab3e174`.
  Exact-capacity, overflow, replacement/removal, delimiter-cleaning, malformed/duplicate, non-NUL token, subnormal,
  rounding-mode, and atomic-failure cases execute the same checked cores used by production. The sandbox killed i386
  execution with exit 159, so no i386 runtime result is claimed. A separate throwaway manual audit—not a built-in CI
  mutation mode—rejected nine regressions covering stale initialization, broad
  entity domains, material zero, offset overflow, two-argument screen parsing, unsafe duration, unvalidated producers,
  unregistered load materials, and shader publication. Two follow-up read-only audits are clean after the capacity,
  material-copy, runtime-coverage, ABI, optional-leading-delimiter, locale, rounding, subnormal, and range-read findings
  were fixed. Original PR run **29590010636** passed all six Windows jobs and exposed the now-fixed Linux range-loop
  warning and missing Apple floating-`from_chars` overload. Run **29591783551** then passed eight jobs and exposed only
  that Xcode requires `<stdlib.h>` plus `<xlocale.h>` to declare `strtof_l`; `b9844b2e` fixes and contract-pins those
  Apple-only includes. Intermediate PR head `d472e136` passed all nine jobs in run **29593740056**. Exact final head
  `7336f10b` passed all nine jobs in run **29594562813**; exact-head Codex found no major issue, all six Gemini threads are
  resolved, and no unresolved review threads remain. PR #45 squash-merged as `d85ed087`, and authoritative post-merge
  master run **29595203160** passed all nine jobs. SP production remains outside hosted coverage: every engine CI job sets
  `KISAK_BUILD_SP=OFF`, the portable executable exercises the shared parser and layout model, and the source contract
  textually pins the SP implementation. Direct SP probes stop earlier on existing ILP32 assertions, undeclared
  `IsValidSeed`, and missing DirectX `d3d9.h`;
- the merged client-target safety checkpoint (`97024d7d`, `3cfb16d1`, `14887046`, `799cf462`, plus review hardening
  `62e317dc`) completes the next audited consumer half without importing d592's raw pointer-bound loop. Shared strict
  parsers reject malformed/overflowing
  configstring indices and lock-on payloads, cap target info at 1,023 bytes, and publish only complete staged values.
  Client/server configstring storage is range-checked before access and replaces script-string handles
  full-table-compatible unpublish-before-release-before-acquire-before-publish ordering. The 28-byte `targetInfo_t` ABI
  and exact 32-entry extent are explicit; vehicle,
  Javelin, pip-on-stick, bouncing-diamond, and target-position consumers use typed traversal, live ordinary-entity
  identity, and bounded weapon/material indices. Complete GCC and Clang builds and **98/98** suites pass, including MP/SP
  endpoint, malformed-token, failure-atomic, overlong, and exact unterminated-buffer cases. A portable source contract is
  selected by measured Windows x86 and pins the live dispatch and range/order invariants; review hardening binds the exact
  guard returns and sole lock-state publication to their validated control flow. Independent production logic/security
  and portability/ABI audits are clean. Codex's full-table replacement finding was fixed in `09be2243`; exact-head run
  **29603408182** passed all nine jobs, exact-head Codex was clean, and the only review thread is resolved. PR #46
  squash-merged as `0eb06224`, and authoritative post-merge run **29604449127** passed all nine jobs. The production SP
  TUs and real native64 target ABI remain an explicit unclosed
  compile gate because every hosted engine configuration still disables
  SP; direct probes caught and fixed one brace imbalance, then reached pre-existing ILP32/vendor/header blockers;
- the focused grenade safe-target candidate (`baccb1f9`, `ff519381`, plus review hardening `1bf38bae`) compares its exact
  three-dimensional squared
  distance with the squared 1.1-times weapon explosion radius through one dependency-free production helper. It preserves
  the inclusive boundary, arithmetic order, invalid-negative-radius behavior, and the separate intentional squared
  ten-unit `Actor_Grenade_ShouldIgnore` tolerance. Portable boundary tests plus a mutation-sensitive live-call source
  contract are selected in measured Windows x86; complete GCC and Clang builds and **100/100** suites pass. This is a
  high-confidence dimensional gameplay correction rather than a claimed retail-binary restoration, and the SP production
  translation unit remains outside hosted compilation;
- merged PR #63 adapts the confirmed non-matrix portion of exact upstream
  `6f0284ad8c1fa367304e5eefa44d39d744ddbefc`. Selected behavior is: typed lexical model/animation sorts over existing
  native-width tables, a bool FX mark predicate, typed material inputs with an explicit three-way-to-less adapter, a
  complete 24-entry shadow sort, deterministic NaN equivalence for material/shadow ordering, and the row-zero omni
  technique in column three. It preserves the material self-identity guard and rejects upstream's raw-integer-to-bool
  comparator conversion. The filesystem hunk is omitted because `d5a6e799` already supplies a stronger tested
  native-pointer service. All `com_math.cpp` precision changes—`MatrixMultiply44`, `MatrixInverse44`, and
  `InfinitePerspectiveMatrix`—are explicitly deferred for a separately evidenced numerical/determinism review. Portable
  runtime and source contracts plus measured Windows x86 enrollment cover native pointer identity, strict weak ordering,
  exact active ranges, manifest presence, and the selected/rejected boundaries. The complete rebased GCC Debug suite is
  **142/142** green; the combined pending/upstream/security focus is **8/8**, and strict GCC/Clang, ASan+UBSan, i386
  compilation, and AArch64 compilation also pass. Exact reviewed head `36aebd29` passed all nine jobs in run
  **29695891172**; Codex was clean, and Gemini's two suggestions were already satisfied or semantically equivalent, so
  both threads were answered without code churn and resolved. PR #63 squash-merged as
  `f79b0bf422bb926dd302a888bdc258e7e8409aa2`; authoritative run **29696199493** passed all nine jobs at that exact
  master commit;
- PR #65 merged the curated U1/U2 `r_dpvs` brush-definition, aim-assist, native-width HUD, signed-angle, command-tail,
  and Miles dry-level reconciliation as `d79069a41e0289f4ed53d174a89d8ee72f40b4a3`. Final reviewed head
  `3a9f0f01da82f0abbff59afb02093bddffd447d1` and authoritative master passed all nine jobs in runs **29703827041** and
  **29704069129**. Exact-head Codex review was clean, and all six Gemini threads were resolved: five fixed or duplicate
  findings and one non-corrective `nullptr` style suggestion. Local evidence remains full GCC Debug **153/153**,
  focused **8/8**, strict GCC/Clang, genuine i386/AArch64 compile-link, U1 focused **7/7** plus a clean independent audit,
  and U2 **146/146** plus its ten-mutation and dependency/symbol contracts. Every commit in pinned range
  `312a9d2e..2164cd1a` has an exact disposition in `docs/UPSTREAM_2164CD1A_LEDGER.md`. PR #66 merge-committed dedicated
  tree-neutral checkpoint
  `12309db16d6514ac0df23293cd6074d7bbd15142` has parents
  `d79069a41e0289f4ed53d174a89d8ee72f40b4a3` and `2164cd1accf6607a05203547e50858211dcef094`;
  its tree `f8a78964c7c89c3c3000f598cb4272782c40d70b` exactly matches its first parent's tree, and its content diff is empty.
  Exact PR head `e209367c920df589162431a584d6fdf7bfc83c43` merged as
  `225759e7d8fd1327210452f3debcd6360465ef2a`; authoritative run **29707497302** passed all nine jobs, and graph
  verification confirms both the checkpoint and exact `2164cd1a` are ancestors of `origin/master`. Dynent
  save/load `ba3c79f3` and unsafe raw SP/save/screenshot, gameplay, and matrix hunks remain explicitly deferred/rejected;
  icon changes remain provenance/optional-packaging work;
- current upstream tip `4ad0a2e2` adds three source commits after the integrated
  `2164cd1a` baseline. The reconciliation in progress adapts `1c30dda2` by
  naming the established memory-tree accounting values while retaining the
  public `int` signatures and existing report-free/hardened control flow;
  adapts `e3dd4ccb` by deriving DObj archive loops from the canonical client and
  entity capacities; and defers `601ddcc4`'s wholesale header-static relocation
  until a dedicated cross-translation-unit linkage audit. Existing runtime
  rejection of stream-delay overflow, bounded configstring lookup, and the
  Disk32 material-vertex-declaration byte contract supersede weaker upstream
  hunks. The outer `4ad0a2e2` object is an ancestry merge, not a fourth content
  batch; this branch will record it with a tree-neutral checkpoint after the
  curated content and source contracts are committed;
- PR #67 merged passive durable-receipt composition as `76d0e065888aab298d430b4bf4e115c07369bc88`:
  one stable allocation, stream-generation, pending-copy, and native-storage receipt capsule belongs to each of the 33
  runtime entries, while the active-stream binding and pending-copy ledger exist once at table scope. It remains
  production-neutral: no generation, PMem scope, pending copy, or legacy loader site is claimed. Review fixed the MSVC
  x86 pending-copy descriptor padding, preserved the headless effects exclusion through neutral inline binding
  operations, used CMake's portable `LINKER:` abstraction, and sealed stable fixture names and direct standard includes.
  Final exact head `422d904a7b0d1aa1372e14308cb6a8a3f4480157` and authoritative merge passed all nine jobs in runs
  **29709263403** and **29709598049**. Exact-head Gemini and independent audits were clean and all five threads resolved;
- merged PR #68 replaces native64-invalid legacy-PMem decompiler indexing with bounded typed entry access,
  returns before every rejected mutation, and removes a second-pass format-string interpretation of allocation names.
  Commits `23de894f`, `815d9961`, and `b721f495` preserve exact x86 and explicit native64 layouts through
  `RUNTIME_SIZE`/`RUNTIME_OFFSET`, add adversarial failure-atomic/runtime/source/CI coverage, and leave checked authority
  unenrolled. Full rebased GCC Debug CTest passed **155/155**; focused PMem/ABI passed **6/6**, strict genuine i386
  compile/link/runtime, AArch64 compile/link, Clang ASan+UBSan runtime, `git diff --check`, and independent audits pass.
  Review follow-up `45b0ec9c` applies `KISAK_CDECL` consistently, names the fixed 32-entry capacity, and corrects
  the variadic assertion fixture to match its production declaration. Initial run **29712199115** exposed the fixture's
  top-level pointer qualification as MSVC-only `LNK2019` on portable Windows amd64/ARM64. Exact final head
  `cabbeb38d1c6bec5fa4c0d861ee8c4b0f61d44e1` and squash merge
  `2ee1e82c4c1a918da8b8222feb2f56d73f2a5def` passed all nine jobs in runs **29712699908** and **29712915522**;
- merged PR #69 core `3b826224` and review hardening move both mutable PMem globals to internal linkage
  and replace the fixture's mutable `extern` with a by-value copy-in/copy-out seam whose whole helper type exists only
  under the one target-local test macro. Containment scans every official production manifest and workflow plus every
  regular file below `src`; a real macro-off `physicalmemory.cpp` object is compiled and inspected with MSVC
  `link /dump /symbols` or
  `CMAKE_NM`, requiring local state and rejecting exported state or helper symbols. Native GCC Debug passes **156/156**;
  focused GCC, Clang, and ASan+UBSan pass **6/6**, and genuine i386/AArch64 compile-link and object inspection pass.
  Review fixed padding-sensitive fixture snapshots and CRLF-sensitive source matching. Hosted macOS then exposed
  AppleClang Release's local lowercase-`b` `__MergedGlobals` coalescing; final head `eeefdf40` recognizes that form only
  when both individual names are absent and separately rejects exported state, merged state, and helper symbols while
  retaining exact ELF/COFF checks. Final run **29715782804** passed all nine jobs, exact-head Codex and the independent
  audit were clean, and zero threads remained before squash merge `534a9b1e`. Authoritative post-merge run
  **29716339199** passed all nine jobs. The merge adds no serializer, retained reservation extent, initialization phase,
  `$init` controller, checked authority, or production enrollment;
- merged PR #70 sequence `293a020c` appends the PMem serializer slot at MP `0x17` and SP `0x24` without renumbering any existing
  slot. Follow-up `716eacc1` uses it for a hidden retained extent plus witnessed initialization phase, failure-atomic
  reserve/commit/release publication, global Begin/End/Free/allocation/getter serialization, exact report-free allocation
  results, and a thread-local legacy shortfall. Commit `0a9128aa` completes that production-neutral boundary with hidden
  fixed-capacity owned-name sidecars that retain caller pointer identity separately, witnessed indexed bindings, a frozen
  pointer-free `0x610` diagnostic snapshot, and `PMem_DumpMemStats` capture-under-lock/report-after-unlock. It preserves
  unbounded synchronous names for the caller-owned legacy helpers while owning global hole reports across unlock and
  sidecar reuse. Focused GCC/Clang and Clang ASan+UBSan runtime/legacy/object/source gates, strict i386/AArch64 MP/SP
  compile-link, ABI/security/source, recursion/contention, production-object, and independent audits pass; i386 execution
  remains blocked only by the established sandbox `SIGSYS`, and LSan is disabled under ptrace. Commit `852e7db9` adds a
  hidden unused witnessed Dormant/Begun/Ended process-life `$init` controller with exact high-prim index-zero binding,
  permanent Ended authority, report-free serialized operations, legacy coexistence guards, and exact End-before-
  initializing-clear source seals. Commit `792ff1c7` authenticates the complete passive stream/relocation singleton and
  pending-copy-ledger topology without widening any per-entry receipt capability. The exact combined head passes native
  **157/157**, focused **32/32**, and affected genuine GCC i386/AArch64 compile-link gates. Exact final head
  `ca2d1149ab7e67d6de56921a0574a662861783b5` is the head of run **29726370638**, whose all nine hosted jobs ultimately
  passed; it squash-merged as `6a67a66e4afd62480bdb62493e666961da9ed837`. The merge command was issued after seven jobs completed; the
  remaining no-Steam and Windows x86 Debug/Release jobs subsequently passed at that same head. Gemini was clean at
  behavior head `c8230927`, the final two changes were MSVC fixture-only corrections, and no review threads remained.
  No checked authority or loader caller is enrolled at the merged baseline;
- merged PR #71 completes the production-neutral exact-key composite-controller bridge from those passive
  resources to one durable exact-generation controller. PMem now exposes report-free range/receipt authentication with
  exact `Freed` terminal evidence that survives low/high slot reuse without ABA ambiguity. Native storage, stream
  ownership, pending copies, and script-string ownership each provide exact composition predicates, while the table
  drives strict callback binding, allocation, storage/stream placement, pending/script staging, admission, abandonment,
  retry-safe Live unload, drain, terminal reset, reuse, and stale-key rejection. Inputs use a single captured descriptor
  or callback image; storage/stream overlap and aligned table output aliases are rejected, as are managed-PMem aliases
  while shared PMem is Ready or Draining, including the
  legacy passive claim/get boundaries. Focused table CTest is **16/16** under GCC; GCC/Clang runtime-table builds and
  Clang ASan+UBSan with `detect_leaks=0` are green. Genuine i386 and AArch64 compile/link gates produce the correct ELF
  classes; direct i386 execution remains blocked by the established sandbox `SIGSYS`. End-to-end/adversarial coverage
  and an independent exact-diff audit are green. At the pre-review checkpoint, the runtime/source/security selection was
  **36/36**; all five macro-off production seals passed, the headless dependency gate was clean, and full native CTest
  was **157/157**.
  PR #71 initial run **29754589268** then found hosted-only headless link closure and MSVC identical-COMDAT fixture
  failures. The final repair adds the database-neutral storage implementation and an explicit fail-closed headless
  bridge to the headless source set, seals that source-profile pair, and makes the alternate callback observably
  distinct. Review hardening also
  closes full retained-output aliases in the composite table and lower stream/pending readers, authenticates exact live
  keys before Bound descriptor disclosure and the complete reset-authoritative FX-workspace topology, prevents mixed legacy and
  composite enrollment, strengthens direct-call authority seals, and fixes warning roots instead of suppressing them.
  Full native CTest is **157/157**, and the affected Clang and ASan+UBSan selections are **38/38** each. Exact repair
  head `a65ff336` passed eight jobs in run **29760022151**, whose initial attempt and rerun both found one optimized
  Win32 fixture automatic-output path sensitive to the new natural-alignment preflight. Head `059aebd0` moved that
  output to stable heap storage and separated begin/allocation/bind diagnostics. Run **29763190487** passed all five
  portable jobs, headless, and no-Steam before the measured builds rejected the resulting implicit C4324 fixture tail
  padding. Exact implementation head `a1f99336` retains a full-width attempt witness, removing that padding without
  suppressing the warning. Focused GCC/Clang/ASan+UBSan runtime tests, genuine i386/AArch64 compile-link, and an i386
  tail-padding probe pass. Exact final head `3f39881cb4f8d5145e01904d95f34c09512b070d` passed all nine jobs in run
  **29764928195** with clean exact-head Codex/Gemini reviews and zero unresolved threads. PR #71 squash-merged as
  `49184106a106d1b115097b96f541ac03551e90e5`; authoritative post-merge run **29765949587** passed all nine jobs at that
  exact master commit. No production caller is enrolled;
- PR #72 exact final head `0c1354c05c92f2f9e66ce285d85d71c90a4119eb` passed all nine jobs in run **29768665063**
  with clean exact-head Codex/Gemini review and zero unresolved threads. It squash-merged the ABI-neutral journal
  capacity/demand split and symmetric storage-pair preflight as `64dfc8c9e3b930a7ef7760b794e37320471280c3`;
  authoritative post-merge run **29770265354** passed all nine jobs. PR #73 completed the production-neutral
  serialized runtime/registry facade prerequisite: one process-lifetime nonblocking outer serializer, mirrored
  global/TLS authority, private coordinator storage/admission, exact composite table/registry forwarding, release refusal
  while child authority remains, fail-closed status validation, and reviewed output, retained-descriptor, and
  mutation-crossing aggregate-input alias separation. Exact method-to-adapter/source seals, realistic forwarding
  fixtures, and a portable macro-off object close the final review gaps. It exposes no raw table/coordinator/admission
  authority, preserves all seven frozen sites, and enrolls no loader caller. Final head `7afb2ca5` passed all nine jobs
  in run **29781843001**, squash-merged as `909f9309`, and passed authoritative post-merge run **29782835695**. The
  PR #74 added a private exact-key callback borrow for Unpublishing, Cleaning, Admitting, and Unloading, bound to
  the owning thread, transaction, purpose, and a mirrored non-wrapping callback-window witness. It preserves zero
  production enrollment and locally passes full GCC CTest **160/160**, focused Clang/ASan+UBSan, source/security, and
  genuine i386/AArch64 gates at `65ced175`. Initial exact head `c0691426` passed all nine hosted jobs in run
  **29785681754**. Review repair `3ed88879` independently masks receipt fields and permits same-callback retry only after
  recoverable coordinator admission contention, inactive reauthentication, and exact marker restoration; all other
  outcomes remain one-shot. Full GCC CTest **160/160**, focused Clang/ASan+UBSan, source/security, genuine
  i386/AArch64, and two independent audits pass on that repair tree. Final head `79413a18` passed all nine jobs in run
  **29787341109** with clean exact-head Codex/Gemini review and zero unresolved threads; PR #74 squash-merged as
  `f996e16b`, and authoritative post-merge run **29788146050** passed all nine jobs. A literal full-chain fixture remains
  a pre-enrollment gate. The remaining loader prerequisites precede the atomic
  seven-site cutover;
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
3. Continue fixed-width `disk32` fast-file widening. PR #32 merged exact FX effect/visual/trail/impact schemas and hardened
   pure transactional native converters with local GCC/Clang, complete sanitizer, strict i386/AArch64, source-contract,
   and all-nine-job candidate CI clean. PR #33 merged the zone-owned aligned native arena and guarded stateful zone adapter
   over the XBlock cursor walk with adversarial sequence/provenance/nesting and canonical-publication coverage; it
   squash-merged as `a004701d`, and post-merge run **29506653705** passed all nine jobs. PR #34 merged the fixed top-level
   XAsset envelopes and bounded eight-byte iterator prerequisite as `3e9b51b0`; PR #35 merged the pure four-byte Disk32
   script-string token walk as `3271b8d6`. PR #36 merged the generation-keyed external slot and distinct
   load-abandon/live-unload recipes as `15469b3d`; post-merge master run **29531440687** passed all nine jobs. Baseline
   journal core `70ef96fc`, hardened/integrated checkpoint `d158d5e9`, prepare/finalize implementation `aa295367`,
   boundary-hardening implementation `81ded193`, and exact source-contract head `83a545ad` complete the
   production-neutral transactional ownership primitive with fixed caller storage, full-u32 per-occurrence IDs, exact
   claimed-versus-duplicate transfer records, reverse outcome-specific rollback, O(1) controller validation, linear
   phase-boundary scans, fail-closed status operations, and portable/Windows x86 build admission. Before production
   wiring, the caller must prepare `Transferred -> CommitReady`, attempt lifecycle publication, roll back from
   `CommitReady` if publication fails, or invoke the unconditional journal finalizer immediately after successful `Live`
   publication. PR #48 now supplies the merged no-report ownership primitives, private journal adapter, dedicated
   serializer, and checked allocator foundation as `7d78222d`; final PR-branch run **29625522997** passed all nine jobs.
   PR #49 completes the constructed production-neutral token/journal/key binding through Live finalization or
   authenticated abandonment and squash-merged as `dcd91cf0`; authoritative post-merge run **29626811250** passed all
   nine jobs.
   PR #50 then merged failure-atomic full/debug-only script-string initialization as `eeca68ba`; authoritative post-merge
   run **29627591759** passed all nine jobs. PR #51 merged the production-owned 33-entry table with stable
   slot/controller storage, by-value generation authority, slot-zero reservation, fail-closed initialization, and no
   production generation claims as `beb2925d`. Exact-head run **29628040709** and post-merge run **29628132007** each
   passed eight jobs and failed only the Windows x86 Debug fixture link. PR #52 fixed that test-only boundary and
   squash-merged as `e792c160`; exact final run **29628599645** and authoritative post-merge run **29628940419** passed
   all nine jobs. PR #53's retained memory-tree validation lease merged as `445d436f` after exact run **29649484692**
   passed all nine jobs. Implementation `34b91875`/`2154e423` retains the transaction boundary; lifetime
   hardening `b193343b` adds by-value registry authority, TLS-authenticated retained-lock release, and terminal
   destructor abandonment. It preserves full transaction-boundary validation and PR #48's bounded leased operation paths
   without a production caller. PR #54 merged retry-safe exact-key terminal reset/Live-unload adapters through
   `d2740fb2`/`7764af22`/`dc4aee23`/`74002a69`/`0eac1f2d`, with review cleanups `e8d7a3f6`/`5bee8bba`, as `8e7fd162`;
   authoritative post-merge run **29651211711** passed all nine jobs. PR #55 merged the string OwnershipBatch's same
   pointer-free lifetime boundary without production enrollment as `f39e0e4a`; exact-head run **29657884407** passed
   all nine jobs and exact-head Codex review was clean. PR #56 merged the sidecar authority seal as `6159275e` after
   exact-head run **29658932268** passed all nine jobs and exact-head Codex review was clean. PR #57 merged the exact keyed
   mutable runtime adapters as `57e2b1a2`; exact-head and post-merge runs **29659895814** and **29660281653** passed all
   nine jobs, and exact-head Codex review was clean. PR #58 then merged the portable-console boundary as `9fb46bea` after
   exact-head run **29666269398** passed all nine jobs and exact-head Codex review was clean; post-merge run
   **29670244884** also passed all nine jobs. PR #59 merged the audited one-slab runtime-storage planner/binder as
   `ff61504e`; exact head `8cec770d` passed all nine jobs in run **29671392540** with clean Codex, Gemini, and independent
   audits. Authoritative post-merge run **29671849514** at exact `ff61504e` passed all nine jobs.
   PR #60 then merged the checked-PMem receipt as `74916b5b`; exact final head `0eec9b1e` and authoritative merge passed
   all nine jobs in runs **29673379640** and **29673608169**; Codex reviewed exact final head `0eec9b1e`, Gemini reviewed
   identical code head `f04c63e0`, both were clean, and zero threads remained. PR #61 then merged the exact stream-
   invalidation stack as `32e6de4efc86823020d1a2eef2c473e013f893ba`; final exact head
   `f9dfaaeb43eaaa32cd44c645e3a0e347c9bebdfc` and authoritative post-merge runs **29691282387** and **29691725277** each
   passed all nine jobs, all four Gemini threads were resolved, and exact-head Codex review was clean. PR #62 then merged
   the production-neutral pending-copy ledger as `888d12e6beedd587602f18cf6763ae04cc067470`; final exact head `6a79677f`
   passed all nine jobs in run **29694906394**, exact-head Codex and Gemini were clean with zero threads, and authoritative
   post-merge run **29695353022** also passed all nine jobs. PR #63 then merged the selectively reconciled upstream
   typed-sort fixes as `f79b0bf422bb926dd302a888bdc258e7e8409aa2`; exact-head and authoritative runs **29695891172**
   and **29696199493** passed all nine jobs. PR #64 then merged the complete borrowed/standalone registry coordinator
   through `774487d1` as `7f030c03269235b3ad703c13404e0975f798bd18`; it remains production-neutral. `9f327514`
   seals its production boundary; `74b56b65` adds portable nonblocking
   hash admission and fixes the real i386 size-test plus pre-held-reader deadlock; `90e8fba7` completes portable test
   wiring; `2a836a0e` maintains authenticated ID-to-entry and entry-to-owner/predecessor inverse certificates across
   intern/unlink, enabling O(1) target resolution and one linear shutdown/head-promotion walk; and `56c97f09` links the
   real coordinator, retained transaction, `FastCriticalSection`, ownership batch, and memory-tree lease into the
   production-stack composition fixture. Final `774487d1` shares one canonical debug-publication pointer gate across
   typed operations, runs one linear full reachable-topology/physical-slot/inverse/debug-total preflight per bulk, and
   exactly reauthenticates retained targets in the second pass before mutation. Same-hash 49-entry, detached-suffix,
   corrupt inverse/debug-pointer, and forged aggregate-total cases fail atomically and poison unsafe batches without
   returning capacity exhaustion or restoring quadratic work. Exact implementation head `774487d1` passes GCC Debug/full
   CTest **145/145**; focused
   source/security/macro-off passes **6/6** and the independent audit selection passes **7/7**. Warning-clean GCC/Clang
   runtime, Clang ASan+UBSan with `detect_leaks=0` under ptrace, and genuine i386 plus AArch64 compile/link evidence also
   pass. Final exact PR head `a73916a8467eb5d4a6cad7d33b5d3ecf1f684c37` and merged master passed all nine jobs in
   runs **29701509815** and **29702009703**, with clean exact-head Codex/Gemini/thread review. PR #65 then merged the
   curated U1/U2 upstream content as `d79069a41e0289f4ed53d174a89d8ee72f40b4a3`; exact reviewed-head and authoritative
   runs **29703827041** and **29704069129** passed all nine jobs, exact-head Codex was clean, and all six Gemini threads
   were resolved. PR #66 merge-committed exact tree-neutral checkpoint `12309db16d6514ac0df23293cd6074d7bbd15142`
   as `225759e7d8fd1327210452f3debcd6360465ef2a`; authoritative run **29707497302** passed all nine jobs and the graph is
   verified. None of the seven raw production sites is enrolled. PR #67 merged the locally and hosted-sealed passive
   durable-receipt composition. PR #68 merged the bounded legacy PMem indexing/failure-atomic repair, and PR #69 merged
   the hidden-state/macro-off-object seal as `534a9b1e`. Merged PR #70 sequence: `293a020c` reserves the exact MP/SP PMem lock slots and
   `716eacc1` supplies the serialized retained-extent/init/allocation/lifecycle core, `0a9128aa` finishes stable owned
   names plus the bounded diagnostic snapshot/read-report split, `852e7db9` adds the unused permanent-Ended process-life
   `$init` controller, and `792ff1c7` completes passive table-wide singleton authentication. PR #71 merged exact-key
   component composition plus the strict allocation/staging/admission/abandonment/unload/drain/reset table controller
   without enrolling a caller. PR #72 merged the capacity/demand prerequisite and PR #73 merged the serialized facade.
   PR #74 published the private callback-borrow prerequisite without enrolling a caller. Final head `79413a18` passed
   all nine hosted jobs in run **29787341109** with clean Codex/Gemini review and zero unresolved threads; it
   squash-merged as `f996e16b`, and authoritative post-merge run **29788146050** passed all nine jobs. After the
   remaining pending-copy/context/no-report prerequisites, atomically bind the loader
   across all seven sites; partial enrollment remains forbidden. Keep static controller
   slots and callback metadata
   outside PMem with
   per-generation native storage inside the named scope. Preserve PR #48's mirrors and bounded scratch implementation
   when binding the recipes and adapter into production with completed-object/alias registration and lifetime tests.
   The journal merged in PR #37 as `7a9bce34`; post-merge run **29542960583** passed all nine jobs. PR #38 merged the
   referenced-fast-file/SYSTEMINFO range and bounded metadata hardening as `a7c485fd` before the ownership table depends
   on those indices; post-merge run **29551990840** passed all nine jobs. Current DB-thread longjmp remains
   process-fatal, so the controller is a longjmp-safe prerequisite rather than
   recoverable production abandonment. Retail wire bytes remain frozen. Production commit must keep admission closed
   through all fallible work, prepare the journal, publish `Live` under the external serializer, unconditionally finalize
   the journal, and only then perform a no-fail/no-drop gate/signal release before dropping that serializer; otherwise
   add an admission-pending state before integration.
4. Widen the script VM value representation and remove pointer-to-32-bit casts.
5. Implement the remaining platform services (sockets, handle-relative deletion,
   process/crash control, and native Win32 headless character-console input) for Windows/POSIX.
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
| **M14** | Full 5-target CI matrix, packaging & required gates | L | M6, M9, M11, M12, M13 | all 5 production engines green as required gates; ASan/UBSan required on linux_amd64; cross-arch parity runs in CI; immutable-tag, least-privilege release artifacts and aggregate checksums published |

M14 workflow parity must be checked against the maintained `jm2/CroMagRally` and `jm2/tributary` patterns. Utility-only
matrix legs do not satisfy target delivery: required production jobs and target-labeled artifacts must cover Windows
amd64/ARM64, Linux amd64/arm64, and macOS arm64 while the legacy Windows x86 engine/no-Steam/headless gates remain
separate. `release.yml` must resolve and validate an exact version tag, fan out one immutable source SHA with persisted
checkout credentials disabled, keep build jobs read-only, give `contents: write` only to a final code-free publisher,
publish a reproducible source archive plus aggregate `SHA256SUMS`, and use bounded jobs, concurrency control, and pinned
actions. Native package/signing formats become required only when their corresponding client milestones are real.

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
virtual-memory, standard-stream console, process/crash control, and BSD sockets. Standard-stream console merged in PR
#58; process/crash control, native Win32 headless character-console input, and sockets remain.

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

Overall porting progress is approximately **82% by current engineering effort**. PR #62 merged the production-neutral
pending-copy ledger, PR #63 merged the curated upstream typed-sort checkpoint, PR #64 merged the production-neutral
registry coordinator, PR #65 merged the curated U1/U2 upstream content reconciliation, and PR #66 merged the exact
tree-neutral ancestry checkpoint, PR #67 merged passive durable-receipt composition, PR #68 merged the legacy-PMem
indexing/failure-atomic prerequisite, PR #69 merged global-state encapsulation plus the cross-toolchain macro-off seal,
PR #70 merged serialized runtime/process-controller/passive shared-resource authentication, and PR #71 merged the
production-neutral exact-key adapter layer, strict table orchestration, exact PMem terminal evidence, alias/overlap
hardening, adversarial composition coverage, and the optimized-Win32 fixture repair. PR #72 merged the capacity/demand
prerequisite and PR #73 merged the production-neutral serialized facade. PR #74 published the private callback-borrow
prerequisite without enrolling a loader caller; final head `79413a18` passed all nine jobs in run **29787341109** with
clean Codex/Gemini review and zero unresolved threads, squash-merged as `f996e16b`, and passed all nine authoritative
post-merge jobs in run **29788146050**. The ancestry checkpoint records reviewed
history without importing code and therefore does not inflate the engineering estimate.
Windows x86 is about
**93%**, shared
foundations/security about **89%**, Windows amd64 about **58%**, Linux amd64 about **49%**, Windows/Linux ARM64 about
**40%**, and macOS arm64 about **31%**. Strict delivered-target status remains **0/5** because no requested
64-bit/non-Windows engine target is enabled end to end yet.
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
**29454579529** also passed all nine jobs. PR #32 merged exact FX fast-file Disk32
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
fixed by `1153eefe`. Codex found no major issue at review head `e5b755a4`, and exact final candidate run **29465922917**
passed all nine jobs. PR #32 squash-merged as `9860617b` from final branch head `0658dcd0`; independent post-merge run
**29466158837** also passed all nine jobs. The zone-owned native arena and guarded stateful XBlock/XAsset zone adapter
are now implemented as portable primitives. Final independent review found and fixed a live-arena rebind bypass and the
missing canonical `DB_AddXAsset` identity return in `503e0b54`/`bf7645d2`; focused GCC/Clang/ASan+UBSan/TSan coverage is
green. Exact hardening head `ca080971` passed all nine required jobs in run **29503163189**. The fresh hosted Codex
review found no major issue at `4ab63c1b`; only trusted-caller contract documentation changed afterward, and an independent
exact-head audit found no blocker at `ca080971`. PR #33 squash-merged as `a004701d` from final documentation head
`73472a50`, and authoritative post-merge master run **29506653705** passed all nine jobs. Production sequencing is
detailed below.
The production-wiring audit found one earlier native64 boundary that must land before stream integration: the retail
`XAssetHeader`/`XAsset`/`ScriptStringList`/`XAssetList` envelopes are fixed 0x4/0x8/0x8/0x10 Disk32 records, while the
corresponding native types widen to 0x8/0x10/0x10/0x20. A native64 loader that branches only at the FX payload has already
read the wrong list size and offsets and iterated the asset array at the wrong stride. PR #34 therefore merged exact
Disk32 envelope schemas and a pure bounded validator/iterator as `3e9b51b0` without changing production streams. That
prerequisite preflights count/token parity, the checked 32768-entry eight-byte span,
raw signed type range, required portable build admission, unaligned exact-stride reads, trailing guard bytes, high-bit
tokens, late rejection, the callback-free empty path, meaningful arithmetic overflow before the lower policy cap, and
output/cursor atomicity. A typed root must be four-byte aligned and receive an exact 0x10-byte copy when sourced from wire;
the record span remains alignment-agnostic. Source tripwires forbid native `XAsset`/`XAssetList` imports and iteration.
Focused GCC/Clang/ASan+UBSan/TSan execution, strict i386/AArch64 compilation/linking, Clang analysis, source-contract
validation, and two independent clean audits pass. Exact reviewed head `ac619d3e` passed all nine jobs in run
**29521272126**; hosted Codex found no major issue, Gemini reported no review comments, and no review threads remain.
Authoritative post-merge master run **29522252342** also passed all nine jobs at squash commit `3e9b51b0`. PR #35 then
merged the exact 0x4 `ScriptStringTokenDisk32` and a separate pure header/span/iterator layer as `3271b8d6`. It computes
checked four-byte extents before
the 65536-entry cap, validates root/span parity, fully preflights the array, preserves null/inline/offset bits, rejects
shared-inline explicitly, supports unaligned borrowed bytes, ignores trailing bytes, and revalidates mutation before
failure-atomic output/cursor publication. Focused GCC/Clang/ASan+UBSan/TSan execution, strict i386 and AArch64
compilation/linking, Clang analysis, the complete GCC/Clang **78/78** suites, the existing XAsset regressions, and both
source contracts pass locally; two independent exact-head audits found no blocker. Exact reviewed head `c5246f67` passed
all nine jobs in PR #35 run **29523406607**; hosted Codex found no major issue, Gemini reported no review comments, and
no review threads remain.
PR #36 merged an explicitly constructed generation-keyed external zone-load context as `15469b3d` without changing
`XZone` or production lifecycle behavior. A fixed 16-byte slot and 16-byte `{generation, slot}` key track
`Empty`, `Loading`, `Live`, and `Abandoning`; reject stale, cross-slot, malformed, and ABA keys; preserve exact
claim/commit/abandon/unload receipts; and fail closed before generation wrap. Loading abandonment drives the reviewed
nine-stage order: cancel input/inflate, abort native-adapter transactions, make partial assets/staged references/copy
records unreachable, invalidate alias/direct/stream/delay state, release geometry, tear down arena/workspace/sidecars,
end the PMem allocation, free PMem, clear registry/loading/queue/recovery-gate/signal state, then release the slot
internally. Committed Live unload instead drives only live-owned teardown: remove live assets/references, reset shared
runtime state, release geometry, tear down native storage, free PMem, remove live registry/handles, then release the slot.
It cannot replay load-only cancellation, adapter abort, `PMem_EndAlloc`, or loading-gate/signal work. Retry retains the
first incomplete cursor; unsafe or unknown callback completion poisons the generation permanently; callback reentry is
`Busy`; state is private outside test fixtures; and terminal receipts stop matching active ownership after release.
The controller has no internal synchronization: one external serializer covers every transition, query, callback, and
destruction, while `Busy` detects callback reentry only. Cleanup callbacks are convergent ensure-postcondition operations,
so normal-path work that is already complete returns Success without replaying one-shot effects. Successful loading keeps
admission closed through all fallible work and `PMem_EndAlloc`, calls `TryCommit` to publish `Live`, then performs an
infallible no-drop gate/signal release before dropping the same serializer. A production release that can fail or exit
nonlocally requires an explicit committing/admission-pending state first.
The complete GCC and Clang suites are **80/80** green, with focused warning-as-error, conversion/sign-conversion,
ASan+UBSan, MSan, TSan, static-analysis, strict i386/AArch64, source-contract, and diff checks also clean. Exact candidate
`f8efc613` passed all nine jobs in PR #36 run **29530465823**. Hosted Codex found no major issue at that head; Gemini
reported no comments on the initial candidate. Codex's sole earlier test-friendship finding is fixed and resolved, and no
review threads remain. PR #36 squash-merged as `15469b3d`, and authoritative post-merge master run **29531440687** passed
all nine jobs.
Baseline core `70ef96fc`, hardened/integrated checkpoint `d158d5e9`, prepare/finalize implementation `aa295367`, and
exact boundary-hardening candidate `81ded193` implement the production-neutral transactional ordinary-reference
ownership primitive. It binds fixed caller-owned storage to the exact lifecycle key,
preflights the complete expected count, records one full nonzero `uint32_t` ID per acquisition without deduplication, and
distinguishes claimed database-user ownership from transfers that released a duplicate. Retry/rejection cannot change
ownership; unknown, unsafe, zero-ID, or corrupt completion/state poisons or rejects the transaction without trusting its
ownership record. The status-bearing prepare step performs the final full scan and moves `Transferred` to `CommitReady`;
both phases remain reversible before lifecycle publication. If lifecycle commit fails, the caller rolls the journal back
from `CommitReady`. If it succeeds, the caller immediately invokes the unchecked, unconditional `void noexcept`
finalizer, which only publishes `Committed`, resets flags, and detaches backing. Reverse rollback invokes the exact
ordinary-removal or targeted database-user-removal path for each occurrence while skipping already-balanced duplicates.
Controller validation is O(1), whole-entry validation occurs only at the first seal/prepare/begin-rollback boundary, and
transfer/rollback operations validate only their current entry; the 65,536-entry lifecycle regression proves total
linear work. Active-state validation rechecks count/cursor bounds and
attached-storage alignment/span/overlap, while guarded corruption fixtures and stateful repeated-ID ownership models
exercise fail-closed status operations. The exact-0x30 journal never allocates or performs destructor cleanup. Its
post-`Live` finalizer intentionally performs no validation, branch, scan, callback, or status reporting; this ordering is
enforced by the caller protocol rather than by the journal. No production `SL_*`, loader, PMem, registry, or native-pointer
wiring is present.
The current runtime still requires adapters to validate `0 < id < 65536`, although the journal retains full-u32 IDs for
future widening. Disk32's 65,536-entry policy also exceeds the packed 65,535 maximum ordinary refcount for one repeated
string, so a count-valid list can reject on its final acquisition and must roll back exactly. One global transaction
serializer must be acquired before journal initialization and held continuously through terminal commit or rollback
return. It cannot be dropped between calls; on success it stays held through prepare, `Live` publication, journal
finalization, and the no-fail/no-drop gate/signal release. It excludes every other journal transaction, every raw
database-user add/transfer/remove/publication, and the global database-user 4 -> 8 sweep; overlapping journals remain
forbidden without shared claim accounting.
At exact code/test head `83a545ad`, the complete GCC and Clang suites are **82/82** green. Strict warning/conversion
builds, ASan+UBSan, MSan, TSan, strict i386 and AArch64 compilation/linking, production-TU/test-access isolation, the
ABI/source contracts, and `git diff --check` pass. The i386 runtime reaches the established sandbox `SIGSYS`; no AArch64
emulator is available.
CI definitions configure all five portable utility jobs to build/run the target and measured Windows x86 to build it
explicitly. The source contract pins the finalizer to four direct assignments, the real lifecycle controller is linked
into composed success/pre-`Live` failure tests, and the maximum-count path prepares to `CommitReady` before exact
rollback. It also pins private rollback-detach bodies, loop-token exclusion, the post-prepare `Loading` state, and the
success-only finalizer guard. Final PR #37 head `376ce097` passed all nine jobs in run **29539650666**. Gemini reported
no comments, hosted Codex found no major issue, and no review threads remained. PR #37 squash-merged as `7a9bce34`;
authoritative post-merge master run **29542960583** passed all nine jobs.
PR #38 corrected the two referenced-fast-file name/checksum loops that formerly iterated indices 0..31.
One canonical definition now pins 33 physical registry entries, reserved/default slot 0, usable slots 1..32, and a
32-live-zone cap. It also makes referenced fast-file and IWD formatting plus SYSTEMINFO aggregation bounded,
failure-atomic, count-consistent, and exact-round-trip checked; preflights complete remote checksum/path metadata before
mutation; and removes native pointer truncation from server-file comparison. Server downloads now admit only a complete
`.iwd`/`.ff` name present in the published active-mod reference list, reject `_svr_`, traversal/namespace/dot aliases, and
names that cannot fit the 64-byte protocol field, and revalidate authorization immediately before opening. File-open,
reset, error, and WWW-redirect paths use nonfatal bounded construction and close/reuse owned resources correctly. The
legacy unauthenticated update namespace consequently fails closed; HTTP redirect transport remains separately
nonfunctional. Complete local GCC/Clang suites are 85/85 green, ASan+UBSan/TSan suites are 84/84 green, and strict i386
execution and AArch64 compilation/linking pass. Exact final head `9fb4fc18` passed all nine jobs in run **29551519068**;
hosted Codex found no major issue at that head and no review threads remain. PR #38 squash-merged as `a7c485fd`, and
authoritative post-merge master run **29551990840** passed all nine jobs.
PR #48 merged the no-report script-string primitives, private journal adapter, dedicated serializer, checked allocator
surface, bounded legacy topology/interval validation, and linear global-sweep preflight as `7d78222d`; final PR-branch
run **29625522997** passed all nine jobs. PR #49 added the constructed production-neutral controller and squash-merged
as `dcd91cf0`; authoritative post-merge run **29626811250** passed all nine jobs. PR #50 then merged failure-atomic
full/debug-only script-string initialization as `eeca68ba`; exact-head run **29627237107** and authoritative post-merge
run **29627591759** passed all nine jobs. The controller
acquires the serializer before journal initialization, authenticates one exact lifecycle slot/key, journal, backing
span/count, token serial, and owner through every operation, stages acquired IDs only after journal publication, and
drives seal, transfer, reversible prepare, lifecycle `Live`, unconditional journal finalization, infallible admission,
and serializer release in order. Its abandonment route convergently makes staged IDs unreachable before `Abandoning`,
rolls exact ownership back before detaching per-zone backing, drives cleanup until lifecycle `Empty`, releases the
serializer last, and preserves a fully authenticated `Abandoned` receipt until reset. Dedicated tests cover retries,
partial rollback, callback reentry, foreign-thread exclusion, stale/swapped bindings, output atomicity, poisoning, and
receipt authentication; the exact GCC Debug suite is **105/105** green.

PR #51 adds a fixed production-owned table whose 33 stable entries each own the lifecycle slot,
controller, and by-value generation key outside zone PMem. Slot zero is reserved; only 1..32 can be claimed. Pristine
initialization precedes asset-pool mutation in `DB_Init`; checked physical lookup, generation claim, and read-only keyed
views fail closed on invalid/default slots, stale/cross-slot/ABA authority, partial initialization, poison, and impossible
lifecycle/controller/serializer combinations. Retaining an entry address cannot upgrade a copied stale key. The complete
GCC suite is **107/107** green, focused Clang/sanitizer/source and x86/AArch64 gates pass, and exact-transplant review is
clean. PR #51 squash-merged as `beb2925d`; exact-head run **29628040709** and post-merge master run **29628132007** each
passed eight jobs but failed Windows x86 Debug on the test fixture's missing `MyAssertHandler` link boundary. PR #52's
narrow test-only repair passed all nine jobs in exact final run **29628599645** and authoritative post-merge run
**29628940419**, then squash-merged as `e792c160`. The production loader deliberately does not claim or consume the table
in this batch.

The merged PR #53 authenticated memory-tree validation lease was developed as implementation `34b91875` and tests/source
contracts `2154e423`, with historical exact patch identities preserved from originals `433e9c5e` and `45eb9b80`. A
private admission token
begins one retained same-thread lock interval only after a full Basic+Forest+Partition validation. Complete operations
keep their exhaustive policy, LegacyLocal operations keep PR #48's bounded mirror/path checks, and Leased operations
authenticate exact by-value address/serial authority before performing those same bounded checks. Successful mutation
counts
are overflow-safe; torn registry fields, serial exhaustion, mutation exhaustion, stale or foreign tokens, recursive
unleased access, and allocator corruption fail closed or poison the lease. Finish repeats the full validation before
clearing authority.

Gemini found a valid lifetime defect at exact PR head `fc496b01`: the default destructor could leave global registry
pointers naming a dead stack lease. Review-hardening commit `b193343b` replaces stored pointers with mirrored by-value
address, serial, and Idle/Active/Poisoned/Frozen lifecycle state. Generic paths inspect those values only; they dereference
a lease solely when an explicit live argument exactly matches authority. Mirrored thread-local address/serial state is an
independent proof that successful Begin retained the current thread's recursive acquisition, preventing fabricated
registry state from causing a double unlock. An exactly authenticated destructor first publishes terminal process-
lifetime `Frozen`, clears the stack identity, and then releases the proven acquisition. Any torn or unauthenticated
destructor also freezes and clears identity but releases only its own recursive probe, leaving the unproven base
acquisition held. A skipped destructor or nonlocal exit may strand that lock, but later generic paths cannot dereference
dead storage. Production has no thaw API, `MT_Init` cannot clear `Frozen`, and all typed/legacy/leased mutation, query,
validation, raw, reset, and reporting traversal entries reject it without state/output changes or diagnostics. Canonical
unrelated and normally finished leases destruct harmlessly. The lease remains 16 bytes and standard-layout, but the
custom destructor intentionally makes it non-trivially destructible.

Lease storage is still caller-owned and must outlive every call that receives its address/reference. The production
contract is same-thread Begin/Finish/destruction while outer SCRIPT_STRING ownership remains held. `Frozen` makes generic
allocator paths reject safely, but it does not legalize a pointer/reference call concurrent with destruction.

Follow-up `847ff969` authenticates global and local token identity before snapshot/member publication, serializes foreign
snapshot/lease calls across normal Finish while storage remains live, and closes all separate legacy check/use windows.
Allocate/free/reallocate and raw queries retain one lock from admission through final state use;
invalid size/score/subtree/node/pointer inputs are bounded without live-state diagnostics; free-subtree recursion follows
only a completely authenticated forest; and debug dumps emit a nonblocking fixed-BSS snapshot with numeric string IDs so
no allocator lock crosses `Com_*`, `iassert`, `va`, or an SL callback.

The GCC Release suite at `b193343b` is **107/107** green. Exact follow-up `847ff969` passes focused GCC Release/
`RELEASE_ASSERTS`, the production script-string ownership build/fixture, Clang ASan+UBSan (leak detection disabled under
the traced runner), 50 repeated locking/thread runs, strict i386 compilation, AArch64 cross-compilation,
source/security invariants, and `git diff --check`. Independent portability validation also passes five focused GCC
Werror tests, strict fixture and production i386/AArch64 objects, a Clang MS-compat fixture, and clang-cl x86/x64/ARM64
sensitive excerpts with the 0x200a8-byte snapshot plus one-byte flag in BSS. Hosted Windows CI remains authoritative for
the unavailable local Microsoft STL/SDK integration. Runtime coverage includes exact same-thread abandonment, foreign
Finish-wake-and-reject with live storage, torn token/address/serial/lifecycle/retained-auth mirrors, arbitrary matched
integer addresses, same-thread raw/query/reset/report rejection, output atomicity, invalid pointer/index/type/subtree
inputs, test-only cleanup, and unrelated canonical destruction. This remains an allocator-only, production-neutral
prerequisite: merged PR #55 consumes the private constructor for its script-string `OwnershipBatch`, but no
loader or raw ownership site consumes the batch yet. Exact final run **29649484692** passed all nine
hosted jobs before PR #53 squash-merged as `445d436f`; authoritative post-merge master run **29649890520** also passed
all nine jobs.

Merged PR #54 extends the production-neutral terminal-adapter control surface without enrolling the legacy
loader. Commit `d2740fb2` adds retry-safe Live-unload ownership while retaining exact key, lifecycle, callback identity,
and the outer transaction serializer through terminal completion. `7764af22` adds exact-key runtime-table Live-unload
and Abandoned/Unloaded receipt-reset adapters; `dc4aee23` hardens the fault-test argument parser; `74002a69` rejects a
lifecycle generation hidden behind an empty durable table key rather than silently normalizing it; and `0eac1f2d` makes
fixture parsing exact and pins the public enrollment/receipt contracts. Reset makes only the ownership controller
canonical Empty: lifecycle terminal kind, generation, and the durable table key remain exact receipt evidence until the
next claim. Retry resumes the exact callback/cursor without replaying prior work; stale, cross-slot, ABA, swapped-
callback, reentrant, malformed-phase, and corrupt-state paths fail closed. Review cleanup `e8d7a3f6` removes a redundant
post-authentication branch without changing that fail-closed behavior. Codex cleanup `5bee8bba` rejects missing, extra,
unknown, and malformed fault-runner arguments and enrolls three negative CTest gates on portable and measured Windows
x86 jobs. Exact final run **29650796617** and authoritative post-merge run **29651211711** passed all nine jobs before
and after squash merge `8e7fd162`.

The complete GCC Release, GCC Debug, and Clang Release suites pass **117/117** at this checkpoint. The focused terminal
runtime/source selection passes **12/12** under GCC, Clang, `RELEASE_ASSERTS`, and Clang ASan+UBSan. Fifty repetitions
apiece across the ownership/retry/reentry and unsafe-boundary matrix (**400/400** invocations), source/security contracts,
strict i386 and AArch64 compilation, and `git diff --check` also pass.

Merged PR #55 implementation head `e9051955` adds the script-string OwnershipBatch over the retained allocator lease. Begin retains
SCRIPT_STRING then MEMORY_TREE and authenticates the complete allocator/string boundary; Finish repeats that complete
validation, while the four typed operations use the retained free-list certificate and bounded leased checks. The batch
stores no pointer authority: mirrored outer/serial/nested/lifecycle globals, independent TLS mirrors, and durable Frozen
poison prove exact release or retain unproven acquisitions fail closed. Exact legacy readers/mutators, opaque RefString,
callback-free fail-fast canonical reset, post-unlock diagnostics, and unsigned character folding close the adjacent
raw-input findings. `d5f6c9ac` additionally authenticates per-ID debug accounting and requires an exact-address,
non-copyable/non-trivially-copyable capability at every nested lease entry; exact-head run **29656098733** passed all
nine hosted jobs. Final hardening `e9051955` removes both reproducible namespace friend shims, keeps batch/lease mutation
private, threads admission only through translation-private helpers, and makes canonical capability initialization and
destruction guard/registration-free. Macro-off compile coverage rejects every ungated operation, test accessor, and
private lifetime helper. Legal waiter tests retain storage through owner Finish and joins before separate exact
destructor-abandonment coverage. GCC Release passes **117/117**; focused `RELEASE_ASSERTS`, Clang ASan+UBSan, and TSan
pass **4/4**; strict i386/AArch64 production-mode API-seal and fixture compile/link, production-TU cross-compiles,
source/security policy, and `git diff --check` pass. Two exact-head authority/lifetime audits and a portability audit are
clean. Exact-head hosted run **29657884407** passed all nine jobs, exact-head hosted Codex review was clean, and PR #55
squash-merged as `f39e0e4a`. No production caller consumes the batch; same-thread storage
lifetime and callback-free use of only its four typed operations remain explicit preconditions.

Production generation enrollment, alias/completed-object unpublication, production admission/cleanup callback wiring,
and exact-key loader routing remain. PR #71 composes stream/PMem/arena/adapter and string/pending authority through
strict admission/abandonment/Live-unload/drain/reset table operations, but remains unenrolled. Two
`SL_GetStringOfSize`, one `SL_AddUser`, two
`SL_GetString`, one `SL_TransferSystem`, and one `SL_ShutdownSystem` site are source-frozen outside the controller.
These are exactly seven total sites; transfer/shutdown implement the global 4 -> 8 sweep and are not additional sites.
The keyed mutable runtime adapters merged in PR #57 as `57e2b1a2`; exact-head and post-merge runs
**29659895814** and **29660281653** passed all nine jobs, exact-head Codex review was clean, and no raw legacy caller was
enrolled.
Merged PR #59 now supplies the exact one-slab planner/binder and terminal teardown for the journal/entries, FX arena,
Disk32 adapter workspace, and aligned backing. It deliberately does not mint a generation, allocate PMem, or enroll a
production caller. Exact head `8cec770d` passed all nine jobs in run **29671392540** with clean Codex, Gemini, and
independent audits before squash merge `ff61504e`; authoritative post-merge run **29671849514** passed all nine jobs at
that exact merge commit.
Merged PR #60 supplies the separate checked typed PMem scope receipt with exact typed both-prim validation, stable
single-use phase-witness authority, and external-storage/no-bypass contracts. Exact final head `0eec9b1e` passed all nine
jobs in run **29673379640**; Codex reviewed exact final head `0eec9b1e`, Gemini reviewed identical code head `f04c63e0`,
both were clean with zero threads, and authoritative post-merge
run **29673608169** passed all nine jobs at squash merge `74916b5b`. PR #68 subsequently merged the legacy native64
`PMem_FreeIndex`/`PMem_EndAllocInPrim` repair as `2ee1e82c`; PR #69 then merged hidden mutable globals, whole-type
test-helper containment, and actual macro-off ELF/COFF/AppleClang object seals as `534a9b1e`. Merged PR #70 sequence: `293a020c` reserves
the MP/SP PMem lock slots and `716eacc1` adds serialized global lifecycle/allocation/getter access, retained reservation
authentication, and coherent initialization state; `0a9128aa` completes stable owned names and bounded dump snapshots;
`852e7db9` adds the unused permanent-Ended process-life `$init` controller; and `792ff1c7` completes passive shared-
resource authentication. PR #71 completes the exact-key adapter/controller layer, PR #72 merges the capacity/demand
prerequisite, and PR #73 merges the serialized facade. PR #74 published callback-scoped registry borrowing; final head
`79413a18` passed all nine jobs in run **29787341109** with clean exact-head reviews and zero unresolved threads,
squash-merged as `f996e16b`, and passed authoritative post-merge run **29788146050**. The other loader prerequisites
and atomic checked/loader enrollment remain. Merged PR #61 adds
exact-key stream/alias
bind and invalidation with a typed aligned
zone identity, hardened production-neutrality seal, stale-terminal retry safety, complete relocation-capacity release,
and full singleton scrub. Its compiler-validated source seal covers phase-2 line splicing and phase-3 comment-separated
access; the exact re-audit reports **PASS**, the focused runtime/source/security selection passes **4/4** under GCC,
Clang, and Clang ASan+UBSan, and the full GCC Release replay passes **137/137**. Final exact head
`f9dfaaeb43eaaa32cd44c645e3a0e347c9bebdfc` passed all nine jobs in run **29691282387** with all four Gemini threads
resolved and exact-head Codex clean; squash merge `32e6de4efc86823020d1a2eef2c473e013f893ba` passed authoritative
post-merge run **29691725277** with all nine jobs. Merged PR #62 supplies the pending-copy ledger from core `08014141`,
protocol `8d6b04f3`, runtime hardening `8935b5a73836bcf31a09b9e7d2d0bb920377bd08`, and final source-seal review head
`a3c21e9db369d02f29b18f4e1208169517353513`. The merged ledger is production-neutral and locally passes **140/140** at
that exact head plus
strict GCC/Clang, ASan+UBSan, analyzer, i386 runtime, AArch64 strict link, native/AArch64 seals, and final-head
source/security/diff gates. It has no production caller. Final exact head `6a79677f` passed all nine jobs in run
**29694906394**; exact-head Codex and Gemini were clean with zero threads, and it squash-merged as
`888d12e6beedd587602f18cf6763ae04cc067470`. Authoritative post-merge run **29695353022** passed all nine jobs at that
exact master commit. PR #63 merged the audited upstream typed-sort selection as
`f79b0bf422bb926dd302a888bdc258e7e8409aa2`; exact reviewed-head and authoritative runs **29695891172** and
**29696199493** passed all nine jobs. PR #64 merged the complete coordinator through `774487d1` as
`7f030c03269235b3ad703c13404e0975f798bd18`; it borrows exact active transaction authority or owns a standalone
transaction and remains production-neutral. `9f327514` seals the boundary;
`74b56b65` supplies nonblocking admission and fixes pre-held-reader/i386 defects; `90e8fba7` completes portable wiring;
`2a836a0e` maintains ID/entry/owner/predecessor certificates for O(1) member resolution and linear shutdown;
`56c97f09` links the real coordinator/transaction/`FastCriticalSection`/ownership/lease stack into its integration test;
and `774487d1` adds the shared canonical debug-pointer gate, one linear full topology/inverse/exact-debug-total preflight
per bulk, and exact retained second-pass reauthentication. Detached suffixes and forged aggregate totals now fail unsafe,
failure-atomically. Exact implementation head `774487d1` passes GCC Debug **145/145**, focused **6/6** plus independent
**7/7**, strict GCC/Clang,
ASan+UBSan, and genuine i386/AArch64 compile-link gates. The design retains the database hash scope once, keeps
low-level mutation behind a private exact admission capability, authenticates pointer-free boundary mirrors before
dereference, poisons rather than clears on fallible close/abandonment, and applies pre-collected IDs without quadratic
collision-chain work. Final exact PR head `a73916a8467eb5d4a6cad7d33b5d3ecf1f684c37` and merged master passed all nine
jobs in runs **29701509815** and **29702009703**, and exact-head Codex/Gemini/thread review was clean. PR #65 merged the
curated upstream U1/U2 adaptations as `d79069a41e0289f4ed53d174a89d8ee72f40b4a3`; exact reviewed-head and authoritative
runs **29703827041** and **29704069129** passed all nine jobs, exact-head Codex was clean, and all six Gemini threads were
resolved. PR #66 merged the exact tree-neutral ancestry checkpoint
`12309db16d6514ac0df23293cd6074d7bbd15142` as `225759e7d8fd1327210452f3debcd6360465ef2a`; run
**29707497302** passed all nine jobs and the ancestry is verified. PR #67 merged passive durable-receipt composition as
`76d0e065888aab298d430b4bf4e115c07369bc88`; exact-head and authoritative runs **29709263403** and **29709598049**
passed all nine jobs. PR #68 merged the legacy PMem indexing repair, PR #69 merged the hidden-state/object seal, and no
production site is enrolled. PR #70 merged the serialized PMem boundary, process-life controller, and passive
shared-resource authentication as `6a67a66e`; exact final head `ca2d1149` ultimately passed all nine jobs in run
**29726370638**. PR #71 completes the exact-key composite adapters, PR #72 merges the capacity/demand prerequisite, and
PR #73 merges the serialized facade. PR #74 published private exact-key callback borrowing but enrolls no caller; final
head `79413a18` passed all nine jobs in run **29787341109** with clean reviews and zero unresolved threads, it
squash-merged as `f996e16b`, and authoritative post-merge run **29788146050** passed all nine jobs. After completing the
remaining loader prerequisites, enroll all seven sites
atomically:
five coordinator operations plus two exact-key
root-journal stages. Root-string staging
must close its OwnershipBatch before later `DB_AddXAsset` registry acquisition; hash-held mark/default/sweep work uses
short borrowed batches under transaction -> registry -> string -> memory-tree order. The bounded legacy compatibility
surface does not replace the typed guarantee. Static context slots and callback metadata
must live
outside and outlast zone PMem. They must survive
`PMem_Free` and allow the controller to publish `Empty`; only per-generation arena/workspace/journal/backing belongs
inside the existing named
PMem zone scope. `XZone` remains ABI-unchanged because the registry zeroes it with `memset`. A checked fixed arena budget
is acceptable only as an initial compatibility cap that atomically rejects an oversized zone. Stable on-demand PMem
chunks remain the general solution, and registered assets must be removed before sidecar unbind/destruction, staged string
references must be released on abandonment, and `PMem_EndAlloc` must precede abandonment `PMem_Free`. Current DB-thread
longjmp remains process-fatal; these pure primitives do not make production abandonment recoverable.
Writer replacement follows later after exact x86
full-image equivalence.
A checked
whole-segment compressed-finalization boundary remains a
later integrity item
because FX reads mid-segment and SND intentionally skips/copies segments. Remaining FX work is checked writer/save-guard
retirement, broader completed-object/fast-file conversion,
and that later segment-finalization boundary. Separate hard M4 blockers
remain: MP `cpose_t::physObjId`, `BreakablePiece::physObjId`, and `DynEntityClient::physObjId` still truncate ODE
pointers into `int32_t`; the 12-byte DynEntity client image is serialized directly, so it needs an explicit saved mirror
or generation-checked token rather than naïve widening. SP `cpose_t` is native-width, but physics save/update/shutdown
paths still narrow it through `int` locals. All three ownership families must be corrected before any native64 engine
runtime can be enabled. The
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

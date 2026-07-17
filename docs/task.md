# KisakCOD porting task status

This is the live implementation checkpoint for `docs/PORTING.md`. Update it in
the same commit whenever milestone status, validation, blockers, or the active
work item changes. Do not create session-specific handoff files.

## Current state

- Current zone-adapter checkpoint: the zone-owned aligned native arena (`FxFastFileNativeArena`) and the guarded stateful
  zone adapter (`FxFastFileZoneAdapterDisk32Workspace`) are implemented as portable EffectsCore sources with no production
  wiring. The arena binds caller/zone-owned 16-byte-aligned storage only while unbound, so replacing or reusing storage
  must cross the explicit lifetime-checked unbind boundary. It issues zero-filled absolute-address-aligned
  reservations only inside an exact two-deep LIFO transaction protocol, ratchets a committed watermark on commit, and
  reclaims/rezeroes only above that watermark on abandonment, so committed (published) storage is never reissued while a
  failed outer impact transaction strands its interleaved bytes as permanently retired zone storage. The adapter is a
  recording state machine driven by the legacy wire walk: db_load.cpp keeps ownership of stream order, and the adapter
  derives its expected report sequence from the Disk32 records' own tokens and counts, validates every reported extent
  against the caller's XBlock cursor oracle, arena-copies retained sound names inside the open transaction, assembles the
  provenance-bounded converter views, replays recorded resolutions and extents through the reviewed pure
  planner/materializer callbacks exactly once, reserves plan-sized output from the arena, commits, and only then publishes
  through the caller's sink. The sink returns the canonical registered root identity (which may differ from the arena root
  after `DB_AddXAsset` shallow-copies it); callers and nested impact slots receive that canonical identity while its
  pointer-owned children remain arena-backed. A rejected publication strands only unreferenced retired storage. One
  impact-table transaction may nest legacy inline-sentinel effect transactions; effect-under-effect and
  impact-under-impact nesting fail closed. Any failure abandons open reservations
  innermost-first, structurally resets both converter workspaces, and returns the adapter to Idle with committed sibling
  publications intact. The exact adapter workspace is 774,216 bytes on x86 and 799,944 bytes on native64; the arena is
  0x58 bytes with 0x18-byte transaction tokens. The stateful db_load.cpp legacy x86 path, XAsset registration, wire
  bytes, writer, and save-side native64 guard remain unchanged, and the new source contract pins db_load.cpp free of
  adapter wiring until the whole-zone ownership/rollback batch lands.
- Current zone-adapter validation: the complete local GCC and Clang suites are **74/74** green; ASan+UBSan (leak
  detection disabled under the traced runner) and TSan are **73/73** green, with only the compiler-generated static-stack
  test intentionally absent under instrumentation. Strict GCC i386 compilation and actual execution of both new suites
  pass locally (they are single-threaded, so the sandbox `SIGSYS` limitation does not apply), as do strict AArch64
  compilation, Clang static analysis, the new `fx_fastfile_zone_adapter` source contract, the existing
  ABI/pointer/security contracts, the 4 KiB helper-frame gate on both new subjects, and `git diff --check`.
  Initial PR #33 run **29469267456** passed five portable/headless jobs and exposed two integration seams: MSVC `/W4 /WX`
  C4324 on test fixtures embedding alignment-specified members (fixed by heap-allocating every over-aligned fixture
  object), and the measured Windows x86 jobs' explicit build-target list not compiling the new suites (fixed and now
  pinned by the source contract alongside the ctest filter). Codex independently reported the build-list gap and one
  real ordering defect — the three string recorders inspected reported name bytes before the cursor oracle validated the
  extent; each recorder now bounds the length, validates through the oracle, then inspects content, with the exact
  sequence counted three times by the source contract. Gemini's alignment-mask suggestion is applied with its
  power-of-two precondition documented. All three review threads are answered and resolved. Exact review-fix head
  `f7a96827` passed all nine jobs in run **29469989032**, including both measured Windows x86 variants executing the new
  arena and adapter suites. A final independent audit then found two production-wiring blockers hidden by the otherwise
  green primitive tests: `TryBind` could bypass `TryUnbind` and reset accounting for still-published storage, and the
  publication callback could not return the canonical pool identity produced by `DB_AddXAsset`. Commits `503e0b54` and
  `bf7645d2` close those lifetime/identity gaps, add canonical-root and rebind regressions, correct the public commit-order
  contract, and pin both invariants in the source test. Focused GCC, Clang, ASan+UBSan, and TSan arena/adapter/source
  suites are green at `bf7645d2`. Exact hardening head `ca080971` passed all nine required jobs in run
  **29503163189**: Linux amd64/arm64, portable Windows amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release,
  no-Steam Windows x86, and headless Windows x86. The fresh hosted Codex review found no major issue at `4ab63c1b`; the
  only later committed change documents trusted-caller isolation, and a separate exact-head audit found no correctness,
  security, lifetime, malformed-input, ABI/portability, test, or documentation blocker at `ca080971`. PR #33
  squash-merged as `a004701d` from final documentation head `73472a50`; authoritative post-merge master run
  **29506653705** passed all nine jobs. The canceled final PR-branch run is intentionally non-authoritative.
- Merged Disk32 XAsset-envelope checkpoint: PR #34 squash-merged as `3e9b51b0` and defines exact portable
  `XAssetHeaderDisk32` (0x4), `XAssetDisk32` (0x8), `ScriptStringListDisk32` (0x8), and `XAssetListDisk32` (0x10)
  records without importing the widened native `xanim.h` types. A pure report-free layer validates the 32768-asset and
  65536-script-string limits, count/token parity, checked `count * 8` extent, caller-provided bounded record span, raw
  signed type range, and deterministic build admission through a required portable callback. It preflights the complete
  array before publishing an iterator, reads unaligned records with exact-stride `memcpy`, permits but never inspects
  trailing guard bytes, accepts the exact empty-list Span/Begin/End path without invoking admission, revalidates each
  borrowed record before output, preserves high-bit/sentinel tokens byte-for-byte, and leaves layout, iterator, record,
  and cursor outputs unchanged on every failure or `End`. Root validation requires a live four-byte-aligned object
  populated by an exact 0x10-byte copy when sourced from wire bytes; only the separately bounded record span is
  alignment-agnostic. Checked arithmetic distinguishes negative counts, true `count * 8` overflow, and the lower
  32768-entry policy cap. Source tripwires forbid importing or iterating native `XAsset`/`XAssetList` representations.
  Production stream globals, `db_load.cpp`, PMem, zone ownership, the legacy x86 route, and retail bytes are unchanged.
- Merged XAsset-envelope validation: exact reviewed head `ac619d3e` passes the complete GCC and Clang portable suites
  **76/76**, focused warning-as-error execution including Clang conversion/sign-conversion diagnostics, ASan+UBSan and
  TSan execution, strict GCC i386 and AArch64 compilation/linking, Clang static analysis, the dedicated source contract,
  and `git diff --check`. The sandbox blocks the linked i386 executable with its established `SIGSYS`, and no AArch64
  emulator is available. Portable CMake integration executes the runtime and source-contract tests on all five utility
  targets; measured Windows x86 Debug/Release explicitly builds and runs the new target. Independent security/logic and
  test/build audits found no remaining blocker after the empty-list, overflow, aligned-root, deterministic-admission,
  and native-type-tripwire hardening. PR #34 run **29521272126** passed all nine required jobs: Linux amd64/arm64,
  portable Windows amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86, and headless
  Windows x86. Hosted Codex found no major issue, Gemini reported no review comments, and there are no unresolved review
  threads. Authoritative post-merge master run **29522252342** also passed all nine jobs at squash commit `3e9b51b0`.
- Merged Disk32 script-string-walk checkpoint: PR #35 squash-merged as `3271b8d6` and adds the exact
  `ScriptStringTokenDisk32` 0x4 record plus a separate pure report-free header/span/iterator layer. Header validation
  computes checked `count * 4` bytes before the 65536-entry policy cap and enforces root count/token presence parity.
  Span/Begin validate the caller-owned bounded array completely before publishing output, accept null, inline, and
  ordinary offset tokens without decoding or rewriting them, reject the unsupported `0xFFFFFFFE` shared-inline sentinel
  explicitly, permit but never inspect trailing bytes, and read unaligned records at the exact four-byte stride with
  `memcpy`. Next re-reads and revalidates each borrowed token so sequential post-Begin mutation to shared-inline leaves the
  output and cursor unchanged; every failure and `End` is output/cursor atomic. The empty Span/Begin/End route and the full
  65,536-entry array are covered. Production streams, script-string registration, PMem, zone state, `Com_Error`, native
  `const char **` representations, the legacy x86 loader, and retail bytes remain untouched.
- Merged script-string-walk validation: exact audited head `638baace`, including rebased commits `06a3e1f7` and
  `4d2d2cad`, passes the complete GCC and Clang suites **78/78**; focused GCC 16 and Clang 22 warning-as-error CMake
  builds and execution for both the new walk and the existing XAsset envelope; direct conversion/sign-conversion builds;
  ASan+UBSan, MSan, and TSan execution; strict i386 compilation/linking and AArch64 compilation/linking; Clang static
  analysis; both dedicated source contracts; and `git diff --check`. The sandbox blocks the linked i386 executable with
  its established `SIGSYS`, and no AArch64 emulator is available. Portable CMake runs the runtime and source-contract
  tests on all five utility targets; measured Windows x86 Debug/Release explicitly builds and runs the new target.
  Independent security/logic and build/test/CI audits found no blocker. Exact reviewed head `c5246f67` passed all nine
  required jobs in PR #35 run **29523406607**: Linux amd64/arm64, portable Windows amd64/ARM64, macOS arm64, measured
  Windows x86 Debug/Release, no-Steam Windows x86, and headless Windows x86. Hosted Codex found no major issue, Gemini
  reported no review comments, and there are no unresolved review threads. PR #35 then squash-merged as `3271b8d6`.
- Merged zone-load-context primitive checkpoint: PR #36 squash-merged as `15469b3d`; its production-neutral, explicitly
  constructed 16-byte external slot and
  16-byte `{generation, slot}` key now model `Empty`, `Loading`, `Live`, and `Abandoning` without changing `XZone` or
  production lifecycle behavior. Generations are full-width, never issue zero, reject stale/cross-slot/ABA keys, and fail
  closed before `UINT64_MAX` wrap. Claim, commit, begin-abandon, finish-abandon, and unload retain exact idempotent
  receipts; the slot is non-copyable and its destructor deliberately performs no cleanup. Controller state is private
  outside a test-only access shim, every transition validates the complete fixed representation, callback reentry returns
  `Busy`, and unknown/unsafe callback results poison ownership permanently.
- Loading abandonment and committed Live unload have separate mandatory recipes. Loading abandonment runs, in order:
  cancel input/inflate; abort native-adapter transactions; make partial assets, staged references, and copy records
  unreachable; reset alias/direct/stream/delay state; release geometry; tear down arena/workspace/sidecars; `PMem_EndAlloc`;
  `PMem_Free`; clear registry/loading/queue/recovery-gate/signal state; then release the slot internally. Live unload runs
  only live-owned teardown: remove live assets/references; reset shared runtime state; release geometry; tear down native
  storage; `PMem_Free`; remove live registry/handles; then release the slot. It cannot replay load-only cancellation,
  adapter abort, `PMem_EndAlloc`, or loading-gate/signal work. `Retry` preserves the first incomplete cursor, unsafe
  completion fails closed, terminal receipts remain usable only until the next generation, and the final callback must
  retain external serialization until `Empty` is published and the controller returns.
- The controller is deliberately non-atomic: one external serializer must cover every transition, accessor/key query,
  callback, and destruction; `Busy` detects callback reentry only. Cleanup callbacks are convergent
  ensure-postcondition operations, so stages already completed by normal-path loading return `Success` without replaying
  one-shot side effects. Successful publication keeps loading/queue/recovery admission closed while all fallible work and
  `PMem_EndAlloc` finish, calls `TryCommit` to publish `Live`, then performs an infallible no-drop gate/signal release and
  drops the same serializer last. A fallible admission release will require a future committing/admission-pending state
  rather than weakening this ordering.
- Merged zone-load-context validation: the complete GCC and Clang suites are **80/80** green. Focused GCC/Clang
  warning-as-error builds, Clang conversion/sign-conversion diagnostics, ASan+UBSan, MSan, TSan, Clang static analysis,
  strict i386 compilation/linking, AArch64 compilation/linking, the dedicated source contract, and `git diff --check`
  pass. The linked i386 executable reaches the established sandbox `SIGSYS`, and no AArch64 emulator is available.
  Portable CMake runs the runtime/source-contract tests on all five utility targets; measured Windows x86 Debug/Release
  explicitly builds and runs the runtime target. Exact candidate head `f8efc613` passed all nine jobs in PR #36 run
  **29530465823**: Linux amd64/arm64, portable Windows amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release,
  no-Steam Windows x86, and headless Windows x86. Hosted Codex found no major issue at that exact head; Gemini reported no
  comments on the initial candidate. Codex's sole earlier P2 finding identified the unconditional test-access friendship;
  `f8efc613` guards both the forward and friend declarations, the source contract pins the production boundary, the
  thread is resolved, and no unresolved review threads remain. Authoritative post-merge master run **29531440687**
  passed all nine jobs at `15469b3d`.
- Merged transactional script-string-journal checkpoint: baseline core `70ef96fc`, hardened/integrated checkpoint
  `d158d5e9`, prepare/finalize implementation `aa295367`, boundary-hardened implementation `81ded193`, and exact
  source-contract head `83a545ad` provide a production-neutral journal bound to one exact `ZoneLoadContextKey`. Fixed
  caller-owned 8-byte entries preserve one full nonzero `uint32_t` ID per
  ordinary-reference acquisition, including
  repeated IDs; nothing is narrowed or deduplicated. Initialization preflights the complete expected count against the
  65,536-entry cap and caller capacity before any acquisition callback can run. `Staging`, `Sealed`, `Transferring`,
  reversible `Transferred`, reversible `CommitReady`, `Committed`, `RollingBack`, and `RolledBack` phases retain exact
  forward/reverse cursors and terminal receipts. Per-occurrence transfer records `DatabaseUserClaimed` versus
  `DuplicateReleased`; retry/rejection guarantees no ownership mutation, while unknown, zero-ID-after-acquire, or unsafe
  callback completion poisons the journal. Reverse rollback removes one ordinary ref for each still-staged entry, invokes
  targeted database-user removal only for entries that claimed that user, and skips duplicates already balanced by
  transfer.
- `d158d5e9` makes controller validation O(1); `aa295367` performs a complete entry scan only on the first seal, prepare,
  or begin-rollback phase boundary, and validates only the current entry for each transfer/rollback step.
  The maximum 65,536-entry
  lifecycle regression therefore exercises total linear work rather than repeated whole-journal scans. Active state
  rechecks the count cap, cursor bounds, attached-storage alignment/span/overlap, exact uninitialized key, and impossible
  rollback cursors; guarded corruption fixtures prove fail-closed handling. Stateful A/A and A/B/A repeated-ID models
  cover commit and rollback with and without pre-existing database-user ownership. The controller has exact 0x30 size
  and 8-byte alignment on 32- and 64-bit hosts.
- The journal is non-copyable, never allocates, and deliberately performs no destructor cleanup. Callback reentry is
  `Busy`; every status-bearing pre-publication mutation validates the exact generation/slot key and fails closed on
  invalid state. `TryPrepareScriptStringJournalCommit` performs the final scan and moves `Transferred` to
  `CommitReady`. If `TryCommitZoneLoadContext` fails, the journal remains rollback-capable from `CommitReady`. If it
  succeeds and publishes `Live`, the caller must immediately invoke `FinalizeScriptStringJournalCommit`: an unchecked,
  unconditional `void noexcept` operation that only publishes `Committed`, resets flags, and detaches backing. It does
  not validate, branch, scan, invoke callbacks, or report status; its ordering and precondition are caller-enforced.
  Repeating it after `Committed` is harmless. Completed finalization/rollback detaches backing so the receipt remains
  safe after per-generation storage is freed. One global transaction serializer must be acquired before journal
  initialization and held continuously through terminal finalization or rollback return, without being dropped between
  calls. On success it remains held through prepare, `Live` publication, journal finalization, and the no-fail/no-drop
  gate/signal release. It excludes every other journal
  transaction, every raw database-user add/transfer/remove/publication, and the global database-user 4 -> 8 ownership
  sweep. Overlapping journals remain forbidden until a future shared claim-accounting layer exists. The production
  `SL_*` APIs are not wired: their current void/`Com_Error`
  behavior cannot supply the journal's no-change, exact-outcome callbacks.
- Journal validation at exact code/test head `83a545ad`: the complete GCC and Clang suites are **82/82** green.
  Strict warning/conversion builds, ASan+UBSan, MSan, TSan, strict i386 compilation/linking, AArch64 compilation/linking,
  production-TU/test-access isolation, the ABI/source contracts, and `git diff --check` pass. The linked i386 executable
  reaches the established sandbox `SIGSYS`, and no AArch64 emulator is available. CI definitions configure all five
  portable utility jobs to build/run the target and measured Windows x86 Debug/Release to build it explicitly. The
  source contract pins the finalizer to four direct assignments, the real lifecycle controller is linked into composed
  success/pre-`Live` failure tests, and the 65,536-entry path prepares to `CommitReady` before exact rollback.
  Source-contract head `83a545ad` also pins private rollback-detach bodies, loop-token exclusion, the post-prepare
  `Loading` state, and the success-only finalizer guard. Final PR #37 head `376ce097` passed all nine jobs in run
  **29539650666**. Gemini reported no comments, hosted Codex found no major issue, and no review threads remain. PR #37
  squash-merged as `7a9bce34`; authoritative post-merge master run **29542960583** passed all nine jobs.
- Two representation limits remain deliberate. Disk32 permits 65,536 string occurrences, but the packed runtime ordinary
  refcount tops out at 65,535, so 65,536 identical acquisitions may reject on the final callback and must roll back
  precisely; count-valid input is not guaranteed to stage. The journal retains arbitrary nonzero 32-bit IDs for the
  future widened runtime, while a current-runtime adapter must reject IDs outside `0 < id < 65536` before `GetRefString`
  or debug-table indexing.
- Merged referenced-fast-file and SYSTEMINFO hardening checkpoint centralizes the database registry at 33 physical zone
  slots: reserved/default slot 0 plus usable slots 1..32 and a 32-live-zone cap. Referenced checksum/name collection now
  visits exactly the usable range, uses bounded native formatting, and publishes no partial result on capacity or
  conversion failure. Referenced IWD names/checksums now use the same staged, bounded, token-safe publication model,
  reject unterminated/empty/unsafe components, and fail SYSTEMINFO publication on name/checksum count mismatch or a
  nonempty field that cannot round-trip exactly. The reentrant info parser validates complete pair grammar and duplicate
  target keys without the engine's shared rotating buffers while preserving the serializer's canonical absent-empty
  representation. SYSTEMINFO assembly validates exact signed-decimal checksum tokens, bounds every append including the
  terminating byte, preserves prior output on failure, and leaves dvar flags intact unless the complete value publishes.
  The filesystem uses native dvar string pointers rather than integer aliases. Remote referenced-file admission preflights
  every checksum and path before acquiring a string or publishing state, rejects traversal, tokenizer, control, quote,
  download-list-delimiter, NTFS ADS/drive, Windows wildcard/metacharacter, DOS-device, and Win32 dot-normalization aliases,
  and remains unchanged on failure. Server-file comparison computes game-directory prefixes entirely at native width,
  requires an exact path-component boundary, and recognizes only an exact case-insensitive `mods/` namespace. `fs_game`
  is consequently
  canonicalized to forward-slash relative form; legacy `mods\name` input is rejected because backslash is an info-string
  field delimiter.
- The same merged checkpoint closes the server's arbitrary download-name boundary. A client may request only an exact
  canonical
  `.iwd` or `.ff` whose stem appears in the published SYSTEMINFO list for the active mod, fits the 64-byte protocol field,
  and is not an `_svr_` server-only IWD. Authorization is repeated immediately before the first file open, while comparison
  refuses to advertise names the protocol field cannot store. Begin/reset/error/redirect paths now close owned handles and
  blocks, reset transfer/WWW state before reuse, reuse rather than reopen the in-band handle for redirect sizing, and fail
  oversized OS paths nonfatally; an empty CD base cannot fall back to a drive root. The unauthenticated legacy
  `updates/...` request route therefore fails closed, while HTTP/www redirect transport remains separately nonfunctional.
  Legacy update code also uses the native dvar string pointer and tests filename content instead of an array address.
- PR #38 validation: complete strict GCC and Clang suites are **85/85** green; ASan+UBSan (leak detection disabled
  under the traced runner) and TSan suites are **84/84** green. Focused helper/security suites pass after the final
  absent-empty and invalid-key grammar fixes. Strict GCC i386 compilation and execution pass outside the seccomp sandbox,
  strict AArch64 compilation/linking passes, the IWD serializer passes GCC/Clang warning-as-error, i386, static-analysis,
  and ASan/UBSan harnesses, and `git diff --check` is clean. Exact final head `9fb4fc18` passed all nine jobs in run
  **29551519068**, including portable Windows amd64/ARM64 and measured Windows x86 Debug/Release. Hosted Codex found no
  major issue at that exact head. Gemini's sole claimed `Info_SetValueForKey_Big` overflow was disproved by the independent
  runtime loop bound and resolved; no review threads remain. PR #38 squash-merged as `a7c485fd`, and authoritative
  post-merge master run **29551990840** passed all nine jobs.
- Production dispatch now needs a constructed per-zone table that owns this lifecycle slot alongside the journal and
  native arena/adapter state, plus no-report script-string adapters and centralized callbacks that bind the pure recipes
  to a future checked error-unwind boundary. The static lifecycle slot must live outside and outlast zone PMem so it
  survives `PMem_Free` and can publish `Empty`; per-generation arena/workspace/journal/backing remains inside the existing
  named PMem scope. `XZone` stays unchanged because the legacy registry zeroes each slot with `memset`. The first arena
  integration may use a checked fixed compatibility budget that fails the whole zone atomically on exhaustion; stable
  on-demand PMem chunks remain the general solution because the one-pass walk cannot precompute exact widened FX storage
  and the physical-memory reservation is only 128 MiB. Current DB-thread longjmp is still process-fatal, so this
  controller is a longjmp-safe prerequisite rather than production-recoverable abandonment.
- Merged fast-file widening checkpoint: PR #32 squash-merged as `9860617b` from final branch head `0658dcd0`, based on
  production-restore checkpoint `1a966369`. Exact FX fast-file Disk32 effect/visual/trail/impact schemas and report-free
  transactional effect-definition and impact-table planner/materializers are implemented. Review hardening now also
  enforces retail
  timing/count/visibility/atlas canonicalization, rejects non-looping trail definitions that the runtime cannot spawn,
  and clamps the normalized visibility endpoint to the final valid sample pair. The complete local GCC/Clang matrix is
  clean at the merged implementation head. PR #33 subsequently merged the zone-owned native arena and guarded
  XBlock/XAsset adapter primitives; PR #34 merged the generic Disk32 envelope, and PR #35 merged the separate
  four-byte script-string token stride before production wiring. The prior upstream-integration baseline remains merge
  `11a9e08c` through upstream `312a9d2e`; upstream remains nine commits ahead by ancestry through `ba3c79f3`, so
  reconciliation tracks reviewed content selectively.
- Merged curated upstream-reconciliation checkpoint: PR #39 applied four independently audited corrections with explicit
  upstream provenance. `526d59fb` fixes the two `r_marks` scoped-list indices; `9e6c5836` accepts zero-valued sight
  thresholds in the affected assertions; only the `cg_draw` blur correction from `d6b4c5e4` was adapted, returning the
  float directly without pointer punning or double conversion; and `d27803d2` initializes `nodesWritten` for the
  zero-node path. A portable source contract pins all four because three affected SP-only translation units are not
  compiled by the present engine CI matrix. The unrelated Xbox safe-area/startup changes from `d6b4c5e4` remain excluded.
  Exact branch head `dd0cfee9` passed all nine jobs in run **29553056639**; Gemini and exact-head Codex review were clean,
  no review threads remained, and the PR squash-merged as `f1007fbb`. Authoritative post-merge master run
  **29553509114** passed all nine jobs.
- Merged radius-damage reconciliation checkpoint: PR #40 adapted only the `G_CanRadiusDamageFromPos` correction from
  `d592fb4a`, so both unobstructed trace paths return success instead of breaking out of the sample loop and falling
  through to failure. Its mutation-tested portable source contract pins both success branches and the sole
  loop-exhaustion failure return. Exact branch head `952fa06d` passed all nine jobs in run **29554051839**; Gemini and
  exact-head Codex review were clean, no review threads remained, and the PR squash-merged as `dbc84ec9`. Authoritative
  post-merge master run **29554663451** passed all nine jobs.
- Merged actor-navigation reconciliation checkpoint: PR #41 manually adapted the justified navigation corrections from
  `3f256654` and the navigation-only parts of `77404c61`, while excluding unrelated API churn and correcting upstream's
  squared-radius-as-Z defect. It forwards the real goal position, enables all five intended ignore filters, uses strict
  full-XYZ distance plus visibility, passes the real plane normal, and preserves three-component cylinder origins.
  Review hardening also rejects missing LOS goals at both public boundaries and applies cylinder/plane constraints before
  accepting the initial/current node as a goal. Exact final head `add3b87f` passed all nine jobs in run **29555716499**;
  Gemini's null-goal and Codex's current-node-filter findings were fixed, exact-head Codex re-review was clean, no
  unresolved threads remained, and the PR squash-merged as `38025fa5`. Authoritative post-merge master run
  **29556169431** passed all nine jobs.
- Merged vehicle material-timing reconciliation checkpoint: PR #42 adapted `b31ea047` without upstream's sentinel
  regression. Only exact value `-1` disables tread animation; zero and other negative values remain valid, including
  reverse motion. A shared helper performs defined modular 32-bit advancement/interpolation, skips the reserved value in
  the direction of travel, and returns the renderer default when either snapshot endpoint is disabled. SP/MP producers
  and consumers share the contract, and SP uses the established `+32` lighting origin. Review/CI hardening replaced the
  NaN self-comparison with `std::isnan` and made all numeric-limit calls safe from Windows `min`/`max` macros. Exact final
  head `614bbabc` passed all nine jobs in run **29557006806**; exact-head Codex found no major issue, both Gemini threads
  were applied/resolved, and the PR squash-merged as `599dbb88`. Authoritative post-merge run **29557583267** passed all
  nine jobs. The producer-side `forcedMaterialSpeed` float-to-int range risk remains explicitly deferred.
- Merged missile-layout reconciliation checkpoint: PR #43 adapted only the missile-field part of `d592fb4a` and the
  complete grenade-prediction-cache correction from `77404c61`. MP/SP retain exact 0x3c/0x54 layouts, typed fields replace
  mover/item aliases, and SP cache validity uses only `predictLandTime`, preserving a valid `{0,0,0}` landing position.
  Exact final head `d33e80cc` passed all nine jobs in run **29582100035**. Gemini's uninitialized fallback prediction-time
  finding was fixed by zero-initializing the output and pinning it in the source contract; that thread was resolved,
  exact-head Codex was clean, and no unresolved threads remained. The PR squash-merged as `2e9c19e7`; authoritative
  post-merge run **29582679571** passed all nine jobs. SP production translation units remain deliberately unbuilt while
  every CI job uses `KISAK_BUILD_SP=OFF`.
- Merged safe-fixlet reconciliation checkpoint: PR #44 contains exactly four audited `77404c61` corrections: SP
  friendly-fire melee suppression, removed-snapshot Shutdown -> Unlink -> FX/DObj teardown, fixed complete/failed
  objective commands, and both translation-unit-local actor miss caches initialized to `6969.0f`. All unrelated omnibus
  hunks and the three rejected actor-aim behavior changes remain excluded. Review hardening normalizes CRLF before source
  slicing, scopes the contract to the measured Windows x86 ctest selection, and reports mutation diagnostics. Exact final
  head `da1ea81f` passed all nine jobs in PR run **29584250420**; exact-head Codex found no major issue, both Gemini
  threads were resolved, and no unresolved review threads remained. The PR squash-merged as `cbb8bdb0`, and
  authoritative post-merge run **29585012405** passed all nine jobs.
- Current SP target-table portability candidate: `0a1e89b2` replaces truncated pointers and raw 28-byte walks with a
  native-width typed 32-entry table; `c63bb68e` adds the shared dependency-light target protocol, dual-width layout and
  executable/source contracts; `d981527f` hardens publication boundaries; and `653fdfdb` scopes the contract's build and
  test assertions to measured Windows x86. The shared parser rejects malformed or duplicate recognized keys, WORLD/NONE
  and out-of-level entities, noncanonical material/offset/flag values, and unsafe lock durations. Load validates every
  live entity identity, duplicate reference, and registered material before authoritatively resetting live `FL_TARGET`
  bits, discarding stale table storage without dereferencing it, and publishing the complete staged table. Target and
  shader producers validate the same domain before mutation, flags round-trip symmetrically, and weapon lock consumers
  fail closed on invalid or stale indices while preserving the retail x86 configstring representation.
- Target candidate validation at exact head `653fdfdb`: fresh GCC 16 and Clang 22 builds each pass **97/97** tests, and
  the focused target runtime/source, pointer-tripwire, and PR #44 source-contract set passes **4/4** under each compiler.
  At production/test head `d981527f` (before only the final CMake source-assertion commit), the runtime contract passes
  Clang ASan+UBSan; strict GCC/Clang i386 object compilation and AArch64 GCC object compilation pass. The sandbox kills
  i386 execution with exit 159, so no i386 runtime result is claimed. A separate throwaway manual audit—not a built-in CI
  mutation mode—proved that all nine stale-init, broad entity-domain, material-zero, offset-overflow, missing-third-arg,
  unsafe-duration, unvalidated-producer, unregistered-load-material, and shader-publication mutations are rejected.
  `git diff --check` is clean. SP production translation units remain unbuilt: direct probes stop before this code on
  pre-existing ILP32 layout assertions, an undeclared `IsValidSeed`, and missing DirectX `d3d9.h`, while every current CI
  engine job still uses `KISAK_BUILD_SP=OFF`.
- Remaining upstream content stays subsystem-scoped: publish the reviewed server target table before implementing a
  strict transactional `CG_TargetsChanged` plus bounded vehicle/Javelin HUD consumers; handle the separately identified
  squared-distance-versus-unsquared grenade safe-radius defect in its own batch; and leave dynent save/load `ba3c79f3`
  deferred until bounded transactional Disk32/native-sidecar semantics exist. Do not import d592's raw-pointer-bound
  `CG_GetTargetPos` loop or unrelated weapon-fire changes.
- Current fast-file widening checkpoint: one canonical portable `FxEffectDef`/`FxElemDef`/visual/trail runtime type family
  now replaces the renderer-only duplicate definition boundary. The effect converter validates exact Disk32 graph
  provenance, freezes each bounded resolver request group before callbacks, snapshots the resolver descriptor, binds
  strings and native identities once, detects source/callback mutation, plans both legacy and widened layouts, and
  placement-constructs the canonical native graph into caller-owned storage. Trail allocation preserves the legacy
  `indCount` vertex-capacity rule while copying only `vertCount` initialized vertices and clearing the tail. The parallel
  impact converter preserves the legacy boolean interpretation of the table token, snapshots all twelve entries, resolves
  396 fixed handle slots plus the owned table name, and materializes actual `FxImpactTable`/`FxImpactEntry` objects. Both
  converters are heap-workspace, allocation-free, callback-free during materialization, failure-atomic before output
  mutation, and guarded against reentry, source/resolver mutation, caller-storage aliasing, partial overlap within every
  resolver-reported retained extent, and stale/tampered plans. The effect workspace is exactly 325,904 bytes on x86 and
  325,928 bytes on native64; the impact workspace is 11,216 and 11,232 bytes, respectively. They are production-manifest
  sources with portable and measured Windows x86 test targets.
  The stateful `db_load.cpp`/XBlock cursor, XAsset registration, legacy x86 loader, archive writer/wire bytes, save-side
  native64 guard, and licensed workflow remain unchanged. PR #33 supplies the zone-owned native arena and guarded
  stateful adapter primitives, and the current envelope checkpoint supplies the fixed top-level Disk32 walk boundary.
- Current fast-file validation: implementation/test head through `1153eefe` passes GCC and Clang **71/71**, ASan+UBSan
  and TSan
  **70/70**, the focused effect converter under Clang MemorySanitizer, and the updated fast-file source/security contract.
  Only the generated static-stack test is intentionally absent under instrumentation. Strict GCC i386 and AArch64
  compilation/linking also pass; the sandbox blocks i386 execution with `SIGSYS` and has no AArch64 emulator. The prior remote head
  `0f376a92` passed Linux amd64/arm64, macOS arm64, and the Windows x86 no-Steam/headless jobs in run **29462535215**;
  portable Windows amd64/ARM64 and measured Windows x86 Debug/Release found test-only `/W4 /WX` conversion, unary-minus,
  and over-alignment diagnostics. Commits `83ae1414`, `dd58dbef`, and `55685e95` remove those representation and compiler
  hazards; the semantic/runtime corrections and canonical regression fixtures follow through `654798ac`. Replacement run
  **29464935543** then passed Linux amd64/arm64, portable Windows amd64/ARM64, macOS arm64, Windows x86 no-Steam, and
  headless Windows x86. Its measured Debug/Release jobs found one remaining test-only MSVC C4324 diagnostic; `1153eefe`
  removes that redundant fixture alignment. Extra Clang conversion/sign-conversion diagnostics are clean for all three
  new fixtures, and Codex found no major issue at review head `e5b755a4`. Exact final candidate run **29465922917** passed
  all nine jobs, as did independent post-merge master run **29466158837**. The enforced
  4 KiB helper-frame gate previously measured 1,056 bytes maximum under GCC and 984 under Clang. No licensed workflow was
  dispatched.
- Scope: multiplayer client and headless dedicated server; single-player is deferred.
- Current production-restore checkpoint: `FX_Restore` now consumes the legacy Disk32 tail through the unified portable
  reader and an independently owned mutable candidate. The 670,976-byte x86 / 695,640-byte native64 reader and
  376,240-byte x86 / 400,904-byte native64 candidate are checked heap workspaces. Candidate construction holds the
  reader's private operation gate across complete source validation and copy, requires the exact lease identity stored by
  the reader plus a currently valid lifecycle lease, revalidates graph/metadata/semantics/physics, relinks every candidate
  pointer to candidate-owned buffers, and publishes `Ready` last. Ready-view access now requires, validates, and rechecks
  that same exact active lease while holding the candidate operation gate; stale, released, forged, reacquired, and
  callback-reentrant access fails without changing the caller's output. Production destroys the reader, validates and
  releases that exact lease, and immediately calls generation-checked `FX_BeginArchive` before any copied asset identity
  can be dereferenced. Centralized staging cleanup owns both workspaces plus desired/replaced physics entries, rollback
  buffers, and transaction scratch; the established publication/rollback/safe-empty controller remains the sole live-state
  commit path. The former raw restore parser, restore-only pointer-width/ABI/address-relocation path, and native64 restore
  guard are removed. `FX_Save`, the legacy writer and save-side native64 guard, wire bytes, and licensed workflow are
  unchanged.
  Implementation commits are reader coverage `78a14fbc`, updated contracts `0f689d9b`, candidate adapter `8d94e7c5`, and
  production switch `e1174d33`; exact-lease Ready-view hardening and its production/contracts/regression follow-ups are
  `597bc6f7`, `9f7ce4c7`, `3d46d8b1`, and `c0b72ed7`. Review cleanup `57c5fa0a` removes an unnecessary const cast without
  changing validation semantics.
- Merged portable-reader prerequisite: the lightweight native `BodyState` leaf and exact `BodyStateDisk32` semantic decoder
  are implemented, the generic `Phys_ObjSave` three-byte disclosure is closed, logically-const Ready-only physics
  enumeration and exact active effect-table lease validation are public, and the complete legacy
  system/buffers/address/body tail is staged transactionally in one heap-owned, report-free reader workspace. The exact
  workspace is 670,976 bytes on x86 and 695,640 bytes on native64. It hides every partial read and callback prefix until
  final `Ready`, validates a borrowed caller-retained definition lease without acquiring or releasing it, and supports retry
  with fresh/repositioned input. Raw and zlib success fixtures cover 0, 2, and 512 bodies; one-body failure/lease paths
  cover truncation at every address width and first-body prefix, structural/semantic/body/lease failures, a high-half lease
  serial, high-bit archived addresses, callback reentry, trailing data, and failure-atomic views. That merged prerequisite
  intentionally left production unchanged; the current checkpoint above has now completed its restore-side integration.
- Merged selector-publication checkpoint: restore now carries visibility-buffer roles as one bounded, deliberately
  zero-invalid `{read, write}` selector pair. Exact-pointer derivation, bounded resolution, and round-trip validation are
  failure-atomic and reject null, foreign, aliased, or out-of-range roles without pointer arithmetic or integer codecs.
  Desired and rollback graph images bind their selectors only to their own staged buffers; publication preflights both
  roles, copies buffers before the system image under archive/allocator exclusion, relinks every base pointer, resolves
  fresh pointers against the live buffers, and validates the exact roles before graph admission. The rollback snapshot
  captures and rebinds its live selector pair inside the coherent `FX_ALLOC` interval, while safe-empty recovery retains
  canonical read-zero/write-one roles. GCC and Clang suites are **62/62** green; ASan+UBSan and TSan are **61/61** green.
  Strict GCC/Clang x86-32 compilation, strict AArch64 compilation, Clang static analysis, the three changed source/security
  contracts, and `git diff --check` pass. This sandbox cannot execute the threaded i386 fixture; exact implementation/
  review-fix head `0fbee229` passed all nine jobs in run **29445375084**, and final documentation head `4fdc0ba7` passed
  all nine again in run **29446277872**, including measured Windows x86 production compilation, runtime contracts, and
  frame budgets. Both actionable review threads are resolved, and PR #29 squash-merged as `559cad41`. No wire bytes,
  native64 guard, writer, safe-empty controller sequence, or licensed-content workflow changed.
- Merged semantic-delegation checkpoint: the production `FX_CollectArchivePhysicsEntries` wrapper is now a bounded output
  adapter over the shared semantic oracle instead of maintaining a second definition-aware graph traversal. Count-only
  collection preserves the legacy capacity behavior; population checks capacity before every write; optional native state
  capture completes before spotlight/count publication; and restore retains the effect-definition lease through both the
  count and population passes. Every intervening failure releases that lease through the centralized fail-closed boundary,
  with physics staging freed first where applicable. The portable native fixture now also covers two physics elements,
  multi-visual model selection, high-bit tokens, native owner indices, deterministic sink order, and a null payload-prepare
  callback. Source contracts pin the shared-oracle delegation, output ordering, absence of duplicate traversal, and the
  complete leased interval. PR #28 is merged after its implementation, review-fix, and final documentation heads each
  passed the complete five-target/measured-Windows gate.
- Merged Ready checkpoint: the shared, portable `FxArchive` semantic oracle keeps renderer-owned `FxElemDef` opaque,
  pins every native-width field/nested-layout assumption in a renderer-facing translation unit, and performs a complete
  callback-free semantic preflight before a bounded second traversal may activate definition-selected union members or
  emit physics descriptors. It validates system/camera/visibility state, effect timing and definition counts, ordinary
  element classes, trails, the singleton spotlight, physics model selection, unsigned legacy tokens, native owner indices,
  and the 512-body ceiling without allocation, locks, reports, or archive I/O. Payload callbacks are mechanically required
  to preserve the complete 40-byte `FxElem` representation. The native finalizer revalidates links/selectors plus the full
  allocation graph, invalidates prior views before mutation, activates the correct payload lifetimes, canonicalizes active
  frame counts and spotlight state, records the physics-body count, and publishes `Ready` as its final mutation. Any failure
  leaves `Empty` and requires a structural rebuild. Definition provenance remains a caller-held lifetime lease; the views
  are shallow-const, staging-only, and non-publishable. At that merge point, production `MemoryFile` integration, live
  publication, the legacy collector, writer, and both native64 archive guards remained unchanged. PR #28 subsequently
  removed the duplicate collector traversal, and PR #29 added selector-aware live publication without changing wire I/O,
  the writer, or either guard.
- Merged structural checkpoint: the 327,128-byte native64 / 310,672-byte x86 heap-owned
  `FxArchiveDisk32NativeWorkspace` decodes the fixed system and metadata, reconstructs all three pool free lists, resolves
  only active definition keys without dereferencing returned definitions, converts every effect, and placement-constructs
  the correct native member of every pool slot. It preserves validated free links plus opaque trailing bytes under exact
  representation contracts, copies visibility/deferred/padding state, links all workspace-local base and exact read/write
  selector pointers, round-trips those selectors through the shared helper, and validates the complete allocation graph
  with workspace-owned scratch. Same-workspace resolver reentry is rejected, every failure leaves the phase `Empty`, and
  `StructurallyValid` is the final successful workspace mutation. Its gated read-only view remains non-publishable and
  unsuitable for definition-dependent payload access until the merged semantic finalizer reaches `Ready`. At that
  structural checkpoint, `MemoryFile`, live publication, production archive I/O, and both native64 production guards were
  unchanged; the current production-switch checkpoint supersedes that restore-side state.
- PR #26 initial run **29430362954 passed four of nine jobs** at `d34e2d09`: Linux amd64/arm64, macOS arm64, and
  headless Windows x86 were green. Portable Windows amd64/ARM64 rejected an uncalled header-only graph-validation
  convenience wrapper whose implicit scratch consumed 22,688 bytes under MSVC analysis; measured Windows x86
  Debug/Release and no-Steam builds independently showed that the private workspace's 310,668-byte GCC i386 total
  needed four bytes of MSVC x86 tail padding. The correction removes the wrapper completely (all production callers
  already provide checked scratch), moves the fixture's former wrapper call to heap-owned scratch, and explicitly aligns
  the private workspace to 8 bytes so every ILP32 compiler has the same 310,672-byte contract without changing any member
  offset. It also addresses both actionable Gemini threads: visibility selectors are redundantly bounded before native
  pointer derivation, and fixed/native padding extents are compile-time equal. Replacement run **29435390924 passed all nine
  jobs** at exact implementation head `cbd82d79`; both threads are answered and resolved, and an independent correction
  audit found no remaining correctness, ABI, security, portability, test, or documentation blocker. PR #26 then
  squash-merged as `6642d0d2` from final documentation head `0a07f546`; documentation-only run **29436148095 passed all
  nine jobs**.
- Merged portable-reader checkpoint validation: GCC and Clang suites are **66/66** green. ASan+UBSan (leak detection
  disabled under the traced command runner) and TSan are **65/65** green, with only the compiler-generated static-frame
  test intentionally omitted under instrumentation. Strict GCC/Clang x86-32 and AArch64 compilation, Clang analysis,
  ABI/pointer/security/source contracts, and `git diff --check` pass. The reader's maximum locally observed frame is at
  most 288 bytes, below the portable 4 KiB helper-frame gate. Independent security, failure-atomicity, ABI, lease,
  integration, stack, and portability audits found no remaining blocker after correcting late lease-loss classification,
  explicit test dependencies, and source-contract false positives. The sandbox cannot execute the threaded i386 fixture;
  the local implementation results are from exact head `aa09e0bb`. Exact PR #30 checkpoint head `42d1c4bb` passed all nine
  jobs in run **29449586954**: Linux amd64/arm64, portable Windows amd64/ARM64, macOS arm64, measured Windows x86
  Debug/Release, no-Steam Windows x86, and headless Windows x86 are green. Gemini reviewed that exact head with no comments
  or additional feedback, and the thread-aware query is empty. Final documentation head `6ce201f4` passed all nine jobs
  again in run **29450294896**, and PR #30 squash-merged as `7cbe7070`.
- Current production-switch validation: the complete local GCC suite is **67/67** green and the complete Clang suite is
  **67/67** green. The complete
  ASan+UBSan and TSan suites are **66/66** green, with only the compiler-generated stack-usage test intentionally omitted
  under instrumentation. Strict i386 compilation and linking pass; this sandbox
  blocks the resulting i386 runtime with `SIGSYS`, so Windows x86 CI remains the executable production-width authority.
  AArch64 compilation/linking, Clang static analysis, the reader/native/effect-table/physics/security source contracts,
  stack/ABI checks, independent
  lease/lifetime/cleanup audits, and `git diff --check` pass through review-fix head `57c5fa0a`. Initial PR head
  `cfc3454a` passed all nine required jobs in run **29452814892**, including all four measured Windows x86 engine
  variants. Gemini's valid const-correctness cleanup is implemented; its two null-cleanup reports conflict with the tested
  idempotent-null destruction contract, and its claimed `BodyState` padding conflicts with the complete pinned 0x00--0x70
  field layout. Those three threads have evidence-backed replies, and all four review threads are resolved. Exact PR head
  `21dae5ca` passed all nine required jobs in run **29453934377**, including all four measured Windows x86 engine
  variants. PR #31 squash-merged as `1a966369` from final documentation head `9fb7dafd`; post-merge run
  **29454579529** also passed all nine jobs, including all four measured Windows x86 engine variants.
- PR #27 initial run **29439592615** at `ff59ef7e` passed Linux amd64/arm64, but portable Windows amd64/ARM64 exposed an
  include-boundary issue before their tests linked: the semantic translation unit imported the complete physics-sidecar
  header only for its token/512-body constants, causing MSVC analysis to charge an unrelated 4,104-byte inline convenience
  wrapper against the semantic target's 4 KiB helper gate. The correction removes that dependency, reads the stored token
  directly as its unsigned object representation, owns a narrow 512-body semantic constant, and pins it against the
  production sidecar where both APIs are already visible. A source contract now forbids reintroducing the stack-heavy
  header. Focused GCC, Clang, ASan+UBSan, TSan, analyzers, actual x86-32 execution, AArch64 linking, ABI debt, and source
  invariants pass. Replacement run **29439953821 passed all nine jobs** at exact correction head `d6699d75`: Linux
  amd64/arm64, portable Windows amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86, and
  headless Windows x86 are green. Gemini reported no findings and the thread-level review query is empty. PR #27 then
  squash-merged as `07e3a8a0` from final documentation head `cdd6f7d3`; documentation-only run **29440697547 passed all
  nine jobs**.
- PR #28 implementation run **29441864655 passed all nine jobs** at `e77539cd`: Linux amd64/arm64, portable Windows
  amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86, and headless Windows x86 are green.
  Gemini identified one real defensive gap in the new source-contract literal counter; `34f1dc4f` rejects an empty needle
  before its loop. Its reported collector null dereference was declined with the callee's explicit pre-index guard as
  evidence: a nonzero count with null entries fails immediately, while the zero-count loop performs no access. Both threads
  are answered and resolved. Replacement run **29442093763 passed all nine jobs** at that review-fix head. A separate
  lease/bounds/cleanup audit found no double free, use-after-free, capacity, failure-atomicity, or post-release lifetime
  defect. Final documentation-only run **29442813157 passed all nine jobs**, and PR #28 squash-merged as `4c454449` from
  final head `6792f3b5`.
- PR #29 implementation/review-fix run **29445375084 passed all nine jobs** at `0fbee229`: Linux amd64/arm64, portable
  Windows amd64/ARM64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86, and headless Windows x86
  are green. Gemini's two actionable threads added local null-context rejection to desired and rollback publication before
  any live-system dereference; both are answered and resolved. Independent review found no production correctness,
  security, locking, pointer-provenance, x86 ABI, or stack blocker. Its concrete regression-protection findings now pin the
  exact relocation-to-selector mapping, matcher/copy/null ordering, subscript-free pointer resolution, exact iterator
  sentinel, aliased outputs, and non-owned one-past roles. Behavioral whole-graph publication/rollback coverage is required
  in the later production-reader integration PR rather than modeled by a duplicate test-only adapter.
- PR #25 squash-merged as `09c05e5f` from final review-fix head `5abf9cbb`. Final run **29427215187 passed all nine jobs**:
  Linux amd64/arm64, portable Windows amd64/arm64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86,
  and headless Windows x86. Initial implementation/docs head `d9ad05ff` also passed all nine jobs in run **29426792491**.
  Gemini's real finding added a compile-time raw-slot extent contract without introducing native `sizeof` ABI debt; its
  request to replace the bounds-proven `.data()` access was declined because hardened indexing could add an assertion/report
  path to the report-free decoder. Both threads were answered and resolved, and an independent exact-head audit found no
  remaining security, lifetime, ABI, malformed-input, portability, documentation, or CI-integration defect.
- Merged FxSystem checkpoint: the first fixed full-system seam introduces a
  strong numeric `ArchiveAddress32` distinct from fast-file tokens and definition keys, exact `FxCameraDisk32` (`0xB0`),
  `FxSpriteInfoDisk32` (`0x10`), and `FxSystemDisk32` (`0xA60`) layouts, plus a pure transactional decoder. It proves the
  complete `0x47480` buffer-address span and exact internal topology without forming pointers, derives distinct visibility
  selectors, validates scalar/byte-boolean/camera/time/pool/ring state, remaps the complete 1,024-entry effect-handle
  permutation, emits a physical active-slot bitmap, and conditionally validates/remaps spotlight handles. Every native
  pool, sprite, and visibility pointer remains null for later full-image linking; malformed input leaves both outputs
  byte-for-byte unchanged. At that decoder checkpoint, production archive I/O and both native64 production guards were
  intentionally unchanged; the current branch has since retired the restore guard and raw parser only.
- Merged buffer checkpoint validation: GCC and Clang suites are **60/60** green. ASan+UBSan (leak detection disabled
  under the command runner) and TSan are **59/59** green, with the compiler-generated static-frame test intentionally
  omitted under instrumentation. The hand-authored fixture covers exact nested/top-level offsets, full/empty/sparse and
  maximum-capacity lists in all three pools, first/middle/last heads, every malformed head/link/count/cycle class, ignored
  active-slot and unrelated bytes, null output, composite all-or-nothing behavior, and input/output byte preservation.
  The true x86 oracle explicitly starts class-valued union members, populates a nonzero effect plus active/free pool
  records, visibility/deferred/padding state, and compares the complete `0x47480` native image. Strict warnings-as-errors
  x86-32 execution and AArch64 linking, extra conversion warnings, Clang analyzers, ABI/pointer/security/source contracts,
  and `git diff --check` pass. Reported GCC/Clang helper frames peak at 912/870 bytes, below the portable 4 KiB gate. Two
  independent audits found and verified the x86 fixture lifetime correction and report no remaining implementation or
  integration blocker. The authoritative five-target and measured Windows x86 CI matrix remains the PR gate.
- PR #24 squash-merged as `2b92e7a7` from final documentation head `94d9ba1a`. Replacement run **29423014541 passed all
  nine jobs** at exact correction head `6671f87b`: Linux
  amd64/arm64, portable Windows amd64/arm64, macOS arm64, measured Windows x86 Debug/Release, no-Steam Windows x86, and
  headless Windows x86. Initial run **29422678108** had already passed Linux amd64/arm64, macOS arm64, and headless
  Windows x86, but portable Windows amd64/arm64 stopped on a fixture-only `/W4 /WX` C4244: class-template argument
  deduction built a temporary `pair<size_t, int>` before converting it to `pair<size_t, uint8_t>`. Commit `337cfe9c`
  replaces that initializer with an explicit fixed-width `U8Mutation` array. Gemini reported no findings at core
  implementation head `b373429e`; the later change is confined to that fixture correction and status documentation. The
  thread-level review query is empty, and two independent local audits found no remaining blocker. Documentation-only
  run **29423861013 passed all nine jobs** after GitHub merged immediately rather than retaining the requested auto-merge;
  no implementation changed after the prior all-green run.
- PR #22 squash-merged as `56760d80` from final documentation head `b86ab94d`. Final run **29418054504 passed all nine
  jobs**; implementation head `f48b04c1` also passed all nine in run **29417195541**. Gemini provided no review comments,
  and Codex found no major issue at the exact implementation head. The merged leaf layer separates full-width native
  definition identity from serialized keys, preserves exact legacy x86 pointer-bit keys, assigns deterministic native64
  opaque keys, pins exact `0x1C` spatial-frame and `0x80` effect-record layouts, explicitly packs the bolt/sort word, and
  transactionally converts owner handles between Disk32 and native strides without enabling native64 archive I/O.
- Merged Disk32 leaf validation: GCC and Clang are **56/56** green; ASan+UBSan (leak detection disabled under the command
  runner) and TSan are **55/55** green. The codec fixture exhausts all 65,536 possible values in both handle directions,
  preserves outputs on every failure, verifies a hand-authored little-endian `0x80` record and high native64 definition
  identities, and proves conditional x86 raw-record equivalence. Strict warnings-as-errors x86-32 and AArch64 compilation,
  x86-32 execution, focused source/security contracts, Clang analyzer checks, exact legacy raw/zlib table parity, and
  `git diff --check` pass.
- PR #21 squash-merged as `0f878ff4` from final documentation head `cb731d6e`. Final run **29414351528 passed all nine
  jobs**; implementation head `7895f7a9` also passed all nine in run **29397910131**, Codex found no major issue at that
  exact implementation commit, and the sole Gemini finding was fixed and resolved. Camera/time publication is
  assembled off-side and invalidated/published through atomic markers. An external fixed-width shared/exclusive camera gate
  serializes ordinary publishers with draw, mark, vertex-generation, spawn-cull, and sprite-sort readers without changing
  frozen `FxSystem`; every camera owner nests inside cooperative archive admission. Visibility swap/query paths likewise
  retain cooperative admission through pointer/payload use. The neutral visibility query now has a report-free try-admission
  path that reads no mutable `FxSystem` state before ownership. Active archives reject the intentional time-to-camera gap,
  while only an exact reset camera is accepted before the first draw frame.
- `FX_Save` now retains the validated effect-definition table, captures raw system and buffer images once under proven
  archive/allocator exclusion, derives exact 0/1 visibility selectors, and relinks only a distinct heap-owned validation
  view. It validates copied pool/definition/physics semantics before any legacy bytes are emitted, rejects effect-definition
  pointers not present in the staged table before dereference, then releases archive ownership and writes the unchanged x86
  sequence (table, system, buffers, system address, physics states). The serialized raw-pointer view is never relinked.
- Merged snapshot validation: GCC and Clang are **55/55** green; ASan+UBSan (leak detection disabled under the command
  runner) and TSan are **54/54** green, with the compiler-generated static-frame test intentionally omitted under sanitizer
  instrumentation. Strict warnings-as-errors compilation passes for the new iterator, snapshot-publication, and bounded
  effect-table fixtures on x86-32 and AArch64. Focused source/security contracts, Clang analyzer checks, and
  `git diff --check` pass. Independent camera-census and archive/provenance audits found no blocker. PR run
  **29397910131** then passed Linux amd64/arm64, Windows amd64/arm64, macOS arm64, Windows x86 headless/no-Steam, and
  measured Windows x86 Debug/Release, including the linked production TLS/error-unwind path, legacy save integration,
  and stack-budget enforcement.
- Merged-baseline validation: master run **29393277892 passed all nine jobs**. GCC and Clang full suites are **53/53**
  green. ASan+UBSan (leak detection disabled under the
  command runner) and TSan are **52/52** green; their compiler-generated static-frame test is intentionally omitted because
  sanitizer instrumentation creates dynamic frames, while their maximum-capacity runtime still runs on a bounded 1 MiB
  worker stack. Strict helper/test builds pass for x86-32 and AArch64. Optimized save-helper reports peak at 80 bytes on
  x86-32 and 64 bytes on native amd64; the current debug full-suite reports peak at 96 bytes for save and 288 bytes for
  restore, all far below the 4 KiB helper ceiling. Three independent pre-publication audits found and verified an
  x86-only constant-expression test guard, complete one-shot write coverage, and two production MSVC report-emission
  corrections; no remaining branch-scope defect was found before publication.
- PR #20 squash-merged as `92ad1429` from exact reviewed head `40577c91`. Final run **29392782518 passed all nine jobs**,
  Codex found no issue at that exact head, and both actionable Gemini threads were fixed and resolved. Initial run
  **29391004484 passed five of nine jobs**: Linux amd64/arm64, macOS arm64, Windows x86 no-Steam,
  and Windows x86 headless were green. Portable Windows amd64/arm64 and measured Windows x86 Debug/Release failed because
  CMake emitted the custom ruleset path as a C++ source operand (`error C2059` at the XML's opening `<`). The measured
  production jobs nevertheless established the 2,752/6,124-byte save/restore frames and found the 10,256-byte wrapper.
  The current correction binds `/analyze:ruleset<path>` as one compiler argument while preserving the portable 4 KiB
  `/WX` helper gate, uses MSVC's following-value form for the XML log, and replaces the large wrapper with caller-owned
  checked heap scratch allocated before archive exclusion and destroyed on every save exit. Gemini's two review findings
  are also addressed: an impossible effect-table snapshot destruction failure now fail-stops before storage release, and
  the PowerShell report sort quotes its `Function` property. Calibration run **29392268431** and review-fix run
  **29392548515** also passed all nine jobs. Debug and Release both enforce 2,756-byte `FX_Save`, 6,124-byte `FX_Restore`,
  and 2,064-byte maximum-other frames against their 4/8/4 KiB budgets.
- PR #19 squash-merged as `885ec28a` after run **29387860025 passed all nine CI jobs**. Gemini reported no findings,
  Codex found no major issue at reviewed head `cd0f1363a4`, and there were no inline review threads. The legacy
  `FX_RestoreEffectDefTable` stack image is now a bounded lifecycle-owned
  BSS transaction. It caps raw records at 1,024, reads every 1--63 byte name and four-byte key through the report-free
  MemoryFile boundary, decodes keys explicitly little-endian, rejects zero/conflicting keys, and collapses only exact
  duplicate key/name pairs before the first registration. Name validation now also blocks traversal, Win32-invalid
  characters, trailing dot/space normalization, and DOS device components (including extension and superscript-digit
  forms). A pointer-width atomic cookie, exact TLS identity, monotonic serial, and real Closing sentinel make stale,
  nested, foreign, and partial cleanup non-destructive. Archive and lifecycle admission use symmetric pre/CAS/post
  handshakes; error cleanup abandons the lease before every other FX gate. That checkpoint preserved pool/allocation graph
  validation before staged pointer fixup, released the lease afterward, and passed the captured generation into later
  archive admission; the current reader/candidate switch now extends exact lease ownership through independent candidate
  materialization and releases it immediately before admission. The obsolete public table types/APIs and their two
  frozen-ABI debt rows are gone, eliminating the 8,196-byte x86 / 16,392-byte native64 table frame and the uninitialized
  truncated-key path
  without fallible heap ownership. Real raw/zlib fixtures cover full capacity, late malformed input (including 0--3
  key bytes), duplicate/registration policy, reentry, abandonment, stale/foreign ownership, immediate reuse, and held-
  owner contention with zero engine reports. Local GCC, Clang, ASan+UBSan, and TSan suites are **50/50** green; strict
  helper/test/atomic compilation passes for x86-32 and AArch64; two independent audits found and verified the late-key
  coverage and Win32 device-name fixes and report no remaining batch issue. The authoritative Windows x86 production
  compile and five-target utility matrix passed at the merge head.
- PR #18 squash-merged as `4f84ffca`. Initial run **29385108870** exposed one macOS arm64-only build seam: the bundled
  zlib header suppresses its `Byte` typedef whenever modern AppleClang defines the legacy `TARGET_OS_MAC` marker, although
  modern Darwin no longer makes that classic-Mac typedef visible. The focused correction keeps the classic compiler
  exception while making zlib self-contained on Apple Mach targets. All ten bundled C translation units compile under simulated Darwin macros;
  replacement run **29385256836 passed all nine jobs**. Gemini then found that one failed raw byte decode still wrote a
  synthetic zero into the first unread output position; the reader now publishes only successfully decoded bytes, and
  the complete **48/48** GCC, Clang, ASan+UBSan, and TSan suites pass again. Codex found no major issue at the original or
  review-fix heads, both Gemini threads were resolved, and final run **29385881598 passed all nine jobs**.
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
- The M5 arena/adapter seam queued at this historical checkpoint has since merged in PR #33, the generic Disk32
  XAsset-envelope prerequisite merged in PR #34, and the pure four-byte script-string walk merged in PR #35.
  Keep `docs/task.md` synchronized before every PR.
  Retain the legacy x86 loader/writer and native64 save guard until their parity fixtures and transactional replacements
  are complete.
- Completed M5 portable-reader slice: `BodyState` now lives in a lightweight physics leaf instead of importing D3D9 through
  `phys_local.h`; the exact `0x70` `BodyStateDisk32` decoder canonicalizes legacy low-byte `underwater` and archive time;
  Ready-only semantic enumeration emits at most 512 immutable physics descriptors; and the outer heap workspace reads the
  fixed system, buffers, nonzero legacy address, and exactly the semantic body count through report-free raw/zlib
  `MemoryFile` calls. Exact lease validation brackets input and finalization, same-workspace callback reentry fails `Busy`,
  all views remain gated until the final `Ready` mutation, and failure requires fresh/repositioned input rather than cursor
  rollback. This was the non-production prerequisite; commits `8d94e7c5` and `e1174d33` now provide the independent
  candidate and production restore switch.
- The BodyState slice closes the confirmed legacy information leak without changing its wire version: `Phys_ObjSave`
  zero-initializes the full state and `Phys_GetStateFromBody` assigns the complete `underwater` integer. The reader still
  accepts dirty legacy upper bytes but publishes only canonical 0/1 values, so the three formerly indeterminate bytes can no
  longer disclose stack contents or make new saves nondeterministic.
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
  the Windows engine build. The effect-table checkpoint removes the remaining 8,196-byte x86 definition-table
  frame through a no-leak BSS lease that survives registration longjmp cleanup. The archive-gate and portable ODE
  occupancy follow-ons are also complete; the remaining sequence begins with measured per-function stack gates.
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
- Progress estimate: approximately **70% complete by engineering effort**. Windows x86 is about **93%**, shared
  foundations/security about **84%**, Windows amd64 about **58%**, Linux amd64 about **48%**, Windows/Linux ARM64 about
  **39%**, and macOS arm64 about **30%**. None of the five requested 64-bit/non-Windows engine targets builds end to end
  yet, so strict target delivery remains **0/5**.
- Initial upstream integration: merged PR #1 at `2b759db`, incorporating upstream `master` through `8a0f14f`
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
  signed-size/capacity overflow transactionally, and verifies planner/writer parity. The first archive Disk32 primitives
  now provide explicit definition-key policy, exact effect/system/buffer mirrors, checked owner-handle stride conversion,
  and bounded raw-slot free-list reconstruction. Disk32 work remains for heap-backed native buffer construction, complete
  graph/semantic validation, production archive integration, and fast-file effect/impact-table reads;
  the loose-file missing-effect fallback is native-width-safe, and the merged physics sidecar is wired through live and
  legacy-x86 archive ownership paths. Freelist allocation/free
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
  validation scratch to checked heap lifetimes without weakening fail-stop ownership, and PR #16 supplies executable
  normal archive-gate control and production integration. The merged ODE occupancy follow-on adds
  exact fixed-pool competition plus intrinsically silent live creation/impact/rollback transactions. Coherent
  camera/scalar/visibility snapshot publication, the first pure native FX fast-file converters, the zone-owned
  adapter/arena primitives, the generic Disk32 XAsset envelope, and the pure four-byte script-string walker are merged;
  the current generation-keyed lifecycle primitive precedes production sidecar wiring, broader asset conversion, and
  writer replacement.
  The bounded save-side definition snapshot and portable stack/runtime gates are merged in PR #20, with
  authoritative production MSVC Debug/Release measurements at 2,756-byte save, 6,124-byte restore, and 2,064-byte
  maximum other frames after removal of the discovered 10,256-byte convenience wrapper.
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
| M2 pointer/security cleanup | In progress | Huffman/disk32 bounds tests, 47 pointer fixes, tripwire, remote-input hardening, exact published-list server-download authorization, bounded/failure-atomic referenced-file and SYSTEMINFO publication, loader/BSP boundaries, generated counts, exact alias/completed-holder provenance, all 50 direct references bounded, pre-publication material/sound/world/model/surface/physics/clipmap-brush/portal/path/FX graph and state validation, build-mode-specific asset admission, bounded runtime material/collision consumers, complete graphics-world AABB topology validation, bounded XSurface/XModel skin/skeleton/collision contracts, transactional FX pool/handle ownership validation, allocation-safe ODE body/user-data/model-collision construction, and a bounded transactional native-width physics pool allocator have landed; handle-relative no-follow/reparse-point file opening, production-path fuzz fixtures, and the load-object bounded cursor remain. |
| M3 platform services | In progress: thread, memory, and filesystem enumeration integrated | Portable contracts and target-owned source sets select tested native Win32/POSIX clock, sleep/yield, recursive/reader-write lock, opaque event/thread lifecycle, processor/priority policy, virtual-memory lifecycle, UTF-8 mkdir/cwd/executable paths, bounded directory enumeration, and a cooperative worker gate used by renderer workers. Linux/macOS engine/headless sets remain empty and engine-gated; handle-relative recursive deletion, POSIX crash freezing, process/console, and socket backends remain. |
| M4 runtime 64-bit ABI | First runtime families in progress | XAnim tree/table, DObj runtime/saved layouts, allocations, preview buffers, SP corpse pointers, the SP target table, EffectsCore effect/pool handle codecs, ODE user-geometry storage, and the generic physics pool allocator are native-width exact. MP `cpose_t::physObjId` and `BreakablePiece::physObjId` still store ODE pointers in `int32_t` and are a hard native64 blocker; XAnimParts/XAnimIndices, the script VM, most runtime structures, and asset payloads also remain 32-bit-layout-bound. |
| M5 disk32 widening loader | FX restore, conversion, zone primitives, generic asset envelopes, script-string walking/journaling, and zone lifecycle control in progress | `disk32::PointerToken`, strong FX archive-key/address types, exact archive effect/system/buffer/body mirrors, exhaustive handle remapping, checked native pool reconstruction/linking, definition-provenance resolution, semantic `Ready`, Ready-only physics enumeration, and transactional raw/zlib restore staging are merged with x86 whole-image evidence. PR #32 merged exact pointer-bearing fast-file effect/visual/trail/impact schemas, canonical native runtime definitions, and bounded two-pass effect/impact converters with frozen resolver transactions, retained-extent overlap checks, callback-free materialization, retail semantic validation, and bounded runtime visibility interpolation. Production restore uses the exact-lease-bound reader/candidate path; the restore-side native64 guard/raw parser are gone. PR #33 merged the zone-owned aligned native arena and guarded stateful zone adapter with exact workspace contracts, nested impact/effect transactions, canonical post-registration identities, and publish-after-materialize ordering. PR #34 merged the fixed 0x4/0x8/0x8/0x10 top-level Disk32 envelopes and bounded, failure-atomic eight-byte asset iterator with portable build admission. PR #35 merged the pure bounded four-byte script-string walker with checked extent/parity, full preflight, raw-token preservation, explicit shared-inline rejection, unaligned reads, mutation revalidation, and failure-atomic outputs. PR #36 merged generation-keyed external slot ownership, stale/ABA rejection, distinct load-abandon and live-unload recipes, exact Retry cursors, fail-closed poisoning, and terminal idempotency as `15469b3d`; post-merge master run **29531440687** passed all nine jobs. PR #37 merged the full-u32 per-acquisition journal, exact key binding, reversible claimed-vs-duplicate transfers, reverse outcome-specific rollback, reversible `CommitReady`, unconditional post-`Live` finalization, fixed caller storage, O(1) controller validation, and linear phase-boundary scans as `7a9bce34`; post-merge run **29542960583** passed all nine jobs. PR #38 merged the referenced-fast-file 0..31 range correction, canonical 33-physical/32-usable slot constants, failure-atomic native/IWD formatting, exact SYSTEMINFO serialization, remote metadata validation, exact bounded server-download authorization, and native-width server-file comparison as `a7c485fd`; post-merge run **29551990840** passed all nine jobs. The constructed whole-zone sidecar table, no-report script-string adapters, production callback routing, canonical alias/publication lifecycle tests, broader completed-object relocation, the writer, and the save-side guard remain. |
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

1. Publish the reviewed SP target-table candidate after hosted CI, automated review, and thread-aware cleanup are green.
   Preserve native pointer layout, strict validate-before-publish loading, stale-storage safety, producer/consumer domain
   symmetry, registered-material checks, retail x86 wire values, and measured Windows x86 contract coverage.
2. Implement strict transactional `CG_TargetsChanged` parsing with bounded typed client storage, then harden the
   vehicle/Javelin HUD consumers without restoring d592's pointer-cast `CG_GetTargetPos` loop. Reuse the shared target
   protocol so server and client accept the same material, offset, flag, and entity-number domains.
3. Fix the separately identified squared-distance-versus-unsquared grenade safe-radius comparison in its own focused
   correctness batch. Keep dynent save/load `ba3c79f3` deferred until its bounded transactional Disk32/native-sidecar
   design exists; do not fold either change into the target/HUD publication.
4. Build the constructed whole-zone ownership table and no-report script-string adapter layer around the external
   lifecycle slot and completed journal. The adapter must acquire one ordinary reference per occurrence, report the exact
   claimed-versus-duplicate database-user transfer outcome, remove ordinary references, and remove only the targeted
   database-user reference without `Com_Error` or nonlocal exit. Validate current runtime IDs as `0 < id < 65536` before
   table access and keep the full-u32 journal representation for future widening. Acquire one global transaction
   serializer before journal initialization and hold it continuously through terminal commit or rollback return. It must
   exclude every other journal transaction, every raw database-user add/transfer/remove/publication, and the global
   database-user 4 -> 8 sweep; overlapping journals remain forbidden without shared claim accounting.
5. Bind that table to the exact abandonment/unload recipes through centralized callbacks. Static controller slots and
   callback metadata must live outside and outlast zone PMem so the controller can survive `PMem_Free` and publish
   `Empty`; only per-generation arena/workspace/journal/backing belongs inside the existing named PMem scope. Treat the
   first fixed arena budget as a checked compatibility cap, remove partial assets before unbinding, release every staged
   ordinary string reference on failure, and enforce `PMem_EndAlloc` before abandonment `PMem_Free`. This remains a
   prerequisite for a future checked error-unwind boundary; current DB-thread longjmp is process-fatal and is not made
   recoverable by these primitives. Keep admission closed through all fallible commit work, prepare the journal, publish
   `Live` under the external serializer, invoke the unconditional journal finalizer, then use a no-fail/no-drop
   gate/signal release before dropping that serializer; introduce an admission-pending state first if production cannot
   satisfy that contract.
6. Wire the guarded adapter into the native production FX/impact route behind the explicit legacy-x86 boundary. Preserve
   retail bytes and the writer; use full-width `DB_ResolveInsertedPointer`, publish `-2` roots through
   `DB_SetInsertedPointer` with the canonical `DB_AddXAsset` identity, and add nested-impact, alias, high-address,
   failure-after-publication, unload-order, slot-generation-reuse, and rollback tests before widening another XAsset family.
7. Replace the 114 XAnim/XModel `Buf_Read<T>` and adjacent raw/string reads with a transactional
   `current/end` cursor plus count, bone, weight, triangle, and string bounds.
8. Keep the licensed-content smoke deferred and do not dispatch it while its required self-hosted runner
   and `KISAKCOD_GAME_DIR` secret are absent. Implement the designed handle-relative recursive deletion
   service without symlink/reparse traversal instead; surface the smoke infrastructure blocker if asked.
9. Extract standard-stream console services, then process/event services and Linux signal-park plus
   macOS Mach crash freezing behind the already isolated terminal API.
10. Widen/tokenize the remaining MP physics pointer fields, continue M1/M5 ABI cleanup, and add production fast-file
   fixtures/fuzzing before enabling any native64 engine target.

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
  integration. PR #17 completes competing non-FX occupancy and the silent production ODE mutation boundary.
  The effect-table restore lease and bounded save table close the parsing/registration and database-enumeration stack
  boundaries. PR #21 merged copied-image validation, staged-definition membership, and coherent camera/scalar/visibility
  publication. The Disk32 sequence now includes address-independent native64 definition keys, fixed effect-record and
  owner-handle conversion, full system/buffer/body conversion, transactional reading, independent mutable candidate
  materialization, and production restore integration. Exact CI/review/merge is the current FX gate; the following M5
  blocker is the next runtime Disk32/fast-file widening seam. The corrected production MSVC stack report remains calibrated
  and enforced in Debug and Release.
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

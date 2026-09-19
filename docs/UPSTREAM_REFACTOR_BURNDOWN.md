# Upstream refactor burndown

Audit date: 2026-09-18 (America/New_York). Fork base `7d51f020cffb1580d599921d1ea6cf872ae1a571`;
upstream endpoint `b3199b90f416c62265eef87e643f59dadbc29838`. Recheck both before each batch.

## Active goal: upstream refactors only (2026-09-19)

The user clarified that the continuing goal is **the deferred upstream refactor
work only**, not every deferred fix or feature in the historical inventory.
The `refactor_goal_scope` field is the active filter. It selects **56 explicit
refactor commit rows, 11 structural/mixed rows for hunk-level audit, and one
vehicle correctness dependency** (`aa2ed669`): 68 candidate records to discharge.
This is not a claim that there are 68 independent refactors. The other **17
records remain historically deferred but are outside this goal**.

For mixed commits, only surviving refactoring hunks count. Examples include
profile-specific dvar organization, database declaration relocation, final client
accessors and typed decompiler cleanup. Do not import unrelated gameplay fixes,
add a new audio backend, change matrix precision, add SIMD, or import Radiant to
close this refactor goal. Backend-only refactors whose prerequisite feature the
fork does not carry need explicit inapplicability evidence, not implementation
of that feature. A later fix needed to avoid recreating a broken intermediate
vehicle refactor remains part of the final endpoint. Raise and defer decisions
that cannot be resolved from the final upstream code and existing fork contract.

R1 has landed in [PR #163](https://github.com/jm2/kisakcod/pull/163), merge
`53a187db`; all required platform/engine checks passed. CI/review monitoring for
this effort polls at most once per hour, with protected auto-merge used as the
completion trigger. The work remains isolated from active Gas City worktrees.
The broader workstream table below is the historical audit map; this scope
filter takes precedence over its unrelated fix/feature rows.

## Scope and accounting

The six historical ledgers contain **85 unique commits with explicitly deferred
content: 44 before the latest reconciliation, plus 41 in B3199B90**. These are
historical commit rows, not 85 independent refactors. They include fixes,
partially imported commits, an icon and an editor import. The machine-readable
[inventory](UPSTREAM_REFACTOR_BURNDOWN.json) preserves every exact commit,
original disposition and remaining boundary, assigning them to **21 workstreams**.
Workstreams are dependency groups, not promises of one PR each.

Start with R1, then R2 and R3. Work against the final upstream source and import
only the useful remaining changes into the current fork. Do not replay an old
commit merely because Git reports its ancestry: past checkpoints intentionally
kept some upstream content out. Never replace an entire divergent file with its
upstream version. The existing portable/native-width ownership and serialized
Disk32 boundaries remain authoritative where upstream still relies on x86 layout.

No historical row is closed merely because a later commit touches the same file.
Each completed row needs a hunk disposition: imported/adapted, already present,
replaced by a later implementation, or intentionally excluded with a reason.
Mixed rows stay open until **all remaining runtime hunks** are accounted for.
`outside-runtime-scope` is an explicit exclusion, not completed engineering work.
The old ledgers remain immutable historical records; update this inventory as
batches land. The count excludes rejected-only rows. One additional partial row,
`6a85d702` (UI/compass/movement), says “not imported” without explicitly saying
“deferred”; inspect its remaining hunks alongside I1/L1 without pretending it was
part of the original 44. Dvar refactor `32598a6a` was imported through PR #121 and
must not be redone.

## Endpoint consolidation and verified exceptions

| Chain / area | Final action and evidence |
|---|---|
| `9d1ab0d5` → `3d40b791` → `482bcd18` | One final technique migration. `3d40b791` is an empty commit (identical tree to its parent), so there is no second intermediate implementation. The last commit moves `MaterialTechniqueType` from `r_rendercmds.h` to `r_material.h`; do not first install the old declaration location and move it again. Retain earlier consumers still present at the endpoint. Omit Radiant consumers absent from the fork. |
| `ea6c2ea1` → `e08d61d9` | One team migration using the final declarations and array extents. The later commit adds remaining consumers; it does **not** make all earlier replacements redundant. Keep the fork's typed scoreboard insertion. |
| `67812f6a` → `dfc84fa0` → `ad325cdf`, then `b3199b90` | Migrate final key/catcher names once, along with surviving typed-accessor changes. Audit autocomplete capacity/cursor behavior separately; a bounded copy alone is insufficient. |
| `4c9bb150` → `5f77d720` → `aa2ed669`, then vehicle names and `9cfde1ae` | Port the final vehicle functions directly. Skip the intermediate WIP implementation and its bad pointer arithmetic. The final corruption fix explicitly repairs `CG_Vehicle_PreControllers` and a missing clamp assignment. Keep the fork's tested tank-tread sentinel and wrap handling. |
| `c182ac29`, `6501b04f` → `3f570489` → `ae9d584d` → `a40f1568` → `43f95fd0`, then `b5feb298` | Review one final audio design with the current audio owner. Do not resurrect the superseded backend splits or copy old vendor trees. The current fork still has no portable backend; this is **not** already completed. See `AUDIO_VOICE_CINEMATIC_PARITY_GATES.md` §3. |
| `f1bab466` → `bdc170ed` | Introduce final configstring declarations once, retaining distinct SP/MP capacities and golden MP slot/byte fixtures. |
| `9cfde1ae`: MP tag allocation | Current `G_EntLinkToInternal` in `game_mp/g_utils_mp.cpp` already allocates `sizeof(tagInfo_s)`, initializes typed members. Preserve it; do not replay the older fixed-size-to-typed rewrite over it. Other hunks in this commit remain open. |
| `9cfde1ae`: generic integer field | `Scr_SetGenericField` in `game_mp/g_spawn_mp.cpp` already writes an integer-sized field. Preserve the fork's narrowed write; audit entity-pointer, dvar-string and size consumers separately. |
| `07eb572a`, `66b37ca5`, `355b68a0` | The fork has a checked native surface stream, owned skinning inputs and separately sealed SP/MP physics masks. Reuse final names where compatible; do not replace those representations or collapse the profile masks. SIMD remains an optional measured optimization, not a prerequisite for naming alignment. |
| `601ddcc4`, `35e8c797`, `7b8980bc` | Still real work: database declarations remain in the shared header; media copying still uses `copy_directory`; SP `Com_SyncThreads` still lacks the upstream wait. Comments about deferral and prior ancestry are not proof of completion. |

## Order, dependencies and exit evidence

Each row below may be split into small reviewable PRs. Prefer one value family
or production behavior per PR; separate naming from observable behavior changes.

| ID | Workstream | Prerequisites / exit evidence |
|---|---|---|
| R1 | Aspect, light, depth and stencil names (4 commits) | First batch. Exact integer values/widths, production aspect/stencil/light fixtures; existing renderer gate and hosted MP/SP compilation. No structure member types/layouts change. |
| R2 | Image formats, flags, semantics, categories and tracking (5) | After R1, start formats/flags and decoder coverage, then semantics/categories. Tracking consumers are a separate slice that waits for overlapping owners. Frozen disk tags/masks and allocation tests; preserve bounded mip arithmetic. |
| R3 | Shader constants/arguments and final techniques (5) | After R1; can proceed while R2 tracking consumers wait. Final enum location, every shader slot frozen, material loader/dispatch tests and hosted renderer builds. Coordinate the database-facing hunks with D1. |
| R4 | Surface dispatch names (1) | After R3. Map endpoint names onto the fork's native stream; retain checked multiplication/reservation and production source contracts. |
| D1 | Database declaration relocation, zone flags, XAsset tags (3) | Audit actual cross-TU references before moving declarations; preserve public facade, callbacks, zone ownership and Disk32 type validation. Existing registry/lifecycle tests plus full engine link. Split relocation from values if it makes review clearer. |
| D2 | Field types and loader tables (1) | After D1/S1. Migrate final names onto bounded native/Disk32 loaders; field-width, table and parser tests before table churn. |
| G1 | Gameplay value families (19) | Start independent enum contracts while N1 is owned elsewhere. Order: shared entity/weapon/state definitions → animation/conditions → team/hints/objectives → masks/physics. Exact SP/MP values, wire/save byte fixtures where serialized, focused production behavior tests. Keep the helicopter water-comparison change separate from mask naming. |
| N1 | Client state, stats, buttons, configstrings and message dummies (6) | Wait for current MP compatibility PRs, then rebase and reuse their production fixtures. Frozen command/state bits and profile-specific configstrings; no change to message validation, baselines or wire bytes. |
| S1 | Script value tags (1) | Preserve widened value cells, ownership and pointer types. Exact tags plus VM production tests. Needed before D2. |
| V1 | Vehicles and identifiers (8) | After relevant G1 names and L1 hunk partition. Final SP controller/gameplay code, occupancy/animation slots, typed pose access, clamps and save contracts. Hosted SP compile plus production controller fixtures; retail-dependent behavior remains explicit until measured. |
| I1 | Keys, catcher flags and client/UI accessors (4) | After N1 settles. Final accessors against live fork layouts; Win32 key coverage, headless guards and production autocomplete boundary tests. Route B3199 audio/snapshot hunks to A1/N1 instead of duplicating them. |
| W1 | Worker command names and SP wait (2) | Separate naming and wait behavior. Retain acquire/release publication; dispatch and shutdown tests. Compile SP and test wait ordering/reentrancy. |
| R5 | FX marks rewrite (1) | Re-derive existing FX production invariants against final functions; test allocation, mark lifetime and serialization. Confirm earlier ki-ya3t work is no longer active before touching its surface. |
| L2 | Dynent save/load (1) | Design explicit Disk32/native physics conversion and restore ownership first; roundtrip/truncation/failed-restore tests. Do not import raw native save images. |
| L3 | Matrix precision remainder (1) | Compare deterministic outputs against frozen baseline/retail evidence before changing precision. Previously fixed typed sorts and filesystem pointers are not backlog. |
| L1 | Mixed gameplay/SP/UI remainders (10) | Partition by function into G1/V1/I1/A1/N1/L2 or an isolated behavior fix. Preserve prior adaptations and rejected actor-aim/offset changes. Use hosted SP builds now; old “SP is unbuilt locally” notes are not a permanent blocker. A compile pass alone does not prove retail behavior. |
| A1 | Final audio backend and sound enums (7) | Follow existing ki-dkeb/PR #160 and its parity-gate document. Reuse current owner work; pin dependencies, validate ownership/lifecycle and platform behavior. New backend acceptance needs the document's production gates. |
| B1 | Build-number updates and media copy race (2) | After release/provenance PR #153. Keep CMake 3.16 compatibility; test repeated/concurrent copies and unchanged outputs, then regenerate byte-parity evidence. |
| R6 | SIMD/skinning evaluation (1) | After R4 and xmodel owner work. Preserve scalar baseline and ownership validation. Adopt only portable, measured, output-equivalent kernels; record obsolete x86-only hunks explicitly. |
| C1 | Console channel names (1) | Last broad mechanical sweep, after active feature work settles. Exact channel values and routing tests; touch only runtime files the fork carries. Avoid repeated conflicts across nearly every subsystem. |
| X1 | Icon and Radiant import (2) | Outside the runtime refactor burndown. Keep separate optional feature/provenance decisions; never import their dependencies as collateral enum churn. |

## Gas City coordination and execution

This audit uses an independent clone beneath `${TMPDIR:-/var/tmp}`, with separate
Git metadata and build directories. The active checkout and Gas City worktrees,
sessions, queues and bead ownership are unchanged. Read-only status confirmed
Gas City active. Do not reset/rebase another worker's branch or claim its bead.

At this audit, open PRs are #161 (netchan), #160 (audio/voice/cinematics), #159
(MSG fixtures), #157 (MP resolver), #153 (release provenance) and #140 (xmodel).
Beads ki-oh65 and ki-dkeb are in progress. Treat this as a dated snapshot and
refresh it before each batch. R1 deliberately avoids their production paths and
adds coverage to an existing registered test so it does not churn shared CI
selectors, inventories or dashboard totals. Before landing, compare merge
conflicts with each still-open PR against the same master baseline; do not
introduce a new conflict into a worker's work.

A path-level audit against fetched PR heads identifies these additional scheduling
constraints. R2 tracking touches `client_mp/cl_main_mp.cpp` (#157) and
`xanim/xmodel_load_obj.cpp` (#140); keep that broad consumer sweep separate from
image format/flag and decoder work. D1 zone-flag consumers also touch #157's
client file, so declaration/reference auditing can proceed before that consumer
slice. G1 serialized values overlap #159's `msg_mp.cpp` and
`sv_msg_write_mp.cpp`; reuse its fixtures after landing. R3 has no direct overlap
with those six PRs and need not wait for the image-tracking slice. A1's ownership
constraint is broader than direct file overlap because ki-dkeb defines its
production acceptance gates. C1 remains last because its broad consumer sweep
overlaps #159, #157 and #140. These are scheduling dependencies, not a claim that
all changes in the same file would conflict.

For every batch: refresh endpoints/owners, identify surviving final hunks, adapt
to the current fork, freeze ABI/wire values where applicable, run focused tests
then required checks, review the diff and publish a normal protected PR. Land
only after protected checks pass; update row status with the PR and evidence.
Never mark an oracle-dependent change done on the strength of a build alone.

## First batch: R1

PR #163 implemented four complete upstream refactor rows. At that checkpoint, the inventory had
**4 implemented, 79 planned runtime rows, and 2 exclusions**.
That PR has now landed. Historical deferral counts above are not rewritten;
the active refactor-only filter supersedes the original broad scope.

| Commit | Completion disposition |
|---|---|
| `37947908` | All aspect-ratio declaration, dvar default/name-array and switch changes adopted. Retain the fork's existing `q_shared.h` include. |
| `707f9b07` | All light-kind changes across ten production files adopted. Preserve `R_GetSceneDObjCount`, atomic cull-state reads, and checked surface handling. Existing `com_bsp.h` → `xanim.h` → `r_bsp.h` supplies the light enum; no additional headless renderer include debt is introduced. |
| `9b808d83` | All 15 draw and prepass selectors adopted; scene remains `0`, full range remains `-1`, viewmodel remains `2`. |
| `40bf0bd9` | Both final stencil enums and every logging-table/count/mask replacement adopted with unchanged numeric values and labels. |

Three introduced declarations match the pinned upstream endpoint exactly.
A temporary token comparison over all 17 changed production files found no
other code changes after normalizing the frozen enum substitutions, constant
expressions, integer nonzero comparisons and tested depth-selector equivalence.
It excludes the three new enum declarations and reviewed include dependencies;
it is an audit aid, not proof of full engine or GPU equivalence.

The existing `renderer-value-encoding-contracts` gate now compiles production
aspect selection, light ordering and stencil-table bodies, plus the actual RHS
of every changed depth selector. Declarations and bodies are regenerated from
production sources at configure time. Exact underlying enum widths/values are
asserted. Renderer and OS services are doubles; no retail/GPU parity is claimed.
No registered tests, workflow selectors, inventory entries or dashboard totals
are added. Hosted Windows MP/dedicated/SP compilation remains required.

Local validation on the final production tree:

- GCC Release: **239/239** portable CTest entries passed.
- Clang ASan + UBSan: renderer production contracts passed with leak detection
  and halt-on-error enabled.
- CI checker regressions: **19** selection, **36** aggregate and **141** dashboard
  cases passed. Portable discovery matches the unchanged inventory exactly;
  the generated dashboard is current.
- All six merge simulations returned valid Git trees and introduced no new
  conflicts. PR #153 already conflicts in `docs/CAPABILITY_DASHBOARD.md`; this
  branch leaves that pre-existing conflict unchanged.

[PR #163](https://github.com/jm2/kisakcod/pull/163) carries the protected hosted
checks and landing evidence. The next implementation slice is R2 formats/flags;
refresh upstream/master, fork master and active Gas City owners before starting
it. R3 can follow independently while R2's tracking consumers wait.

## Second batch: R2 image values

Commits `78a1a09a`, `2f320b36`, `057bdb52`, and `84ef10ab` are implemented
by this batch. Tracking commit `5f30b9a7` remains open and separate. The JSON
records the surviving/adapted/inapplicable hunk disposition for each row.

The format enum exactly retains upstream values 0–15. The file header still
stores format and flags as bytes in its 28-byte disk layout; image semantic and
category fields remain bytes. All 17 changed production files compare equal
after frozen enum substitutions and constant flag unions, excluding only the
new declaration and reviewed includes. Stronger fork sky/normal-map validation,
checked dimensions, typed callback signatures and recovery error reporting are
preserved. Absent Radiant/case-texture functions are not introduced.

The existing renderer value gate compiles current production declarations and
bodies to check dispatch over all byte-valued format IDs, bitmap output and
pixel count, flag combinations, picmip selection/clamping, and category release
and recovery decisions. The D3D/engine services are doubles; this is naming and
behavior regression coverage, not a new decoder or full retail/GPU parity claim.
The unsupported DXN format remains unsupported. Source registrations, CTest
names, CI selectors and dashboard totals remain unchanged.

GCC Release passes **244/244** portable tests; focused Clang ASan+UBSan
contracts pass with leak detection and halt-on-error. The inventory matches
portable discovery exactly and the dashboard is current. Protected hosted
checks are recorded on the batch PR. An hourly monitor
tracks PR completion/review state without changing CI jobs or Gas City state.

## Third batch: R3 shader and material values

Consolidates `17d0a98b`, `0bced932`, `9d1ab0d5` and `482bcd18` against the
final upstream endpoint. `3d40b791` has no changes: tree
`6b95a2384a7ea03d64b389788cad4bdcb98eca80` is identical to its sole parent,
`9d1ab0d5455b5560793e0d80100c7ec0dacde737`. This closes that historical record
without replaying nonexistent work. The four code-bearing records remain
**implemented pending protected merge**, separate from that empty-commit result.

`MaterialTechniqueType` moves directly to its final `r_material.h` home with
unchanged values and increment operators. Shader slots, argument tags, technique
indices and array counts use the upstream names. The two arithmetic shader-kind
selectors become equivalent named ternaries. The monotonically increasing lit
technique loop keeps its range `[7, 14)` with upstream's `<` spelling.

Fork adaptations preserve bounded named-constant lookup and error paths, checked
database loads, vertex-format/pass-count remap validation, and the already
complete `sizeof(stateBitsEntry)` copy in `Material_CreateLayered`. The fork's
runtime technique-enable table is unchanged. Upstream-only Radiant consumers,
editor comments and shadow-caster helper are not introduced.

Validation extends the existing renderer gate, with no new CTest registration:

- 151 frozen numeric assertions cover every code-constant, argument and technique
  declaration; argument types remain 16-bit and material technique arrays remain
  34 entries. Current production declarations are compiled by the fixture.
- Production game-time writes preserve slot 18 and leave all other slots intact;
  all 34 quoted technique names retain their indices and unknown names return 34.
- Production pixel-literal collection and registration preserve argument-group
  boundaries, named/literal tags, sorted destinations, missing-name failures and
  null-table handling. Both changed shader-kind selector expressions are compiled
  directly from the source. Engine services use test doubles; this is not a GPU
  execution or retail asset parity claim.
- All **244/244 portable Release tests pass**. The focused renderer gate also
  passes Clang ASan/UBSan with leak detection and halt-on-error enabled.
- A token audit covers all 17 changed production files after frozen enum value
  substitution. Reviewed equivalent boolean/selector/loop spellings, the identical
  declaration relocation and one diagnostic string are normalized separately.
  No record widths or ownership implementations change.

Integration audit against master `7014af15` (which now includes PR #159) found
no new conflicts with PRs #164, #157, #153 or #140. Those branches already have
unrelated baseline conflicts; this batch does not change their worktrees.

The branch is stacked on R2 while its protected merge is pending. Hosted CI only
runs for PRs targeting `master`, so publish R3 as a **draft targeting master**
with auto-merge off. Its initial diff includes R2; after R2 lands, the effective
diff becomes this R3 slice. Mark it ready and enable protected auto-merge only
then. Never merge the child into the R2 work branch. CI/review observations
remain hourly.

## Fourth slice: R4 surface values

`07eb572a` is included with R3 in PR #166 to share the hosted renderer/engine
validation. Static-model bucket arithmetic and draw-method loop bounds adopt the
final surface names. Scene dispatch already uses the correct rigid-skinned and
skinned names; its two rigid aliases now use `SF_XMODEL_RIGID`. The fork's checked
native surface cursors, offsets, record strides, ownership and validation remain
unchanged. The old upstream raw-pointer parsing is obsolete here.

All three changed production files are token-equivalent after frozen enum
substitution and folding `SF_END_STATICMODEL - SF_BEGIN_STATICMODEL` to four.
The existing renderer gate freezes all 21 surface enumerators and compiles the
actual `R_ForceLitTechType`, checking all 13-by-7 destinations and preserving
adjacent fields for every technique value. Renderer value and native-stream
contracts pass in Release and Clang ASan/UBSan; **244/244 portable Release tests
pass**. No tests are newly registered.

## Verified no-replay records

- **Worker names `336a5da5`: resolved by the fork rewrite.** Commit
  `97727069a627cf9d798f6a60217c19aac0fa30ae` replaces all 17 raw queue buffer
  assignments with `R_BindWorkerCmdBuffer<WRKCMD_...>`, deriving native sizes from
  typed payloads. Both FX pending predicates and the cached-static-model warning
  exception already use the final names. The old callback array and separate
  `R_InitWorkerCmdsPos` are gone. Completion scans retain bounded fixed-width
  indices with `WRKCMD_COUNT` and atomic outstanding-count ownership. Replaying
  upstream's raw sizes or `inSize` polling would regress the fork. The existing
  queue protocol/lifecycle tests and source contracts cover this implementation;
  source checks require exactly 17 typed bindings and prohibit raw buffer sizes.
  The unrelated SP wait fix remains outside the refactor-only goal.
- **Audio macro rename `ae9d584d`: inapplicable to this fork.** For each of its
  five changed files (`r_cinematic.cpp`, `snd_driver.cpp`,
  `snd_driver_load_obj.cpp`, `snd_local.h`, `snd_public.h`), replacing every
  `KISAK_SOUND` in the parent with `KISAK_OPENAL` produces byte-identical commit
  content. Neither macro appears in the fork's CMake/scripts/sound/cinematic
  sources; its OpenAL implementation files are absent. The prerequisite new
  backend feature `6501b04f` is outside this goal. No backend is imported to make
  a macro rename applicable. The other sound cleanup/relocation/name records
  remain open and require their own hunk audits.

## Script value slice: S1

`a97810c6` is implemented on `integration/refactor-s1-script-value-names`,
pending protected integration after the renderer branch. It changes only tag
and mask spellings in the eight upstream runtime/UI files. Fork-only native
pointer fields, union payloads, bounds checks and iterative traversal remain.

The upstream save/load monolith no longer exists in the fork. Its remaining
names are applied in `Scr_LoadEntryValueCell`, `Scr_LoadEntryRuntimeCell` and
`DoSaveEntryValuePayload`. `DoSaveEntryRuntimePayload`,
`DoSaveEntryWithoutStack`, `DoSaveEntryInternal`, `SaveStackIterator` and
`AddSaveEntryInternal` already contain their final named branches. The surviving
`AddSaveEntry` and typed `Scr_SaveShutdown` walk get the equivalent tag names.
Compiler, evaluator and VM conflicts retain typed code-position/vector/stack
access; only the condition labels change. Reference iteration retains native
record strides instead of upstream's old raw pointer offsets.

All eight production files pass token equivalence after enum/mask substitution,
including the fixed-width `~VAR_MASK` and the `[VAR_BEGIN_REF, VAR_END_REF)` range.
The existing value-split fixture now also freezes the **production** `Vartype_t`
values 0..25, aliases, masks and 32-bit enum width alongside its existing disk
encoding checks. No serialized bytes, runtime record layouts or test inventory
entries change. **244/244 portable Release tests pass**; seven focused Clang
ASan/UBSan suites pass: value split, native layout, nested read/write stack,
runtime pointers, save registration, animation fixups and debugger pointers.

## Gameplay slice G1a: perks and combat values

`90b7a52a`, `2a0747b7` and `8b23c310` are implemented on
`integration/refactor-g1-combat-names`, pending protected integration after S1.
The final perk declaration lives in the MP section of `bg_public.h`; the shared
damage flags live in `game_public.h`; the unchanged MP cause-of-death enum moves
before `modNames`. Consumers use the names without changing values, types,
serialized fields, table ordering or gameplay decisions.

The SP and MP fall-damage hunks are adapted to the current fork calculation and
stat access, applying only the flag/death labels. This does not pull in the
separate N1 stat refactor. Three unrelated trailing-newline hunks are omitted.
All other upstream runtime hunks are accounted for in the adopted substitutions.

Validation:

- All 23 production files are token-equivalent after frozen enum substitution,
  constant flag unions and the equivalent explicit `iMOD != MOD_UNKNOWN` test.
  New declarations, the replaced count macro and the identical moved MP enum are
  checked separately.
- The existing weapon-input gate compiles production declarations, perk-name
  lookup, bullet-impact classification and vehicle immunity dispatch. It freezes
  all perk/damage/death values for both profiles, all 20 perk names (including
  case-insensitive/null/unknown lookup), and 32,768 vehicle capability/flag/weapon/
  cause-of-death combinations. Engine services and small entity views are doubles;
  this is a dispatch/numeric contract, not retail gameplay acceptance.
- **244/244 portable Release tests pass**; the focused gameplay gate passes Clang
  ASan/UBSan. CTest registrations and inventories are unchanged. Hosted MP/SP
  compilation remains required before merge.

## Gameplay slice G1b: weapon states and animation values

`0f139a7f` and `5c27f2af` are implemented on
`integration/refactor-g1-weapon-names`, pending protected integration after G1a.
All 16 production files retain their numeric behavior and native layouts. The
shared animation declarations replace the old local enum at their final upstream
location. The fork's grenade input bit and simplified per-frame firing predicate
are retained. Two originally unsigned weapon-state comparisons keep explicit
`uint32_t` conversions, preserving the behavior of invalid negative states.

The animation array already held 33 entries. The new `NUM_WEAP_ANIMS` names that
storage extent, while the rate helper deliberately retains its exclusive bound
of 32 using `WEAP_ANIM_ADS_DOWN`, exactly as upstream does. The boolean ADS selector
retains its existing mapping to slots 31 and 32. Viewmodel range aliases remain
unchanged.

Validation extends the existing weapon-input gate without new CTest entries:

- 93 frozen assertions cover every production weapon-state, animation-file and
  animation-command value; all three enum widths remain 32 bits.
- The production prone-interruption body covers all 27 states plus negative,
  minimum/maximum integer and out-of-range inputs. The full production animation
  dispatcher covers all 30 commands, both toggle-bit states and empty/nonempty
  clips, including previous-animation bookkeeping and both ADS mappings.
- The production animation-slot declaration and rate-offset table retain 33
  entries. The actual rate helper accepts index 31 and asserts at index 32.
  Fixture engine services are doubles; these are numeric/dispatch contracts.
- All 16 production files pass token equivalence after reviewed enum, unsigned
  comparison and boolean-selector substitutions. **244/244 portable Release
  tests pass**, and the focused gameplay gate passes Clang ASan/UBSan. Hosted
  MP/SP engine compilation remains required before merge.

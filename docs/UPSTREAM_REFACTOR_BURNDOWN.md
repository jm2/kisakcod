# Upstream refactor burndown

Audit date: 2026-09-18 (America/New_York). Fork base `7d51f020cffb1580d599921d1ea6cf872ae1a571`;
upstream endpoint `b3199b90f416c62265eef87e643f59dadbc29838`. Recheck both before each batch.

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
| `9d1ab0d5` → `3d40b791` → `482bcd18` | One final technique migration. The last commit moves `MaterialTechniqueType` from `r_rendercmds.h` to `r_material.h`; do not first install the old declaration location and move it again. Retain earlier consumers still present at the endpoint. Omit Radiant consumers absent from the fork. |
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

This change implements four complete upstream refactor rows. The inventory has
**4 implemented in this change, 79 planned runtime rows, and 2 exclusions**.
“Implemented in this change” describes this branch; delivery still requires its
protected PR to land. Historical deferral counts above are not rewritten.

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

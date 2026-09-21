# Upstream reconciliation through `b3199b90`

This reconciliation covers all 46 upstream commits after `32598a6a`, using
fork base `f9abab09f006b189425f9c2b796cd189488c3d67` and exact upstream tip
`b3199b90f416c62265eef87e643f59dadbc29838`. It follows the conservative
approach documented in [the previous ledger](UPSTREAM_5C27F2AF_LEDGER.md).

This is a curated content integration plus an upstream ancestry checkpoint;
it does **not** import every upstream hunk. The raw merge changed 318 paths,
conflicted in 113 files, and attempted to restore previously excluded Radiant
and OpenAL files. Earlier deferred enum/header migrations are absent from
this fork, so an automatic textual merge would not establish correctness.

The content change is limited to five production files: particle axes,
animation offset interpolation, weapon-raise field access, console message
windows, and scoreboard insertion. All other runtime changes remain explicit
in the dispositions below. Deferred changes remain available by their exact
upstream commits; the checkpoint does not mean those features are delivered.

## Commit dispositions

| # | Commit | Disposition | Retained boundary / follow-up |
|---:|---|---|---|
| 1 | `27f4296a` fix: validate last field value before use in MSG_ReadDeltaPlayerstate (#113) | Superseded; fork retained | MSG_ReadLastChangedField already rejects both negative and excessive values, sets overflowed, and returns a bounded count for every caller. Retain that shared signed guard instead of the upstream assertion plus playerstate-only unsigned check. |
| 2 | `9f03704d` particle fix - ty nikolai | Adapted | Correct the particle axis sign to form a perpendicular basis. Retain the original projection threshold and radius policy; executable tests cover mixed signs, handedness, normalization, and the degenerate fallback. |
| 3 | `e87739e3` refactor: name server client states (#114) | Deferred | Server connection-state names cross handshake and snapshot paths; retain current client-state values pending protocol-specific parity review. |
| 4 | `707f9b07` refactor: name gfx light types (#115) | Deferred | Renderer light-kind naming needs frozen asset values and renderer-profile compilation. |
| 5 | `8c033d08` refactor: name console channels (#116) | Deferred | Console-channel renaming spans nearly every subsystem and includes previously excluded editor/audio files; no behavioral fix requires this migration. |
| 6 | `cc8c8dd5` refactor: name vehicle types (#117) | Deferred | Vehicle type names cross SP/MP asset and gameplay records; require exact-value and profile checks. |
| 7 | `3082d41f` refactor: name vehicle ride slots (#118) | Deferred | Vehicle ride-slot identifiers need SP animation/occupant parity coverage. |
| 8 | `07eb572a` refactor: name render surface types (#119) | Deferred | Surface-type migration overlaps the fork's checked native surface stream; retain its existing representation. |
| 9 | `0492cb9e` refactor: name physics geometry types (#120) | Deferred | Physics geometry identifiers cross serialized/ODE boundaries; need explicit value and dispatch coverage. |
| 10 | `17d0a98b` refactor: name renderer code constants (#121) | Deferred | Renderer constant indices are shader ABI; require exact slot-value tests before broad replacement. |
| 11 | `a97810c6` refactor: name script variable types (#122) | Deferred | Script value tags overlap the fork's widened value cells and ownership contracts; preserve the tested runtime migration. |
| 12 | `ea6c2ea1` refactor: name team values (#123) | Deferred | Team constants span script, gameplay, and snapshots; require SP/MP numeric parity. |
| 13 | `336a5da5` refactor: name worker command types (#124) | Deferred | Worker dispatch identifiers overlap atomic/publication changes; require queue and dispatch contracts. |
| 14 | `0bced932` refactor: name material shader argument types (#125) | Deferred | Shader argument tags are asset-facing; require loader and runtime type parity. |
| 15 | `c5477bc3` refactor: name xasset types (#126) | Deferred | XAsset tags span the heavily modified Disk32 registry and lifecycle; require value, dispatch, and ownership validation. |
| 16 | `9b808d83` refactor: name depth range types (#127) | Deferred | Depth-range naming affects renderer state; defer alongside profile-specific renderer tests. |
| 17 | `9d1ab0d5` refactor: name material technique types (#128) | Deferred | Technique indices are shader/material ABI; require frozen-value and dispatch tests. |
| 18 | `3d40b791` refactor: name material technique types (#129) | Deferred | Further technique replacements depend on the preceding material migration; retain the current sealed asset boundary. |
| 19 | `40bf0bd9` refactor: name gfx stencil types (#130) | Deferred | Stencil identifiers need renderer state tests; no isolated behavior correction accompanies the renaming. |
| 20 | `78a1a09a` refactor: name gfx image file formats (#131) | Deferred | Image file-format tags are serialized; require exact numeric and decoder coverage. |
| 21 | `2f320b36` refactor: name image file flags (#132) | Deferred | Image flags cross disk/native loader boundaries; require frozen bitmask tests. |
| 22 | `057bdb52` refactor: name image texture semantics (#133) | Deferred | Texture semantics are material/image ABI; require loader and renderer parity. |
| 23 | `84ef10ab` refactor: name image categories (#134) | Deferred | Image category naming requires asset-value and allocation-policy tests. |
| 24 | `5f30b9a7` refactor: name image track values (#135) | Deferred | Image tracking names require allocation/diagnostic parity; no immediate behavior fix. |
| 25 | `f848c49c` refactor: name animation script conditions (#136) | Deferred | Animation condition identifiers cross script and weapon consumers; require exact index/value contracts. |
| 26 | `67812f6a` refactor: name key codes (#137) | Deferred | Key-code migration spans input and UI; require platform-specific input coverage. |
| 27 | `dfc84fa0` refactor: name remaining key codes (#138) | Deferred | Additional key codes depend on the preceding migration; retain existing input values. |
| 28 | `d39d2d89` refactor: name animation move types (#139) | Deferred | Animation move identifiers require SP/MP animation and serialization parity. |
| 29 | `478087d5` refactor: name cursor hint values (#140) | Deferred | Cursor hints are gameplay/network-facing; require exact-value and snapshot coverage. |
| 30 | `b5feb298` refactor: name sound enums (#141) | Deferred | Sound enum migration overlaps active portable audio/voice work; preserve its current source contracts. |
| 31 | `f51cc930` refactor: name player spread override states (#142) | Deferred | Spread-state naming is gameplay-visible; require weapon-state parity. |
| 32 | `a8690d3e` refactor: name player state values (#143) | Deferred | Player-state naming crosses prediction and snapshots; require frozen MP/SP values and wire tests. |
| 33 | `482bcd18` refactor: name material technique values (#144) | Deferred | Further material-technique replacements depend on earlier deferred shader ABI migrations. |
| 34 | `9cfde1ae` misc decomp artifacts and cleanup | Deferred; existing protections retained | The 33-file SP/MP decompiler cleanup mixes field widths, actor indexing/sorting, allocation sizes, script entity references, and dvar pointers. The fork already has a typed tagInfo allocation and a narrowed generic integer-field store, which must survive. Remaining fixes, including SP actor/console-command consumers, need separate production-body tests. No hunks from this commit are imported. |
| 35 | `e08d61d9` refactor: name team values (#145) | Deferred | Additional team replacements depend on the preceding team migration and commercial protocol parity. |
| 36 | `7d9fadb8` refactor: improve initialization of dummy variables for entity / client / playerstate (#148) | Deferred | Const and static-dummy changes touch message readers/writers and client baselines, overlapping active message compatibility work. Defer until production-path and wire fixtures validate both profiles. |
| 37 | `2dab364a` console decomp badness | Adapted and completed | Replace every con.color-relative MessageWindow calculation with the actual Console::messageBuffer members, including the two mini/error nudge accesses left unchanged upstream. Preserve the console layout, destination enum, and existing caller contracts. |
| 38 | `ab5b3ebb` fix more misc decomp badness | Selectively adapted; renderer superseded | PM_Weapon_FinishWeaponChange reads named WeaponDef timing and animation members instead of fixed int-array offsets. Preserve the existing MP condition indices. Retain the fork's checked surface multiplication, reservation, and native stream layout rather than upstream's unchecked renderer allocation arithmetic. |
| 39 | `9f126330` BG_LerpOffset cleanup | Adapted | Use a float interpolation error, so reciprocal square root receives squared distance rather than numerically converted float bits; retain the production vector operations and snap/no-change branches. |
| 40 | `f1bab466` refactor: name multiplayer configstring values (#146) | Deferred | MP configstring indices are protocol-visible; require the commercial compatibility oracle and exact slot contracts. |
| 41 | `bdc170ed` move MAX_CONFIGSTRINGS | Deferred | Moving MAX_CONFIGSTRINGS depends on the deferred MP configstring migration; preserve separate SP/MP limits. |
| 42 | `ad325cdf` refactor: name key catcher flags (#149) | Deferred | Key-catcher flag naming depends on input/UI migrations and needs exact mask tests. |
| 43 | `f1cef213` refactor: name weapon type checks (#150) | Deferred | Weapon-type naming spans gameplay and renderer consumers; require SP/MP value contracts. |
| 44 | `69f9e020` refactor: name stationary trajectory checks (#151) | Deferred | Stationary trajectory naming crosses prediction and network state; require frozen values. |
| 45 | `2e96b1e4` refactor: name objective state (#152) | Deferred | Objective-state identifiers are snapshot/script-facing; require exact numeric contracts. |
| 46 | `b3199b90` mo' fixes - mo' problems | Selectively adapted; remainder deferred | Use typed score entries for both directions of scoreboard insertion and the actual console activeLineCount member. Defer broad client/UI accessor churn, snapshot initialization and audio script changes. The proposed autocomplete copy still needs a separate trailing-space/cursor capacity audit; importing its bounded copy alone is insufficient. Existing native pose and snapshot protections remain unchanged. |

## Regression scope

MP and SP fixtures linked into the existing
`upstream-reconciliation-angle-math-contracts` gate compile the affected production
function bodies, extracted again whenever their source changes. Console and
score record declarations also come from production headers. Math/engine
services are doubles; the weapon fixture intentionally uses a different
native layout to catch a return to fixed offsets. These fixtures prove the
selected behaviors, not complete engine or commercial gameplay parity.

Both fixtures execute from the existing upstream reconciliation test, already
enrolled in portable CTest discovery, the required Windows x86 build/selection,
and the full portable sanitizer job. This retains existing test/CI registrations
and avoids creating dashboard-count conflicts for concurrent PRs. The production
engine itself remains subject to the existing Windows MP, dedicated, and SP
build gates. No licensed-content compatibility result is claimed here.

## Local validation

- GCC Release: **239/239** portable CTest entries passed.
- Clang ASan + UBSan: both new MP/SP production-function fixtures passed,
  with leak detection and halt-on-error enabled.
- Portable discovery exactly matches the unchanged inventory; both new fixtures
  execute in the test already selected by the Windows x86 manifest and workflow expression.
- Test-selection checker: **19** regression cases passed; CI aggregate
  checker: **36** cases passed; capability dashboard: **141** tests passed.
- The generated capability dashboard is current and `git diff --check` is clean.

Hosted platform/engine results are recorded on the integration PR. Local
portable results alone do not authorize bypassing any protected branch check.

## Isolation and history

Preparation and builds run in an independent clone under `${TMPDIR:-/var/tmp}`.
The active checkout, gascity worktrees, branch tips, index files, sessions,
beads, and queues are not reset, rebased, cleaned, or stopped. Existing fork
history is retained as the first-parent line; exact upstream history is
retained as a merge parent. Landing uses the protected pull-request path.

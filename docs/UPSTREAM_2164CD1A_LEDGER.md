# Upstream reconciliation ledger through `2164cd1a`

This ledger records the disposition of every commit in the pinned upstream
range `312a9d2e..2164cd1a`. It is a content audit; PR #66 supplies the separate
graph evidence by merge-committing the exact tree-neutral ancestry checkpoint.
The approved adaptations and checkpoint have now been merged.

Disposition terms:

- **Present/superseded**: this port already contains the behavior, normally
  behind a stronger portable or security boundary.
- **Adapted**: the useful behavior was merged through the reviewed PR #65
  reconciliation without importing unrelated upstream hunks.
- **Deferred**: the change needs a prerequisite, oracle, provenance decision,
  or production compile gate before it can be accepted safely.
- **Rejected**: the upstream form is unsafe, width-dependent, or weaker than
  the implementation retained here.

## Commit-by-commit disposition

| # | Upstream commit | Disposition | Evidence and retained boundary |
|---:|---|---|---|
| 1 | `3f256654` `(SP) Fix crash in Path_FindPathAwayNotCrossPlanes() - bad arg` | Present/superseded | PR #41 already carries the path-away correction through the port's typed and bounded path boundary. No upstream hunk remains to import. |
| 2 | `d592fb4a` `(SP) G_CanRadiusDamageFromPos fix wrong return values` | Selectively present; remainder deferred | PRs #40, #43, and #46 contain the reviewed radius/target corrections and stronger entity/weapon checks. Further `g_weapon` gameplay changes remain deferred until SP is compiled and a retail behavior oracle exists. |
| 3 | `526d59fb` `r_marks - fix wrong assert cond` | Present | PR #39 already contains the corrected assertion condition. |
| 4 | `9e6c5836` `(SP) Actor_CanSeeEntityEx - fix assert cond` | Present | PR #39 already contains the corrected assertion condition. |
| 5 | `d6b4c5e4` `(SP) Fix CG_BlurRadius` | Selectively present; remainder rejected | PR #39 contains the confirmed floating-point blur correction. Xbox-specific additions are outside this port's target/runtime contract and are not imported. |
| 6 | `d27803d2` `(SP) fix badplace nodesWritten init` | Present | PR #39 already initializes the bad-place write count correctly. |
| 7 | `77404c61` `More fixes. (#82)` | Selectively present; remainder deferred/rejected | Reviewed fixes already landed through PRs #41, #43, and #44. Other raw SP gameplay, UI, and API edits remain deferred; actor-aim behavior changes are rejected pending an executable retail oracle. |
| 8 | `b31ea047` `(SP) tank tread (vehicle) fix` | Superseded | PR #42 preserves the `-1` sentinel, reverse movement, and wrap behavior behind explicit tests, and is stronger than the upstream form. |
| 9 | `ba3c79f3` `(SP) dynent saveloadfix` | Deferred | Dynent save/load still needs a Disk32/native physics schema and lifetime design. Importing the raw fixed-width save image would deepen the native64 blocker. |
| 10 | `07daaf99` `Fix SP vs. MP playerState event differences` | Selectively adapted; remainder deferred | PR #65 corrects the stale brush-definition identity/access in `r_dpvs`. The broader SP event-sequence changes remain deferred until SP production translation units are compiled and tested. |
| 11 | `87b3e139` `fix aim assist entnum assert` | Adapted and hardened | PR #65 validates signed entity numbers before lookup, clamps weapon ranges, handles a null player state, and repairs the native64 aim-overlay call boundary. Runtime boundary and source-contract tests cover MP and SP forms. |
| 12 | `28757acc` `(SP) fix UpdateTurretScopeZoom` | Deferred | This is an unbuilt SP behavior change and needs the SP compile gate plus a gameplay oracle. |
| 13 | `141532d7` `tweak hud elems count` | Adapted and hardened | PR #65 derives MP 62/SP 256 capacities from real layouts, performs bounded atomic-prefix collection, and sorts typed pointers without the old 32-bit pointer comparator. Dual-profile runtime/layout and source tests cover the boundary. |
| 14 | `ea36ca33` `(SP) HUD fixes and 'dolphin dog' fixes.` | Selectively adapted; remainder deferred | PR #65 adapts the signed wrap semantics of `AngleSubtract` through a shared `AngleDelta` implementation with exact-boundary and NaN tests. Other SP HUD, pose, and gameplay changes remain deferred. |
| 15 | `29a94c8c` `epic` | Deferred | The icon asset change needs provenance/license confirmation and belongs in optional packaging, not an engine-content reconciliation batch. |
| 16 | `b19e686a` `Misc bug fixes (#83)` | Sound cleanup adapted; remainder deferred/rejected | PR #65 U2 commit `b40fd7bb` replaces seven scoreboard-derived dry operands with a sound-owned dry-level helper whose value is exactly `1.0f`, avoiding the false sound-to-cgame dependency. The existing wet-level ABI remains unchanged. Other SP behavior is deferred; actor-offset changes are rejected without an oracle. Loaded-but-unused dry/effect fields are a separate audio-backend question. |
| 17 | `6f0284ad` `Fix badly formed std::sort/qsorts.` | Selectively present | PR #63 contains the safe typed-sort, NaN-ordering, complete shadow-candidate, and row-zero fixes. Its filesystem change was superseded by the native-pointer service from `d5a6e799`; matrix precision rewrites remain deferred for numerical/determinism evidence. |
| 18 | `2164cd1a` `Various fixes (#84)` | Selectively adapted; remainder deferred/rejected | PR #65 fixes ordinary and server command lookup through one tested exact linked-list finder, including tail dispatch. SP prone/save work remains deferred. The raw screenshot implementation is rejected in favor of a future bounded, portable capture boundary. |

## Merged adaptation validation

PR #65 carried the U1 reconciliation sequence:

- `faf3917d`: dynent brush identity and shared angle corrections;
- `a401775a`: aim-assist validation and native-width overlay boundary;
- `fc265d13`: exact command-registry dispatch, including the tail;
- `35960465`: bounded MP/SP HUD collection and typed sorting.

At `35960465`, the focused upstream/HUD selection passes **7/7**, and the
complete portable GCC Debug suite passes **152/152**. Individual U1 batches
also pass strict GCC/Clang checks and genuine i386/AArch64 compilation; the HUD
batch additionally passes ASan+UBSan. An independent adversarial audit reports
PASS with no actionable finding at that exact U1 head.

`b40fd7bb` combines the U2 sound cleanup. The complete combined GCC Debug suite
passes **153/153**, and the combined focus passes **8/8**. U2 independently passes **146/146**, its ten-mutation source
contract, strict i386 GCC MP/SP and Clang MP compile-link, and a linked-symbol
audit showing `MSS_GetDryLevel`, the unchanged `MSS_GetWetLevel` ABI, and no
scoreboard dependency. PR #65 review hardening `db3ced51` preserves the original
staged angle arithmetic with explicit float conversions, selects
`std::floor(float)`, rejects null command lookup/node names before comparison,
and adds runtime plus mutation-sensitive regression coverage. Full GCC remains
**153/153**; the focused review selection passes **3/3**, and strict GCC/Clang
plus genuine i386/AArch64 compile-link pass for both touched contracts. These
adaptations reached final reviewed head
`3a9f0f01da82f0abbff59afb02093bddffd447d1`, which passed all nine jobs in run
**29703827041**. Exact-head Codex review was clean. All six Gemini threads were
resolved: five fixed or duplicate findings and one non-corrective `nullptr`
style suggestion. PR #65 squash-merged as
`d79069a41e0289f4ed53d174a89d8ee72f40b4a3`; authoritative master run
**29704069129** passed all nine jobs at that exact SHA, with only non-failing
Node 20 deprecation annotations.

## Merged ancestry-checkpoint evidence

The content PR was merged and `upstream/master` was fetched again. The dedicated
checkpoint completed the tree-neutral construction and merge steps:

1. The pinned object and then-current GitHub `upstream/master` were confirmed as
   exactly `2164cd1accf6607a05203547e50858211dcef094`. Newer upstream commits remain
   subject to a separate ledger and must never be included silently.
2. Dedicated checkpoint `12309db16d6514ac0df23293cd6074d7bbd15142`
   was created from the exact PR #65 master baseline with
   `git merge --no-ff -s ours 2164cd1a`.
3. Its first parent is exact
   `d79069a41e0289f4ed53d174a89d8ee72f40b4a3`, its second parent is exact
   `2164cd1accf6607a05203547e50858211dcef094`, and both it and its first
   parent have tree `f8a78964c7c89c3c3000f598cb4272782c40d70b`.
   The complete diff and file-level diff are empty, so no upstream content was
   imported.
4. PR #66 retained that intact inner merge. Its exact final head was
   `e209367c920df589162431a584d6fdf7bfc83c43`, and GitHub merge-committed it as
   `225759e7d8fd1327210452f3debcd6360465ef2a` with exact parents
   `d79069a41e0289f4ed53d174a89d8ee72f40b4a3` and
   `e209367c920df589162431a584d6fdf7bfc83c43`.
5. Authoritative master run **29707497302** passed all nine jobs. Post-merge graph
   verification confirmed that both `12309db16d6514ac0df23293cd6074d7bbd15142`
   and `2164cd1accf6607a05203547e50858211dcef094` are ancestors of `origin/master`,
   while the checkpoint remained tree-neutral and imported no deferred content.

The checkpoint records that every commit above was inspected. It does not turn
deferred or rejected upstream source into accepted behavior.

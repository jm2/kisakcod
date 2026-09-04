# Upstream reconciliation ledger through `c6be07a2`

This ledger records the disposition of every upstream commit after the
previously integrated `820b0a03` checkpoint. Useful behavior was adapted onto
the stronger portable tree rather than merged raw. Tree-neutral checkpoint
`e22fd6b7cd05afedec5129878a29d8f0ec953269` records exact ancestry through
`c6be07a225450bc38387c27373047ae75e2e8533` without importing deferred,
rejected, superseded, or already-equivalent hunks.

Disposition terms:

- **Adapted**: the useful behavior is implemented through a reviewed boundary
  on this branch.
- **Already equivalent**: this tree independently contains the same (usually
  stronger) protection; no source hunk was needed.
- **Deferred**: the change needs a dedicated design, dependency, oracle, or
  cross-platform validation batch before it can be accepted safely.
- **Rejected**: the upstream form is unsafe, unportable, outside the committed
  runtime scope, cosmetic churn, or weaker than the implementation retained
  here.

## Commit-by-commit disposition

| # | Upstream commit | Disposition | Evidence and retained boundary |
|---:|---|---|---|
| 1 | `fb1b7d3e` `fix marks not appearing on some types of SP objects` | Deferred | A roughly 2,900-line rewrite of `src/EffectsCore/fx_marks.cpp` plus `fx_system.cpp` churn. This tree's FX area is sealed by mutation-sensitive source contracts, and the `security-source-regressions` fx_system invariant count is already under separate repair (ki-ya3t); importing the rewrite here would make that attribution impossible. Needs a dedicated FX review batch with contract re-derivation. |
| 2 | `7b8980bc` `(SP) fix shutdown crash` | Deferred | Four KISAK_SP lines adding `SV_WaitServer()` to `Com_SyncThreads`. This tree's `Com_SyncThreads` differs structurally from upstream's, and no local configuration compiles the SP profile (the Linux host cannot build the windows-x86-sp preset); the fix must land through the hosted SP build loop. |
| 3 | `c55b8bc4` `remove radiant tests` | Rejected | Radiant editor tree is outside the committed MP client/server runtime scope, consistent with the `820b0a03` ledger's rejection of the editor import. |
| 4 | `d4a4b114` `radiant cleanup, asserts, outlined funcs` | Rejected | Editor-only cleanup; outside committed runtime scope. |
| 5 | `b1ae560b` `DEG2RAD cleanup` | Rejected | 38-file mechanical constant rename with no behavioral contract; high conflict cost against a diverged tree for zero behavior change. |
| 6 | `a95a1bc8` `warning cleanup 1` | Rejected | Symmetric ±-line churn (`r_image_wavelet.cpp` 710 lines, yacc tables) without a behavioral contract. Existing checked runtime boundaries retained. |
| 7 | `bd42475f` `unreferenced variable cleanup` | Rejected | 128-file mechanical sweep without behavioral contracts; direct precedent: rows 6-7 of the `820b0a03` ledger. |
| 8 | `cb3c8ded` `warning cleanup 2` | Rejected | Decompiler-style readability cleanup (named types, `bcassert` restatements) without a behavioral contract this tree lacks. |
| 9 | `d48eef69` `C4700 = error (Uninit'd vars used in logic)` | Rejected | Upstream MSVC warning-policy change plus radiant-only code fixes; this tree's platform CMake and warning policy are independently managed and hardened. |
| 10 | `f6e4ebcc` `fix annoying build number rebuilds` | Deferred | Rewrites the build-number update to a `-P` script that only rewrites `buildnumber.h` on change. This tree's `scripts/ci/compare-byte-parity.sh` depends on the current `increment_build.sh` behavior for byte-parity generation; adopting the new scheme requires re-validating that tooling in a dedicated batch. |
| 11 | `35e8c797` `fix annoying post build randomly failing` | Deferred | Upstream uses `cmake -E copy_directory_if_different`, which is newer than this project's declared CMake 3.16 minimum. The race fix needs a dedicated compatibility-preserving implementation; the unsupported command is not imported. |
| 12 | `5322adf6` `radiant - parked items, stale comments` | Rejected | Editor-only; outside committed runtime scope. |
| 13 | `78104bc0` `radiant - more parked items` | Rejected | Editor-only; outside committed runtime scope. |
| 14 | `60e911fc` `radiant - vehicle paths` | Rejected | Editor-only; outside committed runtime scope. |
| 15 | `dd018342` `radiant - port missing funcs, crosshair, ...` | Rejected | Editor-only; outside committed runtime scope. |
| 16 | `67b0ffe0` `radiant - fixes for advanced patch editor` | Rejected | Editor-only; outside committed runtime scope. |
| 17 | `4c9bb150` `wip vehicle refactor` | Deferred | SP vehicle code churn explicitly marked work-in-progress upstream; entangled with #18/#19. Needs a dedicated SP gameplay review batch; this tree compiles SP only through the hosted Windows x86 SP loop. |
| 18 | `5f77d720` `vehicle code refactor/de-dupe` | Deferred | +2,664/−3,679 SP vehicle refactor; prerequisite for #19. Same SP validation constraint as #17. |
| 19 | `aa2ed669` `(SP) fix vehicle corruption in CG_Vehicle_PreControllers` | Deferred | Fixes corruption in code shaped by #17/#18; cannot be imported in isolation. Same SP validation constraint. |
| 20 | `3f570489` `move sound impl specific funcs to snd_driver` | Deferred | Part of the upstream OpenAL restructuring epic deferred at `820b0a03` (row 9 of that ledger); the churn organizes OpenAL-specific implementation surface this tree does not carry. |
| 21 | `ae9d584d` `KISAK_SOUND -> KISAK_OPENAL` | Deferred | Same deferred OpenAL epic; the `KISAK_SOUND` gate does not exist in this tree. |
| 22 | `a40f1568` `move openal code to separate file` | Deferred | Same deferred OpenAL epic (+1,643/−1,501). |
| 23 | `43f95fd0` `more sound cleanup` | Deferred | Same deferred OpenAL epic; touches the shared sound headers only downstream of the OpenAL split. |
| 24 | `1c03702c` `update readme` | Rejected | Upstream README cosmetics; this tree maintains its own README. |
| 25 | `321218cb` `security: harden multiplayer message parsing (#93)` | Adapted and already equivalent in part | Adapted: download-block truncation drop, `MSG_ReadData` negative-length rejection, stat-packet truncation acknowledgment guard, and the 64-bit reliable-acknowledge delta with explicit truncated/negative-read rejection before decode. Already equivalent here (stronger, not re-imported): bounded `Huff_Decompress`/`MSG_ReadBitsCompress` with output-size and symbol-range validation, the compress-overflow drop in `SV_ExecuteClientMessage` and `CL_ParseServerMessage`, the download size clamp, and the stat range clamps in `SV_ReceiveStats`. |
| 26 | `7bffda1a` `clientside net cleanup and defensive code` | Adapted; remainder rejected | Adapted the `CL_ClearState` named local-client bound. The remainder is decompiler-output cleanup (declaration moves, `va()` inlining, switch re-indentation) without behavioral content for this tree. The `BADPACKET` routing is superseded by this tree's existing drop-on-illegible-message handling in `CL_ParseServerMessage`. |
| 27 | `7436f74e` `few more client fixes` | Adapted and already equivalent in part | Adapted: hard network-path bounds in `CL_ParsePacketEntities` (replacing a Release-inert `vassert`) and `CL_ParsePacketClients`, with the `c6be07a2` ordering refinement. Already equivalent here: the `MSG_ReadDeltaStruct` field index is pre-validated by `MSG_ReadLastChangedField` (fail-closed on out-of-range), the hud-elem `lc` bound already exists, and `FS_SV_Rename` pointer arithmetic is cosmetic-only. |
| 28 | `b0539c59` `server net defensive pass` | Adapted and already equivalent in part | Adapted: temp-ban timestamps now use `tempBanSlot_t::banTime` instead of the `mapCenter[9 * banSlot - 136]` float-array alias (functional but undefined behavior; this tree's struct already carried the field), and `ClientCleanName` no longer skips past the NUL terminator when a network-supplied name ends in `'^'`. Already equivalent here: `SVC_Status` player-line handling and the main-thread assertion were retained in their existing reviewed form. |
| 29 | `4c59a1ca` `more net checks (ai)` | Adapted and already equivalent in part | Adapted: dropped the stray `CS_EFFECT_NAMES 244` define so the authoritative `1598` enumerator governs `cgs->fxs[]` indexing (it previously indexed 1,354 slots past the 100-entry array on every effect registration); `BG_ValidateWeaponNumber` guard on the server-supplied offhand index; scores client clamp; shellshock index bound; demo chunk-size rejection; stat-packet picker negative-index skip; 7-bit stat mask test. Already equivalent here: `CL_WritePacket` and `CL_Record_f` compressed-size checks (this tree's bounded compress API returns and callers reject negative sizes). |
| 30 | `c6be07a2` `on 2nd review human review, some logic bugs stuck out` | Adapted | The review-ordering refinements fold into the adapted set: negative entity/client numbers break quietly before the hard error, and the `inuse < 0` hud-elem guard's intent is covered by this tree's fail-closed bit readers setting `overflowed` (loop body never executes on negative counts). |

## Local validation and ancestry evidence

The curated reconciliation contains no raw merge of the divergent upstream
source tree. The complete local portable GCC Release CTest suite is
**205/208**; the three failures (`abi-sizeof-debt-tripwire`,
`abi-sizeof-scanner-fixture`, `security-source-regressions`) are the
pre-existing baseline at base `9d840c96`, byte-identical before and after the
change, and are tracked under ki-9b13 and ki-ya3t. All mutation-sensitive
source contracts scanning the touched files pass, including the
`upstream_820b0a03_*` contract suites. The MP engine profile is compiled by
the hosted Windows/Linux jobs; engine targets intentionally do not compile on
the Linux portable configuration yet.

Checkpoint `e22fd6b7cd05afedec5129878a29d8f0ec953269` has:

- first parent
  `6b6fe07e5cde7882e435b2b89063116d24a094eb` (the curated adaptation commit
  `fix(net): curate upstream net-security hardening through c6be07a2`);
- second parent, exact upstream tip
  `c6be07a225450bc38387c27373047ae75e2e8533`; and
- tree `817ab0398cce134cc2ef9d1398e8c39d72c7d1c4`, exactly matching
  its first parent's tree.

The checkpoint's complete content diff from its first parent is empty.

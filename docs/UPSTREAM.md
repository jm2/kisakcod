# Upstream

Owner: operator.

## Remote

| | |
| --- | --- |
| Upstream | `SwagSoftware/KisakCOD`, branch `master` (remote `upstream`) |
| Last synced | `b3199b90` (the merge-base; upstream has no newer commits) |
| Fork position | about 890 commits ahead, 0 behind |

## Policy

- Sync at most once a month, as one operator bead
  ([AGENTS.md](../AGENTS.md) rule 11). Workers never merge upstream.
- Merge, never rebase: the fork's history is too long.
- Curate: take upstream bug fixes; when a fix conflicts with 64-bit work,
  keep upstream's logic and the fork's pointer-width types.
- Exclude accidental artifacts (patch blobs, generated files).
- Validate with the Linux test build and the Windows x86 CI legs. The Linux
  build does not compile most engine code, so the Windows legs are the real
  check for renderer, xanim and EffectsCore changes.
- Record the new merge-base above; nothing else.

## Name-alignment branches

Upstream's refactor commits replaced numeric constants with named values;
the fork merged them but kept its numeric spellings where they conflicted.
Aligning names reduces future conflicts but moves no KPI, so it waits for a
monthly sync bead and is never refactor burndown (rule 11).

| Branch | State |
| --- | --- |
| `integration/refactor-r2-image-contracts` | merged (PR #165) |
| `integration/refactor-r3-shader-contracts` | PR #166, parked draft; drop its burndown-doc edits if revived |
| `integration/refactor-s1-script-value-names` | unpublished, parked |
| `integration/refactor-g1-*` (7 branches) | unpublished, parked |

The per-sync ledgers and the refactor burndown are archived
([ARCHIVE.md](ARCHIVE.md)).

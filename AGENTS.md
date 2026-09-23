# AGENTS.md: operating rules

Every agent (mayor, refinery, polecat, reviewer) and every human contributor
reads this first. The only way to close a bead is to move a KPI or pass a gate
(`docs/NOW.md`, `docs/ROADMAP.md`). Each rule names the failure it prevents.

## Rules

1. **Dispatch only from the `docs/NOW.md` queue.** Every bead names its gate,
   the KPI it moves, and a done-test a reviewer can run.
   *Prevents: work nobody asked for, and "done" nobody can check.*
2. **Done means merged and measurable.** The bead records the merge SHA plus
   the KPI change or the gate evidence. A service or helper is done only once
   engine code in a real target calls it.
   *Prevents: helpers with no callers counted as progress.*
3. **WIP is at most 6 beads, and at least 4 are on the current gate.**
   *Prevents: 30 half-finished beads across 14 issues.*
4. **PRs are at most about 400 net hand-written lines.** Generated code is
   exempt if its generator is in the same PR.
   *Prevents: week-long reviews and rework spirals.*
5. **At most 3 rework rounds.** After that, split the bead or escalate to the
   reviewer or operator.
   *Prevents: round-17 PRs.*
6. **No new docs outside the doc set** (`docs/CHARTER.md` lists it): no
   ledgers, audits, matrices or "blocked evidence" files. A bead blocked on an
   owner action is marked `blocked-owner`, and the worker takes the next item.
   *Prevents: evidence paperwork replacing code.*
7. **Tests must compile and execute code:** engine code, or helpers the
   engine calls. No source-text (regex or substring) tests, no tests over CI
   or markdown files, no mutation tests of checkers. The only text scans
   allowed are the three debt tripwires in `docs/design/NATIVE64.md`; they
   may shrink, never grow.
   *Prevents: tests that break on refactors and pass on real bugs.*
8. **Never commit generated files.**
   *Prevents: merge conflicts in derived output.*
9. **Polecats branch from fresh master and never sync-merge master into their
   branch.** The refinery rebases and integrates.
   *Prevents: 43 sync merges a month and their conflicts.*
10. **Keep the Windows x86 baseline building.** Don't harden or refactor
    Windows-only code unless it blocks a gate.
    *Prevents: polishing the one target that already works.*
11. **Upstream syncs are at most one operator bead per month** (`docs/UPSTREAM.md`).
    No refactor burndown unless it unblocks a gate.
    *Prevents: alignment churn crowding out the port.*
12. **Codacy's structural rules are advisory on `tests/` and CI code.**
    Security rules stay blocking.
    *Prevents: lint-only commits on test scaffolding.*
13. **Reviewer role.** Reject the PR if any answer is "no":
    - Does it move the named KPI or gate?
    - Is it inside its bead's scope?
    - Does it avoid adding process artifacts?
    - Would its tests fail if the feature broke?

    *Prevents: merged work that moves nothing.*
14. **Mayor role.**
    - Reads only `AGENTS.md`, `docs/NOW.md` and `docs/ROADMAP.md`.
    - Re-ranks the queue only by editing `docs/NOW.md`, and is the only role
      that edits it.
    - Reports the KPI trend weekly, and flags any gate whose KPIs haven't
      moved in 7 days.

    *Prevents: a queue that drifts from the gates, and stalls nobody sees.*
15. **Reading order for a worker:** `AGENTS.md`, `docs/NOW.md`,
    `docs/CHARTER.md`, then the one design doc for its area (`docs/design/`).
    Nothing else unless the bead says so.
    *Prevents: agents burning context on archived plans.*

## Build and test

```sh
cmake --preset linux-amd64-mp
cmake --build build-linux-amd64-mp -j
ctest --test-dir build-linux-amd64-mp -j4 --output-on-failure
```

Windows x86 uses the `windows-x86-mp`, `windows-x86-dedi` and
`windows-x86-sp` presets. The GitHub repo is `jm2/kisakcod`; pin
`gh -R jm2/kisakcod`, because bare `gh` resolves to upstream.

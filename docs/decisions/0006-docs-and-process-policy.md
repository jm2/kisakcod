# ADR-0006: Doc set, budgets and agent rules

Status: accepted
Date: 2026-09-22

## Context

`docs/` held 1.34 MB; the old roadmap file alone was 310 KB, and only about
7% of it was current. Ledgers, audits and a generated dashboard caused most merge
conflicts. About 10% of September's churn advanced native 64-bit work;
roughly half of merged polecat diffs were tests, many of them regexes over
source text.

## Decision

- The doc set is the list in [CHARTER.md](../CHARTER.md), with hard budgets
  and a 100 KB total for `docs/`. `scripts/ci/docs_budget.py` enforces them.
- Live docs carry no history, evidence logs, `file:line` citations or commit
  SHAs (except the archive tag in `ARCHIVE.md` and the last-synced SHA in
  `UPSTREAM.md`).
- [AGENTS.md](../../AGENTS.md) holds the operating rules: dispatch from
  `NOW.md`, done means merged plus a KPI or gate change, WIP 6, PRs about
  400 lines, 3 rework rounds, no source-text tests.
- KPIs K1-K6 measure compiled, linked and executed code, and replace the
  text-pattern gauges (dashboard, ledgers, burndowns).
- Old docs are archived by the tag named in `ARCHIVE.md`, not kept in-tree.

## Consequences

- New findings go into the owning design doc, a bead or an issue, never a
  new file.
- The mayor re-ranks `NOW.md` at least every 10 days; CI warns after that.

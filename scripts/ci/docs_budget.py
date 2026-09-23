#!/usr/bin/env python3
"""Enforce the doc-set budgets from ADR-0006 (docs/decisions/).

Fails when a live doc exceeds its budget, when docs/ holds a file outside the
allowed set, or when docs/ exceeds its total budget. A stale NOW.md review
date is a warning annotation, never a failure. 1 KB = 1024 bytes.
"""

from __future__ import annotations

import datetime as dt
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KB = 1024
BUDGETS = {  # path -> max bytes
    "AGENTS.md": 6 * KB,
    "CLAUDE.md": 1 * KB,
    "docs/CHARTER.md": 8 * KB,
    "docs/ROADMAP.md": 15 * KB,
    "docs/NOW.md": 6 * KB,
    "docs/UPSTREAM.md": 3 * KB,
    "docs/ARCHIVE.md": 1 * KB,
    "docs/capability/manifest.json": 8 * KB,
    "docs/design/NATIVE64.md": 15 * KB,
    "docs/design/FASTFILE_LOADER.md": 12 * KB,
    "docs/design/NET_STEAM18.md": 12 * KB,
    "docs/design/PLATFORM_POSIX.md": 8 * KB,
    "docs/design/DETERMINISM.md": 5 * KB,
    "docs/design/CLIENT.md": 10 * KB,
}
ADR_RE = re.compile(r"^docs/decisions/\d{4}-[a-z0-9-]+\.md$")
ADR_BUDGET = 2 * KB
DOCS_TOTAL = 100 * KB
NOW_MAX_AGE_DAYS = 10


def check_sizes(docs: list[str]) -> tuple[list[str], list[tuple[str, int, int]]]:
    errors = ["%s is not in the doc set (ADR-0006); fold it into a live doc" % rel
              for rel in docs if rel not in BUDGETS and not ADR_RE.match(rel)]
    rows = []
    for rel in sorted(set(BUDGETS) | {d for d in docs if ADR_RE.match(d)}):
        if (ROOT / rel).is_file():
            size, limit = (ROOT / rel).stat().st_size, BUDGETS.get(rel, ADR_BUDGET)
            rows.append((rel, size, limit))
            if size > limit:
                errors.append("%s is %d bytes; budget %d" % (rel, size, limit))
    claude = ROOT / "CLAUDE.md"
    if claude.is_file() and claude.read_text(encoding="utf-8").strip() != "@AGENTS.md":
        errors.append("CLAUDE.md must be exactly the line @AGENTS.md")
    return errors, rows


def warn_stale_now() -> None:
    text = (ROOT / "docs/NOW.md").read_text(encoding="utf-8") if (ROOT / "docs/NOW.md").is_file() else ""
    m = re.search(r"^Last reviewed:\s*(\d{4}-\d{2}-\d{2})", text, re.M)
    if not m:
        print("::warning file=docs/NOW.md::no 'Last reviewed: YYYY-MM-DD' line")
        return
    try:
        age = (dt.date.today() - dt.date.fromisoformat(m.group(1))).days
    except ValueError:
        print("::warning file=docs/NOW.md::'Last reviewed: %s' is not a valid date" % m.group(1))
        return
    if age > NOW_MAX_AGE_DAYS:
        print("::warning file=docs/NOW.md::last reviewed %d days ago (limit %d); "
              "the mayor must re-rank the queue" % (age, NOW_MAX_AGE_DAYS))


def main() -> int:
    docs = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "docs").rglob("*") if p.is_file())
    errors, rows = check_sizes(docs)
    total = sum((ROOT / d).stat().st_size for d in docs)
    if total > DOCS_TOTAL:
        errors.append("docs/ totals %d bytes; budget %d" % (total, DOCS_TOTAL))
    warn_stale_now()
    table = "\n".join(["## Doc-set budgets", "", "| Doc | Bytes | Budget |", "|---|---:|---:|"]
                      + ["| %s | %d | %d |" % row for row in rows + [("**docs/ total**", total, DOCS_TOTAL)]])
    print(table)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(table + "\n\n")
    for e in errors:
        print("::error::%s" % e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

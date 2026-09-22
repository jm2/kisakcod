#!/usr/bin/env python3
"""Render the KPI summary (K1-K6) as Markdown for $GITHUB_STEP_SUMMARY.

Inputs:
  docs/NOW.md                  the KPI table the mayor maintains (all KPIs)
  docs/capability/manifest.json  target x role delivery cells (K6)
  --census PATH                optional native64-census JSON; its measured
                               values replace the NOW.md values for K1, K2,
                               K3 and K5

Exits 1 only when the manifest is malformed. Everything else is reported,
never gated: the KPIs are trend numbers, not pass/fail checks.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEVELS = ("none", "compiles", "links", "boots_map", "fork_peer", "steam18_peer", "packaged")


def check_manifest(m: dict) -> list[str]:
    errors = []
    if m.get("schema_version") != 2:
        errors.append("schema_version must be 2")
    if tuple(m.get("levels", ())) != LEVELS:
        errors.append("levels must be exactly: " + ", ".join(LEVELS))
    targets = {t.get("id"): t for t in m.get("targets", [])}
    roles = m.get("roles", [])
    seen = set()
    for c in m.get("cells", []):
        key = (c.get("target"), c.get("role"))
        if key[0] not in targets or key[1] not in roles or c.get("level") not in LEVELS:
            errors.append("bad cell: %r" % (c,))
        if key in seen:
            errors.append("duplicate cell: %s/%s" % key)
        seen.add(key)
    for tid, t in targets.items():
        for role in roles:
            if t.get("required") and (tid, role) not in seen:
                errors.append("missing cell: %s/%s" % (tid, role))
    return errors


def now_kpi_rows(text: str) -> tuple[list[str], list[list[str]]]:
    """Return (header cells, data rows) of the first table under '## KPIs'."""
    section = re.search(r"^## KPIs\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    rows = []
    for line in (section.group(1) if section else "").splitlines():
        if line.startswith("|") and not re.match(r"^\|[\s:|-]+\|$", line):
            rows.append([c.strip() for c in line.strip().strip("|").split("|")])
    return (rows[0], rows[1:]) if rows else ([], [])


def census_values(c: dict) -> dict[str, str]:
    out = {}
    t = c.get("targets", {})
    k1 = ["%s %s/%s" % (n, t[n].get("pass"), t[n].get("total")) for n in ("win64", "lin64", "a64") if n in t]
    if k1:
        out["K1"] = " · ".join(k1)
    link = c.get("link", {}).get("win64")
    if link:
        probe = link.get("probe", {})
        out["K2"] = "win64 real link: %s; probe: %s, %s undefined" % (
            link.get("real", "?"), probe.get("status", "?"), probe.get("undefined", "?"))
    if "k3" in c:
        out["K3"] = "%s/%s" % (c["k3"].get("compiled"), c["k3"].get("total"))
    if "d3d_stub_tus" in c:
        out["K5"] = "%s TUs (lin64)" % c["d3d_stub_tus"]
    return out


def render(manifest: dict, now_text: str, census: dict | None) -> str:
    out = ["## KPIs", ""]
    header, rows = now_kpi_rows(now_text)
    measured = census_values(census) if census else {}
    if header:
        value_col = header.index("Value") if "Value" in header else 2
        out += ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
        for r in rows:
            if r and r[0] in measured and len(r) > value_col:
                r = list(r)
                r[value_col] = measured[r[0]] + " (census)"
            out.append("| " + " | ".join(r) + " |")
    else:
        out.append("_docs/NOW.md has no `## KPIs` table._")
    if census:
        out += ["", "Census commit: `%s`" % census.get("commit", "unknown")]

    cells = manifest["cells"]
    required = {t["id"] for t in manifest["targets"] if t.get("required")}
    req = [c for c in cells if c["target"] in required]
    out += ["", "## K6 delivery cells", "",
            "Required cells at or above each level (of %d):" % len(req), ""]
    out.append("| " + " | ".join(LEVELS[1:]) + " |")
    out.append("|" + "---|" * (len(LEVELS) - 1))
    out.append("| " + " | ".join(
        str(sum(LEVELS.index(c["level"]) >= i for c in req)) for i in range(1, len(LEVELS))) + " |")
    out += ["", "| Target | Role | Level | Next gate | Evidence |", "|---|---|---|---|---|"]
    for c in cells:
        out.append("| %s | %s | %s | %s | %s |" % (
            c["target"], c["role"], c["level"], c.get("next", ""), c.get("evidence", "")))
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--manifest", default=ROOT / "docs/capability/manifest.json", type=Path)
    ap.add_argument("--now", default=ROOT / "docs/NOW.md", type=Path)
    ap.add_argument("--census", type=Path)
    args = ap.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    errors = check_manifest(manifest)
    for e in errors:
        print("::error file=%s::%s" % (args.manifest, e), file=sys.stderr)
    if errors:
        return 1
    census = None
    if args.census and args.census.is_file():
        census = json.loads(args.census.read_text(encoding="utf-8"))
    now_text = args.now.read_text(encoding="utf-8") if args.now.is_file() else ""
    sys.stdout.write(render(manifest, now_text, census))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

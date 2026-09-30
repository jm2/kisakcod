#!/usr/bin/env python3
"""Release metadata for .github/workflows/release.yml.

Each subcommand prints GitHub step outputs (key=value lines) or a document on
stdout, and reports problems as workflow annotations on stderr.

  metadata --version V [--dry-run true|false]
      Validates V and builds the release matrix from
      docs/capability/manifest.json. Publishing needs vX.Y.Z; a dry run also
      takes vX.Y.Z-suffix and defaults to v0.0.0-dryrun. Every required
      target x role cell at `links` or above is one build leg. Its tier
      follows the level: preview below fork_peer, beta at fork_peer,
      first-class from steam18_peer. A qualifying cell with no recipe in
      RECIPES fails the run, so a release never silently drops a target.
  gate --commit SHA --ref REF --ci-runs N [--tag-commit SHA] [--dry-run B]
      Publishing needs a dispatch from master, a successful ci.yml push run
      on master for this exact commit, and no tag of this version on another
      commit. A dry run reports the same problems as warnings.
  readme --leg JSON --version V --commit SHA [--requires TEXT]
      Prints the README.txt that ships in a leg's package.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from kpi_summary import LEVELS, ROOT, check_manifest  # noqa: E402

MANIFEST = ROOT / "docs/capability/manifest.json"
REPO_URL = "https://github.com/jm2/kisakcod"
_NUM = r"(0|[1-9][0-9]*)"
RELEASE_RE = re.compile(rf"v{_NUM}\.{_NUM}\.{_NUM}", re.ASCII)
DRY_RUN_RE = re.compile(RELEASE_RE.pattern + r"(-[0-9A-Za-z][0-9A-Za-z.-]{0,31})?", re.ASCII)
DRY_RUN_DEFAULT = "v0.0.0-dryrun"
SHA_RE = re.compile(r"[0-9a-f]{40}", re.ASCII)

# (target, role) -> how release.yml builds the leg. A new row needs its steps
# in release.yml; `recipe` selects them.
RECIPES = {
    ("linux-amd64", "headless-server"): {"recipe": "linux-headless", "runner": "ubuntu-24.04",
                                         "platform": "linux-x64", "file_arch": "x86-64"},
    ("linux-arm64", "headless-server"): {"recipe": "linux-headless", "runner": "ubuntu-24.04-arm",
                                         "platform": "linux-arm64", "file_arch": "aarch64"},
}
# role -> (asset name part, binary, description)
ROLES = {
    "headless-server": ("headless", "KisakCOD-dedi", "headless dedicated server"),
    "mp-client": ("client", "KisakCOD-mp", "multiplayer client"),
}
TIER_SUFFIX = {"preview": "-preview", "beta": "-beta", "first-class": ""}
LEVEL_NOTES = {
    "links": "it builds, links and passes an assetless smoke run. It cannot load "
             "retail fast files yet, so it cannot host a game.",
    "boots_map": "it loads stock maps from unmodified Steam 1.8 data. No client has played on it yet.",
    "fork_peer": "KisakCOD clients play full rounds on it. Steam 1.8 clients are not verified yet.",
    "steam18_peer": "it plays with unmodified Steam 1.8 clients in both directions.",
    "packaged": "it is a packaged release target.",
}


class ReleaseError(Exception):
    pass


def tier(level: str) -> str:
    i = LEVELS.index(level)
    if i < LEVELS.index("fork_peer"):
        return "preview"
    return "beta" if i < LEVELS.index("steam18_peer") else "first-class"


def parse_version(raw: str, dry_run: bool) -> dict[str, str]:
    tag = raw.strip() or (DRY_RUN_DEFAULT if dry_run else "")
    if not (DRY_RUN_RE if dry_run else RELEASE_RE).fullmatch(tag):
        shape = "vX.Y.Z or vX.Y.Z-suffix" if dry_run else "vX.Y.Z"
        raise ReleaseError(f"version {raw!r} is not {shape}")
    return {"tag": tag, "version": tag[1:], "name": f"KisakCOD {tag[1:]}"}


def build_matrix(manifest: dict, version: str) -> tuple[list[dict[str, str]], list[str]]:
    """Return (legs, qualifying cells with no recipe)."""
    errors = check_manifest(manifest)
    if errors:
        raise ReleaseError("malformed manifest: " + "; ".join(errors))
    required = {t["id"] for t in manifest["targets"] if t.get("required")}
    legs, missing = [], []
    for cell in manifest["cells"]:
        key = (cell["target"], cell["role"])
        if key[0] not in required or LEVELS.index(cell["level"]) < LEVELS.index("links"):
            continue
        if key not in RECIPES or key[1] not in ROLES:
            missing.append("%s/%s" % key)
            continue
        recipe = RECIPES[key]
        role_part, binary, _ = ROLES[key[1]]
        leg_tier = tier(cell["level"])
        stem = f"kisakcod-{version}-{recipe['platform']}-{role_part}{TIER_SUFFIX[leg_tier]}"
        legs.append({"target": key[0], "role": key[1], "level": cell["level"], "tier": leg_tier,
                     "binary": binary, "stem": stem, **recipe,
                     "label": f"{recipe['platform']} {role_part} ({leg_tier})"})
    return sorted(legs, key=lambda leg: leg["stem"]), missing


def gate_problems(commit: str, ref: str, ci_runs: int, tag_commit: str) -> list[str]:
    problems = []
    if ref != "refs/heads/master":
        problems.append(f"releases are dispatched from master, not {ref}")
    if ci_runs < 1:
        problems.append(f"commit {commit} has no successful ci.yml push run on master")
    if tag_commit and tag_commit != commit:
        problems.append(f"the version's tag already points at {tag_commit}, not {commit}")
    return problems


def readme(leg: dict, version: str, commit: str, requires: str) -> str:
    _, binary, description = ROLES[leg["role"]]
    paragraphs = [
        f"KisakCOD {version}: {leg['platform']} {description} ({leg['tier']})",
        f"{binary} is the KisakCOD {description} for Call of Duty 4 multiplayer, built for "
        f"{leg['platform']} from {REPO_URL} commit {commit}.",
        f"Status: {leg['tier']}. At capability level `{leg['level']}`, {LEVEL_NOTES[leg['level']]}",
        "No game data: this package contains no Call of Duty 4 content. Retail data is the "
        "user's own; set fs_basepath to your own copy of the game.",
        f"Debug symbols: {leg['stem']}-debugsymbols.tar.xz. Extract it into the same directory; "
        f"{binary} finds {binary}.debug through its .gnu_debuglink.",
        f"Requires: {requires}." if requires else "",
        "License: GNU GPL v3, in LICENSE. The source archive of the same release holds the "
        "corresponding source.",
    ]
    return "\n\n".join(textwrap.fill(p, 76, break_on_hyphens=False, break_long_words=False)
                        for p in paragraphs if p) + "\n"


def _bool(text: str) -> bool:
    if text not in ("true", "false"):
        raise argparse.ArgumentTypeError(f"expected true or false, got {text!r}")
    return text == "true"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("metadata")
    p.add_argument("--version", default="")
    p.add_argument("--dry-run", type=_bool, default=True)
    p.add_argument("--manifest", type=Path, default=MANIFEST)
    p = sub.add_parser("gate")
    p.add_argument("--commit", required=True)
    p.add_argument("--ref", required=True)
    p.add_argument("--ci-runs", type=int, required=True)
    p.add_argument("--tag-commit", default="")
    p.add_argument("--dry-run", type=_bool, default=True)
    p = sub.add_parser("readme")
    p.add_argument("--leg", type=json.loads, required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--requires", default="")
    args = parser.parse_args(argv)

    try:
        if args.command == "metadata":
            meta = parse_version(args.version, args.dry_run)
            legs, missing = build_matrix(json.loads(args.manifest.read_text()), meta["version"])
            if missing:
                raise ReleaseError("cells at links or above with no release recipe: " + ", ".join(missing)
                                   + " (add a RECIPES row and its release.yml steps)")
            if not legs:
                raise ReleaseError("no required target x role cell is at links or above")
            meta["matrix"] = json.dumps({"include": legs}, separators=(",", ":"))
            meta["prerelease"] = str(all(leg["tier"] != "first-class" for leg in legs)).lower()
            print("\n".join(f"{k}={v}" for k, v in meta.items()))
        elif args.command == "gate":
            if not SHA_RE.fullmatch(args.commit) or (args.tag_commit and not SHA_RE.fullmatch(args.tag_commit)):
                raise ReleaseError("commits must be full 40-hex SHAs")
            problems = gate_problems(args.commit, args.ref, args.ci_runs, args.tag_commit)
            for problem in problems:
                print(f"::{'warning' if args.dry_run else 'error'}::{problem}", file=sys.stderr)
            if problems and not args.dry_run:
                return 1
            print(f"publish={str(not args.dry_run).lower()}")
        else:
            sys.stdout.write(readme(args.leg, args.version, args.commit, args.requires))
    except ReleaseError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

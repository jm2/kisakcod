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
  pdb-id --exe EXE --pdb PDB
      Prints BUILD_ID=<GUID><age>, the symbol-server key, when the PE's
      CodeView record names the PDB and matches its GUID and age: the two
      halves of a Windows leg belong together (the Linux build-ID check).
  zip --root DIR --stem STEM --epoch SECONDS --out FILE
      Writes DIR/STEM as a zip with sorted names and fixed timestamps, so a
      Windows package is reproducible like the Linux tarballs.
  tarxz --root DIR --stem STEM --epoch SECONDS --out FILE
      Writes DIR/STEM as a tar.xz the way the Linux legs' GNU tar does
      (sorted names, one mtime, owner 0), for the macOS leg, whose runner
      image has no xz for GNU tar to call.
  notes --tag T --commit SHA --matrix JSON --run-url URL [--repo-dir DIR]
      Prints the release notes: the packages and their tiers, then the
      first-parent changes on master since the previous vX.Y.Z tag.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import tarfile
import textwrap
import time
import uuid
import zipfile
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
# in release.yml; `recipe` selects them. `archive` is the package format and
# `exe` the executable suffix.
LINUX = {"recipe": "linux-headless", "archive": "tar.xz", "exe": ""}
WINDOWS = {"recipe": "windows-headless", "archive": "zip", "exe": ".exe"}
RECIPES = {
    ("linux-amd64", "headless-server"): {**LINUX, "runner": "ubuntu-24.04", "platform": "linux-x64",
                                         "file_arch": "x86-64"},
    ("linux-arm64", "headless-server"): {**LINUX, "runner": "ubuntu-24.04-arm", "platform": "linux-arm64",
                                         "file_arch": "aarch64"},
    # file_arch is both the Visual Studio platform and the PE machine name.
    ("windows-amd64", "headless-server"): {**WINDOWS, "runner": "windows-2025", "platform": "windows-x64",
                                           "file_arch": "x64"},
    ("windows-arm64", "headless-server"): {**WINDOWS, "runner": "windows-11-arm", "platform": "windows-arm64",
                                           "file_arch": "ARM64"},
    # file_arch is the Mach-O architecture; the symbols ship as a dSYM bundle.
    ("macos-arm64", "headless-server"): {"recipe": "macos-headless", "archive": "tar.xz", "exe": "",
                                         "runner": "macos-26", "platform": "macos-arm64", "file_arch": "arm64"},
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
        + f"{leg['platform']} from {REPO_URL} commit {commit}.",
        f"Status: {leg['tier']}. At capability level `{leg['level']}`, {LEVEL_NOTES[leg['level']]}",
        "No game data: this package contains no Call of Duty 4 content. Retail data is the "
        + "user's own; set fs_basepath to your own copy of the game.",
        f"Debug symbols: {leg['stem']}-debugsymbols.{leg['archive']}. Extract it into the same directory; "
        + (f"debuggers find {binary}.pdb next to {binary}.exe." if leg["exe"] == ".exe"
           else f"lldb finds {binary}.dSYM next to {binary} by its UUID." if leg["recipe"] == "macos-headless"
           else f"{binary} finds {binary}.debug through its .gnu_debuglink."),
        f"Requires: {requires}." if requires else "",
        "License: GNU GPL v3, in LICENSE. The source archive of the same release holds the "
        + "corresponding source.",
    ]
    return "\n\n".join(textwrap.fill(p, 76, break_on_hyphens=False, break_long_words=False)
                        for p in paragraphs if p) + "\n"


def changes(repo: Path, commit: str, tag: str) -> tuple[str | None, list[str]]:
    """(previous vX.Y.Z tag, one line per first-parent commit since it, newest first)."""
    def git(*args: str) -> str:
        return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout
    tags = git("tag", "--merged", commit, "--list", "v*", "--sort=-v:refname").split()
    previous = next((t for t in tags if RELEASE_RE.fullmatch(t) and t != tag), None)
    log = git("log", "--first-parent", "--format=%H%x1f%s%x1f%b%x1e", f"{previous}..{commit}" if previous else commit)
    return previous, [_change_line(r) for r in (r.strip("\n") for r in log.split("\x1e")) if r]


def _change_line(record: str) -> str:
    """One changelog line: a PR merge by its PR title, any other commit by its subject."""
    sha, subject, body = record.split("\x1f", 2)
    pr = re.fullmatch(r"Merge pull request #(\d+) from \S+", subject)
    if not pr:
        return f"- {subject} ({sha[:8]})"
    title = next((line.strip() for line in body.splitlines() if line.strip()), subject)
    return f"- {title} (#{pr.group(1)})"


def notes(legs: list[dict], tag: str, commit: str, run_url: str, repo: Path, limit: int = 100) -> str:
    version = tag[1:]
    out = [f"# KisakCOD {version}", "",
           f"Built from [`{commit[:12]}`]({REPO_URL}/commit/{commit}) by [this workflow run]({run_url}).", "",
           "## Packages", "", "| Package | Target | Role | Tier | Level |", "| --- | --- | --- | --- | --- |"]
    out += [f"| `{leg['stem']}.{leg['archive']}` | {leg['target']} | {leg['role']} | {leg['tier']} | `{leg['level']}` |"
            for leg in legs]
    out += ["", "Each package has a `-debugsymbols` companion in the same format (extract it into the same "
            + "directory) and a "
            + f"`-provenance.json`. `kisakcod-{version}-source.tar.gz` is the commit's source without the vendor "
            + "runtime binaries (Miles, Bink, Steamworks). `SHA256SUMS.txt` covers every file.", "", "## Status", ""]
    out += [f"- **{leg['label']}**: at capability level `{leg['level']}`, {LEVEL_NOTES[leg['level']]}" for leg in legs]
    out += ["", "## No game data", "", "These packages contain no Call of Duty 4 content. Retail data is the "
            + "user's own.", ""]
    previous, lines = changes(repo, commit, tag)
    out += [f"## Changes since {previous}" if previous else f"## Changes (the latest {limit} on master)", ""]
    out += lines[:limit]
    if len(lines) > limit:
        more = f"compare/{previous}...{commit}" if previous else f"commits/{commit}"
        out.append(f"- ...and {len(lines) - limit} more: {REPO_URL}/{more}")
    return "\n".join(out) + "\n"


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def codeview(exe: bytes) -> tuple[bytes, int, str]:
    """(GUID, age, PDB path) from a PE32+ image's CodeView (RSDS) debug record."""
    pe = _u32(exe, 0x3C)
    opt = pe + 24
    if exe[pe:pe + 4] != b"PE\0\0" or _u16(exe, opt) != 0x20B or _u32(exe, opt + 108) <= 6:
        raise ReleaseError("the executable is not a PE32+ image with a debug directory")
    table = opt + _u16(exe, pe + 20)
    rva, size = struct.unpack_from("<II", exe, opt + 112 + 6 * 8)  # data directory 6: debug
    for i in range(_u16(exe, pe + 6)):
        _, va, raw_size, raw = struct.unpack_from("<IIII", exe, table + 40 * i + 8)
        if va <= rva < va + raw_size:
            break
    else:
        raise ReleaseError("the debug directory lies outside every section")
    for entry in range(rva - va + raw, rva - va + raw + size, 28):
        if _u32(exe, entry + 12) == 2:  # IMAGE_DEBUG_TYPE_CODEVIEW
            start = _u32(exe, entry + 24)
            record = exe[start:start + _u32(exe, entry + 16)]
            if record[:4] == b"RSDS":
                return record[4:20], _u32(record, 20), record[24:].split(b"\0")[0].decode("utf-8", "replace")
    raise ReleaseError("the executable has no CodeView (RSDS) record")


def pdb_signature(msf: bytes) -> tuple[bytes, int]:
    """(GUID, age) of an MSF 7.00 PDB, the values a debugger matches against the PE."""
    # The GUID lives in the PDB info stream (1) and the age in the DBI stream (3).
    if not msf.startswith(b"Microsoft C/C++ MSF 7.00\r\n\x1aDS"):
        raise ReleaseError("the symbol file is not an MSF 7.00 PDB")
    block, dir_bytes, map_block = _u32(msf, 32), _u32(msf, 44), _u32(msf, 52)
    directory = b"".join(msf[n * block:(n + 1) * block] for n in
                         (_u32(msf, map_block * block + 4 * i) for i in range(-(-dir_bytes // block))))
    sizes = [_u32(directory, 4 + 4 * i) for i in range(_u32(directory, 0))]

    def stream(k: int) -> int:
        """Offset of stream k's first block."""
        skip = sum(-(-n // block) for n in sizes[:k] if n != 0xFFFFFFFF)
        return _u32(directory, 4 + 4 * len(sizes) + 4 * skip) * block
    info, dbi = stream(1), stream(3)
    return msf[info + 12:info + 28], _u32(msf, dbi + 8)


def pdb_id(exe: bytes, msf: bytes, pdb_name: str) -> str:
    """The GUID and age that tie a PE to its PDB, as hex; ReleaseError unless they match."""
    try:
        guid, age, path = codeview(exe)
        if re.split(r"[\\/]", path)[-1].lower() != pdb_name.lower():
            raise ReleaseError(f"the executable names {path}, not {pdb_name}")
        if pdb_signature(msf) != (guid, age):
            raise ReleaseError("the PDB's GUID and age do not match the executable's")
    except struct.error as exc:
        raise ReleaseError(f"truncated executable or PDB: {exc}") from exc
    return uuid.UUID(bytes_le=guid).hex.upper() + f"{age:X}"  # the symbol-server key


def write_zip(root: Path, stem: str, epoch: int, out: Path) -> None:
    """root/stem as a zip: sorted names, one fixed timestamp, no host metadata."""
    stamp = time.gmtime(max(epoch, 315532800))[:6]  # zip dates start in 1980
    with zipfile.ZipFile(out, "w") as zf:
        for path in sorted(p for p in (root / stem).rglob("*") if p.is_file()):
            info = zipfile.ZipInfo(path.relative_to(root).as_posix(), stamp)
            info.compress_type, info.external_attr, info.create_system = zipfile.ZIP_DEFLATED, 0o644 << 16, 3
            zf.writestr(info, path.read_bytes())


def write_tar_xz(root: Path, stem: str, epoch: int, out: Path) -> None:
    """root/stem as a tar.xz: sorted names, one mtime, owner 0, no host metadata (GNU tar's
    --sort=name --owner=0 --group=0 --numeric-owner --mtime); modes are 0755 or 0644."""
    def normalise(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.mtime, info.uid, info.gid, info.uname, info.gname = epoch, 0, 0, "", ""
        info.mode = 0o755 if info.isdir() or info.mode & 0o111 else 0o644
        return info
    with tarfile.open(out, "w:xz", format=tarfile.GNU_FORMAT) as tf:
        for path in [root / stem, *sorted((root / stem).rglob("*"))]:
            if path.is_symlink() or not (path.is_dir() or path.is_file()):
                raise ReleaseError(f"{path} is not a regular file or directory")
            tf.add(path, path.relative_to(root).as_posix(), recursive=False, filter=normalise)


def run_metadata(args: argparse.Namespace) -> int:
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
    return 0


def run_gate(args: argparse.Namespace) -> int:
    if not SHA_RE.fullmatch(args.commit) or (args.tag_commit and not SHA_RE.fullmatch(args.tag_commit)):
        raise ReleaseError("commits must be full 40-hex SHAs")
    problems = gate_problems(args.commit, args.ref, args.ci_runs, args.tag_commit)
    for problem in problems:
        print(f"::{'warning' if args.dry_run else 'error'}::{problem}", file=sys.stderr)
    if problems and not args.dry_run:
        return 1
    print(f"publish={str(not args.dry_run).lower()}")
    return 0


def run_readme(args: argparse.Namespace) -> int:
    sys.stdout.write(readme(args.leg, args.version, args.commit, args.requires))
    return 0


def run_pdb_id(args: argparse.Namespace) -> int:
    print("BUILD_ID=" + pdb_id(args.exe.read_bytes(), args.pdb.read_bytes(), args.pdb.name))
    return 0


def run_zip(args: argparse.Namespace) -> int:
    write_zip(args.root, args.stem, args.epoch, args.out)
    return 0


def run_tar_xz(args: argparse.Namespace) -> int:
    write_tar_xz(args.root, args.stem, args.epoch, args.out)
    return 0


def run_notes(args: argparse.Namespace) -> int:
    if not DRY_RUN_RE.fullmatch(args.tag) or not SHA_RE.fullmatch(args.commit):
        raise ReleaseError("notes need a version tag and a full commit SHA")
    sys.stdout.write(notes(args.matrix["include"], args.tag, args.commit, args.run_url, args.repo_dir))
    return 0


def _bool(text: str) -> bool:
    if text not in ("true", "false"):
        raise argparse.ArgumentTypeError(f"expected true or false, got {text!r}")
    return text == "true"


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description="Release metadata for .github/workflows/release.yml.")
    sub = root.add_subparsers(dest="command", required=True)
    p = sub.add_parser("metadata")
    p.set_defaults(run=run_metadata)
    p.add_argument("--version", default="")
    p.add_argument("--dry-run", type=_bool, default=True)
    p.add_argument("--manifest", type=Path, default=MANIFEST)
    p = sub.add_parser("gate")
    p.set_defaults(run=run_gate)
    p.add_argument("--commit", required=True)
    p.add_argument("--ref", required=True)
    p.add_argument("--ci-runs", type=int, required=True)
    p.add_argument("--tag-commit", default="")
    p.add_argument("--dry-run", type=_bool, default=True)
    p = sub.add_parser("readme")
    p.set_defaults(run=run_readme)
    p.add_argument("--leg", type=json.loads, required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--requires", default="")
    p = sub.add_parser("pdb-id")
    p.set_defaults(run=run_pdb_id)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--pdb", type=Path, required=True)
    p = sub.add_parser("zip")
    p.set_defaults(run=run_zip)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--stem", required=True)
    p.add_argument("--epoch", type=int, required=True)
    p.add_argument("--out", type=Path, required=True)
    p = sub.add_parser("tarxz")
    p.set_defaults(run=run_tar_xz)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--stem", required=True)
    p.add_argument("--epoch", type=int, required=True)
    p.add_argument("--out", type=Path, required=True)
    p = sub.add_parser("notes")
    p.set_defaults(run=run_notes)
    p.add_argument("--tag", required=True)
    p.add_argument("--commit", required=True)
    p.add_argument("--matrix", type=json.loads, required=True)
    p.add_argument("--run-url", required=True)
    p.add_argument("--repo-dir", type=Path, default=ROOT)
    return root


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.run(args)
    except ReleaseError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Shared fixtures and helpers for the release provenance contract tests."""

# The release-provenance contract is exercised by several sibling test modules
# (``test_release_provenance.py`` and its split suites). They all build the same
# synthetic release set and invoke the same in-process tool, so the fixture
# construction lives here instead of being duplicated across modules. Keeping
# the shared code out of a ``test_*.py`` module also keeps each test file's
# non-comment line count under the analyzer's per-file limit.

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
import tarfile
import unittest
from collections import namedtuple
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REQUIREMENTS = SCRIPT_DIR / "release-requirements.json"

TAG = "v9.9.9"
COMMIT = "a" * 40
ToolResult = namedtuple("ToolResult", ["returncode", "stdout", "stderr"])


def load_module(name: str, filename: str):
    """Load a sibling module under ``scripts/ci`` without touching sys.path."""
    path = SCRIPT_DIR / filename
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load module {name!r} from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


# Register the package modules in dependency order so the tool's sibling
# imports resolve, then load the tool itself. Loading in-process avoids a
# subprocess and keeps the test runnable from any working directory.
load_module("release_provenance_common", "release_provenance_common.py")
load_module("release_provenance_archive", "release_provenance_archive.py")
TOOL = load_module("release_provenance_under_test", "release_provenance.py")


def scratch_root() -> str | None:
    """Return a scratch parent honoring $TMPDIR, else the platform default."""
    configured = os.environ.get("TMPDIR")
    if configured and os.path.isdir(configured):
        return configured
    return None


INVENTORY_BY_FIELD = {
    "toolchain": {"cmake": "3.30.0", "compiler": "clang 19.1.0"},
    "dependencies": [
        {
            "name": "SDL2",
            "source": "https://example.invalid/sdl2",
            "revision": "abc123",
            "features": ["shared"],
        }
    ],
    "runtime_lookup": [{"name": "zlib", "path": "libz.so.1", "sha256": "b" * 64}],
    "notices": [{"path": "THIRD_PARTY_NOTICES.txt", "sha256": "c" * 64}],
    "symbols": [{"path": "KisakCOD-mp.pdb", "sha256": "d" * 64}],
}


def sha256_file(path: Path) -> str:
    """Return the hex sha256 digest of a file's bytes."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_tool(*args: str) -> ToolResult:
    """Invoke the tool's ``main`` in-process and capture its output streams."""
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        returncode = TOOL.main(list(args))
    return ToolResult(returncode, out.getvalue(), err.getvalue())


def write_inventory(path: Path, fields: list[str]) -> None:
    """Write an inventory JSON file for the requested field names."""
    inventory = {field: INVENTORY_BY_FIELD[field] for field in fields}
    path.write_text(json.dumps(inventory), encoding="utf-8")


def refresh_checksums(root: Path) -> None:
    """Rewrite SHA256SUMS.txt to cover exactly the current dist files."""
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    checksums = requirements["checksums"]
    dist = root / "dist"
    lines = []
    for item in sorted(dist.iterdir()):
        if item.is_file() and item.name != checksums:
            lines.append(f"{sha256_file(item)}  {item.name}\n")
    (dist / checksums).write_text("".join(lines), encoding="utf-8")


def _stage_identity_member(
    root: Path, source: dict, identity_member: bool, identity_text: str | None
) -> list[tuple[Path, str]]:
    """Write the identity JSON member into the staging tree and return it."""
    if not identity_member:
        return []
    identity = root / "source-tree" / source["identity_member"]
    if identity_text is not None:
        identity.parent.mkdir(parents=True, exist_ok=True)
        identity.write_text(identity_text, encoding="utf-8")
    elif not identity.is_file():
        result = run_tool(
            "identity-write", "--tag", TAG, "--commit", COMMIT, "--out", str(identity)
        )
        if result.returncode != 0:
            raise RuntimeError(f"identity-write failed: {result.stderr}")
    return [(identity, source["identity_member"])]


def _stage_carrier_member(
    staging: Path, source: dict, carrier_text: str | None, carrier_arcname: str | None
) -> list[tuple[Path, str]]:
    """Write the build-consumed identity carrier and return its entry."""
    if carrier_text is None:
        return []
    arcname = carrier_arcname or source["carrier"]
    carrier = staging / arcname
    carrier.parent.mkdir(parents=True, exist_ok=True)
    carrier.write_text(carrier_text, encoding="utf-8")
    return [(carrier, arcname)]


def _stage_extra_members(
    staging: Path, extra_members: list[tuple[str, str]]
) -> list[tuple[Path, str]]:
    """Write each caller-supplied extra archive member and return its entry."""
    entries: list[tuple[Path, str]] = []
    for member_name, content in extra_members:
        extra = staging / member_name
        extra.parent.mkdir(parents=True, exist_ok=True)
        extra.write_text(content, encoding="utf-8")
        entries.append((extra, member_name))
    return entries


def build_source_archive(
    root: Path,
    carrier_text: str | None,
    identity_member: bool = True,
    prefix: str = "",
    carrier_arcname: str | None = None,
    extra_members: list[tuple[str, str]] | None = None,
    identity_text: str | None = None,
) -> None:
    """(Re)build the source tarball with a controlled identity carrier."""
    # carrier_text=None omits the build-consumed src/source_identity.txt; an
    # explicit string lets a test ship an unsubstituted placeholder or a
    # conflicting commit. The identity JSON is optional so the missing-member
    # case is exercised too. carrier_arcname overrides where the carrier is
    # stored inside the archive so a test can prove an arbitrarily nested copy
    # is not accepted. extra_members adds non-identity members so a test can
    # mutate the archive while keeping its identity evidence valid.
    # identity_text ships a raw release-identity.json body instead of the
    # writer's output, so a test can pin a member whose schema/tag/commit
    # contract the archive verifier must reject.
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    source = requirements["source"]
    staging = root / "archive-staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    (staging / "CMakeLists.txt").write_text("project(KisakCOD)\n", encoding="utf-8")
    entries = [(staging / "CMakeLists.txt", "CMakeLists.txt")]
    entries.extend(_stage_identity_member(root, source, identity_member, identity_text))
    entries.extend(_stage_carrier_member(staging, source, carrier_text, carrier_arcname))
    entries.extend(_stage_extra_members(staging, extra_members or []))
    archive_path = root / "dist" / source["archive"].replace("${tag}", TAG)
    with tarfile.open(archive_path, "w:gz") as archive:
        for path, arcname in entries:
            archive.add(path, arcname=f"{prefix}{arcname}")


def record_source_manifest(root: Path) -> None:
    """Record the source manifest that binds the current archive digest."""
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    source = requirements["source"]
    archive_name = source["archive"].replace("${tag}", TAG)
    result = run_tool(
        "record",
        "--dist",
        str(root / "dist"),
        "--tag",
        TAG,
        "--commit",
        COMMIT,
        "--target",
        "source",
        "--config",
        "source",
        "--workflow-run",
        "12345",
        "--generated-at",
        "2026-01-01T00:00:00Z",
        "--artifact",
        archive_name,
        "--out",
        str(root / "dist" / source["provenance"].replace("${tag}", TAG)),
    )
    if result.returncode != 0:
        raise RuntimeError(f"source provenance record failed: {result.stderr}")


def build_release_fixture(root: Path) -> None:
    """Create a complete dist/ that satisfies the shipped release contract."""
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    dist = root / "dist"
    dist.mkdir(parents=True)

    for profile in requirements["profiles"]:
        for artifact in profile["artifacts"]:
            (dist / artifact).write_bytes(f"{profile['target']}:{artifact}\n".encode())
        inventory_path = root / f"inventory-{profile['target']}.json"
        write_inventory(inventory_path, profile.get("required_inventory") or [])
        result = run_tool(
            "record",
            "--dist",
            str(dist),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--target",
            profile["target"],
            "--config",
            profile["config"],
            "--workflow-run",
            "12345",
            "--generated-at",
            "2026-01-01T00:00:00Z",
            "--inventory",
            str(inventory_path),
            "--out",
            str(dist / profile["provenance"]),
            *[arg for artifact in profile["artifacts"] for arg in ("--artifact", artifact)],
        )
        if result.returncode != 0:
            raise RuntimeError(f"record failed for {profile['target']}: {result.stderr}")

    (root / "source-tree").mkdir()
    # The carrier git archive would substitute: the full verified commit.
    build_source_archive(root, f"commit={COMMIT}\n")
    record_source_manifest(root)
    refresh_checksums(root)

    prerequisites = {job: "success" for job in requirements["required_prerequisites"]}
    (root / "prerequisites.json").write_text(json.dumps(prerequisites), encoding="utf-8")


class ReleaseProvenanceTestBase(unittest.TestCase):
    """Base case owning the scratch tree and the shared verify helper."""

    maxDiff = None

    def setUp(self) -> None:
        self.tmp = Path(
            tempfile.mkdtemp(prefix="release-provenance-test-", dir=scratch_root())
        )
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.base = self.tmp / "base"
        self.base.mkdir()
        build_release_fixture(self.base)

    def fresh(self, name: str) -> Path:
        """Return a copy of the complete fixture under a fresh name."""
        target = self.tmp / name
        shutil.copytree(self.base, target)
        return target

    def verify(self, root: Path) -> ToolResult:
        """Run the tool's ``verify`` subcommand against a fixture root."""
        return run_tool(
            "verify",
            "--requirements",
            str(REQUIREMENTS),
            "--dist",
            str(root / "dist"),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--prerequisites",
            str(root / "prerequisites.json"),
        )

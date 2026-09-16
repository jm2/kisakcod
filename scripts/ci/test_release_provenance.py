#!/usr/bin/env python3
"""End-to-end tests for scripts/ci/release_provenance.py and the release contract.

Builds a complete synthetic release set that satisfies
``scripts/ci/release-requirements.json``, proves the verifier passes it, then
mutates one invariant at a time and proves the verifier fails closed.

Run directly (``python3 scripts/ci/test_release_provenance.py``) or through
ctest, which registers it next to the other portable contract tests.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
TOOL = SCRIPT_DIR / "release_provenance.py"
REQUIREMENTS = SCRIPT_DIR / "release-requirements.json"

TAG = "v9.9.9"
COMMIT = "a" * 40


def scratch_root() -> str | None:
    """Honor the repo scratch rule: prefer $TMPDIR, else /var/tmp, never /tmp."""
    candidate = os.environ.get("TMPDIR") or "/var/tmp"
    return candidate if os.path.isdir(candidate) else None
INVENTORY_BY_FIELD = {
    "toolchain": {"cmake": "3.30.0", "compiler": "clang 19.1.0"},
    "dependencies": [
        {"name": "SDL2", "source": "https://example.invalid/sdl2", "revision": "abc123", "features": ["shared"]}
    ],
    "runtime_lookup": [{"name": "zlib", "path": "libz.so.1", "sha256": "b" * 64}],
    "notices": [{"path": "THIRD_PARTY_NOTICES.txt", "sha256": "c" * 64}],
    "symbols": [{"path": "KisakCOD-mp.pdb", "sha256": "d" * 64}],
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_tool(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        capture_output=True,
        text=True,
    )


def write_inventory(path: Path, fields: list[str]) -> None:
    inventory = {field: INVENTORY_BY_FIELD[field] for field in fields}
    path.write_text(json.dumps(inventory), encoding="utf-8")


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
        assert result.returncode == 0, result.stderr

    source = requirements["source"]
    staging = root / "source-tree"
    staging.mkdir()
    identity = staging / source["identity_member"]
    result = run_tool(
        "identity-write",
        "--tag",
        TAG,
        "--commit",
        COMMIT,
        "--out",
        str(identity),
    )
    assert result.returncode == 0, result.stderr
    (staging / "CMakeLists.txt").write_text("project(KisakCOD)\n", encoding="utf-8")
    archive_name = source["archive"].replace("${tag}", TAG)
    with tarfile.open(dist / archive_name, "w:gz") as archive:
        archive.add(staging / "CMakeLists.txt", arcname="CMakeLists.txt")
        archive.add(identity, arcname=source["identity_member"])

    source_manifest = {
        "schema_version": 1,
        "tag": TAG,
        "commit": COMMIT,
        "target": "source",
        "config": "source",
        "workflow_run": "12345",
        "generated_at": "2026-01-01T00:00:00Z",
    }
    (dist / source["provenance"].replace("${tag}", TAG)).write_text(
        json.dumps(source_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    checksums = requirements["checksums"]
    lines = []
    for item in sorted(dist.iterdir()):
        if item.is_file() and item.name != checksums:
            lines.append(f"{sha256_file(item)}  {item.name}\n")
    (dist / checksums).write_text("".join(lines), encoding="utf-8")

    prerequisites = {job: "success" for job in requirements["required_prerequisites"]}
    (root / "prerequisites.json").write_text(json.dumps(prerequisites), encoding="utf-8")


class ReleaseProvenanceTests(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="release-provenance-test-", dir=scratch_root()))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.base = self.tmp / "base"
        self.base.mkdir()
        build_release_fixture(self.base)

    def fresh(self, name: str) -> Path:
        target = self.tmp / name
        shutil.copytree(self.base, target)
        return target

    def verify(self, root: Path) -> subprocess.CompletedProcess:
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

    # -- positive -----------------------------------------------------------

    def test_complete_release_passes(self) -> None:
        result = self.verify(self.base)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_shipped_contract_targets_are_unique(self) -> None:
        requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
        targets = [profile["target"] for profile in requirements["profiles"]]
        self.assertEqual(len(targets), len(set(targets)))

    # -- completeness -------------------------------------------------------

    def test_missing_expected_artifact_fails(self) -> None:
        root = self.fresh("missing-artifact")
        (root / "dist" / "KisakCOD-windows-x86.zip").unlink()
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("completeness", result.stderr)

    def test_undeclared_extra_file_fails(self) -> None:
        root = self.fresh("extra-file")
        (root / "dist" / "KisakCOD-surprise.tar.gz").write_text("extra", encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not part of the expected published set", result.stderr)

    def test_manifest_declaring_fewer_artifacts_fails(self) -> None:
        root = self.fresh("manifest-underdeclares")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["artifacts"] = []
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)

    # -- integrity ----------------------------------------------------------

    def test_tampered_artifact_fails(self) -> None:
        root = self.fresh("tampered")
        artifact = root / "dist" / "KisakCOD-windows-x86-sp.zip"
        artifact.write_bytes(artifact.read_bytes() + b"tamper")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match", result.stderr)

    def test_checksums_missing_entry_fails(self) -> None:
        root = self.fresh("checksums-missing")
        checksums = root / "dist" / "SHA256SUMS.txt"
        lines = checksums.read_text(encoding="utf-8").splitlines(keepends=True)
        checksums.write_text("".join(lines[1:]), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not cover", result.stderr)

    def test_empty_checksums_fails(self) -> None:
        root = self.fresh("checksums-empty")
        (root / "dist" / "SHA256SUMS.txt").write_text("", encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("is empty", result.stderr)

    # -- provenance identity ------------------------------------------------

    def test_manifest_with_wrong_commit_fails(self) -> None:
        root = self.fresh("wrong-commit")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["commit"] = "f" * 40
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match verified release", result.stderr)

    def test_missing_inventory_field_fails(self) -> None:
        root = self.fresh("missing-inventory")
        manifest = root / "dist" / "KisakCOD-windows-x86-nosteam-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        del data["notices"]
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_source_archive_without_identity_fails(self) -> None:
        root = self.fresh("source-no-identity")
        archive_path = root / "dist" / f"KisakCOD-{TAG}-source.tar.gz"
        with tarfile.open(archive_path, "w:gz") as archive:
            member = root / "replacement.txt"
            member.write_text("no identity here\n", encoding="utf-8")
            archive.add(member, arcname="replacement.txt")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not contain release-identity.json", result.stderr)

    # -- prerequisites ------------------------------------------------------

    def test_failed_prerequisite_fails(self) -> None:
        root = self.fresh("failed-prereq")
        path = root / "prerequisites.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["build-source"] = "failure"
        path.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build-source", result.stderr)

    def test_missing_prerequisites_file_fails(self) -> None:
        root = self.fresh("no-prereqs")
        result = run_tool(
            "verify",
            "--requirements",
            str(REQUIREMENTS),
            "--dist",
            str(root / "dist"),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("no --prerequisites", result.stderr)

    # -- identity round-trip ------------------------------------------------

    def test_identity_verify_detects_mismatch(self) -> None:
        identity = self.base / "source-tree" / "release-identity.json"
        ok = run_tool("identity-verify", "--identity", str(identity), "--tag", TAG, "--commit", COMMIT)
        self.assertEqual(ok.returncode, 0, ok.stderr)
        bad = run_tool(
            "identity-verify", "--identity", str(identity), "--tag", TAG, "--commit", "0" * 40
        )
        self.assertEqual(bad.returncode, 1)
        self.assertIn("does not match", bad.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

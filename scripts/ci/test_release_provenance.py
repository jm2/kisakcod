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


def build_source_archive(
    root: Path,
    carrier_text: str | None,
    identity_member: bool = True,
    prefix: str = "",
    carrier_arcname: str | None = None,
) -> None:
    """(Re)build the source tarball with a controlled identity carrier.

    ``carrier_text=None`` omits the build-consumed ``src/source_identity.txt``;
    an explicit string lets a test ship an unsubstituted placeholder or a
    conflicting commit. The identity JSON is optional so the missing-member
    case is exercised too. ``carrier_arcname`` overrides where the carrier is
    stored inside the archive so a test can prove an arbitrarily nested copy is
    not accepted.
    """
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    source = requirements["source"]
    staging = root / "archive-staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    (staging / "CMakeLists.txt").write_text("project(KisakCOD)\n", encoding="utf-8")
    entries = [(staging / "CMakeLists.txt", "CMakeLists.txt")]
    if identity_member:
        identity = root / "source-tree" / source["identity_member"]
        if not identity.is_file():
            result = run_tool(
                "identity-write", "--tag", TAG, "--commit", COMMIT, "--out", str(identity)
            )
            assert result.returncode == 0, result.stderr
        entries.append((identity, source["identity_member"]))
    if carrier_text is not None:
        arcname = carrier_arcname or source["carrier"]
        carrier = staging / arcname
        carrier.parent.mkdir(parents=True, exist_ok=True)
        carrier.write_text(carrier_text, encoding="utf-8")
        entries.append((carrier, arcname))
    archive_path = root / "dist" / source["archive"].replace("${tag}", TAG)
    with tarfile.open(archive_path, "w:gz") as archive:
        for path, arcname in entries:
            archive.add(path, arcname=f"{prefix}{arcname}")


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
    (root / "source-tree").mkdir()
    # The carrier git archive would substitute: the full verified commit.
    build_source_archive(root, f"commit={COMMIT}\n")

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

    refresh_checksums(root)

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
        build_source_archive(root, carrier_text=None, identity_member=False)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not contain release-identity.json", result.stderr)

    def test_source_archive_without_build_carrier_fails(self) -> None:
        # The JSON identity member is present but the file the build actually
        # reads without .git is absent: the archive must not pass.
        root = self.fresh("source-no-carrier")
        build_source_archive(root, carrier_text=None)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build-consumed identity carrier", result.stderr)

    def test_source_archive_unexpanded_carrier_fails(self) -> None:
        root = self.fresh("source-unexpanded")
        build_source_archive(root, carrier_text="commit=$Format:%H$\n")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unsubstituted", result.stderr)

    def test_source_archive_conflicting_carrier_fails(self) -> None:
        root = self.fresh("source-conflict")
        build_source_archive(root, carrier_text=f"commit={'e' * 40}\n")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match verified release", result.stderr)

    def test_source_archive_short_commit_carrier_fails(self) -> None:
        root = self.fresh("source-short-commit")
        build_source_archive(root, carrier_text="commit=abc1234\n")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not a full 40-hex commit", result.stderr)

    def test_source_archive_indented_carrier_fails(self) -> None:
        # The resolver matches `^commit=` at column zero (file(STRINGS ... REGEX
        # "^commit=")), so a leading space is not a usable identity. The verifier
        # must not accept a carrier the build cannot consume.
        root = self.fresh("source-indented-carrier")
        build_source_archive(root, carrier_text=f"  commit={COMMIT}\n")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("carries no commit= line", result.stderr)

    def test_source_archive_nested_carrier_fails(self) -> None:
        # The resolver reads <source_dir>/src/source_identity.txt after
        # extraction; a deeper nested copy is never placed there, so matching by
        # trailing components alone would certify an archive the build reads no
        # identity from.
        root = self.fresh("source-nested-carrier")
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            carrier_arcname="extra/nested/src/source_identity.txt",
        )
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build-consumed identity carrier", result.stderr)

    def test_prefixed_source_archive_passes(self) -> None:
        # git archive may prefix the tree with a top-level directory; the
        # carrier is matched at <prefix>/src/source_identity.txt.
        root = self.fresh("source-prefixed")
        build_source_archive(root, carrier_text=f"commit={COMMIT}\n", prefix=f"KisakCOD-{TAG}/")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_extracted_source_archive_resolves_verified_identity(self) -> None:
        # End-to-end: extract the shipped archive and prove the resolver the
        # build uses reads the verified commit back without any .git.
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("cmake is not available to prove the resolver")
        archive_path = self.base / "dist" / f"KisakCOD-{TAG}-source.tar.gz"
        extracted = self.tmp / "extracted-source"
        with tarfile.open(archive_path, "r:*") as archive:
            archive.extractall(extracted)
        self.assertTrue((extracted / "src" / "source_identity.txt").is_file())
        resolver = SCRIPT_DIR.parent / "extern" / "resolve_source_identity.cmake"
        self.assertTrue(resolver.is_file(), resolver)
        script = self.tmp / "resolve-identity.cmake"
        script.write_text(
            'include("%s")\n'
            'kisak_resolve_source_identity("%s" _resolved)\n'
            'if(NOT _resolved STREQUAL "%s")\n'
            '  message(FATAL_ERROR "resolved ${_resolved}, expected %s")\n'
            "endif()\n" % (resolver, extracted, COMMIT, COMMIT),
            encoding="utf-8",
        )
        result = subprocess.run(
            [cmake, "-P", str(script)], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_indented_carrier_alignment_with_resolver(self) -> None:
        # End-to-end regression for the refinery finding: the verifier must not
        # report success on a carrier the exact-head resolver reads as empty.
        # Build an indented carrier, prove verification fails, extract it, and
        # prove the resolver the build uses resolves nothing.
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("cmake is not available to prove the resolver")
        root = self.fresh("source-indented-alignment")
        build_source_archive(root, carrier_text=f"  commit={COMMIT}\n")
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("carries no commit= line", result.stderr)

        archive_path = root / "dist" / f"KisakCOD-{TAG}-source.tar.gz"
        extracted = self.tmp / "indented-extracted-source"
        with tarfile.open(archive_path, "r:*") as archive:
            archive.extractall(extracted)
        resolver = SCRIPT_DIR.parent / "extern" / "resolve_source_identity.cmake"
        script = self.tmp / "resolve-indented-identity.cmake"
        script.write_text(
            'include("%s")\n'
            'kisak_resolve_source_identity("%s" _resolved)\n'
            'if(NOT _resolved STREQUAL "")\n'
            '  message(FATAL_ERROR "resolver accepted indented carrier: ${_resolved}")\n'
            "endif()\n" % (resolver, extracted),
            encoding="utf-8",
        )
        proc = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    # -- inventory typing ---------------------------------------------------

    def test_boolean_inventory_value_fails(self) -> None:
        # A manifest that claims required inventory with `false` is not evidence.
        root = self.fresh("inventory-false")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["notices"] = False
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_zero_inventory_value_fails(self) -> None:
        root = self.fresh("inventory-zero")
        manifest = root / "dist" / "KisakCOD-windows-x86-sp-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["symbols"] = 0
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_empty_object_inventory_value_fails(self) -> None:
        root = self.fresh("inventory-empty-object")
        manifest = root / "dist" / "KisakCOD-windows-x86-nosteam-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["runtime_lookup"] = {}
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_incomplete_dependency_record_fails(self) -> None:
        root = self.fresh("inventory-incomplete-dependency")
        manifest = root / "dist" / "KisakCOD-windows-x86-headless-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        del data["dependencies"][0]["features"]
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("features", result.stderr)

    def test_wrong_typed_inventory_record_fails(self) -> None:
        root = self.fresh("inventory-wrong-type")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["dependencies"] = ["SDL2"]
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("required inventory", result.stderr)

    def test_record_rejects_invalid_inventory(self) -> None:
        # The producer fails closed too: a boolean inventory field never reaches
        # a manifest in the first place.
        root = self.tmp / "record-invalid-inventory"
        dist = root / "dist"
        dist.mkdir(parents=True)
        (dist / "KisakCOD-windows-x86.zip").write_bytes(b"artifact")
        inventory = root / "inventory.json"
        inventory.write_text(json.dumps({"toolchain": False}), encoding="utf-8")
        result = run_tool(
            "record",
            "--dist",
            str(dist),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--target",
            "windows-x86",
            "--config",
            "production-mp-dedi",
            "--inventory",
            str(inventory),
            "--out",
            str(root / "manifest.json"),
            "--artifact",
            "KisakCOD-windows-x86.zip",
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid", result.stderr)

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
        ok = run_tool(
            "identity-verify",
            "--identity",
            str(identity),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
        )
        self.assertEqual(ok.returncode, 0, ok.stderr)
        bad = run_tool(
            "identity-verify", "--identity", str(identity), "--tag", TAG, "--commit", "0" * 40
        )
        self.assertEqual(bad.returncode, 1)
        self.assertIn("does not match", bad.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

#!/usr/bin/env python3
"""End-to-end tests for scripts/ci/release_provenance.py and the release contract."""

# Builds a complete synthetic release set that satisfies
# scripts/ci/release-requirements.json, proves the verifier passes it, then
# mutates one invariant at a time and proves the verifier fails closed.
#
# This module is the entry point registered with ctest. The source-archive and
# inventory suites were split into sibling modules to keep each file's
# non-comment line count under the analyzer's per-file limit; they are imported
# statically below so build_suite() assembles one runnable suite and a direct
# run still executes the whole contract. Run directly
# (``python3 scripts/ci/test_release_provenance.py``) or through ctest.

from __future__ import annotations

import json
import sys
import unittest

import test_release_provenance_archive
import test_release_provenance_archive_version
import test_release_provenance_inventory

from release_provenance_testlib import (
    COMMIT,
    REQUIREMENTS,
    TAG,
    ReleaseProvenanceTestBase,
    run_tool,
)


class ReleaseProvenanceTests(ReleaseProvenanceTestBase):
    """Release completeness, integrity, identity and CLI-contract cases."""

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

    # -- malformed input ----------------------------------------------------

    def test_invalid_utf8_manifest_is_controlled_gate_error(self) -> None:
        # A manifest opened in binary mode with a non-UTF-8 body must surface as
        # a controlled gate failure, not an uncaught traceback.
        root = self.fresh("invalid-utf8-manifest")
        manifest = root / "dist" / "KisakCOD-windows-x86-provenance.json"
        manifest.write_bytes(b"\xff\xfe\x00")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid JSON", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

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

    def test_identity_verify_rejects_missing_schema_version(self) -> None:
        # The CLI and archive paths share one field contract: an identity
        # lacking schema_version must be rejected by both.
        root = self.tmp / "identity-no-schema"
        root.mkdir()
        identity = root / "release-identity.json"
        identity.write_text(
            json.dumps({"tag": TAG, "commit": COMMIT, "version": TAG.lstrip("v")}),
            encoding="utf-8",
        )
        result = run_tool(
            "identity-verify",
            "--identity",
            str(identity),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("schema_version", result.stderr)

    def test_identity_explicit_version_round_trip(self) -> None:
        # identity-write accepts an explicit --version; identity-verify must
        # accept the very identity the writer produced when given that version.
        root = self.tmp / "identity-explicit-version"
        root.mkdir()
        identity = root / "release-identity.json"
        written = run_tool(
            "identity-write",
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--version",
            "custom",
            "--out",
            str(identity),
        )
        self.assertEqual(written.returncode, 0, written.stderr)
        record = json.loads(identity.read_text(encoding="utf-8"))
        self.assertEqual(record["version"], "custom")

        ok = run_tool(
            "identity-verify",
            "--identity",
            str(identity),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--version",
            "custom",
        )
        self.assertEqual(ok.returncode, 0, ok.stderr)

        # Without the explicit version the tag-derived default is required.
        default = run_tool(
            "identity-verify", "--identity", str(identity), "--tag", TAG, "--commit", COMMIT
        )
        self.assertEqual(default.returncode, 1)
        self.assertIn("version", default.stderr)

        # A mismatched explicit version must fail closed.
        mismatch = run_tool(
            "identity-verify",
            "--identity",
            str(identity),
            "--tag",
            TAG,
            "--commit",
            COMMIT,
            "--version",
            "other",
        )
        self.assertEqual(mismatch.returncode, 1)
        self.assertIn("version", mismatch.stderr)


def build_suite() -> unittest.TestSuite:
    """Load this module and its split sibling suites into one runnable suite."""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    suite.addTests(loader.loadTestsFromModule(test_release_provenance_archive))
    suite.addTests(loader.loadTestsFromModule(test_release_provenance_archive_version))
    suite.addTests(loader.loadTestsFromModule(test_release_provenance_inventory))
    return suite


if __name__ == "__main__":
    outcome = unittest.TextTestRunner(verbosity=2).run(build_suite())
    sys.exit(0 if outcome.wasSuccessful() else 1)

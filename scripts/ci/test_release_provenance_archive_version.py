#!/usr/bin/env python3
"""Source-archive version-identity contract tests."""

# Split from test_release_provenance_archive.py to keep each file's
# non-comment line count under the analyzer's per-file limit. These cases pin
# the version half of the archive identity contract: the shipped
# release-identity.json member must carry the effective version (the explicit
# verify --version when supplied, otherwise the tag-derived default), so an
# archive whose schema/tag/commit all match but whose version is wrong or
# missing fails the archive-specific identity gate exactly as the standalone
# identity-verify fails. The entry point in test_release_provenance.py loads
# this module through build_suite().

from __future__ import annotations

import json
import unittest
from pathlib import Path

from release_provenance_testlib import (
    COMMIT,
    REQUIREMENTS,
    TAG,
    ReleaseProvenanceTestBase,
    ToolResult,
    build_source_archive,
    record_source_manifest,
    refresh_checksums,
    run_tool,
)


def identity_body(version: str | None) -> str:
    """Return a release-identity.json body with every field but version valid."""
    record = {"schema_version": 1, "tag": TAG, "commit": COMMIT}
    if version is not None:
        record["version"] = version
    return json.dumps(record)


class SourceArchiveVersionTests(ReleaseProvenanceTestBase):
    """Version half of the archive identity member contract."""

    def verify_extra(self, root: Path, *extra: str) -> ToolResult:
        """Run verify with additional CLI flags after the standard ones."""
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
            *extra,
        )

    def rebuild_consistent(self, root: Path, version: str | None) -> None:
        # Rebuild the archive around a controlled identity member, then
        # regenerate the archive digest, provenance manifest and checksums so
        # the version field is the only thing the case can be failing on.
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            identity_text=identity_body(version),
        )
        record_source_manifest(root)
        refresh_checksums(root)

    # -- negatives -----------------------------------------------------------

    def test_source_archive_identity_wrong_version_fails(self) -> None:
        root = self.fresh("source-identity-wrong-version")
        self.rebuild_consistent(root, "0.0.1")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("version", result.stderr)
        self.assertIn("does not match expected", result.stderr)
        self.assertIn("'0.0.1'", result.stderr)
        self.assertIn(f"'{TAG.lstrip('v')}'", result.stderr)

    def test_source_archive_identity_missing_version_fails(self) -> None:
        root = self.fresh("source-identity-missing-version")
        self.rebuild_consistent(root, None)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("version", result.stderr)
        self.assertIn("does not match expected", result.stderr)
        self.assertIn("None", result.stderr)

    def test_source_archive_explicit_version_mismatch_fails(self) -> None:
        # The explicit override must actually be honored: a member carrying an
        # override version still fails when verify is given a different one.
        root = self.fresh("source-explicit-version-mismatch")
        self.rebuild_consistent(root, "custom")
        result = self.verify_extra(root, "--version", "other")
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match expected", result.stderr)

    # -- positives -----------------------------------------------------------

    def test_source_archive_default_version_passes(self) -> None:
        # The tag-derived default positive: a writer-default identity member
        # keeps the whole rebuilt release verifiable without --version.
        root = self.fresh("source-default-version")
        self.rebuild_consistent(root, TAG.lstrip("v"))
        result = self.verify(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_archive_explicit_version_override_passes(self) -> None:
        # identity-write supports an explicit --version override; verify must
        # accept the same override for the archive identity member.
        root = self.fresh("source-explicit-version")
        self.rebuild_consistent(root, "custom")
        result = self.verify_extra(root, "--version", "custom")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

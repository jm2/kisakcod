#!/usr/bin/env python3
"""Source-archive identity and manifest-binding contract tests."""

# Split out of test_release_provenance.py to keep each file's non-comment line
# count under the analyzer's per-file limit. The entry point in
# test_release_provenance.py loads this module through build_suite().

from __future__ import annotations

import json
import unittest

from release_provenance_testlib import (
    COMMIT,
    TAG,
    ReleaseProvenanceTestBase,
    build_source_archive,
    record_source_manifest,
    refresh_checksums,
)


class SourceArchiveIdentityTests(ReleaseProvenanceTestBase):
    """Archive member identity, carrier grammar and manifest binding cases."""

    # -- source archive identity --------------------------------------------

    def test_source_archive_without_identity_fails(self) -> None:
        root = self.fresh("source-no-identity")
        build_source_archive(root, carrier_text=None, identity_member=False)
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not contain release-identity.json", result.stderr)

    def test_source_archive_identity_missing_schema_fails(self) -> None:
        # The archive member must satisfy the same schema/tag/commit contract as
        # the CLI identity-verify path; a missing schema_version used to pass
        # because only tag/commit were checked.
        root = self.fresh("source-identity-no-schema")
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            identity_text=json.dumps({"tag": TAG, "commit": COMMIT, "version": TAG.lstrip("v")}),
        )
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("schema_version", result.stderr)

    def test_source_archive_identity_wrong_schema_fails(self) -> None:
        root = self.fresh("source-identity-bad-schema")
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            identity_text=json.dumps(
                {
                    "schema_version": 2,
                    "tag": TAG,
                    "commit": COMMIT,
                    "version": TAG.lstrip("v"),
                }
            ),
        )
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("schema_version", result.stderr)

    def test_source_archive_identity_wrong_commit_fails(self) -> None:
        # The archive member tags the wrong commit although the build carrier is
        # correct: the JSON identity is still disconnected from the release.
        root = self.fresh("source-identity-wrong-commit")
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            identity_text=json.dumps(
                {
                    "schema_version": 1,
                    "tag": TAG,
                    "commit": "e" * 40,
                    "version": TAG.lstrip("v"),
                }
            ),
        )
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match verified release", result.stderr)

    def test_source_archive_without_build_carrier_fails(self) -> None:
        # The JSON identity member is present but the file the build actually
        # reads without .git is absent: the archive must not pass.
        root = self.fresh("source-no-carrier")
        build_source_archive(root, carrier_text=None)
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build-consumed identity carrier", result.stderr)

    def test_source_archive_unexpanded_carrier_fails(self) -> None:
        root = self.fresh("source-unexpanded")
        build_source_archive(root, carrier_text="commit=$Format:%H$\n")
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unsubstituted", result.stderr)

    def test_source_archive_conflicting_carrier_fails(self) -> None:
        root = self.fresh("source-conflict")
        build_source_archive(root, carrier_text=f"commit={'e' * 40}\n")
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match verified release", result.stderr)

    def test_source_archive_short_commit_carrier_fails(self) -> None:
        root = self.fresh("source-short-commit")
        build_source_archive(root, carrier_text="commit=abc1234\n")
        record_source_manifest(root)
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
        record_source_manifest(root)
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
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("build-consumed identity carrier", result.stderr)

    def test_prefixed_source_archive_passes(self) -> None:
        # git archive may prefix the tree with a top-level directory; the
        # carrier is matched at <prefix>/src/source_identity.txt.
        root = self.fresh("source-prefixed")
        build_source_archive(root, carrier_text=f"commit={COMMIT}\n", prefix=f"KisakCOD-{TAG}/")
        record_source_manifest(root)
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 0, result.stderr)

    # -- source manifest binding --------------------------------------------

    def test_source_manifest_without_archive_binding_fails(self) -> None:
        # The source manifest must declare exactly the source archive; an empty
        # artifact list leaves the archive unbound to its provenance.
        root = self.fresh("source-unbound")
        manifest = root / "dist" / f"KisakCOD-{TAG}-source-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["artifacts"] = []
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("declares no artifacts", result.stderr)

    def test_source_manifest_extra_artifact_fails(self) -> None:
        root = self.fresh("source-extra-artifact")
        manifest = root / "dist" / f"KisakCOD-{TAG}-source-provenance.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["artifacts"].append({"path": "KisakCOD-unexpected.tar.gz", "sha256": "0" * 64})
        manifest.write_text(json.dumps(data), encoding="utf-8")
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("undeclared artifact(s)", result.stderr)

    def test_source_archive_member_mutation_fails_stale_provenance(self) -> None:
        # Mutating another archive member and regenerating SHA256SUMS.txt must
        # still fail: the source provenance manifest binds the original archive
        # digest, so stale provenance cannot certify the mutated bytes.
        root = self.fresh("source-member-mutation")
        build_source_archive(
            root,
            carrier_text=f"commit={COMMIT}\n",
            extra_members=[("notes.txt", "mutated after provenance\n")],
        )
        refresh_checksums(root)
        result = self.verify(root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match manifest", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)

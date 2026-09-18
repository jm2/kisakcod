#!/usr/bin/env python3
"""Delivery-evidence contract tests for the capability dashboard."""

# Run with:  python3 scripts/ci/test_capability_dashboard_delivery.py
#
# These tests exercise the promoted-row evidence contract (#126): a capability
# may only count toward delivery when it claims the packaged level with an exact
# SHA, a run id and an explicit successful package result, and a validated
# commercial reference must carry its own provenance.  They are split out of
# test_capability_dashboard.py so each module stays a cohesive, readable size.

from __future__ import annotations

import copy
import importlib.util
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"
REPO_ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


class DeliveryEvidenceContractTests(unittest.TestCase):
    """A promoted row needs complete, correctly typed evidence (#126)."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _fully_promoted(self, package_result):
        """Promote every requested-target row and validate both references."""
        # ``package_result`` is the only knob; callers set it to the explicit
        # success token, a failure/pending token, or an arbitrary string.
        manifest = copy.deepcopy(self.manifest)
        for reference in manifest["commercial_references"]:
            reference["status"] = "validated"
            reference["sha256"] = "a" * 64
            reference["evidence_run"] = "release-1"
        for cap in manifest["capabilities"]:
            if cap["target"] == "all-requested":
                continue
            cap["strongest_validation"] = "packaged_clean_machine"
            cap["production_enrolled"] = True
            cap["evidence"]["sha"] = "b" * 40
            cap["evidence"]["run"] = "12345"
            cap["evidence"]["package_result"] = package_result
        return manifest

    def test_fully_evidenced_delivery_is_accepted(self):
        manifest = self._fully_promoted(cd.PACKAGE_RESULT_SUCCESS)
        self.assertEqual(cd.validate_manifest(manifest), [])
        result = cd.compute_aggregate(manifest)
        self.assertTrue(result["references_ok"])
        self.assertEqual(result["delivered"], 5)

    def test_refinery_reproduction_cannot_manufacture_delivery(self):
        # Exact refinery reproduction at 34f24667: references validated, rows
        # promoted with production_enrolled=true and
        # strongest_validation=packaged_clean_machine, but
        # evidence.package_result="failed" and null sha/run.
        broken = copy.deepcopy(self.manifest)
        for reference in broken["commercial_references"]:
            reference["status"] = "validated"
        for cap in broken["capabilities"]:
            cap["production_enrolled"] = True
            cap["strongest_validation"] = "packaged_clean_machine"
            cap["evidence"]["package_result"] = "failed"
        errors = cd.validate_manifest(broken)
        self.assertTrue(errors)
        result = cd.compute_aggregate(broken)
        self.assertEqual(result["requested"], 5)
        self.assertEqual(result["delivered"], 0)
        self.assertFalse(result["references_ok"])

    def test_failed_package_result_blocks_delivery(self):
        manifest = self._fully_promoted("failed")
        errors = cd.validate_manifest(manifest)
        self.assertTrue(any("package_result" in error for error in errors))
        self.assertEqual(cd.compute_aggregate(manifest)["delivered"], 0)

    def test_pending_package_result_blocks_delivery(self):
        manifest = self._fully_promoted("pending")
        errors = cd.validate_manifest(manifest)
        self.assertTrue(any("package_result" in error for error in errors))
        self.assertEqual(cd.compute_aggregate(manifest)["delivered"], 0)

    def test_arbitrary_package_string_is_not_success(self):
        # A truthy non-token string (a hash, "ok", a path) must not count.
        for arbitrary in ("sha256:fixture", "ok", "0", "archive.tar.gz"):
            with self.subTest(value=arbitrary):
                manifest = self._fully_promoted(arbitrary)
                errors = cd.validate_manifest(manifest)
                self.assertTrue(
                    any("package_result" in error for error in errors),
                    msg=f"arbitrary package_result {arbitrary!r} was accepted",
                )
                self.assertEqual(
                    cd.compute_aggregate(manifest)["delivered"],
                    0,
                    msg=f"arbitrary package_result {arbitrary!r} delivered work",
                )

    def test_promoted_capability_requires_exact_sha_and_run(self):
        manifest = self._fully_promoted(cd.PACKAGE_RESULT_SUCCESS)
        for cap in manifest["capabilities"]:
            if cap["target"] == "all-requested":
                continue
            cap["evidence"]["sha"] = None
            cap["evidence"]["run"] = None
        errors = cd.validate_manifest(manifest)
        self.assertTrue(any("evidence.sha" in error for error in errors))
        self.assertTrue(any("evidence.run" in error for error in errors))
        self.assertEqual(cd.compute_aggregate(manifest)["delivered"], 0)

    def test_validated_reference_requires_provenance(self):
        broken = copy.deepcopy(self.manifest)
        for reference in broken["commercial_references"]:
            # validated but sha256/evidence_run remain null
            reference["status"] = "validated"
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("sha256" in error for error in errors))
        self.assertTrue(any("evidence_run" in error for error in errors))
        result = cd.compute_aggregate(broken)
        self.assertFalse(result["references_ok"])
        self.assertEqual(result["delivered"], 0)

    def test_reordered_validation_levels_cannot_promote(self):
        # Move a weak level to the end of the editable enum.  Ranking derived
        # from manifest order would treat it as the strongest level; the fixed
        # module rank must not.
        reordered = copy.deepcopy(self.manifest)
        weak = "linked_production"
        levels = [
            level
            for level in reordered["enums"]["validation_levels"]
            if level != weak
        ]
        levels.append(weak)
        reordered["enums"]["validation_levels"] = levels
        for reference in reordered["commercial_references"]:
            reference["status"] = "validated"
            reference["sha256"] = "a" * 64
            reference["evidence_run"] = "release-1"
        for cap in reordered["capabilities"]:
            if cap["target"] == "all-requested":
                continue
            cap["strongest_validation"] = weak
            cap["production_enrolled"] = True
            cap["evidence"]["sha"] = "b" * 40
            cap["evidence"]["run"] = "12345"
            cap["evidence"]["package_result"] = cd.PACKAGE_RESULT_SUCCESS
        # The declared level *set* is unchanged, so the manifest is still
        # schema-valid ...
        self.assertEqual(cd.validate_manifest(reordered), [])
        # ... but the reordering does not make the weak level the threshold.
        result = cd.compute_aggregate(reordered)
        self.assertTrue(result["references_ok"])
        self.assertEqual(result["delivered"], 0)

    def test_validation_level_set_is_fixed(self):
        added = copy.deepcopy(self.manifest)
        added["enums"]["validation_levels"].append("wishful_thinking")
        self.assertTrue(
            any("unknown levels" in error for error in cd.validate_manifest(added))
        )
        removed = copy.deepcopy(self.manifest)
        removed["enums"]["validation_levels"].remove("linked_production")
        self.assertTrue(
            any(
                "missing canonical levels" in error
                for error in cd.validate_manifest(removed)
            )
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)

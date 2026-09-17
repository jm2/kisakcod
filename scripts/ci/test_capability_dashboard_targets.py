#!/usr/bin/env python3
"""Regression tests for the pinned requested-target contract and row identity."""

# Run with:  python3 scripts/ci/test_capability_dashboard_targets.py
#
# These tests pin two review findings on PR #150: the complete five-target
# requested set is fixed outside the editable manifest (disabling requested
# flags used to shrink the aggregate to requested=1), and a capability row is
# identified by its (target, mode) pair so a copied row with a new id cannot
# make delivery order-dependent.  They are stdlib-only so any hosted Python can
# run them.

from __future__ import annotations

import copy
import importlib.util
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"
REPO_ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_targets_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


class MandatoryRequestedTargetTests(unittest.TestCase):
    """The five requested targets are pinned outside the editable manifest."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def test_pinned_set_is_the_five_delivered_targets(self):
        self.assertEqual(
            tuple(cd.MANDATORY_REQUESTED_TARGET_IDS),
            (
                "windows-amd64",
                "windows-arm64",
                "linux-amd64",
                "linux-arm64",
                "macos-arm64",
            ),
        )

    def test_disabling_all_but_one_requested_target_is_rejected(self):
        # Exact reproduction: setting every target but one to requested=false
        # validated cleanly and shrank the aggregate to requested=1.  The pinned
        # set must reject that and still report five requested targets.
        broken = copy.deepcopy(self.manifest)
        for target in broken["targets"]:
            target["requested"] = target["id"] == "macos-arm64"
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("mandatory and must be requested" in error for error in errors),
            msg=f"shrunk requested set was accepted: {errors}",
        )
        result = cd.compute_aggregate(broken)
        self.assertEqual(result["requested"], 5)
        self.assertEqual(result["delivered"], 0)

    def test_disabling_a_single_mandatory_target_is_rejected(self):
        for target_id in cd.MANDATORY_REQUESTED_TARGET_IDS:
            with self.subTest(target=target_id):
                broken = copy.deepcopy(self.manifest)
                for target in broken["targets"]:
                    if target["id"] == target_id:
                        target["requested"] = False
                errors = cd.validate_manifest(broken)
                self.assertTrue(
                    any(target_id in error for error in errors),
                    msg=f"disabled {target_id!r} was accepted: {errors}",
                )

    def test_removing_a_mandatory_target_is_rejected(self):
        broken = copy.deepcopy(self.manifest)
        broken["targets"] = [
            target
            for target in broken["targets"]
            if target["id"] != "linux-arm64"
        ]
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("linux-arm64" in error for error in errors),
            msg=f"missing mandatory target was accepted: {errors}",
        )
        # The pinned aggregate still reports five rows; the absent one cannot
        # be delivered.
        result = cd.compute_aggregate(broken)
        self.assertEqual(result["requested"], 5)
        self.assertEqual(result["delivered"], 0)

    def test_extra_requested_target_is_rejected(self):
        broken = copy.deepcopy(self.manifest)
        for target in broken["targets"]:
            if target["id"] == "windows-x86":
                target["requested"] = True
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("non-mandatory" in error for error in errors),
            msg=f"extra requested target was accepted: {errors}",
        )


class CapabilityRowIdentityTests(unittest.TestCase):
    """A capability row is unique per (target, mode) pair, in any order."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _duplicated(self, capability_id, position):
        broken = copy.deepcopy(self.manifest)
        original = next(
            cap
            for cap in broken["capabilities"]
            if cap["id"] == capability_id
        )
        duplicate = copy.deepcopy(original)
        duplicate["id"] = f"{capability_id}-copy"
        if position == "prepend":
            broken["capabilities"].insert(0, duplicate)
        else:
            broken["capabilities"].append(duplicate)
        return broken

    def test_appended_duplicate_pair_is_rejected(self):
        # A copied capability with a new id but the same target/mode used to
        # validate cleanly, leaving _find_capability to pick one row.
        broken = self._duplicated("windows-amd64-mp-client", "append")
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("duplicate target/mode pair" in error for error in errors),
            msg=f"duplicate target/mode row was accepted: {errors}",
        )

    def test_prepended_duplicate_pair_is_rejected_either_order(self):
        broken = self._duplicated("linux-amd64-headless-server", "prepend")
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("duplicate target/mode pair" in error for error in errors),
            msg=f"order-dependent duplicate row was accepted: {errors}",
        )

    def test_unique_target_mode_pairs_are_still_accepted(self):
        # Two rows may share a target across different modes, and a mode across
        # different targets; only the identical pair is ambiguous.
        broken = copy.deepcopy(self.manifest)
        for cap in broken["capabilities"]:
            if cap["id"] == "windows-amd64-mp-client":
                cap["mode"] = "sp"
        self.assertEqual(cd.validate_manifest(broken), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)

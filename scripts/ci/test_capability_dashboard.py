#!/usr/bin/env python3
"""Unit tests for scripts/ci/capability-dashboard.py."""

# Run with:  python3 scripts/ci/test_capability_dashboard.py
#
# These tests do not need a CMake build; they exercise the manifest schema,
# the line-oriented CI parser, the test-inventory derivation, and the aggregate
# delivery gate.  They are intentionally stdlib-only so any hosted Python can
# run them.

from __future__ import annotations

import copy
import importlib.util
import tempfile
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


FIXTURE_WORKFLOW = """\
name: Fixture

on:
  push:
    branches: [master]
  workflow_dispatch:
    inputs:
      tag:
        description: a tag
        required: true

jobs:
  portable:
    name: Portable / ${{ matrix.platform }}
    runs-on: ${{ matrix.runner }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - platform: Linux
            runner: ubuntu-24.04
          - platform: macOS
            runner: macos-15
    steps:
      - uses: actions/checkout@v4
      - name: Run
        run: echo hi

  licensed:
    name: Licensed
    runs-on: [self-hosted, kisakcod, windows, x86]
    steps:
      - name: Run
        run: echo hi

  plain:
    runs-on: ubuntu-latest
    steps:
      - name: Run
        run: echo hi
"""


class WorkflowParserTests(unittest.TestCase):
    def test_jobs_block_ignores_on_mapping(self):
        lines = FIXTURE_WORKFLOW.splitlines()
        block = cd._jobs_block(lines)
        jobs = cd._job_blocks(block)
        self.assertEqual([job[0] for job in jobs], ["portable", "licensed", "plain"])

    def test_matrix_legs_counted(self):
        lines = FIXTURE_WORKFLOW.splitlines()
        jobs = dict(cd._job_blocks(cd._jobs_block(lines)))
        self.assertEqual(cd._matrix_legs(jobs["portable"]), 2)
        self.assertEqual(cd._matrix_legs(jobs["plain"]), 0)

    def test_self_hosted_detected(self):
        lines = FIXTURE_WORKFLOW.splitlines()
        jobs = dict(cd._job_blocks(cd._jobs_block(lines)))
        self.assertTrue(
            any('runs-on:' in line and "[self-hosted" in line for line in jobs["licensed"])
        )

    def test_fixture_workflow_via_temp_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(FIXTURE_WORKFLOW, encoding="utf-8")
            inventory = cd.derive_ci_inventory(workflow_dir)
        self.assertEqual(inventory["workflow_count"], 1)
        self.assertEqual(inventory["job_count"], 3)
        # portable expands to 2 legs; licensed and plain are single invocations.
        self.assertEqual(inventory["invocations"], 4)


class RealRepositoryTests(unittest.TestCase):
    def test_real_workflows_parse(self):
        inventory = cd.derive_ci_inventory()
        ids = {job["id"] for wf in inventory["workflows"] for job in wf["jobs"]}
        self.assertIn("portable-tests", ids)
        self.assertIn("windows-x86-headless", ids)
        self.assertIn("preflight", ids)
        self.assertGreaterEqual(inventory["workflow_count"], 3)

    def test_portable_tests_matrix(self):
        inventory = cd.derive_ci_inventory()
        portable = next(
            job
            for wf in inventory["workflows"]
            for job in wf["jobs"]
            if job["id"] == "portable-tests"
        )
        self.assertEqual(portable["matrix_legs"], 5)

    def test_inline_matrix_counted(self):
        # windows-x86 and windows-x86-sp use `config: [Debug, Release]`.
        inventory = cd.derive_ci_inventory()
        windows_x86 = next(
            job
            for wf in inventory["workflows"]
            for job in wf["jobs"]
            if wf["file"] == "ci.yml" and job["id"] == "windows-x86"
        )
        self.assertEqual(windows_x86["matrix_legs"], 2)

    def test_test_inventory_positive(self):
        inventory = cd.derive_test_inventory()
        self.assertGreater(inventory["add_test"], 0)
        self.assertGreater(inventory["add_executable"], 0)
        self.assertGreater(inventory["configure_presets"], 0)


class ManifestValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest_path = REPO_ROOT / "docs" / "capability" / "manifest.json"
        cls.manifest = cd.load_manifest(cls.manifest_path)

    def test_shipped_manifest_is_valid(self):
        self.assertEqual(cd.validate_manifest(self.manifest), [])

    def test_missing_required_field_is_rejected(self):
        broken = copy.deepcopy(self.manifest)
        del broken["capabilities"][0]["owner"]
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("owner" in error for error in errors))

    def test_unknown_validation_level_is_rejected(self):
        broken = copy.deepcopy(self.manifest)
        broken["capabilities"][0]["strongest_validation"] = "totally-made-up"
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("strongest_validation" in error for error in errors))

    def test_supporting_evidence_cannot_claim_delivery(self):
        broken = copy.deepcopy(self.manifest)
        broken["supporting_evidence"][0]["counts_toward_delivery"] = True
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("counts_toward_delivery" in error for error in errors))

    def test_undeclared_reference_is_rejected(self):
        broken = copy.deepcopy(self.manifest)
        # Adding an undeclared reference on top of the mandatory ones is an
        # error; the mandatory references themselves are exercised below.
        broken["aggregate"]["required_commercial_references"] = [
            "commercial-1.7",
            "steam-1.8",
            "steam-9.9",
        ]
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("steam-9.9" in error for error in errors))

    def test_aggregate_requires_declared_mode_and_level(self):
        broken_mode = copy.deepcopy(self.manifest)
        broken_mode["aggregate"]["required_modes"] = ["mp-client", "bogus-mode"]
        mode_errors = cd.validate_manifest(broken_mode)
        self.assertTrue(any("bogus-mode" in error for error in mode_errors))

        broken_level = copy.deepcopy(self.manifest)
        broken_level["aggregate"]["required_strongest_validation"] = "bogus-level"
        level_errors = cd.validate_manifest(broken_level)
        self.assertTrue(
            any("required_strongest_validation" in error for error in level_errors)
        )


class ReferenceIdentityTests(unittest.TestCase):
    """Reference ids are checked before anything indexes by id (PR #150)."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def _append_reference(self, reference):
        manifest = copy.deepcopy(self.manifest)
        manifest["commercial_references"].append(reference)
        return manifest

    def test_missing_reference_id_is_rejected(self):
        # A bare {"status": "pending"} row used to validate cleanly and then
        # raise KeyError("id") while computing the aggregate.
        broken = self._append_reference({"status": "pending"})
        errors = cd.validate_manifest(broken)
        self.assertTrue(
            any("id must be a non-empty string" in error for error in errors),
            msg=f"id-less reference was accepted: {errors}",
        )
        result = cd.compute_aggregate(broken)
        self.assertFalse(result["references_ok"])
        self.assertEqual(result["delivered"], 0)

    def test_empty_and_whitespace_reference_ids_are_rejected(self):
        for bad in ("", "   ", "\t\n"):
            with self.subTest(rid=bad):
                errors = cd.validate_manifest(
                    self._append_reference({"id": bad, "status": "pending"})
                )
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"empty reference id {bad!r} was accepted: {errors}",
                )

    def test_non_string_reference_ids_are_rejected(self):
        for bad in (None, 7, ["commercial-2.0"], {"id": "x"}):
            with self.subTest(rid=bad):
                errors = cd.validate_manifest(
                    self._append_reference({"id": bad, "status": "pending"})
                )
                self.assertTrue(
                    any(
                        "id must be a non-empty string" in error
                        for error in errors
                    ),
                    msg=f"non-string reference id {bad!r} was accepted: {errors}",
                )

    def test_duplicate_reference_ids_are_still_rejected(self):
        broken = copy.deepcopy(self.manifest)
        broken["commercial_references"].append(
            {"id": "commercial-1.7", "status": "pending"}
        )
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("duplicate ids" in error for error in errors))

    def test_valid_extra_reference_is_accepted(self):
        # A syntactically valid additional reference is not an error; the
        # mandatory declared references are what the membership check enforces.
        broken = self._append_reference({"id": "mac-app-2.0", "status": "pending"})
        self.assertEqual(cd.validate_manifest(broken), [])


class MandatoryContractTests(unittest.TestCase):
    """The delivery contract cannot be weakened through the manifest (#126)."""

    @classmethod
    def setUpClass(cls):
        cls.manifest_path = REPO_ROOT / "docs" / "capability" / "manifest.json"
        cls.manifest = cd.load_manifest(cls.manifest_path)

    def test_mandatory_modes_cannot_be_omitted_or_emptied(self):
        for weakened in ([], ["mp-client"], ["headless-server"], ["sp"]):
            with self.subTest(modes=weakened):
                broken = copy.deepcopy(self.manifest)
                broken["aggregate"]["required_modes"] = weakened
                errors = cd.validate_manifest(broken)
                self.assertTrue(
                    any("required_modes" in error for error in errors),
                    msg=f"weakened modes {weakened!r} were accepted: {errors}",
                )

        for cap in cd.MANDATORY_REQUIRED_MODES:
            broken = copy.deepcopy(self.manifest)
            broken["aggregate"]["required_modes"] = [
                m for m in cd.MANDATORY_REQUIRED_MODES if m != cap
            ]
            errors = cd.validate_manifest(broken)
            self.assertTrue(any(cap in error for error in errors))

    def test_mandatory_modes_cannot_be_absent_key(self):
        broken = copy.deepcopy(self.manifest)
        del broken["aggregate"]["required_modes"]
        errors = cd.validate_manifest(broken)
        self.assertTrue(any("required_modes" in error for error in errors))

    def test_mandatory_references_cannot_be_omitted_or_emptied(self):
        for weakened in ([], ["commercial-1.7"], ["steam-1.8"]):
            with self.subTest(refs=weakened):
                broken = copy.deepcopy(self.manifest)
                broken["aggregate"]["required_commercial_references"] = weakened
                errors = cd.validate_manifest(broken)
                self.assertTrue(
                    any(
                        "required_commercial_references" in error
                        for error in errors
                    ),
                    msg=f"weakened refs {weakened!r} were accepted: {errors}",
                )

        missing = copy.deepcopy(self.manifest)
        missing["aggregate"]["required_commercial_references"] = ["commercial-1.7"]
        errors = cd.validate_manifest(missing)
        self.assertTrue(any("steam-1.8" in error for error in errors))

    def test_mandatory_validation_threshold_cannot_be_weakened(self):
        for weakened in ("linked_production", "original_peer_compatibility", "none"):
            with self.subTest(level=weakened):
                broken = copy.deepcopy(self.manifest)
                broken["aggregate"]["required_strongest_validation"] = weakened
                errors = cd.validate_manifest(broken)
                self.assertTrue(
                    any(
                        "required_strongest_validation" in error
                        for error in errors
                    ),
                    msg=f"weakened level {weakened!r} was accepted: {errors}",
                )

    def test_mandatory_boolean_gates_cannot_be_disabled(self):
        for field in (
            "require_commercial_reference_validation",
            "require_package_result",
        ):
            with self.subTest(field=field):
                broken = copy.deepcopy(self.manifest)
                broken["aggregate"][field] = False
                errors = cd.validate_manifest(broken)
                self.assertTrue(
                    any(field in error for error in errors),
                    msg=f"disabled {field} was accepted: {errors}",
                )

    def test_emptied_aggregate_cannot_manufacture_delivery(self):
        # Exact refinery reproduction: emptying the editable policy keys used to
        # return no validation errors and report delivered=5 with every
        # commercial reference pending and no production/package evidence.
        broken = copy.deepcopy(self.manifest)
        broken["aggregate"]["required_modes"] = []
        broken["aggregate"]["required_commercial_references"] = []

        self.assertNotEqual(cd.validate_manifest(broken), [])

        result = cd.compute_aggregate(broken)
        self.assertEqual(result["delivered"], 0)
        self.assertEqual(result["requested"], 5)
        self.assertFalse(result["references_ok"])
        self.assertEqual(result["required_modes"], ["mp-client", "headless-server"])
        self.assertEqual(
            result["required_refs"], ["commercial-1.7", "steam-1.8"]
        )

    def test_weakened_gates_cannot_manufacture_delivery(self):
        # Disabling the boolean gates and refs must not promote unpromoted rows,
        # even when the commercial references are marked validated.
        broken = copy.deepcopy(self.manifest)
        broken["aggregate"]["require_commercial_reference_validation"] = False
        broken["aggregate"]["require_package_result"] = False
        for reference in broken["commercial_references"]:
            reference["status"] = "validated"
        result = cd.compute_aggregate(broken)
        self.assertEqual(result["delivered"], 0)


class AggregateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = cd.load_manifest(
            REPO_ROOT / "docs" / "capability" / "manifest.json"
        )

    def test_current_state_is_zero_of_five(self):
        result = cd.compute_aggregate(self.manifest)
        self.assertEqual(result["requested"], 5)
        self.assertEqual(result["delivered"], 0)
        self.assertFalse(result["references_ok"])

    def test_delivery_requires_all_gates(self):
        unlocked = copy.deepcopy(self.manifest)
        for reference in unlocked["commercial_references"]:
            reference["status"] = "validated"
            reference["sha256"] = "a" * 64
            reference["evidence_run"] = "release-1"
        # Utility/scaffold evidence is not mutated, so a target can only pass if
        # its own capability rows are promoted.
        result = cd.compute_aggregate(unlocked)
        self.assertTrue(result["references_ok"])
        self.assertEqual(result["delivered"], 0)

        for cap in unlocked["capabilities"]:
            if cap["target"] == "all-requested":
                continue
            cap["strongest_validation"] = "packaged_clean_machine"
            cap["production_enrolled"] = True
            cap["evidence"]["sha"] = "b" * 40
            cap["evidence"]["run"] = "12345"
            cap["evidence"]["package_result"] = cd.PACKAGE_RESULT_SUCCESS
        promoted = cd.compute_aggregate(unlocked)
        self.assertEqual(promoted["delivered"], 5)

    def test_non_requested_target_never_counts(self):
        manifest = copy.deepcopy(self.manifest)
        for reference in manifest["commercial_references"]:
            reference["status"] = "validated"
            reference["sha256"] = "a" * 64
            reference["evidence_run"] = "release-1"
        for cap in manifest["capabilities"]:
            if cap["target"] in ("all-requested", "windows-x86"):
                continue
            cap["strongest_validation"] = "packaged_clean_machine"
            cap["production_enrolled"] = True
            cap["evidence"]["sha"] = "b" * 40
            cap["evidence"]["run"] = "12345"
            cap["evidence"]["package_result"] = cd.PACKAGE_RESULT_SUCCESS
        # windows-x86 is deliberately left unpromoted; it must not contribute.
        result = cd.compute_aggregate(manifest)
        self.assertEqual(result["delivered"], 5)


class RenderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rendered, cls.aggregate = cd.build_dashboard()

    def test_render_is_deterministic(self):
        second, _ = cd.build_dashboard()
        self.assertEqual(self.rendered, second)

    def test_render_contains_generated_marker(self):
        self.assertIn("GENERATED FILE - DO NOT EDIT", self.rendered)

    def test_render_reports_zero_of_five(self):
        self.assertIn("## Requested target delivery: 0/5", self.rendered)

    def test_render_contains_derived_inventories(self):
        self.assertIn("## Derived CI inventory", self.rendered)
        self.assertIn("## Derived test inventory", self.rendered)

    def test_check_mode_against_current_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            current = Path(tmp) / "dashboard.md"
            current.write_text(self.rendered, encoding="utf-8")
            self.assertEqual(cd.main(["--check", "--output", str(current)]), 0)

            stale = Path(tmp) / "stale.md"
            stale.write_text("stale\n", encoding="utf-8")
            self.assertEqual(cd.main(["--check", "--output", str(stale)]), 1)

            missing = Path(tmp) / "missing.md"
            self.assertEqual(cd.main(["--check", "--output", str(missing)]), 1)

    def test_stdout_mode_emits_exactly_the_rendered_dashboard(self):
        # --stdout must emit only the rendered dashboard on stdout.  Comparing
        # the entire captured stream catches appended or interleaved advisory
        # text; a Markdown prefix check plus a JSONDecodeError assertion did
        # not, because Markdown already fails JSON parsing regardless of any
        # extra trailing envelope.
        import io
        import contextlib

        expected, _ = cd.build_dashboard()
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = cd.main(["--stdout"])
        self.assertEqual(code, 0)
        self.assertEqual(buffer.getvalue(), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)

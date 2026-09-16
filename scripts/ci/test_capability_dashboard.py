#!/usr/bin/env python3
"""Unit tests for scripts/ci/capability-dashboard.py.

Run with:  python3 scripts/ci/test_capability_dashboard.py

These tests do not need a CMake build; they exercise the manifest schema,
the line-oriented CI parser, the test-inventory derivation, and the aggregate
delivery gate.  They are intentionally stdlib-only so any hosted Python can
run them.
"""

from __future__ import annotations

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"
REPO_ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location("capability_dashboard", MODULE_PATH)
assert spec and spec.loader, f"cannot load {MODULE_PATH}"
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

    def test_aggregate_requires_declared_references(self):
        broken = copy.deepcopy(self.manifest)
        broken["aggregate"]["required_commercial_references"] = ["commercial-1.7"]
        # Only referencing a declared reference is fine; an undeclared one is not.
        self.assertEqual(cd.validate_manifest(broken), [])
        broken["aggregate"]["required_commercial_references"] = ["steam-9.9"]
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
            cap["evidence"]["package_result"] = "sha256:fixture"
        promoted = cd.compute_aggregate(unlocked)
        self.assertEqual(promoted["delivered"], 5)

    def test_non_requested_target_never_counts(self):
        manifest = copy.deepcopy(self.manifest)
        for reference in manifest["commercial_references"]:
            reference["status"] = "validated"
        for cap in manifest["capabilities"]:
            if cap["target"] in ("all-requested", "windows-x86"):
                continue
            cap["strongest_validation"] = "packaged_clean_machine"
            cap["production_enrolled"] = True
            cap["evidence"]["package_result"] = "sha256:fixture"
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

    def test_stdout_mode_keeps_json_stdout_clean(self):
        # --stdout must emit only the rendered dashboard on stdout.  A JSON
        # parser must not see advisory text mixed into stdout.
        import io
        import contextlib

        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = cd.main(["--stdout"])
        self.assertEqual(code, 0)
        text = buffer.getvalue()
        self.assertTrue(text.startswith("# KisakCOD capability dashboard"))
        # Reparsing as JSON must fail on markdown, proving nothing JSON-ish was
        # interleaved as a trailing envelope.
        with self.assertRaises(json.JSONDecodeError):
            json.loads(text)


if __name__ == "__main__":
    unittest.main(verbosity=2)

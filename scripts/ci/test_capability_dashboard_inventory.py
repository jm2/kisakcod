#!/usr/bin/env python3
"""Regression tests for CI inventory derivation and matrix expansion."""

# Run with:  python3 scripts/ci/test_capability_dashboard_inventory.py
#
# These tests pin the two concrete review findings on PR #150: matrix legs must
# be a real Cartesian product with GitHub include/exclude semantics (and must
# fail explicitly on an unsupported shape instead of guessing a count), and the
# workflow inventory must cover both ``*.yml`` and ``*.yaml``.  They are
# stdlib-only so any hosted Python can run them.

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_inventory_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


MINIMAL_WORKFLOW = """\
name: Fixture

on:
  push:

jobs:
  plain:
    runs-on: ubuntu-latest
    steps:
      - name: Run
        run: echo hi
"""


class YamlInventoryTests(unittest.TestCase):
    """Both workflow spellings must be inventoried (#150 review r4030126257)."""

    def test_yaml_suffix_is_inventoried(self):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(
                MINIMAL_WORKFLOW, encoding="utf-8"
            )
            (workflow_dir / "fixture.yaml").write_text(
                MINIMAL_WORKFLOW, encoding="utf-8"
            )
            inventory = cd.derive_ci_inventory(workflow_dir)
        self.assertEqual(inventory["workflow_count"], 2)
        self.assertEqual(
            {workflow["file"] for workflow in inventory["workflows"]},
            {"fixture.yml", "fixture.yaml"},
        )

    def test_yaml_only_workflow_is_inventoried(self):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "extra.yaml").write_text(
                MINIMAL_WORKFLOW, encoding="utf-8"
            )
            inventory = cd.derive_ci_inventory(workflow_dir)
        self.assertEqual(inventory["workflow_count"], 1)
        self.assertEqual(inventory["workflows"][0]["file"], "extra.yaml")


class MatrixExpansionTests(unittest.TestCase):
    """Matrix legs must be a real Cartesian product (#150 review r4030126249)."""

    @staticmethod
    def _job(*lines):
        return ["    runs-on: ubuntu-latest", *lines]

    def test_cartesian_product_is_axes_product(self):
        # Exact refinery reproduction: two axes of 2x3 summed to 5 instead of
        # expanding to the 6 real job invocations.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac, win]",
            "        config: [Debug, Release]",
        )
        self.assertEqual(cd.matrix_legs(job), 6)

    def test_include_only_matrix_is_one_leg_per_entry(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        include:",
            "          - platform: Linux",
            "            runner: ubuntu-24.04",
            "          - platform: macOS",
            "            runner: macos-15",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_include_augments_matching_combinations(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        include:",
            "          - os: linux",
            "            arch: x64",
        )
        # The include augments the linux combination; it adds no leg.
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_include_with_new_axis_key_adds_a_leg(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        include:",
            "          - os: win",
            "            arch: x64",
        )
        # No base combination has os=win, so the include becomes its own leg.
        self.assertEqual(cd.matrix_legs(job), 3)

    def test_exclude_removes_matching_combinations(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        config: [Debug, Release]",
            "        exclude:",
            "          - os: mac",
            "            config: Debug",
        )
        self.assertEqual(cd.matrix_legs(job), 3)

    def test_exclude_then_reinclude_keeps_combination(self):
        # Exact refinery reproduction: GitHub evaluates ``exclude`` against the
        # original combinations and only then applies ``include``, so an
        # include re-adds an excluded combination instead of being stripped
        # after the fact.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        exclude:",
            "          - os: linux",
            "        include:",
            "          - os: linux",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_exclude_key_from_include_does_not_remove_combinations(self):
        # Excludes only match the original axis combinations.  A field carried
        # solely by an include must not delete the augmented leg.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        exclude:",
            "          - arch: x64",
            "        include:",
            "          - os: linux",
            "            arch: x64",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_include_only_matrix_is_not_pruned_by_excludes(self):
        # An include-only matrix has no original combinations, so excludes
        # cannot remove any of the include-created legs.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        exclude:",
            "          - platform: Linux",
            "        include:",
            "          - platform: Linux",
            "            runner: ubuntu-24.04",
            "          - platform: macOS",
            "            runner: macos-15",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_unsupported_inline_matrix_fails_explicitly(self):
        job = self._job("    strategy:", "      matrix: {os: [linux]}")
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)

    def test_non_scalar_axis_entry_fails_explicitly(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os:",
            "          - name: linux",
        )
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)

    def test_inline_axis_must_be_a_list(self):
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: linux",
        )
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)


class SelfHostedRunsOnTests(unittest.TestCase):
    """The complete ``runs-on`` value is parsed, or parsing fails closed."""

    @staticmethod
    def _probe(runs_on_lines):
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  probe:",
                *runs_on_lines,
                "    steps:",
                "      - name: Run",
                "        run: echo hi",
                "",
            ]
        )
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(workflow, encoding="utf-8")
            inventory = cd.derive_ci_inventory(workflow_dir)
        return inventory["workflows"][0]["jobs"][0]

    def test_scalar_self_hosted(self):
        job = self._probe(["    runs-on: self-hosted"])
        self.assertTrue(job["self_hosted"])

    def test_quoted_scalar_self_hosted(self):
        job = self._probe(['    runs-on: "self-hosted"'])
        self.assertTrue(job["self_hosted"])

    def test_flow_list_self_hosted_first(self):
        job = self._probe(["    runs-on: [self-hosted, linux]"])
        self.assertTrue(job["self_hosted"])

    def test_flow_list_self_hosted_not_first(self):
        # The old probe only matched ``[self-hosted`` and missed this shape.
        job = self._probe(["    runs-on: [linux, self-hosted]"])
        self.assertTrue(job["self_hosted"])

    def test_block_list_self_hosted(self):
        job = self._probe(
            ["    runs-on:", "      - linux", "      - self-hosted"]
        )
        self.assertTrue(job["self_hosted"])

    def test_hosted_runner_is_not_self_hosted(self):
        job = self._probe(["    runs-on: ubuntu-latest"])
        self.assertFalse(job["self_hosted"])

    def test_flow_list_of_hosted_runners_is_not_self_hosted(self):
        job = self._probe(["    runs-on: [ubuntu-24.04, macos-15]"])
        self.assertFalse(job["self_hosted"])

    def test_expression_is_not_statically_self_hosted(self):
        job = self._probe(["    runs-on: ${{ matrix.runner }}"])
        self.assertFalse(job["self_hosted"])

    def test_block_mapping_fails_closed(self):
        with self.assertRaises(cd.UnsupportedRunsOnError):
            self._probe(
                [
                    "    runs-on:",
                    "      group: kisakcod",
                    "      labels: [self-hosted]",
                ]
            )

    def test_inline_flow_mapping_fails_closed(self):
        with self.assertRaises(cd.UnsupportedRunsOnError):
            self._probe(["    runs-on: {group: kisakcod}"])

    def test_real_repository_self_hosted_jobs_are_detected(self):
        inventory = cd.derive_ci_inventory()
        jobs = {
            job["id"]: job["self_hosted"]
            for wf in inventory["workflows"]
            if wf["file"] == "licensed-smoke.yml"
            for job in wf["jobs"]
        }
        # The two Windows legs run on the self-hosted runner; the preflight
        # job is hosted and must not be reported as self-hosted.
        self.assertTrue(jobs["windows-x86"])
        self.assertTrue(jobs["windows-x86-headless"])
        self.assertFalse(jobs["preflight"])

    def test_inventory_derivation_writes_nothing_to_stdout(self):
        # The generator emits advisory text on stderr only; deriving the
        # inventory must not pollute the ``--stdout`` dashboard stream.
        import contextlib
        import io

        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            cd.derive_ci_inventory()
        self.assertEqual(buffer.getvalue(), "")


if __name__ == "__main__":
    unittest.main(verbosity=2)

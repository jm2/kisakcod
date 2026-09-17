#!/usr/bin/env python3
"""Regression tests for the dashboard's self-hosted ``runs-on`` parsing."""

# Run with:  python3 scripts/ci/test_capability_dashboard_self_hosted.py
#
# Split from test_capability_dashboard_inventory.py so each module
# stays a cohesive, readable size; every test keeps its original
# identity and assertions.  These tests are stdlib-only so any
# hosted Python can run them.

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_self_hosted_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


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

    def test_scalar_with_trailing_comment_is_self_hosted(self):
        # The comment used to stay attached, so ``self-hosted  # box`` was
        # compared verbatim and reported as a hosted runner.
        job = self._probe(["    runs-on: self-hosted  # dedicated box"])
        self.assertTrue(job["self_hosted"])

    def test_flow_list_with_trailing_comment_parses(self):
        # The comment broke the ``]`` shape check and raised instead of
        # parsing a fully supported flow value.
        job = self._probe(
            ["    runs-on: [self-hosted, linux]  # dedicated box"]
        )
        self.assertTrue(job["self_hosted"])

    def test_quoted_hash_is_not_a_comment(self):
        # A quoted ``#`` is value text: the scalar is not the self-hosted
        # label, and parsing must neither raise nor strip into the quotes.
        job = self._probe(['    runs-on: "self # host"  # annotated'])
        self.assertFalse(job["self_hosted"])

    def test_flow_list_quoted_comma_item_still_finds_self_hosted(self):
        # PR #150 rework review (runs-on shares the flow-list splitter): a
        # comma inside a quoted scalar must not corrupt the split, or the
        # self-hosted label after it stops matching.
        job = self._probe(['    runs-on: ["linux,debug", self-hosted]'])
        self.assertTrue(job["self_hosted"])

    def test_expression_with_trailing_comment_is_not_self_hosted(self):
        job = self._probe(["    runs-on: ${{ matrix.runner }}  # runtime"])
        self.assertFalse(job["self_hosted"])

    def test_comment_only_key_reads_following_block_labels(self):
        # PR #150 rework review (exact-head reproduction): the scalar versus
        # block decision used ``match.group(1).strip()`` before removing
        # YAML comments, so ``runs-on:  # runner labels`` treated the comment
        # as the scalar value and never read the block labels that follow;
        # the same job reported hosted.
        job = self._probe(
            [
                "    runs-on:  # runner labels",
                "      - self-hosted",
                "      - linux",
            ]
        )
        self.assertTrue(job["self_hosted"])

    def test_comment_only_key_with_hosted_block_labels_is_hosted(self):
        # The comment-only key must genuinely parse the labels rather than
        # default to either answer: a hosted block stays hosted.
        job = self._probe(
            [
                "    runs-on:  # runner labels",
                "      - ubuntu-24.04",
                "      - macos-15",
            ]
        )
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

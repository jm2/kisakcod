#!/usr/bin/env python3
"""Regression tests for the dashboard's matrix expansion."""

# Run with:  python3 scripts/ci/test_capability_dashboard_matrix.py
#
# Split from test_capability_dashboard_inventory.py so each module
# stays a cohesive, readable size; every test keeps its original
# identity and assertions.  These tests are stdlib-only so any
# hosted Python can run them.

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE_PATH = HERE / "capability-dashboard.py"

spec = importlib.util.spec_from_file_location(
    "capability_dashboard_matrix_launcher", MODULE_PATH
)
if spec is None or spec.loader is None:
    raise ImportError(f"cannot load {MODULE_PATH}")
cd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cd)


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

    def test_inline_include_flow_list_fails_explicitly(self):
        # Exact refinery reproduction: the parser discarded the inline value
        # and reported 0 legs (rendered as 1 invocation) for a two-entry
        # include-only matrix.  It must fail closed instead of undercounting.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        include: [{os: linux}, {os: windows}]",
        )
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)

    def test_inline_exclude_flow_list_fails_explicitly(self):
        # Exact refinery reproduction: an inline exclude of one of two axis
        # values left 2 legs instead of 1.  Reject the unsupported shape
        # rather than silently publishing the inaccurate count.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, windows]",
            "        exclude: [{os: windows}]",
        )
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)

    def test_axis_flow_list_with_trailing_comment_parses(self):
        # ``os: [linux, mac]  # note`` reached the flow-list parser with the
        # comment attached and failed the ``]`` shape check.  A trailing
        # comment is valid YAML and must not change the derived leg count.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]  # both build on CI",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_axis_block_value_with_trailing_comment_is_clean(self):
        # A block-sequence axis value kept its comment attached, so the axis
        # value silently became ``linux  # first``.  The comment must be
        # stripped and the value preserved verbatim.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os:",
            "          - linux  # first leg",
            "          - windows",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_quoted_hash_in_axis_value_is_preserved(self):
        # A ``#`` inside a quoted scalar is value text, not a comment.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        label: [\"win #1\"]",
        )
        self.assertEqual(cd.matrix_legs(job), 1)

    def test_quoted_comma_in_axis_flow_item_is_not_a_separator(self):
        # PR #150 rework review: ``["linux,debug", "windows"]`` was split
        # blindly on commas and returned 3 legs instead of 2.  A comma
        # inside a quoted scalar is value text, not a separator.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        label: [\"linux,debug\", \"windows\"]",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_escaped_quotes_in_axis_flow_item_are_intact(self):
        # A backslash escapes the next character inside double quotes, so
        # the escaped quote must not terminate the quoted span and split
        # the item.
        job = self._job(
            "    strategy:",
            "      matrix:",
            '        label: ["win \\"x\\"", mac]',
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_exclude_value_with_trailing_comment_still_matches(self):
        # PR #150 rework review: the exclude value kept its comment
        # (``windows # omit windows``) and never matched, leaving 2 legs
        # instead of 1.  The comment must be stripped so the exclusion
        # applies.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, windows]",
            "        exclude:",
            "          - os: windows # omit windows",
        )
        self.assertEqual(cd.matrix_legs(job), 1)

    def test_include_value_with_trailing_comment_still_matches(self):
        # Same finding on the include side: stripping the comment must keep
        # the include augmenting its matching combination (2 axes legs, no
        # extra include leg).
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        include:",
            "          - os: linux  # annotate the linux leg",
            "            arch: x64",
        )
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_quoted_hash_in_exclude_value_is_preserved(self):
        # A quoted ``#`` inside an include/exclude value is value text: the
        # exclusion must still match the literal ``win #1`` combination
        # while the unquoted trailing comment is stripped.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        label: [\"win #1\", mac]",
            "        exclude:",
            '          - label: "win #1"  # drop the odd label',
        )
        self.assertEqual(cd.matrix_legs(job), 1)

    def test_comment_only_include_value_fails_explicitly(self):
        # A comment-only value reads as YAML null, which this explicit
        # contract does not support; fail closed instead of guessing.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        include:",
            "          - os:  # null is not a supported include value",
        )
        with self.assertRaises(cd.MatrixExpansionError):
            cd.matrix_legs(job)

    def test_run_payload_heredoc_matrix_is_not_job_configuration(self):
        # Exact #150 review P2 reproduction: one job with no ``strategy:``
        # whose run payload contains a literal ``matrix:`` line.  The old
        # scan read the heredoc as the job's matrix and expanded three
        # phantom invocations; the heredoc is shell data, not workflow
        # configuration.
        job = self._job(
            "    steps:",
            "      - name: Emit",
            "        run: |",
            "          cat <<'EOF'",
            "          matrix:",
            "            label: [a, b, c]",
            "          EOF",
        )
        self.assertEqual(cd.matrix_legs(job), 0)

    def test_strategy_matrix_still_expands_after_payload_scan_narrowing(self):
        # Control for the heredoc regression: a real job-level
        # ``strategy.matrix`` keeps expanding while the payload scan is
        # restricted to the job's direct keys.
        job = self._job(
            "    strategy:",
            "      matrix:",
            "        label: [a, b, c]",
        )
        self.assertEqual(cd.matrix_legs(job), 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)

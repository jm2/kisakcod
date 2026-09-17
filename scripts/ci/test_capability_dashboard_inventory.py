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


class StructuralCommentTests(unittest.TestCase):
    """Trailing comments on structural keys must not delete inventory rows."""

    COMMENTED_JOBS_WORKFLOW = """\
name: Fixture

on:
  push:

jobs:  # build jobs
  plain:
    runs-on: ubuntu-latest
    steps:
      - name: Run
        run: echo hi
"""

    COMMENTED_JOB_KEY_WORKFLOW = """\
name: Fixture

on:
  push:

jobs:
  plain:  # linux build
    runs-on: ubuntu-latest
    steps:
      - name: Run
        run: echo hi
"""

    @staticmethod
    def _inventory(workflow_text):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(
                workflow_text, encoding="utf-8"
            )
            return cd.derive_ci_inventory(workflow_dir)

    def test_jobs_key_with_trailing_comment_still_inventories(self):
        # Exact refinery reproduction at PR #150 head 66b401db: the bare
        # ``jobs:`` line inventories job_count=1/invocations=1, but the same
        # fixture with ``jobs:  # build jobs`` matched no structural key and
        # silently published job_count=0/invocations=0 without any error.
        inventory = self._inventory(self.COMMENTED_JOBS_WORKFLOW)
        workflow = inventory["workflows"][0]
        self.assertEqual(workflow["job_count"], 1)
        self.assertEqual(workflow["invocations"], 1)

    def test_job_key_with_trailing_comment_still_inventories(self):
        # A trailing comment on a job key has the corresponding omission
        # risk: the bare-key match must see the comment-stripped key or the
        # job merges into its predecessor and silently disappears.
        inventory = self._inventory(self.COMMENTED_JOB_KEY_WORKFLOW)
        workflow = inventory["workflows"][0]
        self.assertEqual([job["id"] for job in workflow["jobs"]], ["plain"])
        self.assertEqual(workflow["invocations"], 1)

    def test_matrix_key_with_trailing_comment_parses(self):
        # ``matrix:  # build matrix`` is the mapping boundary, not an
        # unsupported inline declaration.
        job = [
            "    runs-on: ubuntu-latest",
            "    strategy:",
            "      matrix:  # build matrix",
            "        os: [linux, mac]",
        ]
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_axis_block_after_commented_inline_placeholder_parses(self):
        # A comment after the axis key is not an inline flow value: the
        # block sequence that follows must still be read instead of the
        # comment text failing the flow-list shape check.
        job = [
            "    runs-on: ubuntu-latest",
            "    strategy:",
            "      matrix:",
            "        os:  # both build on CI",
            "          - linux",
            "          - windows",
        ]
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_commented_include_key_still_parses_block_list(self):
        # ``include:  # note`` must read the block list that follows rather
        # than treating the comment as an unsupported inline declaration.
        job = [
            "    runs-on: ubuntu-latest",
            "    strategy:",
            "      matrix:",
            "        os: [linux, mac]",
            "        include:  # annotate the linux leg",
            "          - os: linux",
            "            arch: x64",
        ]
        self.assertEqual(cd.matrix_legs(job), 2)

    def test_commented_out_matrix_stays_ignored(self):
        # A commented-out matrix is not a matrix: the job must stay a
        # single leg instead of expanding the commented shape.
        job = [
            "    runs-on: ubuntu-latest",
            "    # matrix:",
            "    #   os: [linux, mac]",
        ]
        self.assertEqual(cd.matrix_legs(job), 0)


class JobsIndentationTests(unittest.TestCase):
    """Any consistent job-key indent inventories; unsupported shapes fail."""

    FOUR_SPACE_WORKFLOW = """\
name: Fixture

on:
  push:

jobs:
    build:
        runs-on: ubuntu-latest
        steps:
            - name: Run
              run: echo hi
"""

    TWO_FOUR_SPACE_JOBS_WORKFLOW = """\
name: Fixture

on:
  push:

jobs:
    build:
        runs-on: ubuntu-latest
        steps:
            - name: Build
              run: echo build
    test:
        runs-on: [ubuntu-24.04]
        steps:
            - name: Test
              run: echo test
"""

    @staticmethod
    def _inventory(workflow_text):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(
                workflow_text, encoding="utf-8"
            )
            return cd.derive_ci_inventory(workflow_dir)

    def test_four_space_job_is_inventoried(self):
        # PR #150 rework review (exact-head reproduction): job keys were
        # only recognised at exactly indent 2, so this valid workflow with
        # a four-space jobs mapping reported job_count=0/invocations=0
        # without any error.
        inventory = self._inventory(self.FOUR_SPACE_WORKFLOW)
        workflow = inventory["workflows"][0]
        self.assertEqual([job["id"] for job in workflow["jobs"]], ["build"])
        self.assertEqual(workflow["job_count"], 1)
        self.assertEqual(workflow["invocations"], 1)

    def test_multiple_four_space_jobs_keep_boundaries(self):
        # Peer job keys at the mapping's own indent must start new jobs:
        # neither merging into one job nor dropping the second job.
        inventory = self._inventory(self.TWO_FOUR_SPACE_JOBS_WORKFLOW)
        workflow = inventory["workflows"][0]
        self.assertEqual(
            [job["id"] for job in workflow["jobs"]], ["build", "test"]
        )
        self.assertEqual(workflow["job_count"], 2)
        self.assertEqual(workflow["invocations"], 2)

    def test_two_space_jobs_still_inventories(self):
        # The standard shape keeps its exact behaviour alongside the
        # newly supported indents.
        inventory = self._inventory(MINIMAL_WORKFLOW)
        workflow = inventory["workflows"][0]
        self.assertEqual([job["id"] for job in workflow["jobs"]], ["plain"])
        self.assertEqual(workflow["invocations"], 1)

    def test_non_job_peer_at_job_indent_fails_explicitly(self):
        # A block-sequence item at the job-key indent is a shape this
        # contract does not support: it must fail explicitly instead of
        # silently publishing an empty or partial inventory.
        with self.assertRaises(cd.JobsStructureError):
            self._inventory(
                "\n".join(
                    [
                        "name: Fixture",
                        "",
                        "on:",
                        "  push:",
                        "",
                        "jobs:",
                        "  build:",
                        "    runs-on: ubuntu-latest",
                        "  - name: stray",
                        "",
                    ]
                )
            )

    def test_dedent_below_job_indent_fails_explicitly(self):
        # A mapping key dedented below the jobs mapping's child indent is
        # not a job peer this parser can bound: fail explicitly.
        with self.assertRaises(cd.JobsStructureError):
            self._inventory(
                "\n".join(
                    [
                        "name: Fixture",
                        "",
                        "on:",
                        "  push:",
                        "",
                        "jobs:",
                        "    build:",
                        "        runs-on: ubuntu-latest",
                        "  stray: peer",
                        "",
                    ]
                )
            )


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

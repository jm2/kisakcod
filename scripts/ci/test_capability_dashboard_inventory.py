#!/usr/bin/env python3
"""Regression tests for CI workflow inventory derivation."""

# Run with:  python3 scripts/ci/test_capability_dashboard_inventory.py
#
# These tests pin the concrete PR #150 review findings on the workflow
# inventory itself: both ``*.yml`` and ``*.yaml`` spellings are
# inventoried, trailing comments on structural keys delete no inventory
# rows, and any consistent job-key indent inventories while unsupported
# shapes fail explicitly.  The matrix-expansion and self-hosted
# ``runs-on`` groups live in their own sibling modules.  All of these
# tests are stdlib-only so any hosted Python can run them.

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


class JobNameTests(unittest.TestCase):
    """A derived job name comes from the job's own ``name:`` key only."""

    def _job_name(self, workflow_text):
        with tempfile.TemporaryDirectory() as tmp:
            workflow_dir = Path(tmp)
            (workflow_dir / "fixture.yml").write_text(
                workflow_text, encoding="utf-8"
            )
            inventory = cd.derive_ci_inventory(workflow_dir)
        return inventory["workflows"][0]["jobs"][0]["name"]

    def test_step_name_never_becomes_the_job_name(self):
        # A step written as ``uses:`` with ``name:`` on the continuation
        # line puts ``name:`` at a deeper indent; the old scan matched any
        # line and renamed the job ``build (Checkout)``.  Only the job's
        # own key indent may rename it.
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  build:",
                "    runs-on: ubuntu-latest",
                "    steps:",
                "      - uses: actions/checkout@v4",
                "        name: Checkout",
                "",
            ]
        )
        self.assertEqual(self._job_name(workflow), "build")

    def test_dash_step_name_without_job_name_keeps_job_id(self):
        # MINIMAL_WORKFLOW has no job-level ``name:`` at all, so the
        # derived name must stay the job id.
        self.assertEqual(self._job_name(MINIMAL_WORKFLOW), "plain")

    def test_job_level_name_with_comment_is_stripped(self):
        # A trailing comment is not value text: ``Build  # linux`` must not
        # render as ``build (Build  # linux)``.
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  build:",
                "    name: Build  # linux",
                "    runs-on: ubuntu-latest",
                "    steps:",
                "      - name: Run",
                "        run: echo hi",
                "",
            ]
        )
        self.assertEqual(self._job_name(workflow), "build (Build)")

    def test_quoted_name_keeps_hash_and_drops_quotes(self):
        # A quoted ``#`` is value text and the quotes are not part of it.
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  build:",
                '    name: "Build #1"',
                "    runs-on: ubuntu-latest",
                "",
            ]
        )
        self.assertEqual(self._job_name(workflow), "build (Build #1)")

    def test_expression_name_keeps_the_job_id(self):
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  build:",
                "    name: ${{ github.workflow }}",
                "    runs-on: ubuntu-latest",
                "",
            ]
        )
        self.assertEqual(self._job_name(workflow), "build")

    def test_job_name_before_deeper_keys_is_still_found(self):
        # A job-level ``name:`` declared before ``runs-on:``/``steps:`` is
        # found, and a later step name does not override it.
        workflow = "\n".join(
            [
                "name: Fixture",
                "",
                "on:",
                "  push:",
                "",
                "jobs:",
                "  build:",
                "    name: Build",
                "    runs-on: ubuntu-latest",
                "    steps:",
                "      - name: Checkout",
                "        uses: actions/checkout@v4",
                "",
            ]
        )
        self.assertEqual(self._job_name(workflow), "build (Build)")


if __name__ == "__main__":
    unittest.main(verbosity=2)

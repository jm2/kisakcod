#!/usr/bin/env python3
"""
Negative and positive regression tests for check-ci-aggregate.py.

The aggregate-enrollment checker is a fail-closed CI gate, so its failure
modes matter as much as its success path.  Each case below builds a small
synthetic workflow in a temporary directory and asserts the checker's exit
status.  In particular the regressions cover the ways the single
branch-protection aggregate can silently stop protecting the build:

* a required gate dropped from `scaffolding-complete.needs` (the case the
  #134 rework review found for the sanitizer and checker jobs),
* a job that exists but is not enrolled anywhere,
* a job concealed from the enrollment comparison by an unindented YAML
  comment (the #134 re-review false-success reproduction: comments are
  transparent to the jobs mapping, so the hidden gate still fails),
* syntax the jobs parser cannot faithfully attribute (flow-style or
  tab-indented entries) instead of silently swallowing a job,
* a `needs:` entry that references no real job,
* duplicate needs entries, an empty needs list, and a missing aggregate,
* an enforcement step that consumes the results but ignores non-success,
* an enforcement step that silently skips one dependency's result while
  consuming the rest (the middle-position-loss mutation: caught by
  simulating a non-success result at every needs position, not only the
  first and last),
* an enforcement step that does not consume the results at all,
* the #134 rework-review false-success mutations: a step-level `if: false`
  that makes GitHub skip the sole enforcement step, and step-level
  `continue-on-error: true` that makes GitHub ignore its failure — both at
  their exact reproduced position (directly after the step name) and at
  the equally valid position after the run block,
* aggregate-job-level skip/error-tolerance controls: a job `if:` other
  than the pinned safe shape (or no `if:` at all) can skip the aggregate
  when a dependency fails, and a job-level `continue-on-error` discards a
  failed enforcement result.

The positive cases validate a well-formed synthetic workflow and the real
checked-in `.github/workflows/ci.yml`, so a required gate cannot silently
disappear from selection or from the aggregate without this suite failing.

Run directly:

    python3 scripts/ci/test_check_ci_aggregate.py

Exits non-zero and prints the failing case names if any assertion fails.
"""

from __future__ import annotations

import os
# subprocess is the only way to exercise the checker as a real process,
# which is the contract under test (its exit status). The command is a
# fixed interpreter plus this repository's own checked-in script.
import subprocess  # nosec
import sys
import tempfile
from typing import List, Optional

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "check-ci-aggregate.py")
REAL_WORKFLOW = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    os.pardir, os.pardir, ".github", "workflows", "ci.yml"))

GOOD_ENFORCEMENT = """\
          results="${{ join(needs.*.result, ' ') }}"
          echo "required results: $results"
          fail=0
          for r in $results; do
            if [ "$r" != "success" ]; then
              echo "required job result: $r" >&2
              fail=1
            fi
          done
          exit $fail
"""

# Consumes every result but exits success unconditionally.
PERMISSIVE_ENFORCEMENT = """\
          results="${{ join(needs.*.result, ' ') }}"
          echo "required results: $results"
          exit 0
"""

# Consumes nothing and exits success unconditionally.
NONCONSUMING_ENFORCEMENT = """\
          echo "aggregate ok"
          exit 0
"""

# Consumes every result but silently skips the second dependency's
# result: the exact mutation the #134 rework review reproduced against a
# checker that simulated only the first and last needs positions. A
# checker must exercise a non-success result at EVERY needs position to
# catch it.
MIDDLE_IGNORING_ENFORCEMENT = """\
          results="${{ join(needs.*.result, ' ') }}"
          echo "required results: $results"
          fail=0
          i=0
          for r in $results; do
            i=$((i + 1))
            if [ "$i" -eq 2 ]; then
              continue
            fi
            if [ "$r" != "success" ]; then
              echo "required job result: $r" >&2
              fail=1
            fi
          done
          exit $fail
"""


def synthetic_workflow(needs: List[str],
                       jobs: Optional[List[str]] = None,
                       enforcement: str = GOOD_ENFORCEMENT,
                       include_aggregate: bool = True,
                       interject: str = "") -> str:
    """
    Build a minimal workflow with the real file's shape and indentation.

    `interject` is spliced verbatim immediately before the aggregate block,
    so fixtures can place comments (any indentation) inside the jobs
    mapping the way the real workflow does.
    """
    job_ids = jobs if jobs is not None else ["gate-a", "gate-b"]
    blocks = []
    for job_id in job_ids:
        blocks.append(
            "  %s:\n"
            "    name: Gate %s\n"
            "    runs-on: ubuntu-24.04\n"
            "    steps:\n"
            "      - name: Run\n"
            "        run: echo %s\n" % (job_id, job_id, job_id))
    if include_aggregate:
        blocks.append(interject)
        needs_lines = "".join("      - %s\n" % entry for entry in needs)
        blocks.append(
            "  scaffolding-complete:\n"
            "    name: Portable scaffolding complete\n"
            "    needs:\n"
            + needs_lines +
            "    if: ${{ always() && !cancelled() }}\n"
            "    runs-on: ubuntu-24.04\n"
            "    timeout-minutes: 10\n"
            "    steps:\n"
            "      - name: Enforce required job results\n"
            "        run: |\n"
            + enforcement)
    return (
        "name: CI\n"
        "\n"
        "on:\n"
        "  push:\n"
        "    branches: [master]\n"
        "\n"
        "permissions:\n"
        "  contents: read\n"
        "\n"
        "jobs:\n"
        + "".join(blocks))


def append_gate(workflow: str, job_id: str, leading_comment: str = "") -> str:
    """Append an unenrolled failing gate after the aggregate job."""
    return (
        workflow
        + leading_comment
        + "  %s:\n" % job_id
        + "    name: Hidden gate %s\n" % job_id
        + "    runs-on: ubuntu-24.04\n"
        + "    steps:\n"
        + "      - name: Run\n"
        + "        run: exit 1\n")


def comment_between_needs(workflow: str) -> str:
    """Split the aggregate's needs list with an unindented comment."""
    return workflow.replace(
        "      - gate-a\n      - gate-b\n",
        "      - gate-a\n# comment between needs entries\n      - gate-b\n")


ENFORCEMENT_NAME_LINE = "      - name: Enforce required job results\n"
AGGREGATE_IF_LINE = "    if: ${{ always() && !cancelled() }}\n"
TIMEOUT_LINE = "    timeout-minutes: 10\n"


def insert_after_enforcement_name(workflow: str, insertion: str) -> str:
    """
    Splice step-level lines directly after the enforcement step name.

    This is the exact position the #134 rework review used for its two
    reproduced false-success mutations against the real ci.yml.
    """
    return workflow.replace(ENFORCEMENT_NAME_LINE,
                            ENFORCEMENT_NAME_LINE + insertion, 1)


def append_after_run_block(workflow: str, insertion: str) -> str:
    """
    Place step-level lines after the enforcement run block.

    A mapping key may follow the `run: |` block, so this position is as
    valid YAML — and as effective against GitHub — as the one above.
    """
    return workflow + insertion


def replace_aggregate_if(workflow: str, replacement: str) -> str:
    """Swap the aggregate job's own condition for another value."""
    return workflow.replace(AGGREGATE_IF_LINE, replacement, 1)


class Case:
    """One synthetic workflow fixture and the checker exit it must yield."""

    def __init__(self, name: str, expect_rc: int, workflow: str) -> None:
        """
        Record the case name, expected checker exit status, and workflow.
        """
        self.name = name
        self.expect_rc = expect_rc
        self.workflow = workflow


CASES: List[Case] = [
    # --- success paths -------------------------------------------------
    Case(
        "synthetic_happy",
        0,
        synthetic_workflow(needs=["gate-a", "gate-b"]),
    ),
    # --- failure paths -------------------------------------------------
    # The #134 rework review finding: a required gate described as
    # required but absent from the aggregate's needs.
    Case(
        "missing_required_gate_fails",
        1,
        synthetic_workflow(needs=["gate-a"], jobs=["gate-a", "gate-b"]),
    ),
    # A job that exists but is enrolled nowhere protects nothing.
    Case(
        "unenrolled_job_fails",
        1,
        synthetic_workflow(needs=["gate-a"],
                           jobs=["gate-a", "gate-b", "gate-c"]),
    ),
    # The #134 re-review false-success reproduction: an unindented comment
    # is valid YAML *inside* the jobs mapping, so the job after it must
    # still be discovered and its absence from needs must fail.
    Case(
        "comment_hidden_unenrolled_job_fails",
        1,
        append_gate(synthetic_workflow(needs=["gate-a", "gate-b"]),
                    "hidden-gate",
                    "# Additional required gate\n"),
    ),
    # Control for the case above: the identical hidden gate without the
    # comment also fails, proving the comment was the only difference.
    Case(
        "hidden_job_without_comment_fails",
        1,
        append_gate(synthetic_workflow(needs=["gate-a", "gate-b"]),
                    "hidden-gate"),
    ),
    # Comments between jobs (any indentation) must NOT break parsing of an
    # otherwise well-formed, fully enrolled workflow.
    Case(
        "comment_interjection_accepted",
        0,
        synthetic_workflow(
            needs=["gate-a", "gate-b"],
            interject=("# prose comment at column zero\n"
                       + "  # indented note between jobs\n")),
    ),
    # A flow-style job entry at job-key indentation is invisible to the
    # line parser, so it must fail closed instead of being swallowed.
    Case(
        "flow_style_entry_after_comment_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b"])
        + "# Additional required gate\n"
        + "  hidden-gate: {name: Flow style gate}\n",
    ),
    # Tab indentation is invalid YAML; refuse to guess rather than parse.
    Case(
        "tab_indented_entry_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b"])
        + "# Additional required gate\n"
        + "\thidden-gate:\n",
    ),
    # A comment splitting the needs list truncates the parsed list; the
    # then-missing entry fails closed (never silently dropped the other
    # way).
    Case(
        "comment_inside_needs_fails",
        1,
        comment_between_needs(
            synthetic_workflow(needs=["gate-a", "gate-b"],
                               jobs=["gate-a", "gate-b"])),
    ),
    # A needs entry referencing no real job cannot protect anything.
    Case(
        "ghost_needs_entry_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b", "ghost-gate"]),
    ),
    # Duplicate entries do not make a gate "more required".
    Case(
        "duplicate_needs_entry_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-a"], jobs=["gate-a"]),
    ),
    # An empty needs list proves nothing.
    Case(
        "empty_needs_fails",
        1,
        synthetic_workflow(needs=[]),
    ),
    # A workflow without the aggregate fails closed.
    Case(
        "missing_aggregate_fails",
        1,
        synthetic_workflow(needs=[], include_aggregate=False),
    ),
    # Consuming results but ignoring non-success must fail the simulation.
    Case(
        "permissive_enforcement_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b"],
                           enforcement=PERMISSIVE_ENFORCEMENT),
    ),
    # Not consuming results at all must fail the structural check.
    Case(
        "nonconsuming_enforcement_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b"],
                           enforcement=NONCONSUMING_ENFORCEMENT),
    ),
    # A mutant enforcement script that skips the second dependency's
    # result must fail: the checker simulates a non-success result at
    # EVERY needs position, so the ignored middle position is exercised.
    # A checker that sampled only the first and last positions accepted
    # this exact script — the #134 rework review reproduction.
    Case(
        "middle_position_loss_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b", "gate-c"],
                           jobs=["gate-a", "gate-b", "gate-c"],
                           enforcement=MIDDLE_IGNORING_ENFORCEMENT),
    ),
    # --- workflow-control false-success mutations (#134 rework P2) ------
    # Step-level `if: false` directly after the enforcement step name (the
    # exact reproduced mutation): GitHub skips the sole enforcement step
    # and the aggregate reports success despite failed dependencies.
    Case(
        "enforcement_step_if_false_fails",
        1,
        insert_after_enforcement_name(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "        if: false\n"),
    ),
    # The same control at the equally valid position after the run block.
    Case(
        "enforcement_step_if_after_run_block_fails",
        1,
        append_after_run_block(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "        if: false\n"),
    ),
    # Step-level `continue-on-error: true` directly after the step name
    # (the second exact reproduced mutation): GitHub runs the step but
    # ignores its failure.
    Case(
        "enforcement_step_continue_on_error_fails",
        1,
        insert_after_enforcement_name(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "        continue-on-error: true\n"),
    ),
    # The same control after the run block.
    Case(
        "enforcement_step_continue_on_error_after_run_block_fails",
        1,
        append_after_run_block(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "        continue-on-error: true\n"),
    ),
    # A job-level `if: false` skips the whole aggregate: the skipped
    # required gate reports success while every dependency result is
    # discarded.
    Case(
        "aggregate_job_if_false_fails",
        1,
        replace_aggregate_if(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "    if: false\n"),
    ),
    # No job-level `if:` at all: default `needs` semantics skip the
    # aggregate when a dependency fails, so a failed gate never reaches
    # the enforcement script.
    Case(
        "aggregate_job_if_missing_fails",
        1,
        replace_aggregate_if(
            synthetic_workflow(needs=["gate-a", "gate-b"]), ""),
    ),
    # Any condition other than the pinned safe shape can skip the
    # aggregate (this one skips it whenever the run is cancelled).
    Case(
        "aggregate_job_if_alternate_condition_fails",
        1,
        replace_aggregate_if(
            synthetic_workflow(needs=["gate-a", "gate-b"]),
            "    if: ${{ !cancelled() }}\n"),
    ),
    # A job-level `continue-on-error` turns a failed enforcement result
    # into a green run.
    Case(
        "aggregate_job_continue_on_error_fails",
        1,
        synthetic_workflow(needs=["gate-a", "gate-b"]).replace(
            TIMEOUT_LINE, TIMEOUT_LINE + "    continue-on-error: true\n"),
    ),
]


def run_case(case: Case, workflow_path: str,
             workflow_text: Optional[str]) -> Optional[str]:
    with tempfile.TemporaryDirectory(prefix="check-aggregate-") as tmp:
        if workflow_text is None:
            path = workflow_path
        else:
            path = os.path.join(tmp, "ci.yml")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(workflow_text)
        command = [sys.executable, CHECKER, "--workflow", path]
        # The checker under test is this repository's own checked-in
        # script run against a fixture this process just wrote; a nonzero
        # exit is the negative cases' expected outcome, so check=False is
        # intentional — the exit status is the assertion.
        proc = subprocess.run(  # nosec
            command, capture_output=True, text=True, check=False)
        if proc.returncode != case.expect_rc:
            return ("%s: expected rc=%d, got rc=%d\n%s"
                    % (case.name, case.expect_rc, proc.returncode,
                       proc.stderr.strip()))
        return None


def main() -> int:
    failures = []
    for case in CASES:
        problem = run_case(case, "", case.workflow)
        if problem:
            failures.append(problem)
    # Positive case against the real checked-in workflow: the checker must
    # accept it as-is, so a required gate cannot silently disappear.
    real_case = Case("real_workflow_happy", 0, "")
    problem = run_case(real_case, REAL_WORKFLOW, None)
    if problem:
        failures.append(problem)
    total = len(CASES) + 1
    if failures:
        print("FAIL: check-ci-aggregate regressions (%d):"
              % len(failures), file=sys.stderr)
        for problem in failures:
            print("  " + problem.replace("\n", "\n  "), file=sys.stderr)
        return 1
    print("OK: check-ci-aggregate regressions passed (%d cases)."
          % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())

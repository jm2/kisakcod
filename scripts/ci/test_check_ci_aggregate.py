#!/usr/bin/env python3
"""Negative and positive regression tests for check-ci-aggregate.py.

The aggregate-enrollment checker is a fail-closed CI gate, so its failure
modes matter as much as its success path.  Each case below builds a small
synthetic workflow in a temporary directory and asserts the checker's exit
status.  In particular the regressions cover the ways the single
branch-protection aggregate can silently stop protecting the build:

* a required gate dropped from `scaffolding-complete.needs` (the case the
  #134 rework review found for the sanitizer and checker jobs),
* a job that exists but is not enrolled anywhere,
* a `needs:` entry that references no real job,
* duplicate needs entries, an empty needs list, and a missing aggregate,
* an enforcement step that consumes the results but ignores non-success,
* an enforcement step that does not consume the results at all.

The positive cases validate a well-formed synthetic workflow and the real
checked-in `.github/workflows/ci.yml`, so a required gate cannot silently
disappear from selection or from the aggregate without this suite failing.

Run directly:

    python3 scripts/ci/test_check_ci_aggregate.py

Exits non-zero and prints the failing case names if any assertion fails.
"""

from __future__ import annotations

import os
import subprocess
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


def synthetic_workflow(needs: List[str],
                       jobs: Optional[List[str]] = None,
                       enforcement: str = GOOD_ENFORCEMENT,
                       include_aggregate: bool = True) -> str:
    """Build a minimal workflow with the real file's shape and indentation."""
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


class Case:
    def __init__(self, name: str, expect_rc: int, workflow: str) -> None:
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
        proc = subprocess.run(command, capture_output=True, text=True)
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

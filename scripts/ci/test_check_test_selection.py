#!/usr/bin/env python3
"""Negative and positive regression tests for check-test-selection.py.

The checker is a fail-closed CI gate, so its failure modes matter as much as
its success path.  Each case below builds a small synthetic manifest in a
temporary directory and asserts the checker's exit status.  In particular the
regressions cover the three ways a selection can silently stop protecting the
build:

* a selected test that is no longer discovered (removed or renamed target),
* a run that executes no tests at all,
* a selected test that reports a non-execution status (disabled, skipped,
  failed dependency) while ``ctest`` still exits 0,
* a legitimate platform absence that is explicitly classified rather than
  silently intersected away.

Run directly:

    python3 scripts/ci/test_check_test_selection.py

Exits non-zero and prints the failing case names if any assertion fails.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from typing import List, Optional

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "check-test-selection.py")


class Case:
    def __init__(self, name: str, expect_rc: int, **files: str) -> None:
        self.name = name
        self.expect_rc = expect_rc
        self.files = files


CASES: List[Case] = [
    # --- success paths -------------------------------------------------
    # Canonical (POSIX reference) validation: exact inventory match.
    Case(
        "canonical_exact_happy",
        0,
        inventory="A\nB\nC\n",
        selected="A\n",
        excluded="B\treason-b\nC\treason-c\n",
        discovered="A\nB\nC\n",
        scope="exact",
    ),
    # Platform profile validation with an explicit, justified absence.
    Case(
        "platform_absence_happy",
        0,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
        discovered="A\nB\n",
        executed="1/1 Test #1: A ... Passed\n",
        enforce="1",
    ),
    # --- failure paths -------------------------------------------------
    # A selected test that the platform stopped discovering must fail.
    Case(
        "removed_selected_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
        discovered="B\n",
        executed="1/1 Test #1: B ... Passed\n",
        enforce="1",
    ),
    # Zero executed tests must fail even if discovery looks fine.
    Case(
        "empty_executed_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
        discovered="A\nB\n",
        executed="",
        enforce="1",
    ),
    # Empty discovery must fail (selected test cannot have run).
    Case(
        "empty_discovery_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
        discovered="",
        executed="",
        enforce="1",
    ),
    # A discovered test that is in neither selected nor excluded must fail.
    Case(
        "unclassified_discovered_fails",
        1,
        inventory="A\nB\nC\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="\n",
        discovered="A\nB\nC\n",
        executed="1/1 Test #1: A ... Passed\n",
        enforce="1",
    ),
    # A disabled selected test alongside a passing selected test produces a
    # zero ctest exit status but proves nothing: the disabled entry reports
    # "***Not Run (Disabled)" and must count as non-execution.
    Case(
        "disabled_selected_fails",
        1,
        inventory="A\nB\nC\n",
        selected="A\nB\n",
        excluded="",
        absent="C\treason-c\n",
        discovered="A\nB\n",
        executed=("1/2 Test #1: A ................................"
                  "***Not Run (Disabled)   0.00 sec\n"
                  "    Start 2: B\n"
                  "2/2 Test #2: B ................................"
                  "   Passed    0.01 sec\n"
                  "\n"
                  "100% tests passed, 0 tests failed out of 1\n"
                  "\n"
                  "The following tests did not run:\n"
                  "\t  1 - A (Disabled)\n"),
        enforce="1",
    ),
    # A selected test whose dependency failed reports "***Not Run (Depends
    # on failed test)"; that is not execution either.
    Case(
        "dependency_not_run_selected_fails",
        1,
        inventory="A\nB\nC\n",
        selected="A\nB\n",
        excluded="",
        absent="C\treason-c\n",
        discovered="A\nB\n",
        executed=("1/2 Test #1: A ................................"
                  "   Passed    0.01 sec\n"
                  "2/2 Test #2: B ................................"
                  "***Not Run (Depends on failed test)   0.00 sec\n"),
        enforce="1",
    ),
    # A skipped selected test (SKIP_RETURN_CODE) executes no test body.
    Case(
        "skipped_selected_fails",
        1,
        inventory="A\nB\nC\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\n",
        discovered="A\nB\n",
        executed=("1/2 Test #1: A ................................"
                  "***Skipped   0.00 sec\n"
                  "2/2 Test #2: B ................................"
                  "   Passed    0.01 sec\n"),
        enforce="1",
    ),
    # A result line without a recognizable status is unattributable; the
    # checker must refuse the evidence instead of trusting it.
    Case(
        "unattributable_execution_fails",
        1,
        inventory="A\nB\nC\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\n",
        discovered="A\nB\n",
        executed=("1/2 Test #1: A ................................"
                  "   0.01 sec\n"
                  "2/2 Test #2: B ................................"
                  "   Passed    0.01 sec\n"),
        enforce="1",
    ),
    # Control for the disabled case: the same manifest with every selected
    # test affirmatively executed passes.
    Case(
        "full_execution_control_passes",
        0,
        inventory="A\nB\nC\n",
        selected="A\nB\n",
        excluded="",
        absent="C\treason-c\n",
        discovered="A\nB\n",
        executed=("1/2 Test #1: A ................................"
                  "   Passed    0.01 sec\n"
                  "2/2 Test #2: B ................................"
                  "   Passed    0.01 sec\n"),
        enforce="1",
    ),
    # A not-enrolled exclusion without a reason column must fail.
    Case(
        "exclusion_without_reason_fails",
        1,
        inventory="A\nB\n",
        selected="A\n",
        excluded="B\n",
        discovered="A\nB\n",
        scope="exact",
    ),
    # A platform-absence entry without a reason must fail.
    Case(
        "absence_without_reason_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\t\nD\treason-d\n",
        discovered="A\nB\n",
        executed="1/1 Test #1: A ... Passed\n",
        enforce="1",
    ),
    # Executed tests outside the selected set must fail.
    Case(
        "executed_outside_selection_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
        discovered="A\nB\n",
        executed="1/2 Test #1: A ... Passed\n2/2 Test #2: B ... Passed\n",
        enforce="1",
    ),
    # An inventory test removed from the exact discovery must fail.
    Case(
        "inventory_removal_fails_exact",
        1,
        inventory="A\nB\n",
        selected="A\n",
        excluded="B\treason-b\n",
        discovered="A\n",
        scope="exact",
    ),
    # A discovered test outside the inventory must fail.
    Case(
        "unclassified_addition_fails",
        1,
        inventory="A\nB\n",
        selected="A\n",
        excluded="B\treason-b\n",
        discovered="A\nB\nZ\n",
        scope="exact",
    ),
    # An empty selection proves nothing.
    Case(
        "empty_selection_fails",
        1,
        inventory="A\nB\n",
        selected="\n",
        excluded="B\treason-b\n",
        discovered="A\nB\n",
        scope="exact",
    ),
]


def run_case(case: Case) -> Optional[str]:
    with tempfile.TemporaryDirectory(prefix="check-selection-") as tmp:
        paths = {}
        for key in ("inventory", "selected", "excluded", "absent",
                    "discovered", "executed"):
            value = case.files.get(key)
            if value is None:
                continue
            path = os.path.join(tmp, key + ".txt")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(value)
            paths[key] = path

        command = [sys.executable, CHECKER, "--label", case.name,
                   "--inventory", paths["inventory"],
                   "--selected", paths["selected"],
                   "--excluded", paths["excluded"]]
        if "absent" in paths:
            command += ["--absent", paths["absent"]]
        if "discovered" in paths:
            command += ["--discovered", paths["discovered"],
                        "--discovered-scope", case.files.get("scope", "subset")]
        if "executed" in paths:
            command += ["--executed", paths["executed"]]
        if case.files.get("enforce"):
            command.append("--enforce-platform-absence")

        proc = subprocess.run(command, capture_output=True, text=True)
        if proc.returncode != case.expect_rc:
            return ("%s: expected rc=%d, got rc=%d\n%s"
                    % (case.name, case.expect_rc, proc.returncode,
                       proc.stderr.strip()))
        return None


def main() -> int:
    failures = [problem for problem in (run_case(c) for c in CASES)
                if problem]
    if failures:
        print("FAIL: check-test-selection regressions (%d):"
              % len(failures), file=sys.stderr)
        for problem in failures:
            print("  " + problem.replace("\n", "\n  "), file=sys.stderr)
        return 1
    print("OK: check-test-selection regressions passed (%d cases)."
          % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())

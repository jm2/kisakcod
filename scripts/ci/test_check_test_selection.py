#!/usr/bin/env python3
"""Negative and positive regression tests for check-test-selection.py."""
#
# The checker is a fail-closed CI gate, so its failure modes matter as much as
# its success path.  Each case below builds a small synthetic manifest in a
# temporary directory and asserts the checker's exit status.  In particular the
# regressions cover the three ways a selection can silently stop protecting the
# build:
#
# * a selected test that is no longer discovered (removed or renamed target),
# * a run that executes no tests at all,
# * a selected test that reports a non-execution status (disabled, skipped,
#   failed dependency) while ``ctest`` still exits 0,
# * a legitimate platform absence that is explicitly classified rather than
#   silently intersected away.
#
# Run directly:
#
#     python3 scripts/ci/test_check_test_selection.py
#
# Exits non-zero and prints the failing case names if any assertion fails.

from __future__ import annotations

import contextlib
import importlib.util
import io
import os
# subprocess remains only for the single end-of-suite CLI smoke test, which
# executes this repository's own checked-in script with a fully static,
# auditable argv; every per-case invocation runs the checker's CLI entry
# point in-process instead (see run_checker_cli and cli_smoke_test).
import subprocess  # nosec
import sys
import tempfile
from typing import List, Optional, Tuple

CHECKER_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "check-test-selection.py")
REPO_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir))

# Load the checked-in checker as a module so each case can call its CLI
# entry point (`main`) directly and capture the exit status it would hand
# the shell, without spawning a process per case.
_CHECKER_SPEC = importlib.util.spec_from_file_location(
    "check_test_selection", CHECKER_PATH)
if _CHECKER_SPEC is None or _CHECKER_SPEC.loader is None:
    # Fail closed when the module cannot be loaded as a spec/loader pair:
    # a missing or malformed checker file must break the suite loudly, not
    # crash mid-assertion with an opaque AttributeError.
    raise RuntimeError(
        "cannot load %s as a Python module (missing spec or loader)"
        % CHECKER_PATH)
CHECKER = importlib.util.module_from_spec(_CHECKER_SPEC)
_CHECKER_SPEC.loader.exec_module(CHECKER)


class Case:
    """One synthetic manifest set and the checker exit it must yield."""

    def __init__(self, name: str, expect_rc: int, **files: str) -> None:
        """Record the case name, expected checker exit status, and manifests."""
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
    # Platform enforcement with --discovered omitted must be rejected as a
    # usage error: the early "no discovery evidence requested" success path
    # would otherwise let the advertised fail-closed enforcement mode pass
    # on zero evidence.
    Case(
        "enforcement_without_discovery_flag_fails",
        1,
        inventory="A\nB\nC\nD\n",
        selected="A\n",
        excluded="B\treason-b\n",
        absent="C\treason-c\nD\treason-d\n",
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
                  + "***Not Run (Disabled)   0.00 sec\n"
                  + "    Start 2: B\n"
                  + "2/2 Test #2: B ................................"
                  + "   Passed    0.01 sec\n"
                  + "\n"
                  + "100% tests passed, 0 tests failed out of 1\n"
                  + "\n"
                  + "The following tests did not run:\n"
                  + "\t  1 - A (Disabled)\n"),
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
                  + "   Passed    0.01 sec\n"
                  + "2/2 Test #2: B ................................"
                  + "***Not Run (Depends on failed test)   0.00 sec\n"),
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
                  + "***Skipped   0.00 sec\n"
                  + "2/2 Test #2: B ................................"
                  + "   Passed    0.01 sec\n"),
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
                  + "   0.01 sec\n"
                  + "2/2 Test #2: B ................................"
                  + "   Passed    0.01 sec\n"),
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
                  + "   Passed    0.01 sec\n"
                  + "2/2 Test #2: B ................................"
                  + "   Passed    0.01 sec\n"),
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
    # --reference-absent under the default subset discovery scope is
    # silently vacuous: check_discovery consults the exemption (and its
    # stale-entry check) only in exact scope, so the combination must be
    # rejected up front instead of exiting 0 while proving nothing. The
    # manifest itself would pass subset validation without the guard.
    Case(
        "reference_absent_subset_scope_fails",
        1,
        inventory="A\nB\n",
        selected="A\n",
        excluded="B\treason-b\n",
        discovered="A\nB\n",
        reference_absent="B\n",
    ),
    # Control for the vacuity guard: the same exemption in exact scope,
    # with the reference-absent test genuinely undiscovered, keeps its
    # full force and passes.
    Case(
        "reference_absent_exact_scope_passes",
        0,
        inventory="A\nB\n",
        selected="A\n",
        excluded="B\treason-b\n",
        discovered="A\n",
        reference_absent="B\n",
        scope="exact",
    ),
]


def run_checker_cli(argv: List[str]) -> Tuple[int, str]:
    """Run the checker's CLI main() in-process; return (rc, stderr text)."""
    stderr = io.StringIO()
    with contextlib.redirect_stderr(stderr), \
            contextlib.redirect_stdout(io.StringIO()):
        try:
            code = CHECKER.main(argv)
        except SystemExit as exit_exc:  # argparse usage exits, if any
            raw = exit_exc.code
            code = raw if isinstance(raw, int) else (0 if raw is None else 1)
    return code, stderr.getvalue()


def run_case(case: Case) -> Optional[str]:
    with tempfile.TemporaryDirectory(prefix="check-selection-") as tmp:
        paths = {}
        for key in ("inventory", "selected", "excluded", "absent",
                    "discovered", "executed", "reference_absent"):
            value = case.files.get(key)
            if value is None:
                continue
            path = os.path.join(tmp, key + ".txt")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(value)
            paths[key] = path

        # Each case invokes the checker's own CLI entry point — the same
        # main() argv surface the shell would reach, with the optional
        # manifests appended exactly as the CLI consumes them — and asserts
        # the exit status it returns: a nonzero status is the negative
        # cases' expected outcome, so no explicit exception handling is
        # wanted here either.
        argv = ["--label", case.name,
                "--inventory", paths["inventory"],
                "--selected", paths["selected"],
                "--excluded", paths["excluded"]]
        if "absent" in paths:
            argv += ["--absent", paths["absent"]]
        if "discovered" in paths:
            argv += ["--discovered", paths["discovered"],
                     "--discovered-scope", case.files.get("scope", "subset")]
        if "reference_absent" in paths:
            argv += ["--reference-absent", paths["reference_absent"]]
        if "executed" in paths:
            argv += ["--executed", paths["executed"]]
        if case.files.get("enforce"):
            argv += ["--enforce-platform-absence"]
        code, stderr = run_checker_cli(argv)
        if code != case.expect_rc:
            return ("%s: expected rc=%d, got rc=%d\n%s"
                    % (case.name, case.expect_rc, code, stderr.strip()))
        return None


def cli_smoke_test() -> Optional[str]:
    """Run the checked-in checker as a real process, argv fully static."""
    # The one process-level assertion: the checked-in script executes under
    # the system python3 interpreter (the same interpreter its shebang
    # resolves and the one running this suite) against this repository's
    # own checked-in selection manifests and exits 0. Every argv element is
    # a string literal, so static analyzers can verify the invocation
    # without tracing dynamic values; the relative paths resolve against
    # the repo root passed as cwd.
    proc = subprocess.run(  # nosec
        ["python3", "scripts/ci/check-test-selection.py",
         "--label", "cli-smoke",
         "--inventory", "scripts/ci/test-selection/portable-inventory.txt",
         "--selected", "scripts/ci/test-selection/windows-x86.selected.txt",
         "--excluded", "scripts/ci/test-selection/windows-x86.excluded.tsv",
         "--absent", "scripts/ci/test-selection/windows-x86.absent.tsv"],
        cwd=REPO_ROOT, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        return ("cli_smoke: expected rc=0, got rc=%d\n%s"
                % (proc.returncode, proc.stderr.strip()))
    return None


def main() -> int:
    failures = [problem for problem in (run_case(c) for c in CASES)
                if problem]
    # Process-level smoke: the checked-in script itself runs as a CLI
    # against the repository's real selection manifests.
    smoke = cli_smoke_test()
    if smoke:
        failures.append(smoke)
    total = len(CASES) + 1
    if failures:
        print("FAIL: check-test-selection regressions (%d):"
              % len(failures), file=sys.stderr)
        for problem in failures:
            print("  " + problem.replace("\n", "\n  "), file=sys.stderr)
        return 1
    print("OK: check-test-selection regressions passed (%d cases)."
          % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())

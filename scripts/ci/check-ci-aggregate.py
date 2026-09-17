#!/usr/bin/env python3
"""Fail closed on required-gate enrollment drift in .github/workflows/ci.yml.

`scaffolding-complete` is the single branch-protection aggregate for CI: the
workflow promises that it depends on EVERY other job in the workflow, and its
"Enforce required job results" step must fail when any dependency result is
not `success`. Without mechanical pinning, two silent failure modes exist:

* a required gate (e.g. a sanitizer or checker job) is added to the workflow
  but never enrolled in `needs:` — the aggregate then succeeds while the gate
  fails or never runs;
* the enforcement step is weakened (results not consumed, non-success
  ignored, hard-coded exit) — the aggregate then succeeds despite a failed
  dependency.

This checker pins both invariants. It parses the workflow, requires the
aggregate's `needs:` list to equal every other job exactly (missing and
extra entries both fail), extracts the enforcement script, and executes it
against synthetic result vectors: an all-success run must exit 0, and each
non-success kind (failure / skipped / cancelled) must exit non-zero wherever
it appears, so a failed required gate cannot yield aggregate success.

Run directly:

    python3 scripts/ci/check-ci-aggregate.py [--workflow PATH]

Exits non-zero on any violation; an unparsable or absent aggregate fails
closed as well.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

AGGREGATE_JOB = "scaffolding-complete"
ENFORCEMENT_STEP_NAME = "Enforce required job results"
JOIN_EXPRESSION = re.compile(
    r"\$\{\{\s*join\(needs\.\*\.result,\s*'\s'\)\s*\}\}")
JOB_KEY = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(?:#.*)?$")
NEEDS_KEY = re.compile(r"^    needs:\s*$")
NEEDS_ITEM = re.compile(r"^      - ([A-Za-z0-9_-]+)\s*$")
NON_SUCCESS_KINDS = ("failure", "skipped", "cancelled")
SIMULATION_TIMEOUT_SECONDS = 30
DEFAULT_WORKFLOW = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    os.pardir, os.pardir, ".github", "workflows", "ci.yml"))


class CheckError(Exception):
    """A fail-closed violation of the aggregate contract."""


def read_text(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read()
    except OSError as exc:
        raise CheckError("cannot read workflow %s: %s" % (path, exc))


def split_jobs(text: str) -> dict:
    """Return {job_id: job_body} for the top-level `jobs:` section.

    Only two-space-indented `key:` lines inside the `jobs:` block are job
    ids; everything else (steps, strategy, matrix entries) is nested deeper
    or list items and stays inside the current job's body.
    """
    lines = text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if line.rstrip() == "jobs:":
            start = index + 1
            break
    if start is None:
        raise CheckError("workflow has no top-level `jobs:` section")
    order = []
    bodies = {}
    current = None
    for line in lines[start:]:
        if line and not line.startswith(" "):
            break  # dedent below the jobs section: next top-level key
        match = JOB_KEY.match(line)
        if match:
            current = match.group(1)
            if current in bodies:
                raise CheckError("duplicate job key: %s" % current)
            order.append(current)
            bodies[current] = []
        elif current is not None:
            bodies[current].append(line)
    if not order:
        raise CheckError("the `jobs:` section parsed to zero jobs")
    return {name: "\n".join(bodies[name]) for name in order}


def extract_needs(job_body: str) -> list:
    """Extract the aggregate's `needs:` entry list, order preserved."""
    lines = job_body.splitlines()
    for index, line in enumerate(lines):
        if NEEDS_KEY.match(line):
            needs = []
            for candidate in lines[index + 1:]:
                item = NEEDS_ITEM.match(candidate)
                if item is None:
                    break
                needs.append(item.group(1))
            if not needs:
                raise CheckError(
                    "%s.needs is empty: the aggregate protects nothing"
                    % AGGREGATE_JOB)
            return needs
    raise CheckError("%s has no `needs:` list" % AGGREGATE_JOB)


def extract_enforcement(job_body: str) -> str:
    """Extract the aggregate enforcement step's shell script."""
    lines = job_body.splitlines()
    for index, line in enumerate(lines):
        if "- name:" in line and ENFORCEMENT_STEP_NAME in line:
            run_index = None
            for offset in range(index + 1, min(index + 6, len(lines))):
                if re.match(r"^\s+run: \|\s*$", lines[offset]):
                    run_index = offset
                    break
            if run_index is None:
                raise CheckError(
                    "the %s step has no literal `run: |` block"
                    % ENFORCEMENT_STEP_NAME)
            run_line = lines[run_index]
            base_indent = len(run_line) - len(run_line.lstrip())
            block = []
            for candidate in lines[run_index + 1:]:
                if not candidate.strip():
                    block.append("")
                    continue
                if len(candidate) - len(candidate.lstrip()) <= base_indent:
                    break
                block.append(candidate)
            while block and not block[-1].strip():
                block.pop()
            script = "\n".join(
                line[min(base_indent + 2, len(line)):] if line.strip()
                else "" for line in block)
            if not script.strip():
                raise CheckError(
                    "the %s step's run block is empty"
                    % ENFORCEMENT_STEP_NAME)
            return script
    raise CheckError(
        "no `%s` step found in %s" % (ENFORCEMENT_STEP_NAME, AGGREGATE_JOB))


def simulate(script: str, results: list) -> int:
    """Execute the enforcement script with a synthetic result vector."""
    substituted = JOIN_EXPRESSION.sub(" ".join(results), script, count=1)
    descriptor, path = tempfile.mkstemp(
        prefix="aggregate-sim-", suffix=".sh")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(substituted)
        try:
            completed = subprocess.run(
                ["bash", path], capture_output=True, text=True,
                timeout=SIMULATION_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            raise CheckError(
                "enforcement simulation timed out after %ds"
                % SIMULATION_TIMEOUT_SECONDS)
        return completed.returncode
    finally:
        os.unlink(path)


def check_workflow(path: str) -> str:
    """Validate enrollment completeness and fail-closed enforcement."""
    jobs = split_jobs(read_text(path))
    if AGGREGATE_JOB not in jobs:
        raise CheckError("no `%s` aggregate job in the workflow"
                         % AGGREGATE_JOB)
    others = set(jobs) - {AGGREGATE_JOB}
    needs = extract_needs(jobs[AGGREGATE_JOB])
    need_set = set(needs)
    duplicates = sorted(name for name in need_set
                        if needs.count(name) > 1)
    if duplicates:
        raise CheckError("duplicate %s.needs entries: %s"
                         % (AGGREGATE_JOB, ", ".join(duplicates)))
    missing = sorted(others - need_set)
    if missing:
        raise CheckError(
            "required gates missing from %s.needs (they can fail without "
            "failing the aggregate): %s"
            % (AGGREGATE_JOB, ", ".join(missing)))
    extra = sorted(need_set - others)
    if extra:
        raise CheckError("%s.needs entries with no such job: %s"
                         % (AGGREGATE_JOB, ", ".join(extra)))
    script = extract_enforcement(jobs[AGGREGATE_JOB])
    if not JOIN_EXPRESSION.search(script):
        raise CheckError(
            "the enforcement step does not consume every needs result via "
            "join(needs.*.result)")
    simulations = 0
    if simulate(script, ["success"] * len(needs)) != 0:
        raise CheckError(
            "the enforcement step fails an all-success aggregate run")
    simulations += 1
    for kind in NON_SUCCESS_KINDS:
        for position in sorted({0, len(needs) - 1}):
            vector = ["success"] * len(needs)
            vector[position] = kind
            if simulate(script, vector) == 0:
                raise CheckError(
                    "the aggregate succeeds while a required gate is %s "
                    "(needs position %d)" % (kind, position))
            simulations += 1
    return ("OK: %s enrolls all %d other jobs; enforcement verified "
            "fail-closed (%d simulations)."
            % (AGGREGATE_JOB, len(needs), simulations))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pin scaffolding-complete aggregate enrollment and "
                    "fail-closed enforcement.")
    parser.add_argument(
        "--workflow", default=DEFAULT_WORKFLOW,
        help="path to the workflow file (default: %(default)s)")
    arguments = parser.parse_args()
    try:
        print(check_workflow(arguments.workflow))
    except CheckError as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

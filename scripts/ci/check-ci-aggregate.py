#!/usr/bin/env python3
"""Fail closed on required-gate enrollment drift in .github/workflows/ci.yml."""
# `scaffolding-complete` is the single branch-protection aggregate for CI: the
# workflow promises that it depends on EVERY other job in the workflow, and its
# "Enforce required job results" step must fail when any dependency result is
# not `success`. Without mechanical pinning, silent failure modes exist:
#
# * a required gate (e.g. a sanitizer or checker job) is added to the workflow
#   but never enrolled in `needs:` — the aggregate then succeeds while the gate
#   fails or never runs;
# * the enforcement step is weakened (results not consumed, non-success
#   ignored, hard-coded exit) — the aggregate then succeeds despite a failed
#   dependency;
# * the enforcement step is suppressed by a workflow control rather than by
#   its script: a step-level `if:` evaluating false makes GitHub skip the
#   sole enforcement step, and `continue-on-error: true` makes GitHub ignore
#   its failure — in both cases the aggregate job reports success even though
#   enforcement never rejected anything, while a simulation of the script
#   body alone would still pass (the #134 rework review reproduced both
#   false-success mutations against the exact workflow);
# * the aggregate job itself is skipped or its failure tolerated: without its
#   own `if: ${{ always() && !cancelled() }}` GitHub skips the job when a
#   dependency fails, and a job-level `continue-on-error` turns a failed
#   aggregate into a green run.
#
# This checker pins every invariant. It parses the workflow — treating
# comment and blank lines as transparent so an unindented comment cannot
# conceal a job from the enrollment comparison — requires the aggregate's
# `needs:` list to equal every other job exactly (missing and extra entries
# both fail), pins the aggregate job's condition to the known-safe shape and
# rejects job-level error tolerance, extracts the enforcement step and
# rejects any `if:` / `continue-on-error:` key in that step's mapping
# (outside the run block, where such text is shell), and executes the
# enforcement script against synthetic result vectors: an all-success run
# must exit 0, and each non-success kind (failure / skipped / cancelled)
# must exit non-zero at EVERY needs position, so no dependency can be
# silently ignored — a checker that sampled only the first and last
# positions accepted a mutant enforcement script that skipped the second
# dependency's result (#134 rework review).
#
# Run directly:
#
#     python3 scripts/ci/check-ci-aggregate.py [--workflow PATH]
#
# Exits non-zero on any violation; an unparsable or absent aggregate fails
# closed as well.

from __future__ import annotations

import argparse
import os
import re
import shutil
# subprocess is required to execute the enforcement script under
# simulation; the invocation below is list-form with a literal
# interpreter name and a private script file (see simulate).
import subprocess  # nosec
import sys
import tempfile
from typing import Optional, Sequence

AGGREGATE_JOB = "scaffolding-complete"
ENFORCEMENT_STEP_NAME = "Enforce required job results"
JOIN_EXPRESSION = re.compile(
    r"\$\{\{\s*join\(needs\.\*\.result,\s*'\s'\)\s*\}\}")
JOB_KEY = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(?:#.*)?$")
NEEDS_KEY = re.compile(r"^    needs:\s*$")
NEEDS_ITEM = re.compile(r"^      - ([A-Za-z0-9_-]+)\s*$")
NON_SUCCESS_KINDS = ("failure", "skipped", "cancelled")
SIMULATION_TIMEOUT_SECONDS = 30
# The only aggregate-job condition that keeps the sole enforcement step
# running when a dependency fails (so the script can reject the run) while
# still letting a cancelled run stay cancelled. Pinned exactly: any other
# condition can skip the aggregate and turn a failed gate into success.
AGGREGATE_JOB_IF = "${{ always() && !cancelled() }}"
JOB_IF_KEY = re.compile(r"^    if:(.*)$")
JOB_CONTINUE_ON_ERROR_KEY = re.compile(r"^    continue-on-error:")
# Step-mapping skip/error-tolerance controls. No value of either key is
# safe on the enforcement step: a false `if` skips it, `continue-on-error`
# discards its failure.
STEP_CONTROL_KEY = re.compile(r"^\s+(if|continue-on-error):")
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
        raise CheckError("cannot read workflow %s: %s" % (path, exc)) from exc


def jobs_section_start(lines: list) -> int:
    """Index one past the top-level `jobs:` key, or fail closed."""
    for index, line in enumerate(lines):
        if line.rstrip() == "jobs:":
            return index + 1
    raise CheckError("workflow has no top-level `jobs:` section")


def begin_job(line: str, order: list, bodies: dict) -> str:
    """Start a new two-space job mapping entry, refusing invisible syntax."""
    match = JOB_KEY.match(line)
    if match is None:
        raise CheckError(
            "unsupported job entry at two-space indent (flow style, "
            "quoted key, or inline value would be invisible to the "
            "exact-needs check): %r" % line.strip())
    name = match.group(1)
    if name in bodies:
        raise CheckError("duplicate job key: %s" % name)
    order.append(name)
    bodies[name] = []
    return name


def ends_jobs_section(line: str) -> bool:
    """True when a non-comment, non-blank line dedents to the top level."""
    return (bool(line.strip())
            and not line.lstrip().startswith("#")
            and not line.startswith(" ")
            and not line.startswith("\t"))


def transcribe_jobs_line(line: str, current, order: list,
                         bodies: dict):
    """Fold one indented `jobs:` line into its job's body."""
    # Comment and blank lines are transparent (kept in the current job body
    # verbatim). Any line this parser cannot faithfully attribute fails
    # closed instead of being silently swallowed, so no syntax can make a
    # job disappear here while GitHub Actions would still run it.
    if not line.strip() or line.lstrip().startswith("#"):
        # Transparent for section boundaries, but kept in the current
        # job body so downstream extraction sees the file verbatim.
        if current is not None:
            bodies[current].append(line)
        return current
    if line.startswith("\t"):
        raise CheckError(
            "tab-indented line inside `jobs:` (invalid YAML "
            "indentation; refusing to guess): %r" % line.strip())
    indent = len(line) - len(line.lstrip(" "))
    if indent == 2:
        return begin_job(line, order, bodies)
    if current is None:
        raise CheckError(
            "content before the first job key inside `jobs:` (refusing "
            "to guess): %r" % line.strip())
    bodies[current].append(line)
    return current


def split_jobs(text: str) -> dict:
    """Return {job_id: job_body} for the top-level `jobs:` section."""
    # Only two-space-indented `key:` lines inside the `jobs:` block are job
    # ids; everything else (steps, strategy, matrix entries) is nested deeper
    # or list items and stays inside the current job's body.
    #
    # Comment and blank lines are transparent at any indentation: YAML allows
    # them between mapping entries, so they never end the `jobs:` mapping and
    # never hide a following job from this parser (an *unindented* comment is
    # still inside the mapping — treating it as a boundary would let a comment
    # conceal every job after it from the exact-needs check). A non-comment
    # column-0 line is the next top-level key and ends the section.
    lines = text.splitlines()
    order = []
    bodies = {}
    current = None
    for line in lines[jobs_section_start(lines):]:
        if ends_jobs_section(line):
            break  # dedent below the jobs section: next top-level key
        current = transcribe_jobs_line(line, current, order, bodies)
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


def find_run_marker(lines: list, name_index: int) -> int:
    """Index of the enforcement step's literal `run: |` line."""
    for offset in range(name_index + 1, min(name_index + 6, len(lines))):
        if re.match(r"^\s+run: \|\s*$", lines[offset]):
            return offset
    raise CheckError(
        "the %s step has no literal `run: |` block" % ENFORCEMENT_STEP_NAME)


def dedent_run_block(lines: list, run_index: int) -> str:
    """Extract and dedent the run block's shell text, failing on empty."""
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
            "the %s step's run block is empty" % ENFORCEMENT_STEP_NAME)
    return script


def extract_enforcement(job_body: str) -> str:
    """Extract the aggregate enforcement step's shell script."""
    # Also fails closed when the step mapping carries a skip or
    # error-tolerance control (`if:`, `continue-on-error:`) anywhere outside
    # the run block: GitHub would skip the sole enforcement step or ignore
    # its failure while the script-body simulation still passes, so the step
    # mapping is pinned to the unconditional, error-intolerant shape.
    lines = job_body.splitlines()
    for index, line in enumerate(lines):
        if "- name:" not in line or ENFORCEMENT_STEP_NAME not in line:
            continue
        step_indent = len(line) - len(line.lstrip(" "))
        run_index = find_run_marker(lines, index)
        script = dedent_run_block(lines, run_index)
        run_line = lines[run_index]
        base_indent = len(run_line) - len(run_line.lstrip())
        reject_enforcement_step_controls(
            lines, index, run_index, base_indent, step_indent)
        return script
    raise CheckError(
        "no `%s` step found in %s" % (ENFORCEMENT_STEP_NAME, AGGREGATE_JOB))


def run_block_end(lines: list, run_index: int, base_indent: int) -> int:
    """Index of the first non-blank line at or below the run block indent."""
    for offset in range(run_index + 1, len(lines)):
        candidate = lines[offset]
        if candidate.strip() and (
                len(candidate) - len(candidate.lstrip(" ")) <= base_indent):
            return offset
    return len(lines)


def reject_enforcement_step_controls(lines: list, name_index: int,
                                     run_index: int, base_indent: int,
                                     step_indent: int) -> None:
    """Fail closed on enforcement-step skip/error-tolerance controls."""
    # GitHub skips a step whose `if:` evaluates false and ignores a step's
    # failure under `continue-on-error: true`. Either control on the sole
    # enforcement step lets the aggregate job report success even though
    # nothing rejected the dependency results, while the script simulation
    # still passes — the #134 rework review reproduced both false-success
    # mutations against the exact workflow. No value of either key is safe
    # here, so any occurrence in the step mapping outside the run block
    # (where the same text would be shell) fails closed. Comment and blank
    # lines are transparent; a dedent to the step-list indent ends the step.
    block_end = run_block_end(lines, run_index, base_indent)
    for offset in range(name_index + 1, len(lines)):
        candidate = lines[offset]
        stripped = candidate.strip()
        if not stripped or stripped.startswith("#"):
            continue
        indent = len(candidate) - len(candidate.lstrip(" "))
        if indent <= step_indent:
            break  # left the enforcement step's mapping
        if run_index < offset < block_end:
            continue  # run-block shell text, not a step mapping key
        if STEP_CONTROL_KEY.match(candidate):
            raise CheckError(
                "the %s step carries a skip/error-tolerance control (%r): "
                "GitHub would skip the sole enforcement step or ignore its "
                "failure while the simulated script still passes"
                % (ENFORCEMENT_STEP_NAME, stripped))


def check_aggregate_job_controls(job_body: str) -> None:
    """Pin the aggregate job's own skip/error-tolerance controls."""
    # The aggregate must run when a dependency fails — otherwise GitHub
    # skips the job (default `needs` semantics) and the skipped required
    # gate reports success. Its job-level `if:` is therefore pinned to the
    # known-safe `${{ always() && !cancelled() }}` expression, and any
    # job-level `continue-on-error` (which would discard a failed
    # enforcement result) is rejected.
    job_if = None
    for line in job_body.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if JOB_CONTINUE_ON_ERROR_KEY.match(line):
            raise CheckError(
                "%s carries a job-level `continue-on-error` control: "
                "GitHub would ignore a failed enforcement result"
                % AGGREGATE_JOB)
        match = JOB_IF_KEY.match(line)
        if match is not None:
            if job_if is not None:
                raise CheckError(
                    "%s has more than one job-level `if:`" % AGGREGATE_JOB)
            job_if = " ".join(match.group(1).split())
    if job_if is None:
        raise CheckError(
            "%s has no job-level `if:` — without `%s` GitHub skips the "
            "aggregate when a dependency fails and the skipped required "
            "gate reports success" % (AGGREGATE_JOB, AGGREGATE_JOB_IF))
    if job_if != AGGREGATE_JOB_IF:
        raise CheckError(
            "%s.if is `%s`; the pinned safe shape is `%s` — any other "
            "condition can skip the aggregate and turn a failed gate "
            "into aggregate success" % (AGGREGATE_JOB, job_if,
                                        AGGREGATE_JOB_IF))


def ensure_bash() -> None:
    """Fail closed unless a bash interpreter is available for simulation."""
    if shutil.which("bash") is None:
        raise CheckError(
            "no bash interpreter found for the enforcement simulation")


def simulate(script: str, results: list) -> int:
    """Execute the enforcement script with a synthetic result vector."""
    substituted = JOIN_EXPRESSION.sub(" ".join(results), script, count=1)
    descriptor, path = tempfile.mkstemp(
        prefix="aggregate-sim-", suffix=".sh")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(substituted)
        try:
            # The simulated script's exit status is the result under test:
            # an intentional nonzero outcome is data, never a checker
            # error, so `check=False` is required. The invocation shape is
            # static and auditable: the literal interpreter name (its
            # presence was just verified by ensure_bash) followed by the
            # private mkstemp file this process just wrote — no shell, no
            # untrusted input, and static analyzers can verify the argv.
            completed = subprocess.run(  # nosec
                ["bash", path], capture_output=True, text=True, check=False,
                timeout=SIMULATION_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            raise CheckError(
                "enforcement simulation timed out after %ds"
                % SIMULATION_TIMEOUT_SECONDS) from exc
        return completed.returncode
    finally:
        os.unlink(path)


def check_enrollment(jobs: dict, needs: list) -> None:
    """Fail closed unless the aggregate's needs list equals the job set."""
    # Missing entries let a gate fail without failing the aggregate, extras
    # reference no real job and protect nothing, and duplicates do not make
    # a gate "more required" — all three are drift and all three fail.
    others = set(jobs) - {AGGREGATE_JOB}
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


def verify_enforcement(script: str, needs: list) -> int:
    """Simulate the enforcement script against every result vector."""
    # The all-success vector must pass, and every non-success kind must fail
    # the aggregate at EVERY needs position: a checker that sampled only the
    # first and last positions accepted a mutant enforcement script that
    # skipped the second dependency's result (#134 rework review). Returns
    # the number of simulations executed.
    simulations = 1
    if simulate(script, ["success"] * len(needs)) != 0:
        raise CheckError(
            "the enforcement step fails an all-success aggregate run")
    for kind in NON_SUCCESS_KINDS:
        for position in range(len(needs)):
            vector = ["success"] * len(needs)
            vector[position] = kind
            if simulate(script, vector) == 0:
                raise CheckError(
                    "the aggregate succeeds while a required gate is %s "
                    "(needs position %d)" % (kind, position))
            simulations += 1
    return simulations


def check_workflow(path: str) -> str:
    """Validate enrollment, job/step controls, and fail-closed enforcement."""
    jobs = split_jobs(read_text(path))
    if AGGREGATE_JOB not in jobs:
        raise CheckError("no `%s` aggregate job in the workflow"
                         % AGGREGATE_JOB)
    check_aggregate_job_controls(jobs[AGGREGATE_JOB])
    needs = extract_needs(jobs[AGGREGATE_JOB])
    check_enrollment(jobs, needs)
    script = extract_enforcement(jobs[AGGREGATE_JOB])
    if not JOIN_EXPRESSION.search(script):
        raise CheckError(
            "the enforcement step does not consume every needs result via "
            "join(needs.*.result)")
    ensure_bash()  # fail closed before any simulation if bash is absent
    simulations = verify_enforcement(script, needs)
    return ("OK: %s enrolls all %d other jobs; job condition pinned, "
            "enforcement step unconditional; enforcement verified "
            "fail-closed (%d simulations)."
            % (AGGREGATE_JOB, len(needs), simulations))


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Pin scaffolding-complete aggregate enrollment and "
                    "fail-closed enforcement.")
    parser.add_argument(
        "--workflow", default=DEFAULT_WORKFLOW,
        help="path to the workflow file (default: %(default)s)")
    # `argv=None` makes argparse read sys.argv, so the CLI behavior is
    # unchanged; an explicit list lets the regression suite invoke this
    # exact entry point in-process.
    arguments = parser.parse_args(argv)
    try:
        print(check_workflow(arguments.workflow))
    except CheckError as error:
        print("FAIL: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

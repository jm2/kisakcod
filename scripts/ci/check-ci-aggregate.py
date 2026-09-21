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
#   aggregate into a green run;
# * the enforcement step is faked: locating the step name anywhere in the
#   job body also matches a `- name:` inside another step's run block — a
#   `cat <<'EOF'` heredoc whose text embeds an indented fake enforcement
#   step and a copy of the original script — so the checker reports
#   enforced while the aggregate job runs no enforcement at all (the
#   1aaba85b rework review reproduced this against the exact workflow);
# * the effective shell is customized: GitHub binds `shell:` with step >
#   job `defaults.run.shell` > workflow `defaults.run.shell` precedence,
#   and a wrapper such as `bash -c 'bash "{0}"; exit 0'` discards the
#   enforcement exit status while a checker that always simulates plain
#   bash still sees every vector pass (the 1aaba85b rework review
#   reproduced this at the step scope against the exact workflow).
#
# This checker pins every invariant. It parses the workflow — treating
# comment and blank lines as transparent so an unindented comment cannot
# conceal a job from the enrollment comparison — requires the aggregate's
# `needs:` list to equal every other job exactly (missing and extra entries
# both fail), pins the aggregate job's condition to the known-safe shape and
# rejects job-level error tolerance, parses the aggregate's steps list as an
# exact mapping (a step is a `- name:` list item; its keys sit two spaces
# deeper; `run: |` blocks are consumed as opaque shell text) so literal
# content inside a run block can never masquerade as a step, requires
# exactly one enforcement step owning its own `run:` block, rejects any
# `if:` / `continue-on-error:` key in that step's parsed mapping, resolves
# the enforcement step's effective shell (step > job defaults > workflow
# defaults) and accepts only the GitHub default or plain `bash` — simulated
# with the exact argv GitHub would run — and executes the enforcement
# script against synthetic result vectors: an all-success run must exit 0,
# and each non-success kind (failure / skipped / cancelled) must exit
# non-zero at EVERY needs position, so no dependency can be silently
# ignored — a checker that sampled only the first and last positions
# accepted a mutant enforcement script that skipped the second dependency's
# result (#134 rework review).
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
# simulation; both accepted invocation shapes are list-form with a
# literal interpreter name and a private script file (see
# run_shell_simulation).
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
# The aggregate's steps list is parsed as an exact mapping: a step is a
# six-space `- name:` list item, its keys sit at eight-space indent, and a
# `run: |` block is consumed as opaque shell text. Literal content inside a
# run block — comments, heredocs, fake step text — can never masquerade as
# a step (the 1aaba85b rework review reproduced a `cat <<'EOF'` heredoc
# fake that a text-marker extractor accepted with a full simulation pass),
# and any shape this parser cannot attribute exactly fails closed.
STEPS_KEY = re.compile(r"^    steps:\s*(?:#.*)?$")
STEP_ITEM_NAME = re.compile(r"^      - name:(.*)$")
STEP_MAPPING_KEY = re.compile(r"^        ([A-Za-z][A-Za-z0-9_-]*):(.*)$")
# The only run-block indicators whose body the extraction reproduces as
# shell text exactly; folded (`>`) or quoted scalars would change what the
# simulation executes relative to what GitHub runs.
RUN_BLOCK_INDICATORS = ("|", "|-")
# `defaults:` blocks parse as the exact three-line shape at the scope's own
# indents (`defaults:` / `run:` / `shell: value`). Flow style, a nested
# block, a second key, or an incomplete block fails closed: an unparsed
# shell default could change what executes the enforcement script.
JOB_DEFAULTS_KEY = re.compile(r"^(    defaults:)(.*)$")
JOB_DEFAULTS_RUN = re.compile(r"^      run:\s*$")
JOB_DEFAULTS_SHELL = re.compile(r"^        shell:(.*)$")
WORKFLOW_DEFAULTS_KEY = re.compile(r"^(defaults:)(.*)$")
WORKFLOW_DEFAULTS_RUN = re.compile(r"^  run:\s*$")
WORKFLOW_DEFAULTS_SHELL = re.compile(r"^    shell:(.*)$")
# GitHub runs the enforcement script under the most specific `shell:`
# binding (step > job `defaults.run.shell` > workflow `defaults.run.shell`).
# Only the unset default and plain `bash` preserve exit-status semantics,
# so only they are accepted, and the simulation uses the exact argv GitHub
# would run: a wrapper such as `bash -c 'bash "{0}"; exit 0'` — the
# 1aaba85b rework reproduction, applicable at any of the three scopes —
# discards the enforcement exit status and is refused.
GITHUB_DEFAULT_SHELL_COMMAND = ["bash", "-e"]
PLAIN_BASH_SHELL_COMMAND = ["bash", "--noprofile", "--norc", "-eo",
                            "pipefail"]
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


def collect_raw_block(lines: list, run_index: int) -> list:
    """Gather the raw lines of a `run: |` block until it dedents."""
    # Content is every following line indented deeper than the `run:` key
    # (blank lines transparent); the caller dedents the result, so the
    # extracted text is what GitHub hands the shell.
    run_indent = len(lines[run_index]) - len(lines[run_index].lstrip(" "))
    block = []
    for candidate in lines[run_index + 1:]:
        if not candidate.strip():
            block.append("")
            continue
        if len(candidate) - len(candidate.lstrip(" ")) <= run_indent:
            break
        block.append(candidate)
    return block


def trim_blank_tail(block: list, owner: str) -> list:
    """Drop trailing blank lines; fail closed on an all-blank block."""
    while block and not block[-1].strip():
        block.pop()
    if not block:
        raise CheckError("the %r step's run block is empty" % owner)
    return block


def dedent_run_block(block: list, owner: str) -> str:
    """Dedent raw block lines by their shallowest content indent."""
    # The remaining block must dedent to non-blank shell text, or the
    # step's run body is empty, which fails closed.
    base = min(len(line) - len(line.lstrip(" "))
               for line in block if line.strip())
    script = "\n".join(
        line[base:] if line.strip() else "" for line in block)
    if not script.strip():
        raise CheckError("the %r step's run block is empty" % owner)
    return script


def collect_run_block(lines: list, run_index: int, owner: str) -> str:
    """Extract and dedent a `run: |` block's shell text, failing on empty."""
    return dedent_run_block(
        trim_blank_tail(collect_raw_block(lines, run_index), owner), owner)


def find_steps_index(lines: list) -> int:
    """Locate the aggregate's sole `steps:` key, failing on drift."""
    steps_index = None
    for index, line in enumerate(lines):
        if STEPS_KEY.match(line):
            if steps_index is not None:
                raise CheckError(
                    "%s has more than one `steps:` list" % AGGREGATE_JOB)
            steps_index = index
    if steps_index is None:
        raise CheckError("%s has no `steps:` list" % AGGREGATE_JOB)
    return steps_index


def parse_step_item(line: str) -> dict:
    """Parse a six-space `- name:` list item into a step mapping."""
    item = STEP_ITEM_NAME.match(line)
    if item is None:
        raise CheckError(
            "unsupported %s steps entry (steps parse as exact "
            "`- name:` mappings; flow style, name-less or oddly "
            "indented items refuse to guess): %r"
            % (AGGREGATE_JOB, line.strip()))
    return {"name": item.group(1).strip()}


def skip_run_block(lines: list, index: int, run_indent: int) -> int:
    """Advance past a consumed `run: |` block's opaque shell body."""
    index += 1
    while index < len(lines):
        candidate = lines[index]
        if candidate.strip() and (
                len(candidate) - len(candidate.lstrip(" ")) <= run_indent):
            break
        index += 1
    return index


def bind_run_block(lines: list, index: int, key_indent: int,
                   step: dict, rest: str) -> int:
    """Bind a step's `run: |` block; return the next parse index."""
    if rest not in RUN_BLOCK_INDICATORS:
        raise CheckError(
            "the %r step in %s must carry a literal `run: |` "
            "block (a `%s` scalar would not reproduce as the "
            "shell text GitHub runs)"
            % (step["name"], AGGREGATE_JOB, rest))
    step["run"] = collect_run_block(lines, index, step["name"])
    return skip_run_block(lines, index, key_indent)


def parse_step_keys(lines: list, index: int, step: dict) -> int:
    """Consume one step's eight-space mapping entries; return next index."""
    # A step's keys sit at eight-space indent; a dedent to six spaces or
    # shallower ends the step (next `- name:` item, or the list's end).
    while index < len(lines):
        inner = lines[index]
        inner_stripped = inner.strip()
        if not inner_stripped or inner_stripped.startswith("#"):
            index += 1
            continue
        inner_indent = len(inner) - len(inner.lstrip(" "))
        if inner_indent <= 6:
            break  # next step item, or the end of the steps list
        key_match = STEP_MAPPING_KEY.match(inner)
        if key_match is None:
            raise CheckError(
                "unsupported key inside the %r step in %s (only "
                "eight-space `key: value` entries parse): %r"
                % (step["name"], AGGREGATE_JOB, inner_stripped))
        key = key_match.group(1)
        rest = key_match.group(2).strip()
        if key == "run":
            index = bind_run_block(lines, index, inner_indent, step, rest)
            continue
        if not rest:
            raise CheckError(
                "the %r step key `%s` in %s carries a nested block; "
                "only inline `key: value` entries and `run: |` blocks "
                "parse" % (step["name"], key, AGGREGATE_JOB))
        step[key] = rest
        index += 1
    return index


def parse_aggregate_steps(job_body: str) -> list:
    """Parse the aggregate job's steps list into ordered step mappings."""
    # Structure-aware extraction: a step is a six-space `- name:` list item
    # and its keys sit at eight-space indent, so a `- name:` inside another
    # step's run block is shell text, not a step — the 1aaba85b rework
    # review reproduced a heredoc fake (`cat <<'EOF'` followed by an
    # indented fake `- name:` / `run: |` / script copy) that a text-marker
    # extractor accepted with a full simulation pass. Comment and blank
    # lines are transparent; a dedent to four-space indent or below ends
    # the list; any shape this parser cannot attribute exactly fails
    # closed rather than being silently swallowed.
    lines = job_body.splitlines()
    steps = []
    index = find_steps_index(lines) + 1
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            index += 1
            continue
        indent = len(line) - len(line.lstrip(" "))
        if indent <= 4:
            break  # left the steps list for the rest of the job body
        step = parse_step_item(line)
        steps.append(step)
        index = parse_step_keys(lines, index + 1, step)
    return steps


def extract_enforcement_step(job_body: str) -> dict:
    """Return the aggregate's sole enforcement step from the parsed steps."""
    # The parsed steps list must contain EXACTLY one step with the
    # enforcement name, and that step must own its `run:` block: a fake
    # step name inside another step's run block or a comment no longer
    # matches at all, and name-count drift fails closed.
    steps = parse_aggregate_steps(job_body)
    matches = [step for step in steps
               if step.get("name") == ENFORCEMENT_STEP_NAME]
    if not matches:
        raise CheckError(
            "no `%s` step in %s's parsed steps list (%d steps parsed): a "
            "name match inside another step's run block or a comment no "
            "longer counts" % (ENFORCEMENT_STEP_NAME, AGGREGATE_JOB,
                               len(steps)))
    if len(matches) > 1:
        raise CheckError(
            "%s declares %d `%s` steps; exactly one enforcement step is "
            "pinned" % (AGGREGATE_JOB, len(matches), ENFORCEMENT_STEP_NAME))
    step = matches[0]
    if "run" not in step:
        raise CheckError(
            "the `%s` step in %s has no `run:` key of its own"
            % (ENFORCEMENT_STEP_NAME, AGGREGATE_JOB))
    return step


def reject_enforcement_step_controls(step: dict) -> None:
    """Fail closed on enforcement-step skip/error-tolerance controls."""
    # GitHub skips a step whose `if:` evaluates false and ignores a step's
    # failure under `continue-on-error: true`. Either control on the sole
    # enforcement step lets the aggregate job report success even though
    # nothing rejected the dependency results, while the script simulation
    # still passes — the #134 rework review reproduced both false-success
    # mutations. No value of either key is safe, so any occurrence in the
    # step's parsed mapping (by construction: outside the run block, where
    # the same text is shell) fails closed.
    for key in ("if", "continue-on-error"):
        if key in step:
            raise CheckError(
                "the %s step carries a skip/error-tolerance control "
                "(`%s: %s`): GitHub would skip the sole enforcement step "
                "or ignore its failure while the simulated script still "
                "passes" % (ENFORCEMENT_STEP_NAME, key, step[key]))


def next_significant(lines: list, index: int, base_indent: int):
    """Next non-transparent line deeper than `base_indent`, or None."""
    # Returns (index, indent, stripped_text) for the next line that is
    # neither blank nor a comment and does not dedent to `base_indent` or
    # shallower; None when the block ends (dedent or end of input).
    while index < len(lines):
        text = lines[index].strip()
        if text and not text.startswith("#"):
            indent = len(lines[index]) - len(lines[index].lstrip(" "))
            if indent <= base_indent:
                return None
            return (index, indent, text)
        index += 1
    return None


def expect_defaults_run(lines: list, index: int, base_indent: int,
                        run_key, scope: str) -> int:
    """Consume the required `run:` key line of a `defaults:` block."""
    found = next_significant(lines, index, base_indent)
    if found is not None:
        found_index, indent, text = found
        if indent == base_indent + 2 and run_key.match(lines[found_index]):
            return found_index + 1
        raise CheckError(
            "unsupported %s `defaults:` shape (expected `run:` at "
            "%d-space indent): %r"
            % (scope, base_indent + 2, text))
    raise CheckError(
        "incomplete %s `defaults:` block (expected the exact "
        "`defaults:` / `run:` / `shell:` shape)" % scope)


def expect_defaults_shell(lines: list, index: int, base_indent: int,
                          shell_key, scope: str):
    """Consume the required `shell:` value line; return (shell, next)."""
    found = next_significant(lines, index, base_indent)
    if found is not None:
        found_index, indent, text = found
        shell_match = shell_key.match(lines[found_index])
        if indent == base_indent + 4 and shell_match is not None:
            return shell_match.group(1).strip(), found_index + 1
        raise CheckError(
            "unsupported %s `defaults:` shape (expected `shell:` at "
            "%d-space indent): %r" % (scope, base_indent + 4, text))
    raise CheckError(
        "incomplete %s `defaults:` block (expected the exact "
        "`defaults:` / `run:` / `shell:` shape)" % scope)


def reject_defaults_trailer(lines: list, index: int, base_indent: int,
                            shell_key, scope: str) -> None:
    """Fail on any content after `shell:` before the block dedents."""
    # A second `shell:` entry does not make a default "more set", and any
    # other trailing key would be an unparsed shape: both fail closed.
    found = next_significant(lines, index, base_indent)
    if found is None:
        return
    found_index, indent, text = found
    if indent == base_indent + 4 and shell_key.match(lines[found_index]):
        raise CheckError(
            "%s declares more than one `defaults.run.shell`" % scope)
    raise CheckError(
        "unsupported %s `defaults:` shape (expected `shell:` at "
        "%d-space indent): %r" % (scope, base_indent + 4, text))


def scan_defaults_shell(lines: list, defaults_key, run_key, shell_key,
                        scope: str):
    """Return the scope's `defaults.run.shell` value, or None if unset."""
    # `defaults:` blocks parse as the exact three-line shape at the scope's
    # own indents (`defaults:` / `run:` / `shell: value`). Flow style, a
    # nested block, a second key, or an incomplete block fails closed: an
    # unparsed shell default could change what executes the enforcement
    # script, so guessing is never an option. Comment and blank lines are
    # transparent; a dedent to the `defaults:` indent ends the block.
    for index, line in enumerate(lines):
        match = defaults_key.match(line)
        if match is None:
            continue
        rest = match.group(2).strip()
        if rest and not rest.startswith("#"):
            raise CheckError(
                "unsupported %s `defaults:` entry (flow style or an inline "
                "value would be invisible to the shell check): %r"
                % (scope, line.strip()))
        base_indent = len(line) - len(line.lstrip(" "))
        after_run = expect_defaults_run(lines, index + 1, base_indent,
                                        run_key, scope)
        shell, after_shell = expect_defaults_shell(
            lines, after_run, base_indent, shell_key, scope)
        reject_defaults_trailer(lines, after_shell, base_indent,
                                shell_key, scope)
        return shell
    return None


def pinned_shell_command(value: str, scope: str) -> list:
    """Bind an explicit `shell:` value to the exact argv GitHub would run."""
    # Plain `bash` means `bash --noprofile --norc -eo pipefail {0}`;
    # anything else can alter or discard the enforcement exit status (the
    # 1aaba85b rework reproduction `bash -c 'bash "{0}"; exit 0'` always
    # exits 0), so it is refused with the offending scope named.
    normalized = " ".join(value.split())
    if normalized == "bash":
        return PLAIN_BASH_SHELL_COMMAND
    raise CheckError(
        "unsupported effective `shell: %s` from %s: a custom shell can "
        "discard the enforcement exit status, so only the GitHub default "
        "or plain `bash` is accepted — and simulated exactly"
        % (value, scope))


def effective_shell_command(step: dict, job_body: str, text: str) -> list:
    """Resolve the enforcement script's effective GitHub shell to an argv."""
    # GitHub binds `shell:` with step > job `defaults.run.shell` >
    # workflow `defaults.run.shell` precedence; the unset default on a
    # Linux runner is plain `bash -e {0}`. A checker that always simulates
    # plain bash accepts any wrapper the workflow actually runs (the
    # 1aaba85b rework reproduction passed at step scope against the exact
    # workflow), so the most specific binding decides and the simulation
    # runs exactly what GitHub would run.
    if "shell" in step:
        return pinned_shell_command(
            step["shell"], "the %s step" % ENFORCEMENT_STEP_NAME)
    job_shell = scan_defaults_shell(
        job_body.splitlines(), JOB_DEFAULTS_KEY, JOB_DEFAULTS_RUN,
        JOB_DEFAULTS_SHELL, "the %s job" % AGGREGATE_JOB)
    if job_shell is not None:
        return pinned_shell_command(
            job_shell, "the %s job defaults" % AGGREGATE_JOB)
    workflow_shell = scan_defaults_shell(
        text.splitlines(), WORKFLOW_DEFAULTS_KEY, WORKFLOW_DEFAULTS_RUN,
        WORKFLOW_DEFAULTS_SHELL, "workflow-level")
    if workflow_shell is not None:
        return pinned_shell_command(
            workflow_shell, "workflow-level defaults")
    return GITHUB_DEFAULT_SHELL_COMMAND


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


def run_shell_simulation(command: list, path: str) -> int:
    """Run the enforcement script under the pinned effective-shell argv."""
    # The simulated script's exit status is the result under test: an
    # intentional nonzero outcome is data, never a checker error, so
    # `check=False` is required. Both accepted GitHub shell shapes are
    # simulated with their exact literal argv — only the private mkstemp
    # script path this process just wrote varies — so each shape keeps
    # its distinct flags and the invocation stays static and
    # analyzer-verifiable; `command` is the pinned argv selected by
    # effective_shell_command, and any other value fails closed rather
    # than simulating a shell the checker never accepted. No shell, no
    # untrusted input; the interpreter presence ensure_bash verified.
    if command == GITHUB_DEFAULT_SHELL_COMMAND:
        completed = subprocess.run(  # nosec
            ["bash", "-e", path], capture_output=True, text=True,
            check=False, timeout=SIMULATION_TIMEOUT_SECONDS)
        return completed.returncode
    if command == PLAIN_BASH_SHELL_COMMAND:
        completed = subprocess.run(  # nosec
            ["bash", "--noprofile", "--norc", "-eo", "pipefail", path],
            capture_output=True, text=True, check=False,
            timeout=SIMULATION_TIMEOUT_SECONDS)
        return completed.returncode
    raise CheckError(
        "refusing to simulate under an unpinned shell argv %r; only the "
        "GitHub default and plain `bash` shapes are accepted"
        % (command,))


def simulate(script: str, results: list, command: list) -> int:
    """Execute the enforcement script with a synthetic result vector."""
    substituted = JOIN_EXPRESSION.sub(" ".join(results), script, count=1)
    descriptor, path = tempfile.mkstemp(
        prefix="aggregate-sim-", suffix=".sh")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(substituted)
        try:
            return run_shell_simulation(command, path)
        except subprocess.TimeoutExpired as exc:
            raise CheckError(
                "enforcement simulation timed out after %ds"
                % SIMULATION_TIMEOUT_SECONDS) from exc
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


def verify_enforcement(script: str, needs: list, command: list) -> int:
    """Simulate the enforcement script against every result vector."""
    # The all-success vector must pass, and every non-success kind must fail
    # the aggregate at EVERY needs position: a checker that sampled only the
    # first and last positions accepted a mutant enforcement script that
    # skipped the second dependency's result (#134 rework review). Returns
    # the number of simulations executed. `command` is the pinned argv for
    # the effective GitHub shell, so the simulation runs what GitHub runs.
    simulations = 1
    if simulate(script, ["success"] * len(needs), command) != 0:
        raise CheckError(
            "the enforcement step fails an all-success aggregate run")
    for kind in NON_SUCCESS_KINDS:
        for position in range(len(needs)):
            vector = ["success"] * len(needs)
            vector[position] = kind
            if simulate(script, vector, command) == 0:
                raise CheckError(
                    "the aggregate succeeds while a required gate is %s "
                    "(needs position %d)" % (kind, position))
            simulations += 1
    return simulations


def check_workflow(path: str) -> str:
    """Validate enrollment, controls, effective shell, and enforcement."""
    text = read_text(path)
    jobs = split_jobs(text)
    if AGGREGATE_JOB not in jobs:
        raise CheckError("no `%s` aggregate job in the workflow"
                         % AGGREGATE_JOB)
    job_body = jobs[AGGREGATE_JOB]
    check_aggregate_job_controls(job_body)
    needs = extract_needs(job_body)
    check_enrollment(jobs, needs)
    step = extract_enforcement_step(job_body)
    reject_enforcement_step_controls(step)
    script = step["run"]
    if not JOIN_EXPRESSION.search(script):
        raise CheckError(
            "the enforcement step does not consume every needs result via "
            "join(needs.*.result)")
    command = effective_shell_command(step, job_body, text)
    ensure_bash()  # fail closed before any simulation if bash is absent
    simulations = verify_enforcement(script, needs, command)
    return ("OK: %s enrolls all %d other jobs; job condition pinned, "
            "enforcement step unconditional, effective shell bound; "
            "enforcement verified fail-closed (%d simulations)."
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

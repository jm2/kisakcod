"""Derive the CI and test inventories from repository files."""

# The dashboard never hand-maintains job counts or completion percentages.  CI
# jobs are parsed from ``.github/workflows/*.yml`` and ``*.yaml``; matrix legs
# are expanded with GitHub's Cartesian product and include/exclude semantics so
# the reported invocation count is real.  A matrix shape this line-oriented
# parser cannot interpret is reported as an explicit error instead of a guessed
# count.

from __future__ import annotations

import json
import re
from pathlib import Path

from .paths import CMAKE_PRESETS, TESTS_CMAKE, WORKFLOW_DIR

JOB_KEY_RE = re.compile(r"^[A-Za-z0-9_.-]+:$")
_ENTRY_KEY_RE = re.compile(r"^([A-Za-z0-9_.-]+):\s*(.*)$")
_MAPPING_VALUE_RE = re.compile(r"^[A-Za-z0-9_.-]+\s*:(\s|$)")
_WORKFLOW_SUFFIXES = (".yml", ".yaml")


class MatrixExpansionError(ValueError):
    """Raised when a matrix block uses a shape this parser cannot expand."""


# A parsed matrix is a plain tuple so the parser stays a set of cohesive
# functions: ``(axes, includes, excludes)``.
MatrixSpec = tuple[
    dict[str, list[str]], list[dict[str, str]], list[dict[str, str]]
]


def _indent(line: str) -> int:
    """Return the leading-space width of ``line``."""
    return len(line) - len(line.lstrip(" "))


def _is_skippable(line: str) -> bool:
    """Return True for blank lines and YAML comments."""
    return not line.strip() or line.lstrip().startswith("#")


def _unquote(value: str) -> str:
    """Strip a matching pair of single or double quotes from ``value``."""
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        return value[1:-1]
    return value


# --------------------------------------------------------------------------
# Job-level YAML scanning
# --------------------------------------------------------------------------
def _jobs_block(lines: list[str]) -> list[str]:
    """Return the lines strictly inside the top-level ``jobs:`` mapping."""
    start = None
    for index, raw in enumerate(lines):
        if raw.rstrip() == "jobs:":
            start = index
            break
    if start is None:
        return []
    return _collect_indented(lines, start + 1, 0)


def _collect_indented(lines: list[str], start: int, parent_indent: int) -> list[str]:
    """Collect lines more indented than ``parent_indent``."""
    block: list[str] = []
    for raw in lines[start:]:
        if _is_skippable(raw):
            block.append(raw)
            continue
        if _indent(raw) <= parent_indent:
            break
        block.append(raw)
    return block


def _job_blocks(block: list[str]) -> list[tuple[str, list[str]]]:
    """Split the jobs block into ``(job_id, lines)`` pairs at indent 2."""
    jobs: list[tuple[str, list[str]]] = []
    current_id = None
    current_lines: list[str] = []
    for raw in block:
        if _is_skippable(raw):
            if current_id is not None:
                current_lines.append(raw)
            continue
        stripped = raw.strip()
        if _indent(raw) == 2 and JOB_KEY_RE.match(stripped):
            if current_id is not None:
                jobs.append((current_id, current_lines))
            current_id = stripped[:-1]
            current_lines = [raw]
        elif current_id is not None:
            current_lines.append(raw)
    if current_id is not None:
        jobs.append((current_id, current_lines))
    return jobs


# --------------------------------------------------------------------------
# Matrix expansion
# --------------------------------------------------------------------------
def _matrix_block(job_lines: list[str]) -> list[str] | None:
    """Return the lines inside a job's ``matrix:`` mapping, or None."""
    for index, raw in enumerate(job_lines):
        stripped = raw.strip()
        if not stripped.startswith("matrix:"):
            continue
        if stripped != "matrix:":
            raise MatrixExpansionError(
                "inline matrix declarations are not supported: "
                f"{stripped!r}"
            )
        return _collect_indented(job_lines, index + 1, _indent(raw))
    return None


def _parse_flow_list(text: str, key: str) -> list[str]:
    """Parse an inline flow list such as ``[Debug, Release]``."""
    stripped = text.strip()
    if not (stripped.startswith("[") and stripped.endswith("]")):
        raise MatrixExpansionError(
            f"matrix axis {key!r} must be a list, got {stripped!r}"
        )
    inner = stripped[1:-1].strip()
    if not inner:
        return []
    return [_unquote(item.strip()) for item in inner.split(",") if item.strip()]


def _parse_axis_value(raw: str, key: str) -> str:
    """Parse one block-sequence axis value, rejecting non-scalars."""
    stripped = raw.strip()
    if not stripped.startswith("- "):
        raise MatrixExpansionError(
            f"matrix axis {key!r} is not a scalar list: {stripped!r}"
        )
    value = stripped[2:].strip()
    if not value or _MAPPING_VALUE_RE.match(value):
        raise MatrixExpansionError(
            f"matrix axis {key!r} has a non-scalar entry {value!r}"
        )
    return _unquote(value)


def _parse_axis_values(
    lines: list[str], start: int, key_indent: int, inline: str, key: str
) -> tuple[list[str], int]:
    """Parse an axis, returning its values and the next unconsumed index."""
    if inline:
        return _parse_flow_list(inline, key), start
    values: list[str] = []
    index = start
    while index < len(lines):
        raw = lines[index]
        if _is_skippable(raw):
            index += 1
            continue
        if _indent(raw) <= key_indent:
            break
        values.append(_parse_axis_value(raw, key))
        index += 1
    return values, index


def _split_entry(text: str, label: str) -> tuple[str, str]:
    """Parse one ``key: value`` pair inside an include/exclude list."""
    match = _ENTRY_KEY_RE.match(text)
    if match is None or not match.group(2).strip():
        raise MatrixExpansionError(
            f"matrix {label} entry must be key: value, got {text!r}"
        )
    return match.group(1), _unquote(match.group(2).strip())


def _entry_indent_guard(item_indent: int, indent: int, label: str) -> None:
    """Reject an include/exclude list whose entries are not aligned."""
    if item_indent >= 0 and indent != item_indent:
        raise MatrixExpansionError(
            f"matrix {label} entries are not consistently indented"
        )


def _parse_entry_list(
    lines: list[str], start: int, parent_indent: int, label: str
) -> tuple[list[dict[str, str]], int]:
    """Parse an include/exclude list of small key/value mappings."""
    entries: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    item_indent = -1
    index = start
    while index < len(lines):
        raw = lines[index]
        if _is_skippable(raw):
            index += 1
            continue
        indent = _indent(raw)
        if indent <= parent_indent:
            break
        stripped = raw.strip()
        if stripped.startswith("- "):
            _entry_indent_guard(item_indent, indent, label)
            if current is not None:
                entries.append(current)
            item_indent = indent
            current = {}
            key, value = _split_entry(stripped[2:].strip(), label)
            current[key] = value
        elif current is None:
            raise MatrixExpansionError(
                f"matrix {label} must be a list of mappings"
            )
        elif indent <= item_indent:
            break
        else:
            key, value = _split_entry(stripped, label)
            current[key] = value
        index += 1
    if current is not None:
        entries.append(current)
    return entries, index


def _parse_matrix_block(lines: list[str]) -> MatrixSpec:
    """Parse a job's ``matrix:`` block into axes, includes and excludes."""
    axes: dict[str, list[str]] = {}
    includes: list[dict[str, str]] = []
    excludes: list[dict[str, str]] = []
    index = 0
    while index < len(lines):
        raw = lines[index]
        if _is_skippable(raw):
            index += 1
            continue
        match = _ENTRY_KEY_RE.match(raw.strip())
        if match is None:
            raise MatrixExpansionError(
                f"unsupported matrix line: {raw.strip()!r}"
            )
        key, inline = match.group(1), match.group(2).strip()
        if key in ("include", "exclude"):
            entries, index = _parse_entry_list(
                lines, index + 1, _indent(raw), key
            )
            (includes if key == "include" else excludes).extend(entries)
        else:
            values, index = _parse_axis_values(
                lines, index + 1, _indent(raw), inline, key
            )
            axes[key] = values
    return axes, includes, excludes


def _cartesian(axes: dict[str, list[str]]) -> list[dict[str, str]]:
    """Expand matrix axes into their Cartesian product of assignments."""
    # An include-only matrix declares no axes, and GitHub turns each ``include``
    # entry into its own job rather than a single empty combination.  Returning
    # an empty base list here is what makes ``_apply_includes`` append every
    # include as a separate leg instead of collapsing them onto ``{}``.
    if not axes:
        return []
    combinations: list[dict[str, str]] = [{}]
    for key, values in axes.items():
        combinations = [
            {**combination, key: value}
            for combination in combinations
            for value in values
        ]
    return combinations


def _include_qualifies(
    combination: dict[str, str], include: dict[str, str], axis_keys: set[str]
) -> bool:
    """Return True when an include augments a combination without overwriting it."""
    return all(
        combination.get(key) == value
        for key, value in include.items()
        if key in axis_keys
    )


def _apply_includes(
    base: list[dict[str, str]],
    includes: list[dict[str, str]],
    axis_keys: set[str],
) -> list[dict[str, str]]:
    """Apply GitHub's include semantics to the axis combinations."""
    extras: list[dict[str, str]] = []
    for include in includes:
        matched = [
            combination
            for combination in base
            if _include_qualifies(combination, include, axis_keys)
        ]
        if matched:
            for combination in matched:
                combination.update(include)
        else:
            extras.append(dict(include))
    return base + extras


def _apply_excludes(
    combinations: list[dict[str, str]], excludes: list[dict[str, str]]
) -> list[dict[str, str]]:
    """Remove every combination matched by an exclude entry."""

    def survives(combination: dict[str, str]) -> bool:
        return not any(
            all(combination.get(key) == value for key, value in exclude.items())
            for exclude in excludes
        )

    return [combination for combination in combinations if survives(combination)]


def _expand_matrix(spec: MatrixSpec) -> int:
    """Return the number of invocations a parsed matrix expands to."""
    axes, includes, excludes = spec
    combinations = _apply_includes(_cartesian(axes), includes, set(axes))
    return len(_apply_excludes(combinations, excludes))


def matrix_legs(job_lines: list[str]) -> int:
    """Return a job's matrix invocations (0 when it declares no matrix)."""
    block = _matrix_block(job_lines)
    if block is None:
        return 0
    return _expand_matrix(_parse_matrix_block(block))


# --------------------------------------------------------------------------
# Inventory derivation
# --------------------------------------------------------------------------
def _workflow_paths(workflow_dir: Path) -> list[Path]:
    """Return every ``*.yml``/``*.yaml`` workflow, deduplicated and sorted."""
    found: dict[str, Path] = {}
    for suffix in _WORKFLOW_SUFFIXES:
        for path in workflow_dir.glob(f"*{suffix}"):
            found[path.name] = path
    return [found[name] for name in sorted(found)]


def _job_entry(job_id: str, job_lines: list[str]) -> dict:
    """Build one derived CI job record."""
    name = job_id
    for raw in job_lines:
        stripped = raw.strip()
        if stripped.startswith("name:"):
            value = stripped[len("name:") :].strip()
            if value and not value.startswith("${{"):
                name = f"{job_id} ({value})"
            break
    return {
        "id": job_id,
        "name": name,
        "matrix_legs": matrix_legs(job_lines),
        "self_hosted": any(
            "runs-on:" in raw and "[self-hosted" in raw for raw in job_lines
        ),
    }


def _workflow_entry(path: Path) -> dict:
    """Build one derived workflow record."""
    lines = path.read_text(encoding="utf-8").splitlines()
    jobs = [
        _job_entry(job_id, job_lines)
        for job_id, job_lines in _job_blocks(_jobs_block(lines))
    ]
    return {
        "file": path.name,
        "jobs": jobs,
        "job_count": len(jobs),
        "invocations": sum(max(1, int(job["matrix_legs"])) for job in jobs),
    }


def derive_ci_inventory(workflow_dir: Path = WORKFLOW_DIR) -> dict:
    """Return the derived workflow, job and matrix-invocation inventory."""
    workflows = [_workflow_entry(path) for path in _workflow_paths(workflow_dir)]
    return {
        "workflows": workflows,
        "workflow_count": len(workflows),
        "job_count": sum(workflow["job_count"] for workflow in workflows),
        "invocations": sum(workflow["invocations"] for workflow in workflows),
    }


def derive_test_inventory(
    tests_cmake: Path = TESTS_CMAKE, presets: Path = CMAKE_PRESETS
) -> dict:
    """Return the test inventory derived from CMake files and presets."""
    text = tests_cmake.read_text(encoding="utf-8")
    inventory = {
        "add_test": len(re.findall(r"\badd_test\s*\(", text)),
        "add_executable": len(re.findall(r"\badd_executable\s*\(", text)),
        "test_source_files": len(list((tests_cmake.parent).glob("*_tests.cpp"))),
        "source_contract_files": len(
            list((tests_cmake.parent).glob("*_source_test.cmake"))
        ),
        "configure_presets": 0,
    }
    if presets.exists():
        with presets.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        inventory["configure_presets"] = len(data.get("configurePresets") or [])
    return inventory

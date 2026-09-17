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


def _quoted_span_end(value: str, start: int) -> int:
    """Return the index just past the closing quote opening at ``start``."""
    # Doubled single quotes continue a single-quoted scalar and a backslash
    # escapes the next character inside double quotes.  An unterminated
    # scalar consumes the rest of ``value``: no comment can start inside it.
    quote = value[start]
    index = start + 1
    while index < len(value):
        char = value[index]
        if quote == "'":
            if char == "'":
                if index + 1 < len(value) and value[index + 1] == "'":
                    index += 2
                    continue
                return index + 1
        elif quote == '"':
            if char == "\\":
                index += 2
                continue
            if char == '"':
                return index + 1
        index += 1
    return len(value)


def _strip_yaml_comment(value: str) -> str:
    """Remove an unquoted trailing YAML comment from ``value``."""
    # A ``#`` starts a comment only when it begins the value or is preceded
    # by whitespace, and never inside a quoted scalar.  Stripping it keeps
    # ``runs-on: self-hosted  # box`` from being compared with its comment
    # still attached (reported as hosted) and keeps ``[a, b]  # note`` a
    # readable flow list instead of an unsupported shape.
    index = 0
    while index < len(value):
        char = value[index]
        if char in ("'", '"'):
            index = _quoted_span_end(value, index)
            continue
        if char == "#" and (index == 0 or value[index - 1].isspace()):
            return value[:index].strip()
        index += 1
    return value.strip()


def _split_flow_items(inner: str) -> list[str]:
    """Split flow-list content on commas outside quoted scalars."""
    # A quoted scalar may contain commas (``["linux,debug", "windows"]``),
    # doubled single quotes, and backslash escapes; ``_quoted_span_end``
    # consumes one in a single step, so separators inside it never split an
    # item and quoted hashes and escaped quotes stay intact.
    items: list[str] = []
    start = 0
    index = 0
    while index < len(inner):
        char = inner[index]
        if char in ("'", '"'):
            index = _quoted_span_end(inner, index)
            continue
        if char == ",":
            items.append(inner[start:index])
            start = index + 1
        index += 1
    items.append(inner[start:])
    return items


# --------------------------------------------------------------------------
# Job-level YAML scanning
# --------------------------------------------------------------------------
def _jobs_block(lines: list[str]) -> list[str]:
    """Return the lines strictly inside the top-level ``jobs:`` mapping."""
    # ``jobs:  # build jobs`` is valid YAML: matching the raw line instead
    # of the comment-stripped one missed the mapping entirely and silently
    # inventoried zero jobs for the whole workflow.
    start = None
    for index, raw in enumerate(lines):
        if _indent(raw) == 0 and _strip_yaml_comment(raw) == "jobs:":
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
        # A trailing comment on the job key (``build:  # linux build``) is
        # valid YAML; match the comment-stripped key or the job merges into
        # its predecessor and silently disappears from the inventory.
        normalized = _strip_yaml_comment(raw)
        if _indent(raw) == 2 and JOB_KEY_RE.match(normalized):
            if current_id is not None:
                jobs.append((current_id, current_lines))
            current_id = normalized[:-1]
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
    # ``matrix:  # build matrix`` is the mapping boundary, not an inline
    # declaration; strip the comment before deciding.
    for index, raw in enumerate(job_lines):
        stripped = _strip_yaml_comment(raw)
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
    stripped = _strip_yaml_comment(text)
    if not (stripped.startswith("[") and stripped.endswith("]")):
        raise MatrixExpansionError(
            f"matrix axis {key!r} must be a list, got {stripped!r}"
        )
    inner = stripped[1:-1].strip()
    if not inner:
        return []
    return [
        _unquote(item.strip())
        for item in _split_flow_items(inner)
        if item.strip()
    ]


def _parse_axis_value(raw: str, key: str) -> str:
    """Parse one block-sequence axis value, rejecting non-scalars."""
    stripped = _strip_yaml_comment(raw)
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
    # An unquoted trailing comment is not part of the value: keeping it
    # attached made an exclude such as ``- os: windows # omit windows``
    # never match its combination.  A value that is only a comment reads
    # as YAML null, which this explicit contract does not support.
    value = _strip_yaml_comment(match.group(2))
    if not value:
        raise MatrixExpansionError(
            f"matrix {label} entry must be key: value, got {text!r}"
        )
    return match.group(1), _unquote(value)


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
        key = match.group(1)
        # A trailing comment after the key is not an inline value: keeping
        # it attached made ``include:  # note`` and ``os:  # axes below``
        # fail as unsupported inline declarations instead of reading the
        # block that follows.
        inline = _strip_yaml_comment(match.group(2))
        if key in ("include", "exclude"):
            # ``include``/``exclude`` hold a list of mappings, which this
            # line-oriented parser cannot read from a flow value.  Silently
            # dropping the inline value would publish an inaccurate invocation
            # count, so reject it explicitly instead of guessing.
            if inline:
                raise MatrixExpansionError(
                    f"inline matrix {key} declarations are not supported: "
                    f"{raw.strip()!r}"
                )
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
    # GitHub evaluates ``exclude`` against the original axis combinations and
    # only then applies ``include``.  Exclusions therefore run first, which is
    # what lets an include re-add a combination that an exclude removed and
    # keeps include-only keys out of exclusion matching.
    axes, includes, excludes = spec
    base = _apply_excludes(_cartesian(axes), excludes)
    return len(_apply_includes(base, includes, set(axes)))


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


_RUNS_ON_RE = re.compile(r"^runs-on:\s*(.*)$")


class UnsupportedRunsOnError(ValueError):
    """Raised when a job's ``runs-on`` value uses a shape we cannot read."""


def _flow_items(text: str) -> list[str]:
    """Parse an inline flow list such as ``[self-hosted, linux]``."""
    stripped = _strip_yaml_comment(text)
    if not (stripped.startswith("[") and stripped.endswith("]")):
        raise UnsupportedRunsOnError(
            f"runs-on flow value must be a list, got {stripped!r}"
        )
    inner = stripped[1:-1].strip()
    if not inner:
        return []
    return [
        _unquote(item.strip())
        for item in _split_flow_items(inner)
        if item.strip()
    ]


def _scalar_self_hosted(value: str) -> bool:
    """Return True when one ``runs-on`` scalar is the self-hosted label."""
    stripped = _strip_yaml_comment(value)
    if stripped.startswith("${{"):
        # A GitHub expression is a supported value whose runner is only known
        # at run time; it is not statically the self-hosted label.
        return False
    if stripped.startswith("{") or stripped.startswith("["):
        raise UnsupportedRunsOnError(
            f"unsupported runs-on value {stripped!r}"
        )
    return _unquote(stripped) == "self-hosted"


def _block_self_hosted(
    job_lines: list[str], index: int, key_indent: int
) -> bool:
    """Return True when a block-sequence ``runs-on`` contains self-hosted."""
    found = False
    cursor = index + 1
    while cursor < len(job_lines):
        raw = job_lines[cursor]
        if _is_skippable(raw):
            cursor += 1
            continue
        if _indent(raw) <= key_indent:
            break
        stripped = raw.strip()
        if not stripped.startswith("- "):
            raise UnsupportedRunsOnError(
                "unsupported block runs-on entry; expected a scalar list "
                f"item, got {stripped!r}"
            )
        if _scalar_self_hosted(stripped[2:]):
            found = True
        cursor += 1
    return found


def _self_hosted(job_lines: list[str]) -> bool:
    """Return True when a job's ``runs-on`` selects a self-hosted runner."""
    # The previous probe only recognised ``[self-hosted`` and therefore reported
    # ``self_hosted=False`` for the scalar form, a list that does not start with
    # the label, and a block sequence.  Parse the complete supported value (or
    # raise) so a hidden self-hosted job cannot be reported as hosted.
    for index, raw in enumerate(job_lines):
        match = _RUNS_ON_RE.match(raw.strip())
        if match is None:
            continue
        inline = match.group(1).strip()
        if inline:
            if inline.startswith("["):
                return any(
                    _unquote(item) == "self-hosted"
                    for item in _flow_items(inline)
                )
            return _scalar_self_hosted(inline)
        return _block_self_hosted(job_lines, index, _indent(raw))
    return False


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
        "self_hosted": _self_hosted(job_lines),
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

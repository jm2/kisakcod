#!/usr/bin/env python3
"""Generate the KisakCOD capability dashboard from the evidence manifest.

The manifest (``docs/capability/manifest.json``) is the small authoritative
dataset.  This tool:

* validates the manifest against its own declared enums and the aggregate
  contract (strict fail-closed validation -- a schema error is not a warning);
* derives the CI job inventory from ``.github/workflows/*.yml`` and the test
  inventory from ``tests/CMakeLists.txt`` / ``CMakePresets.json`` so the
  dashboard never hand-maintains job counts or completion percentages;
* renders a deterministic Markdown dashboard and, in ``--check`` mode, fails
  when the committed copy is stale.

Supporting evidence (utility/test/scaffold/instrument rows) is rendered in its
own section and can never advance a production target.  A requested target is
delivered only when every required mode reaches ``packaged_clean_machine`` with
an explicit *successful* package result and a complete, correctly typed
evidence record (exact SHA and run), and both required commercial reference
profiles are validated with their own provenance.  Package success is an
explicit token, not truthiness; a ``failed``/``pending``/arbitrary string never
counts.  Validation-level ranking is fixed in this module, so reordering the
manifest's editable ``enums.validation_levels`` cannot promote a weak level.

This tool has no third-party dependencies; it is intentionally a small
line-oriented parser so it can run in any CI job with a system Python.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = REPO_ROOT / "docs" / "capability" / "manifest.json"
DEFAULT_OUTPUT = REPO_ROOT / "docs" / "CAPABILITY_DASHBOARD.md"
WORKFLOW_DIR = REPO_ROOT / ".github" / "workflows"
TESTS_CMAKE = REPO_ROOT / "tests" / "CMakeLists.txt"
CMAKE_PRESETS = REPO_ROOT / "CMakePresets.json"

GENERATED_MARKER = (
    "<!-- GENERATED FILE - DO NOT EDIT. "
    "Regenerate with `python3 scripts/ci/capability-dashboard.py`. -->"
)

# --------------------------------------------------------------------------
# Mandatory delivery contract
# --------------------------------------------------------------------------
# The delivery policy is fixed by the product requirements (#122 network
# compatibility, #126 acceptance) and must not be editable through the
# manifest.  ``validate_manifest`` rejects an aggregate that omits or weakens
# any of these requirements, and ``compute_aggregate`` always evaluates against
# these values -- never against the manifest's editable ``aggregate`` block --
# so emptying ``required_modes``/``required_commercial_references`` (or flipping
# the boolean gates off) cannot manufacture delivered targets.
MANDATORY_REQUIRED_MODES = ("mp-client", "headless-server")
MANDATORY_REQUIRED_COMMERCIAL_REFERENCES = ("commercial-1.7", "steam-1.8")
MANDATORY_REQUIRED_STRONGEST_VALIDATION = "packaged_clean_machine"
MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION = True
MANDATORY_REQUIRE_PACKAGE_RESULT = True

# Canonical validation ordering.  Ranking is fixed here so that reordering the
# manifest's editable ``enums.validation_levels`` cannot promote a weak level
# past the delivery threshold.  ``validate_manifest`` requires the declared set
# to match these names exactly (order-insensitive), while ``compute_aggregate``
# always ranks against this constant -- never against the manifest order.
CANONICAL_VALIDATION_ORDER = (
    "none",
    "configured",
    "compiled",
    "linked_production",
    "synthetic_integration",
    "licensed_content_startup",
    "original_peer_compatibility",
    "packaged_clean_machine",
)
VALIDATION_RANK = {
    name: rank for rank, name in enumerate(CANONICAL_VALIDATION_ORDER)
}

# Explicit package-result contract.  A package smoke is successful only when it
# reports the success token; ``failed``, ``pending`` or any arbitrary truthy
# string (a hash, ``ok``, a path, ...) is not a successful package result.
PACKAGE_RESULT_SUCCESS = "passed"
PACKAGE_RESULT_VALUES = ("pending", "passed", "failed")

_SHA_RE = re.compile(r"^[0-9a-fA-F]{7,64}$")
_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")

REQUIRED_CAPABILITY_FIELDS = (
    "id",
    "target",
    "mode",
    "owner",
    "dependencies",
    "implementation",
    "production_enrolled",
    "strongest_validation",
    "evidence_kinds",
    "evidence",
    "blocker",
)


def load_manifest(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


# --------------------------------------------------------------------------
# Evidence typing helpers
# --------------------------------------------------------------------------
def _is_sha(value) -> bool:
    """True for a non-empty commit-style SHA (7-64 hex characters)."""
    return isinstance(value, str) and bool(_SHA_RE.match(value.strip()))


def _is_sha256(value) -> bool:
    """True for a full 64-hex-character SHA-256 digest."""
    return isinstance(value, str) and bool(_SHA256_RE.match(value.strip()))


def _is_evidence_run(value) -> bool:
    """True for a non-empty run identifier (string or int, never bool)."""
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return True
    return isinstance(value, str) and bool(value.strip())


def claims_delivery_level(cap: dict) -> bool:
    """True when a capability claims the mandatory packaged-delivery level."""
    rank = VALIDATION_RANK.get(cap.get("strongest_validation"))
    return rank is not None and rank >= VALIDATION_RANK[
        MANDATORY_REQUIRED_STRONGEST_VALIDATION
    ]


def capability_evidence_complete(cap: dict) -> bool:
    """Complete, correctly typed evidence for a promoted capability row.

    Pending rows may leave ``sha``/``run``/``package_result`` null, but a row
    claiming the packaged delivery level must carry an exact SHA, a run id and
    an explicit successful package result.
    """
    evidence = cap.get("evidence") or {}
    return (
        _is_sha(evidence.get("sha"))
        and _is_evidence_run(evidence.get("run"))
        and evidence.get("package_result") == PACKAGE_RESULT_SUCCESS
    )


def reference_provenance_complete(reference: dict) -> bool:
    """True when a validated commercial reference carries its own provenance."""
    return _is_sha256(reference.get("sha256")) and _is_evidence_run(
        reference.get("evidence_run")
    )


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------
def validate_manifest(manifest: dict) -> list[str]:
    """Return a list of human-readable schema errors (empty means valid)."""
    errors: list[str] = []

    if manifest.get("schema_version") != 1:
        errors.append("schema_version must be 1")

    enums = manifest.get("enums") or {}
    validation_levels = enums.get("validation_levels") or []
    modes = enums.get("modes") or []
    impl_states = enums.get("implementation_states") or []
    evidence_kinds = enums.get("evidence_kinds") or []
    provenance_classes = enums.get("provenance_classes") or []

    if not validation_levels:
        errors.append("enums.validation_levels must be a non-empty list")
    else:
        if len(validation_levels) != len(set(validation_levels)):
            errors.append("enums.validation_levels must not contain duplicates")
        unknown_levels = [
            level for level in validation_levels if level not in VALIDATION_RANK
        ]
        missing_levels = [
            level
            for level in CANONICAL_VALIDATION_ORDER
            if level not in validation_levels
        ]
        if unknown_levels:
            errors.append(
                "enums.validation_levels contains unknown levels "
                f"{unknown_levels!r}; the level set is fixed and its order is "
                "not significant"
            )
        if missing_levels:
            errors.append(
                "enums.validation_levels is missing canonical levels "
                f"{missing_levels!r}"
            )
    if not modes:
        errors.append("enums.modes must be a non-empty list")

    targets = manifest.get("targets") or []
    target_ids = [t.get("id") for t in targets]
    if len(set(target_ids)) != len(target_ids):
        errors.append("targets contain duplicate ids")
    if not any(t.get("requested") for t in targets):
        errors.append("targets must contain at least one requested target")

    references = manifest.get("commercial_references") or []
    ref_ids = [r.get("id") for r in references]
    if len(set(ref_ids)) != len(ref_ids):
        errors.append("commercial_references contain duplicate ids")

    # The aggregate must declare the mandatory contract exactly.  Omitted,
    # empty or weakened policy is a schema error, not a warning.
    aggregate = manifest.get("aggregate")
    if not isinstance(aggregate, dict):
        errors.append(
            "aggregate must be an object declaring the mandatory delivery "
            "policy"
        )
        aggregate = {}

    declared_modes = aggregate.get("required_modes")
    if not isinstance(declared_modes, list) or not declared_modes:
        errors.append(
            "aggregate.required_modes must be a non-empty list containing the "
            f"mandatory modes {list(MANDATORY_REQUIRED_MODES)!r}"
        )
        declared_modes = []
    for mode in MANDATORY_REQUIRED_MODES:
        if mode not in declared_modes:
            errors.append(
                f"aggregate.required_modes must include mandatory mode {mode!r}"
            )
    for mode in declared_modes:
        if mode not in modes:
            errors.append(
                f"aggregate.required_mode {mode!r} is not a declared mode"
            )

    declared_refs = aggregate.get("required_commercial_references")
    if not isinstance(declared_refs, list) or not declared_refs:
        errors.append(
            "aggregate.required_commercial_references must be a non-empty "
            "list containing both mandatory commercial references "
            f"{list(MANDATORY_REQUIRED_COMMERCIAL_REFERENCES)!r}"
        )
        declared_refs = []
    for required in MANDATORY_REQUIRED_COMMERCIAL_REFERENCES:
        if required not in declared_refs:
            errors.append(
                "aggregate.required_commercial_references must include "
                f"mandatory reference {required!r}"
            )
    for required in declared_refs:
        if required not in ref_ids:
            errors.append(
                f"aggregate requires commercial reference {required!r} "
                "which is not declared"
            )

    declared_level = aggregate.get("required_strongest_validation")
    if declared_level != MANDATORY_REQUIRED_STRONGEST_VALIDATION:
        errors.append(
            "aggregate.required_strongest_validation must be "
            f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r}"
        )
    elif declared_level not in validation_levels:
        errors.append(
            "aggregate.required_strongest_validation must be a declared "
            "validation level"
        )

    if (
        aggregate.get("require_commercial_reference_validation")
        is not MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION
    ):
        errors.append(
            "aggregate.require_commercial_reference_validation must be true"
        )
    if (
        aggregate.get("require_package_result")
        is not MANDATORY_REQUIRE_PACKAGE_RESULT
    ):
        errors.append("aggregate.require_package_result must be true")

    for reference in references:
        rid = reference.get("id", "<missing>")
        if reference.get("status") not in ("pending", "validated", "blocked"):
            errors.append(f"reference {rid}: status must be pending/validated/blocked")
        elif reference.get("status") == "validated":
            # A validated reference is the compatibility oracle; it must carry
            # its own exact provenance or the "validated" claim is unsupported.
            if not _is_sha256(reference.get("sha256")):
                errors.append(
                    f"reference {rid}: validated status requires a 64-hex "
                    "sha256 digest"
                )
            if not _is_evidence_run(reference.get("evidence_run")):
                errors.append(
                    f"reference {rid}: validated status requires an evidence_run"
                )

    seen_capability_ids: set[str] = set()
    for cap in manifest.get("capabilities") or []:
        cid = cap.get("id", "<missing>")
        for field in REQUIRED_CAPABILITY_FIELDS:
            if field not in cap:
                errors.append(f"capability {cid}: missing field {field!r}")
        if cid in seen_capability_ids:
            errors.append(f"capability {cid}: duplicate id")
        seen_capability_ids.add(cid)

        if cap.get("target") not in target_ids + ["all-requested"]:
            errors.append(
                f"capability {cid}: unknown target {cap.get('target')!r}"
            )
        if cap.get("mode") not in modes:
            errors.append(f"capability {cid}: unknown mode {cap.get('mode')!r}")
        if cap.get("implementation") not in impl_states:
            errors.append(
                f"capability {cid}: unknown implementation "
                f"{cap.get('implementation')!r}"
            )
        if not isinstance(cap.get("production_enrolled"), bool):
            errors.append(f"capability {cid}: production_enrolled must be a bool")
        if cap.get("strongest_validation") not in validation_levels:
            errors.append(
                f"capability {cid}: unknown strongest_validation "
                f"{cap.get('strongest_validation')!r}"
            )
        for kind in cap.get("evidence_kinds") or []:
            if kind not in evidence_kinds:
                errors.append(
                    f"capability {cid}: unknown evidence kind {kind!r}"
                )
        if not (cap.get("owner") or "").strip():
            errors.append(f"capability {cid}: owner must be non-empty")
        if not isinstance(cap.get("dependencies"), list):
            errors.append(f"capability {cid}: dependencies must be a list")
        evidence = cap.get("evidence") or {}
        for field in ("sha", "run", "package_result"):
            if field not in evidence:
                errors.append(f"capability {cid}: evidence missing {field!r}")
        evidence_sha = evidence.get("sha")
        evidence_run = evidence.get("run")
        package_result = evidence.get("package_result")
        if evidence_sha is not None and not _is_sha(evidence_sha):
            errors.append(
                f"capability {cid}: evidence.sha must be a commit SHA or null"
            )
        if evidence_run is not None and not _is_evidence_run(evidence_run):
            errors.append(
                f"capability {cid}: evidence.run must be a non-empty run id "
                "or null"
            )
        if package_result is not None and package_result not in PACKAGE_RESULT_VALUES:
            errors.append(
                f"capability {cid}: evidence.package_result must be one of "
                f"{list(PACKAGE_RESULT_VALUES)!r} or null (arbitrary strings "
                "are not a package result)"
            )
        if claims_delivery_level(cap):
            # A row claiming the packaged delivery level must be fully
            # evidenced; pending rows keep null fields.
            if not _is_sha(evidence_sha):
                errors.append(
                    f"capability {cid}: claiming "
                    f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires an "
                    "exact evidence.sha"
                )
            if not _is_evidence_run(evidence_run):
                errors.append(
                    f"capability {cid}: claiming "
                    f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires an "
                    "evidence.run"
                )
            if package_result != PACKAGE_RESULT_SUCCESS:
                errors.append(
                    f"capability {cid}: claiming "
                    f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires "
                    f"evidence.package_result == {PACKAGE_RESULT_SUCCESS!r}"
                )
        if not (cap.get("blocker") or "").strip():
            errors.append(f"capability {cid}: blocker must be non-empty")

    for item in manifest.get("supporting_evidence") or []:
        sid = item.get("id", "<missing>")
        if item.get("provenance") not in provenance_classes:
            errors.append(
                f"supporting evidence {sid}: unknown provenance "
                f"{item.get('provenance')!r}"
            )
        if item.get("evidence_kind") not in evidence_kinds + ["none"]:
            errors.append(
                f"supporting evidence {sid}: unknown evidence kind "
                f"{item.get('evidence_kind')!r}"
            )
        if item.get("strongest_validation") not in validation_levels:
            errors.append(
                f"supporting evidence {sid}: unknown strongest_validation "
                f"{item.get('strongest_validation')!r}"
            )
        if item.get("counts_toward_delivery") is not False:
            errors.append(
                f"supporting evidence {sid}: supporting evidence must set "
                "counts_toward_delivery=false"
            )

    return errors


# --------------------------------------------------------------------------
# Derivation: CI workflows
# --------------------------------------------------------------------------
JOB_KEY_RE = re.compile(r"^[A-Za-z0-9_.-]+:$")


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _jobs_block(lines: list[str]) -> list[str]:
    """Return the lines strictly inside the top-level ``jobs:`` mapping."""
    start = None
    for index, raw in enumerate(lines):
        if raw.rstrip() == "jobs:":
            start = index
            break
    if start is None:
        return []
    block: list[str] = []
    for raw in lines[start + 1 :]:
        if not raw.strip():
            block.append(raw)
            continue
        if raw.lstrip().startswith("#"):
            block.append(raw)
            continue
        if _indent(raw) == 0:
            break
        block.append(raw)
    return block


def _job_blocks(block: list[str]) -> list[tuple[str, list[str]]]:
    """Split the jobs block into (job_id, lines) pairs at indent 2."""
    jobs: list[tuple[str, list[str]]] = []
    current_id = None
    current_lines: list[str] = []
    for raw in block:
        if not raw.strip() or raw.lstrip().startswith("#"):
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


def _matrix_legs(job_lines: list[str]) -> int:
    """Count matrix legs in a job's ``strategy.matrix`` block.

    Handles both ``include:`` list entries (``- platform: ...``) and inline
    flow lists (``config: [Debug, Release]``).  ``exclude:`` blocks are not
    expected in this repository's workflows and are intentionally not
    subtracted; adding one would require revisiting this count.
    """
    total = 0
    index = 0
    while index < len(job_lines):
        stripped = job_lines[index].strip()
        if stripped == "matrix:":
            matrix_indent = _indent(job_lines[index])
            cursor = index + 1
            while cursor < len(job_lines):
                probe = job_lines[cursor]
                if not probe.strip() or probe.lstrip().startswith("#"):
                    cursor += 1
                    continue
                if _indent(probe) <= matrix_indent:
                    break
                probe_stripped = probe.strip()
                flow = re.fullmatch(
                    r"[A-Za-z0-9_.-]+:\s*\[([^\]]*)\]", probe_stripped
                )
                if flow:
                    total += len(
                        [entry for entry in flow.group(1).split(",") if entry.strip()]
                    )
                elif probe_stripped.startswith("- "):
                    total += 1
                cursor += 1
            index = cursor
            continue
        index += 1
    return total


def derive_ci_inventory(workflow_dir: Path = WORKFLOW_DIR) -> dict:
    workflows = []
    for path in sorted(workflow_dir.glob("*.yml")):
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        block = _jobs_block(lines)
        jobs = []
        for job_id, job_lines in _job_blocks(block):
            name = job_id
            for raw in job_lines:
                stripped = raw.strip()
                if stripped.startswith("name:"):
                    value = stripped[len("name:") :].strip()
                    if value and not value.startswith("${{"):
                        name = f"{job_id} ({value})"
                    break
            legs = _matrix_legs(job_lines)
            self_hosted = any(
                "runs-on:" in raw and "[self-hosted" in raw for raw in job_lines
            )
            jobs.append(
                {
                    "id": job_id,
                    "name": name,
                    "matrix_legs": legs,
                    "self_hosted": self_hosted,
                }
            )
        workflows.append(
            {
                "file": path.name,
                "jobs": jobs,
                "job_count": len(jobs),
                "invocations": sum(max(1, job["matrix_legs"]) for job in jobs),
            }
        )
    return {
        "workflows": workflows,
        "workflow_count": len(workflows),
        "job_count": sum(w["job_count"] for w in workflows),
        "invocations": sum(w["invocations"] for w in workflows),
    }


# --------------------------------------------------------------------------
# Derivation: tests / presets
# --------------------------------------------------------------------------
def derive_test_inventory(
    tests_cmake: Path = TESTS_CMAKE, presets: Path = CMAKE_PRESETS
) -> dict:
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


# --------------------------------------------------------------------------
# Aggregate
# --------------------------------------------------------------------------
def compute_aggregate(manifest: dict) -> dict:
    """Compute requested-target delivery against the mandatory contract.

    The required modes, commercial references, validation threshold and boolean
    gates are read from the module-level mandatory policy, never from the
    manifest's editable ``aggregate`` block.  A weakened or emptied aggregate
    therefore cannot change the computed result.  Validation-level rank comes
    from the fixed ``VALIDATION_RANK`` constant, package success from the
    explicit ``PACKAGE_RESULT_SUCCESS`` token, and promoted rows must carry a
    complete evidence record; validated references must carry their own
    provenance.
    """
    refs = {r["id"]: r for r in manifest.get("commercial_references") or []}
    required_refs = list(MANDATORY_REQUIRED_COMMERCIAL_REFERENCES)
    require_ref_validation = MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION
    required_level = MANDATORY_REQUIRED_STRONGEST_VALIDATION
    # Ranking always comes from the fixed module constant; the manifest's
    # editable level order can never influence delivery.
    required_rank = VALIDATION_RANK[required_level]
    required_modes = list(MANDATORY_REQUIRED_MODES)
    require_package = MANDATORY_REQUIRE_PACKAGE_RESULT

    refs_ok = True
    if require_ref_validation:
        refs_ok = all(
            refs.get(rid, {}).get("status") == "validated"
            and reference_provenance_complete(refs.get(rid, {}))
            for rid in required_refs
        )

    rows = []
    delivered_count = 0
    for target in manifest.get("targets") or []:
        if not target.get("requested"):
            continue
        mode_states = {}
        delivered = True
        for mode in required_modes:
            cap = next(
                (
                    c
                    for c in manifest.get("capabilities") or []
                    if c.get("target") == target["id"] and c.get("mode") == mode
                ),
                None,
            )
            if cap is None:
                mode_states[mode] = {"state": "missing", "capability": None}
                delivered = False
                continue
            reached = VALIDATION_RANK.get(cap.get("strongest_validation"), -1) >= (
                required_rank
            )
            package_ok = (
                (cap.get("evidence") or {}).get("package_result")
                == PACKAGE_RESULT_SUCCESS
            ) or not require_package
            enrolled = bool(cap.get("production_enrolled"))
            evidence_ok = capability_evidence_complete(cap)
            mode_delivered = reached and package_ok and enrolled and evidence_ok
            mode_states[mode] = {
                "state": "delivered" if mode_delivered else "pending",
                "capability": cap,
            }
            delivered = delivered and mode_delivered
        if delivered and refs_ok:
            delivered_count += 1
        rows.append(
            {
                "target": target,
                "modes": mode_states,
                "delivered": delivered and refs_ok,
            }
        )

    return {
        "rows": rows,
        "delivered": delivered_count,
        "requested": sum(1 for t in manifest.get("targets") or [] if t.get("requested")),
        "references_ok": refs_ok,
        "required_level": required_level,
        "required_modes": required_modes,
        "required_refs": required_refs,
    }


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------
def _fmt(value) -> str:
    if value in (None, "", []):
        return "pending"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def _escape(value) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def _evidence_cell(evidence: dict) -> str:
    parts = []
    parts.append(f"sha={_fmt(evidence.get('sha'))}")
    parts.append(f"run={_fmt(evidence.get('run'))}")
    parts.append(f"package={_fmt(evidence.get('package_result'))}")
    return ", ".join(parts)


def render_dashboard(manifest: dict, ci: dict, tests: dict, aggregate: dict) -> str:
    lines: list[str] = []
    lines.append("# KisakCOD capability dashboard")
    lines.append("")
    lines.append(GENERATED_MARKER)
    lines.append("")
    lines.append(
        "Source manifest: `docs/capability/manifest.json`; "
        "manifest authoring base: `{}`.".format(_fmt(manifest.get("authoring_base")))
    )
    lines.append(
        "CI and test inventories are derived from `.github/workflows/*.yml`, "
        "`tests/CMakeLists.txt` and `CMakePresets.json`, not maintained by hand."
    )
    lines.append("")

    # -- Aggregate ---------------------------------------------------------
    requested = aggregate["requested"]
    delivered = aggregate["delivered"]
    lines.append(f"## Requested target delivery: {delivered}/{requested}")
    lines.append("")
    agg = manifest.get("aggregate") or {}
    required_level = aggregate["required_level"]
    required_modes = ", ".join(aggregate["required_modes"])
    required_refs = ", ".join(aggregate["required_refs"])
    lines.append(
        "A requested target counts as delivered only when every required mode "
        f"({required_modes}) reaches `{required_level}` with a package result, "
        "the capability is production-enrolled, and both required commercial "
        f"reference profiles ({required_refs}) are validated. "
        "Test, scaffold, instrument, CoD4x and fork-only evidence is excluded "
        "from this aggregate."
    )
    lines.append("")
    lines.append("| Requested target | " + " | ".join(aggregate["required_modes"]) + " | Delivered |")
    lines.append(
        "|---|" + "|".join(["---"] * len(aggregate["required_modes"])) + "|---|"
    )
    for row in aggregate["rows"]:
        cells = []
        for mode in aggregate["required_modes"]:
            state = row["modes"].get(mode, {}).get("state", "missing")
            cells.append(state)
        lines.append(
            f"| {row['target']['id']} | " + " | ".join(cells) + " | "
            f"{'yes' if row['delivered'] else 'no'} |"
        )
    if not aggregate["references_ok"]:
        lines.append("")
        lines.append(
            "Commercial reference gate is **not satisfied**, so no requested "
            "target can be delivered even if a mode row reaches the required level."
        )
    lines.append("")

    # -- Commercial references --------------------------------------------
    lines.append("## Commercial reference gate (#122)")
    lines.append("")
    lines.append("| Reference | Origin | Required | Status | SHA-256 | Evidence run | Blocker |")
    lines.append("|---|---|---|---|---|---|---|")
    for reference in manifest.get("commercial_references") or []:
        lines.append(
            "| {id} | {origin} | {required} | {status} | {sha} | {run} | {blocker} |".format(
                id=_escape(reference.get("id")),
                origin=_escape(reference.get("origin")),
                required=_escape(_fmt(reference.get("required"))),
                status=_escape(reference.get("status")),
                sha=_escape(_fmt(reference.get("sha256"))),
                run=_escape(_fmt(reference.get("evidence_run"))),
                blocker=_escape(reference.get("blocker")),
            )
        )
    lines.append("")

    # -- Capability rows ---------------------------------------------------
    target_order = {
        target["id"]: index
        for index, target in enumerate(manifest.get("targets") or [])
    }
    mode_order = {
        mode: index
        for index, mode in enumerate((manifest.get("enums") or {}).get("modes") or [])
    }
    capabilities = sorted(
        manifest.get("capabilities") or [],
        key=lambda c: (
            target_order.get(c.get("target"), 999),
            mode_order.get(c.get("mode"), 999),
            c.get("id", ""),
        ),
    )
    lines.append("## Capability rows")
    lines.append("")
    lines.append(
        "| Capability | Target | Mode | Owner | Implementation | Production enrolled | "
        "Strongest validation | Evidence | Blocker |"
    )
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for cap in capabilities:
        lines.append(
            "| {id} | {target} | {mode} | {owner} | {impl} | {enrolled} | {level} | {evidence} | {blocker} |".format(
                id=_escape(cap.get("id")),
                target=_escape(cap.get("target")),
                mode=_escape(cap.get("mode")),
                owner=_escape(cap.get("owner")),
                impl=_escape(cap.get("implementation")),
                enrolled=_escape(_fmt(cap.get("production_enrolled"))),
                level=_escape(cap.get("strongest_validation")),
                evidence=_escape(_evidence_cell(cap.get("evidence") or {})),
                blocker=_escape(cap.get("blocker")),
            )
        )
    lines.append("")

    # -- Supporting evidence ----------------------------------------------
    lines.append("## Supporting evidence (never advances a production row)")
    lines.append("")
    lines.append(
        "| Evidence | Provenance | Kind | Strongest validation | Counts toward delivery | Note |"
    )
    lines.append("|---|---|---|---|---|---|")
    for item in manifest.get("supporting_evidence") or []:
        lines.append(
            "| {label} (`{id}`) | {prov} | {kind} | {level} | {counts} | {note} |".format(
                id=_escape(item.get("id")),
                label=_escape(item.get("label")),
                prov=_escape(item.get("provenance")),
                kind=_escape(item.get("evidence_kind")),
                level=_escape(item.get("strongest_validation")),
                counts=_escape(_fmt(item.get("counts_toward_delivery"))),
                note=_escape((item.get("evidence") or {}).get("note", "")),
            )
        )
    lines.append("")

    # -- Derived CI inventory ---------------------------------------------
    lines.append("## Derived CI inventory")
    lines.append("")
    lines.append(
        f"{ci['workflow_count']} workflows, {ci['job_count']} jobs, "
        f"{ci['invocations']} matrix-expanded job invocations."
    )
    lines.append("")
    lines.append("| Workflow | Job | Matrix legs | Self-hosted |")
    lines.append("|---|---|---|---|")
    for workflow in ci["workflows"]:
        for job in workflow["jobs"]:
            lines.append(
                f"| {workflow['file']} | {_escape(job['name'])} | "
                f"{job['matrix_legs'] or 1} | "
                f"{_escape(_fmt(job['self_hosted']))} |"
            )
    lines.append("")

    # -- Derived test inventory -------------------------------------------
    lines.append("## Derived test inventory")
    lines.append("")
    lines.append("| Item | Count |")
    lines.append("|---|---|")
    lines.append(f"| Configure presets | {tests['configure_presets']} |")
    lines.append(f"| `add_executable` registrations | {tests['add_executable']} |")
    lines.append(f"| `add_test` registrations | {tests['add_test']} |")
    lines.append(f"| `*_tests.cpp` source files | {tests['test_source_files']} |")
    lines.append(
        f"| `*_source_test.cmake` contract files | {tests['source_contract_files']} |"
    )
    lines.append("")
    lines.append(
        "Counts describe the current tree only. Dated local/CI snapshots from "
        "earlier trees belong in `docs/task.md`'s historical sections, not here."
    )
    lines.append("")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------
def build_dashboard(manifest_path: Path = DEFAULT_MANIFEST) -> tuple[str, dict]:
    manifest = load_manifest(manifest_path)
    errors = validate_manifest(manifest)
    if errors:
        raise SystemExit(
            "manifest validation failed:\n  - " + "\n  - ".join(errors)
        )
    ci = derive_ci_inventory()
    tests = derive_test_inventory()
    aggregate = compute_aggregate(manifest)
    return render_dashboard(manifest, ci, tests, aggregate), aggregate


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail (exit 1) if the committed dashboard is not current",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="print the dashboard instead of writing it",
    )
    args = parser.parse_args(argv)

    try:
        rendered, aggregate = build_dashboard(args.manifest)
    except SystemExit as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if args.stdout:
        sys.stdout.write(rendered)
        return 0

    if args.check:
        if not args.output.exists():
            print(
                f"dashboard missing: {args.output}", file=sys.stderr
            )
            return 1
        current = args.output.read_text(encoding="utf-8")
        if current != rendered:
            print(
                f"dashboard is stale: {args.output}\n"
                "Regenerate with scripts/ci/capability-dashboard.py",
                file=sys.stderr,
            )
            return 1
        print(
            f"dashboard current: {aggregate['delivered']}/{aggregate['requested']} "
            "requested targets delivered",
            file=sys.stderr,
        )
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    print(
        f"wrote {args.output} "
        f"({aggregate['delivered']}/{aggregate['requested']} requested targets delivered)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

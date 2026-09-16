"""
Render the deterministic Markdown capability dashboard.

The renderer is split into one helper per section so each stays simple.  The
output is byte-for-byte deterministic for a given manifest and inventory, which
is what makes the ``--check`` currency gate meaningful.
"""

from __future__ import annotations

from .paths import GENERATED_MARKER


def _fmt(value) -> str:
    """Render a manifest value for a table cell."""
    if value in (None, "", []):
        return "pending"
    if isinstance(value, bool):
        return "yes" if value else "no"
    return str(value)


def _escape(value) -> str:
    """Escape a value for a Markdown table cell."""
    return str(value).replace("|", "\\|").replace("\n", " ")


def _evidence_cell(evidence: dict) -> str:
    """Render a capability's evidence record as one table cell."""
    return ", ".join(
        (
            f"sha={_fmt(evidence.get('sha'))}",
            f"run={_fmt(evidence.get('run'))}",
            f"package={_fmt(evidence.get('package_result'))}",
        )
    )


def _render_header(manifest: dict) -> list[str]:
    """Render the title, generated marker and provenance preamble."""
    return [
        "# KisakCOD capability dashboard",
        "",
        GENERATED_MARKER,
        "",
        (
            "Source manifest: `docs/capability/manifest.json`; "
            "manifest authoring base: "
            f"`{_fmt(manifest.get('authoring_base'))}`."
        ),
        (
            "CI and test inventories are derived from "
            "`.github/workflows/*.yml`, `.github/workflows/*.yaml`, "
            "`tests/CMakeLists.txt` and `CMakePresets.json`, not maintained "
            "by hand."
        ),
        "",
    ]


def _render_aggregate(aggregate: dict) -> list[str]:
    """Render the requested-target delivery table and its policy note."""
    modes = aggregate["required_modes"]
    lines = [
        f"## Requested target delivery: {aggregate['delivered']}/{aggregate['requested']}",
        "",
        (
            "A requested target counts as delivered only when every required "
            f"mode ({', '.join(modes)}) reaches "
            f"`{aggregate['required_level']}` with a package result, the "
            "capability is production-enrolled, and both required commercial "
            "reference profiles "
            f"({', '.join(aggregate['required_refs'])}) are validated. "
            "Test, scaffold, instrument, CoD4x and fork-only evidence is "
            "excluded from this aggregate."
        ),
        "",
        "| Requested target | " + " | ".join(modes) + " | Delivered |",
        "|---|" + "|".join(["---"] * len(modes)) + "|---|",
    ]
    for row in aggregate["rows"]:
        cells = [
            row["modes"].get(mode, {}).get("state", "missing") for mode in modes
        ]
        delivered = "yes" if row["delivered"] else "no"
        lines.append(
            f"| {row['target']['id']} | " + " | ".join(cells) + f" | {delivered} |"
        )
    if not aggregate["references_ok"]:
        lines.append("")
        lines.append(
            "Commercial reference gate is **not satisfied**, so no requested "
            "target can be delivered even if a mode row reaches the required "
            "level."
        )
    lines.append("")
    return lines


def _render_references(manifest: dict) -> list[str]:
    """Render the #122 commercial reference gate table."""
    lines = [
        "## Commercial reference gate (#122)",
        "",
        "| Reference | Origin | Required | Status | SHA-256 | Evidence run | Blocker |",
        "|---|---|---|---|---|---|---|",
    ]
    for reference in manifest.get("commercial_references") or []:
        lines.append(
            "| {id} | {origin} | {required} | {status} | {sha} | {run} | "
            "{blocker} |".format(
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
    return lines


def _render_capabilities(manifest: dict) -> list[str]:
    """Render every capability row, ordered by target then mode."""
    target_order = {
        target["id"]: index
        for index, target in enumerate(manifest.get("targets") or [])
    }
    mode_order = {
        mode: index
        for index, mode in enumerate(
            (manifest.get("enums") or {}).get("modes") or []
        )
    }
    capabilities = sorted(
        manifest.get("capabilities") or [],
        key=lambda cap: (
            target_order.get(cap.get("target"), 999),
            mode_order.get(cap.get("mode"), 999),
            cap.get("id", ""),
        ),
    )
    lines = [
        "## Capability rows",
        "",
        "| Capability | Target | Mode | Owner | Implementation | "
        "Production enrolled | Strongest validation | Evidence | Blocker |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for cap in capabilities:
        lines.append(
            "| {id} | {target} | {mode} | {owner} | {impl} | {enrolled} | "
            "{level} | {evidence} | {blocker} |".format(
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
    return lines


def _render_supporting(manifest: dict) -> list[str]:
    """Render supporting evidence, which never advances a production row."""
    lines = [
        "## Supporting evidence (never advances a production row)",
        "",
        "| Evidence | Provenance | Kind | Strongest validation | "
        "Counts toward delivery | Note |",
        "|---|---|---|---|---|---|",
    ]
    for item in manifest.get("supporting_evidence") or []:
        lines.append(
            "| {label} (`{id}`) | {prov} | {kind} | {level} | {counts} | "
            "{note} |".format(
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
    return lines


def _render_ci_inventory(ci: dict) -> list[str]:
    """Render the derived CI workflow/job/matrix inventory."""
    lines = [
        "## Derived CI inventory",
        "",
        (
            f"{ci['workflow_count']} workflows, {ci['job_count']} jobs, "
            f"{ci['invocations']} matrix-expanded job invocations."
        ),
        "",
        "| Workflow | Job | Matrix legs | Self-hosted |",
        "|---|---|---|---|",
    ]
    for workflow in ci["workflows"]:
        for job in workflow["jobs"]:
            lines.append(
                f"| {workflow['file']} | {_escape(job['name'])} | "
                f"{job['matrix_legs'] or 1} | "
                f"{_escape(_fmt(job['self_hosted']))} |"
            )
    lines.append("")
    return lines


def _render_test_inventory(tests: dict) -> list[str]:
    """Render the derived CMake test/preset inventory."""
    return [
        "## Derived test inventory",
        "",
        "| Item | Count |",
        "|---|---|",
        f"| Configure presets | {tests['configure_presets']} |",
        f"| `add_executable` registrations | {tests['add_executable']} |",
        f"| `add_test` registrations | {tests['add_test']} |",
        f"| `*_tests.cpp` source files | {tests['test_source_files']} |",
        f"| `*_source_test.cmake` contract files | {tests['source_contract_files']} |",
        "",
        (
            "Counts describe the current tree only. Dated local/CI snapshots "
            "from earlier trees belong in `docs/task.md`'s historical "
            "sections, not here."
        ),
        "",
    ]


def render_dashboard(manifest: dict, ci: dict, tests: dict, aggregate: dict) -> str:
    """Render the full dashboard as deterministic Markdown."""
    lines: list[str] = []
    for section in (
        _render_header(manifest),
        _render_aggregate(aggregate),
        _render_references(manifest),
        _render_capabilities(manifest),
        _render_supporting(manifest),
        _render_ci_inventory(ci),
        _render_test_inventory(tests),
    ):
        lines.extend(section)
    return "\n".join(lines)

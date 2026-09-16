"""Compute requested-target delivery against the fixed mandatory contract."""

# The required modes, commercial references, validation threshold and boolean
# gates come from the module-level policy in
# :mod:`capability_dashboard.contract`, never from the manifest's editable
# ``aggregate`` block, so a weakened or emptied aggregate cannot change the
# computed result.  Promoted rows must carry a complete evidence record and
# validated references must carry their own provenance.

from __future__ import annotations

from .contract import (
    MANDATORY_REQUIRED_COMMERCIAL_REFERENCES,
    MANDATORY_REQUIRED_MODES,
    MANDATORY_REQUIRED_STRONGEST_VALIDATION,
    MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION,
    MANDATORY_REQUIRE_PACKAGE_RESULT,
    PACKAGE_RESULT_SUCCESS,
    VALIDATION_RANK,
    capability_evidence_complete,
    reference_provenance_complete,
    validation_rank,
)


def _reference_gate(manifest: dict) -> bool:
    """
    Return True when both mandatory references are validated with provenance.

    Rows without a usable id are skipped rather than indexed, so aggregating a
    manifest that never passed ``validate_manifest`` cannot raise ``KeyError``
    (it simply fails the gate).
    """
    references: dict[str, dict] = {}
    for reference in manifest.get("commercial_references") or []:
        if not isinstance(reference, dict):
            continue
        rid = reference.get("id")
        if isinstance(rid, str) and rid.strip():
            references[rid] = reference
    return all(
        references.get(rid, {}).get("status") == "validated"
        and reference_provenance_complete(references.get(rid, {}))
        for rid in MANDATORY_REQUIRED_COMMERCIAL_REFERENCES
    )


def _find_capability(
    capabilities: list[dict], target_id: str, mode: str
) -> dict | None:
    """Return the capability row for a target/mode, or None when absent."""
    for capability in capabilities:
        if (
            capability.get("target") == target_id
            and capability.get("mode") == mode
        ):
            return capability
    return None


def _package_ok(capability: dict) -> bool:
    """Return True when the capability reports an explicit package success."""
    if not MANDATORY_REQUIRE_PACKAGE_RESULT:
        return True
    result = (capability.get("evidence") or {}).get("package_result")
    return result == PACKAGE_RESULT_SUCCESS


def _mode_delivered(capability: dict, required_rank: int) -> bool:
    """Return True when one target/mode row satisfies every delivery gate."""
    rank = validation_rank(capability.get("strongest_validation"))
    reached = rank is not None and rank >= required_rank
    return (
        reached
        and _package_ok(capability)
        and bool(capability.get("production_enrolled"))
        and capability_evidence_complete(capability)
    )


def _target_row(
    target: dict,
    capabilities: list[dict],
    required_rank: int,
    references_ok: bool,
) -> dict:
    """Build the per-mode delivery row for one requested target."""
    mode_states: dict[str, dict] = {}
    delivered = True
    for mode in MANDATORY_REQUIRED_MODES:
        capability = _find_capability(capabilities, target["id"], mode)
        if capability is None:
            mode_states[mode] = {"state": "missing", "capability": None}
            delivered = False
            continue
        mode_delivered = _mode_delivered(capability, required_rank)
        mode_states[mode] = {
            "state": "delivered" if mode_delivered else "pending",
            "capability": capability,
        }
        delivered = delivered and mode_delivered
    return {
        "target": target,
        "modes": mode_states,
        "delivered": delivered and references_ok,
    }


def compute_aggregate(manifest: dict) -> dict:
    """Return requested-target delivery computed against the fixed contract."""
    capabilities = manifest.get("capabilities") or []
    required_rank = VALIDATION_RANK[MANDATORY_REQUIRED_STRONGEST_VALIDATION]
    references_ok = True
    if MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION:
        references_ok = _reference_gate(manifest)
    rows = [
        _target_row(target, capabilities, required_rank, references_ok)
        for target in manifest.get("targets") or []
        if target.get("requested")
    ]
    return {
        "rows": rows,
        "delivered": sum(1 for row in rows if row["delivered"]),
        "requested": len(rows),
        "references_ok": references_ok,
        "required_level": MANDATORY_REQUIRED_STRONGEST_VALIDATION,
        "required_modes": list(MANDATORY_REQUIRED_MODES),
        "required_refs": list(MANDATORY_REQUIRED_COMMERCIAL_REFERENCES),
    }

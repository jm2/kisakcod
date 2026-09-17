"""Manifest loading and strict, fail-closed schema validation."""

# ``validate_manifest`` never treats a schema problem as a warning: an omitted,
# empty or weakened mandatory delivery policy is an error.  The checks are split
# into small, independently auditable helpers so each stays simple and the
# mandatory contract stays in exactly one place.

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import TypeGuard

from .contract import (
    CANONICAL_VALIDATION_ORDER,
    MANDATORY_REQUESTED_TARGET_IDS,
    MANDATORY_REQUIRED_COMMERCIAL_REFERENCES,
    MANDATORY_REQUIRED_MODES,
    MANDATORY_REQUIRED_STRONGEST_VALIDATION,
    MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION,
    MANDATORY_REQUIRE_PACKAGE_RESULT,
    PACKAGE_RESULT_SUCCESS,
    PACKAGE_RESULT_VALUES,
    REQUIRED_CAPABILITY_FIELDS,
    VALIDATION_RANK,
    claims_delivery_level,
    is_evidence_run,
    is_sha,
    is_sha256,
)


@dataclass
class SchemaContext:
    """The declared enum sets and target ids a row is validated against."""

    target_ids: list[str]
    modes: list[str]
    implementation_states: list[str]
    validation_levels: list[str]
    evidence_kinds: list[str]
    provenance_classes: list[str]

    def knows_target(self, target: object) -> bool:
        """Return True when ``target`` is a declared or aggregate target."""
        return target in self.target_ids or target == "all-requested"

    def knows_kind(self, kind: object) -> bool:
        """Return True when ``kind`` is a declared evidence kind."""
        return kind in self.evidence_kinds

    def knows_level(self, level: object) -> bool:
        """Return True when ``level`` is a declared validation level."""
        return level in self.validation_levels


def load_manifest(path: Path) -> dict:
    """Load and return the JSON manifest at ``path``."""
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _validate_validation_levels(levels: list, errors: list[str]) -> None:
    """Validate the editable validation-level set against the fixed names."""
    if not levels:
        errors.append("enums.validation_levels must be a non-empty list")
        return
    if len(levels) != len(set(levels)):
        errors.append("enums.validation_levels must not contain duplicates")
    unknown = [level for level in levels if level not in VALIDATION_RANK]
    missing = [
        level for level in CANONICAL_VALIDATION_ORDER if level not in levels
    ]
    if unknown:
        errors.append(
            "enums.validation_levels contains unknown levels "
            f"{unknown!r}; the level set is fixed and its order is "
            "not significant"
        )
    if missing:
        errors.append(
            "enums.validation_levels is missing canonical levels "
            f"{missing!r}"
        )


def _validate_modes(modes: list, errors: list[str]) -> None:
    """Require a non-empty declared mode set."""
    if not modes:
        errors.append("enums.modes must be a non-empty list")


def _target_ids(targets: list, errors: list[str]) -> list[str]:
    """Return the declared target ids, in declaration order."""
    # Malformed rows never contribute an id: a null or scalar row used to
    # raise AttributeError from ``.get()`` and a non-string id used to reach
    # the duplicate check, where an unhashable id raised TypeError.  The
    # validation contract is a list of errors, never an exception.
    target_ids: list[str] = []
    for target in targets:
        if not isinstance(target, dict):
            errors.append("targets entries must be objects")
            continue
        target_id = target.get("id")
        if not isinstance(target_id, str) or not target_id.strip():
            errors.append(
                f"target id must be a non-empty string (got {target_id!r})"
            )
            continue
        target_ids.append(target_id)
    return target_ids


def _requested_target_ids(targets: list) -> set[str]:
    """Return the ids a target marks as requested and usable."""
    return {
        target["id"]
        for target in targets
        if (
            isinstance(target, dict)
            and target.get("requested")
            and isinstance(target.get("id"), str)
            and target.get("id").strip()
        )
    }


def _check_duplicate_targets(target_ids: list[str], errors: list[str]) -> None:
    """Reject a target list that repeats an id."""
    if len(set(target_ids)) != len(target_ids):
        errors.append("targets contain duplicate ids")


def _check_mandatory_targets(
    target_ids: list[str], requested_ids: set[str], errors: list[str]
) -> None:
    """Require every mandatory target to be declared and requested."""
    for required in MANDATORY_REQUESTED_TARGET_IDS:
        if required not in target_ids:
            errors.append(
                f"targets must declare the mandatory target {required!r}"
            )
        elif required not in requested_ids:
            errors.append(
                f"target {required!r} is mandatory and must be requested"
            )


def _check_extra_requested(requested_ids: set[str], errors: list[str]) -> None:
    """Reject requested targets outside the fixed mandatory set."""
    extra = sorted(
        rid for rid in requested_ids if rid not in MANDATORY_REQUESTED_TARGET_IDS
    )
    if extra:
        errors.append(
            "targets mark non-mandatory targets as requested: "
            f"{extra!r}; the requested set is fixed"
        )


def _validate_targets(targets: list, errors: list[str]) -> list[str]:
    """Validate the requested-target list and return its ids."""
    # The requested set is fixed by #122/#126: a manifest cannot drop or disable
    # a mandatory target, and it cannot smuggle an extra target into the
    # aggregate.  ``requested`` alone is editable, so it cannot be the only
    # source of truth for which targets the aggregate reports.
    target_ids = _target_ids(targets, errors)
    requested_ids = _requested_target_ids(targets)
    _check_duplicate_targets(target_ids, errors)
    _check_mandatory_targets(target_ids, requested_ids, errors)
    _check_extra_requested(requested_ids, errors)
    return target_ids


def _build_context(manifest: dict, errors: list[str]) -> SchemaContext:
    """Return the validated enum sets and target ids for a manifest."""
    enums = manifest.get("enums") or {}
    levels = enums.get("validation_levels") or []
    _validate_validation_levels(levels, errors)
    modes = enums.get("modes") or []
    _validate_modes(modes, errors)
    target_ids = _validate_targets(manifest.get("targets") or [], errors)

    return SchemaContext(
        target_ids=target_ids,
        modes=modes,
        implementation_states=enums.get("implementation_states") or [],
        validation_levels=levels,
        evidence_kinds=enums.get("evidence_kinds") or [],
        provenance_classes=enums.get("provenance_classes") or [],
    )


def _validate_reference(reference: dict, errors: list[str]) -> None:
    """Validate one commercial reference row."""
    rid = reference.get("id", "<missing>")
    status = reference.get("status")
    if status not in ("pending", "validated", "blocked"):
        errors.append(f"reference {rid}: status must be pending/validated/blocked")
        return
    if status != "validated":
        return
    # A validated reference is the compatibility oracle; it must carry its own
    # exact provenance or the "validated" claim is unsupported.
    if not is_sha256(reference.get("sha256")):
        errors.append(
            f"reference {rid}: validated status requires a 64-hex sha256 digest"
        )
    if not is_evidence_run(reference.get("evidence_run")):
        errors.append(
            f"reference {rid}: validated status requires an evidence_run"
        )


def _is_reference_id(value: object) -> TypeGuard[str]:
    """Return True for a usable commercial-reference id."""
    # An id must be a string with at least one non-whitespace character.  A
    # missing, null, empty, whitespace-only or non-string id is not usable:
    # downstream code keys references by id, so a malformed id used to slip past
    # validation and then raise KeyError while aggregating.
    return isinstance(value, str) and bool(value.strip())


def _validate_references(references: list, errors: list[str]) -> list[str]:
    """Validate every commercial reference and return their usable ids."""
    # Invalid rows never contribute an id, so the duplicate and membership
    # checks below cannot be defeated by an id-less row, and the returned ids
    # are safe to index by.
    valid: list[dict] = []
    ref_ids: list[str] = []
    for reference in references:
        if not isinstance(reference, dict):
            errors.append("commercial_references entries must be objects")
            continue
        rid = reference.get("id")
        if not _is_reference_id(rid):
            errors.append(
                "commercial reference id must be a non-empty string "
                f"(got {rid!r})"
            )
            continue
        valid.append(reference)
        ref_ids.append(rid)
    if len(set(ref_ids)) != len(ref_ids):
        errors.append("commercial_references contain duplicate ids")
    for reference in valid:
        _validate_reference(reference, errors)
    return ref_ids


def _validate_aggregate_modes(
    aggregate: dict, modes: list[str], errors: list[str]
) -> None:
    """Require the mandatory modes to be declared and known."""
    declared = aggregate.get("required_modes")
    if not isinstance(declared, list) or not declared:
        errors.append(
            "aggregate.required_modes must be a non-empty list containing the "
            f"mandatory modes {list(MANDATORY_REQUIRED_MODES)!r}"
        )
        declared = []
    for mode in MANDATORY_REQUIRED_MODES:
        if mode not in declared:
            errors.append(
                f"aggregate.required_modes must include mandatory mode {mode!r}"
            )
    for mode in declared:
        if mode not in modes:
            errors.append(
                f"aggregate.required_mode {mode!r} is not a declared mode"
            )


def _validate_aggregate_references(
    aggregate: dict, ref_ids: list[str], errors: list[str]
) -> None:
    """Require both mandatory commercial references to be declared and known."""
    declared = aggregate.get("required_commercial_references")
    if not isinstance(declared, list) or not declared:
        errors.append(
            "aggregate.required_commercial_references must be a non-empty "
            "list containing both mandatory commercial references "
            f"{list(MANDATORY_REQUIRED_COMMERCIAL_REFERENCES)!r}"
        )
        declared = []
    for required in MANDATORY_REQUIRED_COMMERCIAL_REFERENCES:
        if required not in declared:
            errors.append(
                "aggregate.required_commercial_references must include "
                f"mandatory reference {required!r}"
            )
    for required in declared:
        if required not in ref_ids:
            errors.append(
                f"aggregate requires commercial reference {required!r} "
                "which is not declared"
            )


def _validate_aggregate_level(
    aggregate: dict, validation_levels: list[str], errors: list[str]
) -> None:
    """Require the fixed packaged-delivery threshold."""
    declared = aggregate.get("required_strongest_validation")
    if declared != MANDATORY_REQUIRED_STRONGEST_VALIDATION:
        errors.append(
            "aggregate.required_strongest_validation must be "
            f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r}"
        )
    elif declared not in validation_levels:
        errors.append(
            "aggregate.required_strongest_validation must be a declared "
            "validation level"
        )


def _validate_aggregate_gates(aggregate: dict, errors: list[str]) -> None:
    """Require both mandatory boolean gates to be enabled."""
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


def _validate_aggregate(
    manifest: dict, context: SchemaContext, ref_ids: list[str], errors: list[str]
) -> None:
    """Validate the aggregate's declared mandatory policy."""
    aggregate = manifest.get("aggregate")
    if not isinstance(aggregate, dict):
        errors.append(
            "aggregate must be an object declaring the mandatory delivery "
            "policy"
        )
        return
    _validate_aggregate_modes(aggregate, context.modes, errors)
    _validate_aggregate_references(aggregate, ref_ids, errors)
    _validate_aggregate_level(aggregate, context.validation_levels, errors)
    _validate_aggregate_gates(aggregate, errors)


def _validate_capability_identity(
    capability: dict, cid: str, context: SchemaContext, errors: list[str]
) -> None:
    """Validate the identity/enum fields shared by every capability row."""
    for field in REQUIRED_CAPABILITY_FIELDS:
        if field not in capability:
            errors.append(f"capability {cid}: missing field {field!r}")
    if not context.knows_target(capability.get("target")):
        errors.append(
            f"capability {cid}: unknown target {capability.get('target')!r}"
        )
    if capability.get("mode") not in context.modes:
        errors.append(f"capability {cid}: unknown mode {capability.get('mode')!r}")
    if capability.get("implementation") not in context.implementation_states:
        errors.append(
            f"capability {cid}: unknown implementation "
            f"{capability.get('implementation')!r}"
        )
    if not isinstance(capability.get("production_enrolled"), bool):
        errors.append(f"capability {cid}: production_enrolled must be a bool")
    if not (capability.get("owner") or "").strip():
        errors.append(f"capability {cid}: owner must be non-empty")
    if not isinstance(capability.get("dependencies"), list):
        errors.append(f"capability {cid}: dependencies must be a list")


def _validate_capability_levels(
    capability: dict, cid: str, context: SchemaContext, errors: list[str]
) -> None:
    """Validate the capability's validation level and evidence kinds."""
    if not context.knows_level(capability.get("strongest_validation")):
        errors.append(
            f"capability {cid}: unknown strongest_validation "
            f"{capability.get('strongest_validation')!r}"
        )
    for kind in capability.get("evidence_kinds") or []:
        if not context.knows_kind(kind):
            errors.append(f"capability {cid}: unknown evidence kind {kind!r}")


def _validate_evidence_types(
    cid: str, evidence: dict, errors: list[str]
) -> None:
    """Validate the evidence record's presence and value types."""
    for field in ("sha", "run", "package_result"):
        if field not in evidence:
            errors.append(f"capability {cid}: evidence missing {field!r}")
    evidence_sha = evidence.get("sha")
    evidence_run = evidence.get("run")
    package_result = evidence.get("package_result")
    if evidence_sha is not None and not is_sha(evidence_sha):
        errors.append(
            f"capability {cid}: evidence.sha must be a commit SHA or null"
        )
    if evidence_run is not None and not is_evidence_run(evidence_run):
        errors.append(
            f"capability {cid}: evidence.run must be a non-empty run id or null"
        )
    if package_result is not None and package_result not in PACKAGE_RESULT_VALUES:
        errors.append(
            f"capability {cid}: evidence.package_result must be one of "
            f"{list(PACKAGE_RESULT_VALUES)!r} or null (arbitrary strings "
            "are not a package result)"
        )


def _validate_promoted_evidence(
    cid: str, evidence: dict, errors: list[str]
) -> None:
    """Require complete evidence for a row claiming the delivery level."""
    if not is_sha(evidence.get("sha")):
        errors.append(
            f"capability {cid}: claiming "
            f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires an "
            "exact evidence.sha"
        )
    if not is_evidence_run(evidence.get("run")):
        errors.append(
            f"capability {cid}: claiming "
            f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires an "
            "evidence.run"
        )
    if evidence.get("package_result") != PACKAGE_RESULT_SUCCESS:
        errors.append(
            f"capability {cid}: claiming "
            f"{MANDATORY_REQUIRED_STRONGEST_VALIDATION!r} requires "
            f"evidence.package_result == {PACKAGE_RESULT_SUCCESS!r}"
        )


def _validate_evidence_fields(
    capability: dict, cid: str, evidence: dict, errors: list[str]
) -> None:
    """Validate a capability's evidence record against the fixed contract."""
    _validate_evidence_types(cid, evidence, errors)
    if claims_delivery_level(capability):
        _validate_promoted_evidence(cid, evidence, errors)


def _validate_capabilities(
    manifest: dict, context: SchemaContext, errors: list[str]
) -> None:
    """Validate every capability row and reject duplicate ids."""
    seen_ids: set[str] = set()
    seen_pairs: set[tuple[str, str]] = set()
    for capability in manifest.get("capabilities") or []:
        cid = capability.get("id", "<missing>")
        if cid in seen_ids:
            errors.append(f"capability {cid}: duplicate id")
        seen_ids.add(cid)
        # A capability is matched to an aggregate row by its (target, mode)
        # pair, so two rows sharing a pair make delivery order-dependent: the
        # first row found wins and a copied row with a new id can add a target
        # without adding delivery.  Reject the ambiguity instead of picking one.
        target = capability.get("target")
        mode = capability.get("mode")
        if isinstance(target, str) and isinstance(mode, str):
            if (target, mode) in seen_pairs:
                errors.append(
                    f"capability {cid}: duplicate target/mode pair "
                    f"{(target, mode)!r}"
                )
            seen_pairs.add((target, mode))
        _validate_capability_identity(capability, cid, context, errors)
        _validate_capability_levels(capability, cid, context, errors)
        _validate_evidence_fields(
            capability, cid, capability.get("evidence") or {}, errors
        )
        if not (capability.get("blocker") or "").strip():
            errors.append(f"capability {cid}: blocker must be non-empty")


def _validate_supporting_evidence(
    manifest: dict, context: SchemaContext, errors: list[str]
) -> None:
    """Validate supporting rows, which can never count toward delivery."""
    for item in manifest.get("supporting_evidence") or []:
        sid = item.get("id", "<missing>")
        if item.get("provenance") not in context.provenance_classes:
            errors.append(
                f"supporting evidence {sid}: unknown provenance "
                f"{item.get('provenance')!r}"
            )
        if item.get("evidence_kind") not in context.evidence_kinds + ["none"]:
            errors.append(
                f"supporting evidence {sid}: unknown evidence kind "
                f"{item.get('evidence_kind')!r}"
            )
        if not context.knows_level(item.get("strongest_validation")):
            errors.append(
                f"supporting evidence {sid}: unknown strongest_validation "
                f"{item.get('strongest_validation')!r}"
            )
        if item.get("counts_toward_delivery") is not False:
            errors.append(
                f"supporting evidence {sid}: supporting evidence must set "
                "counts_toward_delivery=false"
            )


def validate_manifest(manifest: dict) -> list[str]:
    """Return a list of human-readable schema errors (empty means valid)."""
    errors: list[str] = []
    if manifest.get("schema_version") != 1:
        errors.append("schema_version must be 1")

    context = _build_context(manifest, errors)
    references = manifest.get("commercial_references") or []
    ref_ids = _validate_references(references, errors)
    _validate_aggregate(manifest, context, ref_ids, errors)
    _validate_capabilities(manifest, context, errors)
    _validate_supporting_evidence(manifest, context, errors)
    return errors

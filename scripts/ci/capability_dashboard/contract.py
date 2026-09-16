"""Fixed delivery contract and typed evidence predicates."""

# This module owns the parts of the capability dashboard that must not be
# editable through ``docs/capability/manifest.json``: the mandatory delivery
# policy (#122 network compatibility, #126 acceptance), the canonical
# validation-level ranking, and the explicit package-result token.  Both
# validation and aggregation import these constants, so a weakened or reordered
# manifest cannot manufacture a delivered target.

from __future__ import annotations

import re

# The mandatory delivery policy.  ``validate_manifest`` rejects an aggregate
# that omits or weakens any of these, and the aggregate always evaluates
# against these values -- never against the manifest's editable ``aggregate``
# block.
MANDATORY_REQUIRED_MODES: tuple[str, ...] = ("mp-client", "headless-server")
MANDATORY_REQUIRED_COMMERCIAL_REFERENCES: tuple[str, ...] = (
    "commercial-1.7",
    "steam-1.8",
)
MANDATORY_REQUIRED_STRONGEST_VALIDATION = "packaged_clean_machine"
MANDATORY_REQUIRE_COMMERCIAL_REFERENCE_VALIDATION = True
MANDATORY_REQUIRE_PACKAGE_RESULT = True

# Canonical validation ordering.  Ranking is fixed here so that reordering the
# manifest's editable ``enums.validation_levels`` cannot promote a weak level
# past the delivery threshold.  ``validate_manifest`` requires the declared set
# to match these names exactly (order-insensitive), while the aggregate ranks
# against this constant -- never against the manifest order.
CANONICAL_VALIDATION_ORDER: tuple[str, ...] = (
    "none",
    "configured",
    "compiled",
    "linked_production",
    "synthetic_integration",
    "licensed_content_startup",
    "original_peer_compatibility",
    "packaged_clean_machine",
)
VALIDATION_RANK: dict[str, int] = {
    name: rank for rank, name in enumerate(CANONICAL_VALIDATION_ORDER)
}

# Explicit package-result contract.  A package smoke is successful only when it
# reports the success token; ``failed``, ``pending`` or any arbitrary truthy
# string (a hash, ``ok``, a path, ...) is not a successful package result.
PACKAGE_RESULT_SUCCESS = "passed"
PACKAGE_RESULT_VALUES: tuple[str, ...] = ("pending", "passed", "failed")

REQUIRED_CAPABILITY_FIELDS: tuple[str, ...] = (
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

_SHA_RE = re.compile(r"^[0-9a-fA-F]{7,64}$")
_SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")


def is_sha(value: object) -> bool:
    """Return True for a non-empty commit-style SHA (7-64 hex characters)."""
    return isinstance(value, str) and bool(_SHA_RE.match(value.strip()))


def is_sha256(value: object) -> bool:
    """Return True for a full 64-hex-character SHA-256 digest."""
    return isinstance(value, str) and bool(_SHA256_RE.match(value.strip()))


def is_evidence_run(value: object) -> bool:
    """Return True for a non-empty run identifier, never a bool."""
    if isinstance(value, bool):
        return False
    if isinstance(value, int):
        return True
    return isinstance(value, str) and bool(value.strip())


def validation_rank(level: object) -> int | None:
    """Return the fixed rank for a validation level, or None when unknown."""
    if not isinstance(level, str):
        return None
    return VALIDATION_RANK.get(level)


def claims_delivery_level(capability: dict) -> bool:
    """Return True when a capability claims the mandatory delivery level."""
    rank = validation_rank(capability.get("strongest_validation"))
    if rank is None:
        return False
    return rank >= VALIDATION_RANK[MANDATORY_REQUIRED_STRONGEST_VALIDATION]


def capability_evidence_complete(capability: dict) -> bool:
    """
    Return True when a promoted capability row carries complete evidence.

    Pending rows may leave ``sha``/``run``/``package_result`` null, but a row
    claiming the packaged delivery level must carry an exact SHA, a run id and
    an explicit successful package result.
    """
    evidence = capability.get("evidence") or {}
    return (
        is_sha(evidence.get("sha"))
        and is_evidence_run(evidence.get("run"))
        and evidence.get("package_result") == PACKAGE_RESULT_SUCCESS
    )


def reference_provenance_complete(reference: dict) -> bool:
    """Return True when a validated reference carries its own provenance."""
    return is_sha256(reference.get("sha256")) and is_evidence_run(
        reference.get("evidence_run")
    )

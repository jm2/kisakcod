"""
Capability dashboard generator package.

The stable entry point is ``scripts/ci/capability-dashboard.py``; this package
holds the implementation split into cohesive modules:

* :mod:`capability_dashboard.contract` -- the fixed, non-editable policy;
* :mod:`capability_dashboard.manifest` -- loading and strict validation;
* :mod:`capability_dashboard.inventory` -- CI/test inventory derivation;
* :mod:`capability_dashboard.aggregate` -- requested-target delivery;
* :mod:`capability_dashboard.render` -- deterministic Markdown rendering;
* :mod:`capability_dashboard.cli` -- argument parsing and the ``--check`` gate.
"""

from __future__ import annotations

from .aggregate import compute_aggregate
from .cli import build_dashboard, main
from .contract import (
    CANONICAL_VALIDATION_ORDER,
    MANDATORY_REQUIRED_COMMERCIAL_REFERENCES,
    MANDATORY_REQUIRED_MODES,
    MANDATORY_REQUIRED_STRONGEST_VALIDATION,
    PACKAGE_RESULT_SUCCESS,
    PACKAGE_RESULT_VALUES,
    REQUIRED_CAPABILITY_FIELDS,
    VALIDATION_RANK,
    capability_evidence_complete,
    claims_delivery_level,
    is_evidence_run,
    is_sha,
    is_sha256,
    reference_provenance_complete,
)
from .inventory import (
    MatrixExpansionError,
    derive_ci_inventory,
    derive_test_inventory,
    matrix_legs,
)
from .manifest import load_manifest, validate_manifest
from .paths import (
    DEFAULT_MANIFEST,
    DEFAULT_OUTPUT,
    GENERATED_MARKER,
    REPO_ROOT,
    WORKFLOW_DIR,
)
from .render import render_dashboard

# ``_matrix_legs`` and ``_jobs_block``/``_job_blocks`` remain available for
# existing callers and tests that referenced the original single-file module.
_matrix_legs = matrix_legs

__all__ = [
    "CANONICAL_VALIDATION_ORDER",
    "DEFAULT_MANIFEST",
    "DEFAULT_OUTPUT",
    "GENERATED_MARKER",
    "MANDATORY_REQUIRED_COMMERCIAL_REFERENCES",
    "MANDATORY_REQUIRED_MODES",
    "MANDATORY_REQUIRED_STRONGEST_VALIDATION",
    "MatrixExpansionError",
    "PACKAGE_RESULT_SUCCESS",
    "PACKAGE_RESULT_VALUES",
    "REPO_ROOT",
    "REQUIRED_CAPABILITY_FIELDS",
    "VALIDATION_RANK",
    "WORKFLOW_DIR",
    "_matrix_legs",
    "build_dashboard",
    "capability_evidence_complete",
    "claims_delivery_level",
    "compute_aggregate",
    "derive_ci_inventory",
    "derive_test_inventory",
    "is_evidence_run",
    "is_sha",
    "is_sha256",
    "load_manifest",
    "main",
    "matrix_legs",
    "reference_provenance_complete",
    "render_dashboard",
    "validate_manifest",
]

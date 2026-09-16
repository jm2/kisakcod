#!/usr/bin/env python3
"""Generate the KisakCOD capability dashboard from the evidence manifest."""

# The implementation lives in the ``capability_dashboard`` package next to this
# launcher.  This file only keeps the documented
# ``python3 scripts/ci/capability-dashboard.py`` entry point stable, and
# re-exports the package API for callers that import the script path directly.
#
# The manifest (``docs/capability/manifest.json``) is the small authoritative
# dataset.  The package validates it against the fixed mandatory delivery
# contract, derives the CI job inventory from ``.github/workflows/*.yml`` and
# ``*.yaml`` plus the test inventory from ``tests/CMakeLists.txt`` /
# ``CMakePresets.json``, and renders a deterministic Markdown dashboard that
# ``--check`` verifies is current.

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capability_dashboard import (  # noqa: E402,F401  pylint: disable=wrong-import-position,import-error
    CANONICAL_VALIDATION_ORDER,
    MANDATORY_REQUIRED_COMMERCIAL_REFERENCES,
    MANDATORY_REQUIRED_MODES,
    MANDATORY_REQUIRED_STRONGEST_VALIDATION,
    PACKAGE_RESULT_SUCCESS,
    PACKAGE_RESULT_VALUES,
    REQUIRED_CAPABILITY_FIELDS,
    VALIDATION_RANK,
    MatrixExpansionError,
    build_dashboard,
    capability_evidence_complete,
    claims_delivery_level,
    compute_aggregate,
    derive_ci_inventory,
    derive_test_inventory,
    is_evidence_run,
    is_sha,
    is_sha256,
    load_manifest,
    main,
    matrix_legs,
    reference_provenance_complete,
    render_dashboard,
    validate_manifest,
)
from capability_dashboard.inventory import (  # noqa: E402  pylint: disable=wrong-import-position,import-error
    _job_blocks,
    _jobs_block,
)

_matrix_legs = matrix_legs

__all__ = [
    "CANONICAL_VALIDATION_ORDER",
    "MANDATORY_REQUIRED_COMMERCIAL_REFERENCES",
    "MANDATORY_REQUIRED_MODES",
    "MANDATORY_REQUIRED_STRONGEST_VALIDATION",
    "MatrixExpansionError",
    "PACKAGE_RESULT_SUCCESS",
    "PACKAGE_RESULT_VALUES",
    "REQUIRED_CAPABILITY_FIELDS",
    "VALIDATION_RANK",
    "_job_blocks",
    "_jobs_block",
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


if __name__ == "__main__":
    raise SystemExit(main())

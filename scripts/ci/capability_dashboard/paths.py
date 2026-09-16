"""
Filesystem locations and the generated-file marker for the dashboard.

The package lives at ``scripts/ci/capability_dashboard``; every repository
path is derived from this file's location so the generator works regardless of
the current working directory or the checkout root.
"""

from __future__ import annotations

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_MANIFEST = REPO_ROOT / "docs" / "capability" / "manifest.json"
DEFAULT_OUTPUT = REPO_ROOT / "docs" / "CAPABILITY_DASHBOARD.md"
WORKFLOW_DIR = REPO_ROOT / ".github" / "workflows"
TESTS_CMAKE = REPO_ROOT / "tests" / "CMakeLists.txt"
CMAKE_PRESETS = REPO_ROOT / "CMakePresets.json"

GENERATED_MARKER = (
    "<!-- GENERATED FILE - DO NOT EDIT. "
    "Regenerate with `python3 scripts/ci/capability-dashboard.py`. -->"
)

"""
Command-line entry point for the capability dashboard generator.

This module wires loading, validation, inventory derivation, aggregation and
rendering together, and implements the ``--check`` currency gate used by CI.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .aggregate import compute_aggregate
from .inventory import derive_ci_inventory, derive_test_inventory
from .manifest import load_manifest, validate_manifest
from .paths import DEFAULT_MANIFEST, DEFAULT_OUTPUT
from .render import render_dashboard


def build_dashboard(manifest_path: Path = DEFAULT_MANIFEST) -> tuple[str, dict]:
    """Return the rendered dashboard and its aggregate for ``manifest_path``."""
    manifest = load_manifest(manifest_path)
    errors = validate_manifest(manifest)
    if errors:
        raise SystemExit(
            "manifest validation failed:\n  - " + "\n  - ".join(errors)
        )
    aggregate = compute_aggregate(manifest)
    rendered = render_dashboard(
        manifest, derive_ci_inventory(), derive_test_inventory(), aggregate
    )
    return rendered, aggregate


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    """Parse the command-line arguments."""
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
    return parser.parse_args(argv)


def _check_dashboard(output: Path, rendered: str, aggregate: dict) -> int:
    """Compare the committed dashboard with the rendered copy."""
    if not output.exists():
        print(f"dashboard missing: {output}", file=sys.stderr)
        return 1
    if output.read_text(encoding="utf-8") != rendered:
        print(
            f"dashboard is stale: {output}\n"
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


def main(argv: list[str] | None = None) -> int:
    """Run the generator CLI and return its exit status."""
    args = _parse_args(argv)
    try:
        rendered, aggregate = build_dashboard(args.manifest)
    except SystemExit as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if args.stdout:
        sys.stdout.write(rendered)
        return 0
    if args.check:
        return _check_dashboard(args.output, rendered, aggregate)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    print(
        f"wrote {args.output} "
        f"({aggregate['delivered']}/{aggregate['requested']} requested "
        "targets delivered)",
        file=sys.stderr,
    )
    return 0

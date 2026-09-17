#!/usr/bin/env python3
"""Release provenance recorder and completeness gate for KisakCOD packaging."""

# Why this exists
# ---------------
# A checksum file cannot certify that a release is complete. A workflow can hash
# whatever files happen to be present and emit a perfectly self-consistent
# ``SHA256SUMS.txt`` while an expected production artifact is silently absent,
# or while a provenance manifest still names a different commit than the
# verified release ref. Hashing proves consistency, not completeness.
#
# This tool implements the stronger contract required for production packages
# (fork issue #137, acceptance criteria 1 and 2):
#
# * ``record``          build a provenance manifest for one release profile from
#                       an explicit artifact list plus an inventory of dependency
#                       sources/revisions/features, runtime lookup, notices/SBOM
#                       and symbols. Every artifact is hashed into the manifest.
# * ``verify``          fail closed unless the assembled (flat) release
#                       directory contains exactly the expected artifacts and
#                       provenance manifests, every manifest identifies the
#                       verified tag and commit, every required inventory field
#                       is present, every required prerequisite succeeded, the
#                       source manifest declares exactly the source archive it
#                       binds (digest included), and ``SHA256SUMS.txt`` covers
#                       exactly the published file set.
# * ``identity-write``  write a deterministic ``release-identity.json`` so a
#                       source archive carries its version identity.
# * ``identity-verify`` verify that identity against an expected tag/commit.
#
# The tool is Python-standard-library only, never mutates the release directory,
# and prints a precise ``FAIL:`` line for every violated invariant before
# exiting non-zero. Exit codes: 0 pass, 1 gate failure, 2 usage error.

from __future__ import annotations

import argparse
import sys
from datetime import datetime, timezone
from pathlib import Path

from release_provenance_archive import verify_source_archive
from release_provenance_common import (
    IDENTITY_KEYS,
    INVENTORY_FIELDS,
    INVENTORY_VALIDATORS,
    SCHEMA_VERSION,
    SHA256_RE,
    GateError,
    ManifestExpectation,
    check_identity_shape,
    load_json,
    sha256_file,
    verify_manifest_identity,
    write_json,
)


def utc_timestamp() -> str:
    """Return the current UTC time as an ISO-8601 ``Z`` timestamp."""
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def substitute_tag(value: str, tag: str) -> str:
    """Expand the ``${tag}`` placeholder in a configured file name."""
    return value.replace("${tag}", tag)


def parse_checksums(path: Path) -> dict[str, str]:
    """Parse a flat ``sha256  basename`` file. Fail closed on malformed lines."""
    entries: dict[str, str] = {}
    duplicates: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        raise GateError(f"{path}: checksum manifest not found")
    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            raise GateError(f"{path}:{number}: malformed checksum line {raw!r}")
        digest, name = parts
        name = name.lstrip("*")
        if SHA256_RE.match(digest) is None:
            raise GateError(f"{path}:{number}: {digest!r} is not a sha256 digest")
        if name in entries:
            duplicates.append(name)
            continue
        entries[name] = digest
    if duplicates:
        raise GateError(f"{path}: duplicate checksum entries for {sorted(set(duplicates))}")
    return entries


# ---------------------------------------------------------------------------
# identity-write / identity-verify
# ---------------------------------------------------------------------------


def cmd_identity_write(args: argparse.Namespace) -> int:
    """Write a deterministic release-identity.json for a tag/commit."""
    check_identity_shape(args.tag, args.commit)
    version = args.version if args.version is not None else args.tag.lstrip("v")
    if not version:
        raise GateError(f"cannot derive a version from tag {args.tag!r}; pass --version")
    write_json(
        Path(args.out),
        {
            "schema_version": SCHEMA_VERSION,
            "tag": args.tag,
            "commit": args.commit,
            "version": version,
        },
    )
    return 0


def cmd_identity_verify(args: argparse.Namespace) -> int:
    """Verify a release-identity.json against an expected tag and commit."""
    check_identity_shape(args.tag, args.commit)
    identity = load_json(Path(args.identity))
    if identity.get("schema_version") != SCHEMA_VERSION:
        raise GateError(
            f"{args.identity}: schema_version {identity.get('schema_version')!r} "
            f"is not {SCHEMA_VERSION}"
        )
    for key, expected in zip(IDENTITY_KEYS, (args.tag, args.commit)):
        actual = identity.get(key)
        if actual != expected:
            raise GateError(
                f"{args.identity}: {key} {actual!r} does not match expected {expected!r}"
            )
    expected_version = args.tag.lstrip("v")
    actual_version = identity.get("version")
    if actual_version != expected_version:
        raise GateError(
            f"{args.identity}: version {actual_version!r} does not match tag-derived "
            f"{expected_version!r}"
        )
    return 0


# ---------------------------------------------------------------------------
# record
# ---------------------------------------------------------------------------


def _record_inventory(manifest: dict, args: argparse.Namespace) -> None:
    """Copy the typed, validated inventory records into the manifest."""
    if args.toolchain:
        manifest["toolchain"] = load_json(Path(args.toolchain))
    if not args.inventory:
        return
    inventory = load_json(Path(args.inventory))
    for field in INVENTORY_FIELDS:
        if field not in inventory:
            continue
        problem = INVENTORY_VALIDATORS[field](inventory[field])
        if problem is not None:
            raise GateError(f"inventory field {field!r} is invalid: {problem}")
        manifest[field] = inventory[field]


def _record_artifacts(dist: Path, names: list[str]) -> list[dict[str, object]]:
    """Hash each declared artifact into a sorted manifest record list."""
    artifacts = []
    for name in names:
        if "/" in name or "\\" in name:
            raise GateError(
                f"artifact {name!r} must be a flat basename; the published release "
                "set is assembled flat (download-artifact merge-multiple)"
            )
        path = dist / name
        if not path.is_file():
            raise GateError(f"artifact {name}: not found under {dist}")
        artifacts.append(
            {
                "path": name,
                "sha256": sha256_file(path),
                "size": path.stat().st_size,
            }
        )
    return sorted(artifacts, key=lambda item: item["path"])


def cmd_record(args: argparse.Namespace) -> int:
    """Record a provenance manifest for one release profile."""
    check_identity_shape(args.tag, args.commit)
    dist = Path(args.dist)
    if not dist.is_dir():
        raise GateError(f"{dist}: release directory does not exist")
    if not args.artifact:
        raise GateError("record requires at least one --artifact")

    manifest: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "tag": args.tag,
        "commit": args.commit,
        "target": args.target,
        "config": args.config,
        "workflow_run": args.workflow_run,
        "generated_at": args.generated_at or utc_timestamp(),
        "artifacts": [],
    }
    _record_inventory(manifest, args)
    manifest["artifacts"] = _record_artifacts(dist, args.artifact)
    write_json(Path(args.out), manifest)
    return 0


# ---------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------


def _prerequisite_failures(requirements: dict, args: argparse.Namespace) -> list[str]:
    """Fail closed on any declared prerequisite that did not succeed."""
    required = requirements.get("required_prerequisites") or []
    if not required:
        return []
    if not args.prerequisites:
        message = (
            "prerequisites: requirements declare "
            f"{len(required)} required prerequisite(s) but no "
            "--prerequisites results file was supplied"
        )
        return [message]
    results = load_json(Path(args.prerequisites))
    return [
        f"prerequisites: required job {job!r} result {results.get(job)!r} is not 'success'"
        for job in required
        if results.get(job) != "success"
    ]


def _manifest_set_failures(label: str, declared: list[str], wanted: set[str]) -> list[str]:
    """Return the declared/expected artifact-set differences for one manifest."""
    declared_set = set(declared)
    failures: list[str] = []
    missing = sorted(wanted - declared_set)
    extra = sorted(declared_set - wanted)
    if missing:
        failures.append(f"{label}: manifest does not declare expected artifact(s) {missing}")
    if extra:
        failures.append(f"{label}: manifest declares undeclared artifact(s) {extra}")
    return failures


def _verify_profiles(
    requirements: dict, dist: Path, tag: str, commit: str
) -> tuple[list[str], set[str]]:
    """Verify every binary profile manifest and return (failures, expected set)."""
    failures: list[str] = []
    expected: set[str] = set()
    for profile in requirements["profiles"]:
        target = profile["target"]
        config = profile.get("config")
        artifact_names = {substitute_tag(name, tag) for name in profile["artifacts"]}
        provenance_name = substitute_tag(profile["provenance"], tag)
        expected |= artifact_names
        expected.add(provenance_name)

        manifest_path = dist / provenance_name
        if not manifest_path.is_file():
            failures.append(f"{target}: provenance manifest {provenance_name} is missing from dist")
            continue
        expectation = ManifestExpectation(
            label=target,
            tag=tag,
            commit=commit,
            target=target,
            config=config,
            required_inventory=tuple(profile.get("required_inventory") or ()),
        )
        profile_failures, declared = verify_manifest_identity(manifest_path, expectation, dist)
        failures.extend(profile_failures)
        failures.extend(_manifest_set_failures(target, declared, artifact_names))
    return failures, expected


def _verify_source(
    source: dict, dist: Path, tag: str, commit: str
) -> tuple[list[str], set[str]]:
    """Verify the source profile and its archive identity."""
    archive_name = substitute_tag(source["archive"], tag)
    provenance_name = substitute_tag(source["provenance"], tag)
    expected = {archive_name, provenance_name}
    failures: list[str] = []

    manifest_path = dist / provenance_name
    if manifest_path.is_file():
        # The source manifest must declare exactly the source archive and bind
        # its digest, otherwise mutating any other archive member and
        # regenerating SHA256SUMS.txt would leave stale provenance passing.
        expectation = ManifestExpectation(label="source", tag=tag, commit=commit, config="source")
        manifest_failures, declared = verify_manifest_identity(manifest_path, expectation, dist)
        failures.extend(manifest_failures)
        failures.extend(_manifest_set_failures("source", declared, {archive_name}))
    else:
        failures.append(f"source: provenance manifest {provenance_name} is missing from dist")

    failures.extend(
        verify_source_archive(
            dist / archive_name,
            source.get("identity_member", "release-identity.json"),
            source.get("carrier", "src/source_identity.txt"),
            tag,
            commit,
        )
    )
    return failures, expected


def _completeness_failures(
    dist: Path, expected: set[str], checksums_name: str | None
) -> list[str]:
    """Require dist to hold exactly the expected published file set."""
    actual_files = sorted(
        item.name for item in dist.iterdir() if item.is_file() and item.name != checksums_name
    )
    failures = [
        f"completeness: expected published file {name} is missing from dist"
        for name in sorted(expected)
        if name not in actual_files
    ]
    failures += [
        f"completeness: {name} is present in dist but not part of the expected published set"
        for name in actual_files
        if name not in expected
    ]
    return failures


def _checksum_entry_failures(
    dist: Path, checksums_name: str, entries: dict[str, str], expected: set[str]
) -> list[str]:
    """Return coverage and digest mismatches for one checksum manifest."""
    failures = [
        f"checksums: {checksums_name} does not cover {name}"
        for name in sorted(expected)
        if name not in entries
    ]
    for name in sorted(entries):
        if name not in expected:
            failures.append(
                f"checksums: {checksums_name} lists {name}, which is not part of "
                "the expected published set"
            )
        elif (dist / name).is_file():
            actual = sha256_file(dist / name)
            if actual != entries[name]:
                failures.append(
                    f"checksums: {name} sha256 {actual} does not match manifest {entries[name]}"
                )
    return failures


def _checksum_failures(
    dist: Path, checksums_name: str | None, expected: set[str]
) -> list[str]:
    """Verify SHA256SUMS.txt covers exactly the expected set and matches bytes."""
    if not checksums_name:
        return []
    try:
        entries = parse_checksums(dist / checksums_name)
    except GateError as exc:
        return [str(exc)]
    failures: list[str] = []
    if not entries and expected:
        failures.append(
            f"checksums: {checksums_name} is empty but {len(expected)} file(s) are "
            "expected in the published set"
        )
    failures.extend(_checksum_entry_failures(dist, checksums_name, entries, expected))
    return failures


def _report_verify(args: argparse.Namespace, failures: list[str], files: int, profiles: int) -> int:
    """Print the precise failure set (or the PASS line) and return the exit code."""
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        print(
            f"release-provenance: FAILED ({len(failures)} invariant(s)) for "
            f"tag={args.tag} commit={args.commit}",
            file=sys.stderr,
        )
        return 1
    print(
        f"release-provenance: PASS tag={args.tag} commit={args.commit} "
        f"files={files} profiles={profiles}"
    )
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    """Fail closed on an incomplete or inconsistent release set."""
    check_identity_shape(args.tag, args.commit)
    requirements_path = Path(args.requirements)
    requirements = load_json(requirements_path)
    if requirements.get("schema_version") != SCHEMA_VERSION:
        raise GateError(
            f"{requirements_path}: schema_version {requirements.get('schema_version')!r} "
            f"is not {SCHEMA_VERSION}"
        )
    dist = Path(args.dist)
    if not dist.is_dir():
        raise GateError(f"{dist}: release directory does not exist")
    profiles = requirements.get("profiles") or []
    if not profiles:
        raise GateError(f"{requirements_path}: no profiles declared")

    # A green artifact set built on a failed prerequisite is not releasable, and
    # dist must contain exactly the expected publish set: an undeclared file is
    # as much a release defect as a missing one.
    failures = _prerequisite_failures(requirements, args)
    profile_failures, expected = _verify_profiles(requirements, dist, args.tag, args.commit)
    failures.extend(profile_failures)
    source = requirements.get("source")
    if source:
        source_failures, source_expected = _verify_source(source, dist, args.tag, args.commit)
        failures.extend(source_failures)
        expected |= source_expected
    checksums_name = requirements.get("checksums")
    if checksums_name is not None and not isinstance(checksums_name, str):
        raise GateError(
            f"{requirements_path}: checksums must name a checksum manifest string"
        )
    failures.extend(_completeness_failures(dist, expected, checksums_name))
    failures.extend(_checksum_failures(dist, checksums_name, expected))
    return _report_verify(args, failures, len(expected), len(profiles))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    """Build the argument parser for every subcommand."""
    parser = argparse.ArgumentParser(
        prog="release_provenance.py",
        description="Record release provenance and gate release completeness.",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    identity_write = sub.add_parser(
        "identity-write", help="write a deterministic release-identity.json"
    )
    identity_write.add_argument("--tag", required=True)
    identity_write.add_argument("--commit", required=True)
    identity_write.add_argument("--version", default=None)
    identity_write.add_argument("--out", required=True)
    identity_write.set_defaults(func=cmd_identity_write)

    identity_verify = sub.add_parser(
        "identity-verify", help="verify a release-identity.json against a tag/commit"
    )
    identity_verify.add_argument("--identity", required=True)
    identity_verify.add_argument("--tag", required=True)
    identity_verify.add_argument("--commit", required=True)
    identity_verify.set_defaults(func=cmd_identity_verify)

    record = sub.add_parser("record", help="record a provenance manifest")
    record.add_argument("--dist", required=True)
    record.add_argument("--tag", required=True)
    record.add_argument("--commit", required=True)
    record.add_argument("--target", required=True)
    record.add_argument("--config", required=True)
    record.add_argument("--artifact", action="append", default=[])
    record.add_argument("--toolchain", default=None)
    record.add_argument("--inventory", default=None)
    record.add_argument("--workflow-run", default=None)
    record.add_argument("--generated-at", default=None)
    record.add_argument("--out", required=True)
    record.set_defaults(func=cmd_record)

    verify = sub.add_parser("verify", help="fail closed on an incomplete release set")
    verify.add_argument("--requirements", required=True)
    verify.add_argument("--dist", required=True)
    verify.add_argument("--tag", required=True)
    verify.add_argument("--commit", required=True)
    verify.add_argument("--prerequisites", default=None)
    verify.set_defaults(func=cmd_verify)

    return parser


def main(argv: list[str] | None = None) -> int:
    """Parse arguments and dispatch to the selected subcommand."""
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except GateError as exc:
        print(f"release-provenance: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

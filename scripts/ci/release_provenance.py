#!/usr/bin/env python3
"""Release provenance recorder and completeness gate for KisakCOD packaging.

Why this exists
---------------
A checksum file cannot certify that a release is complete. A workflow can hash
whatever files happen to be present and emit a perfectly self-consistent
``SHA256SUMS.txt`` while an expected production artifact is silently absent, or
while a provenance manifest still names a different commit than the verified
release ref. Hashing proves consistency, not completeness.

This tool implements the stronger contract required for production packages
(fork issue #137, acceptance criteria 1 and 2):

* ``record``          build a provenance manifest for one release profile from
                      an explicit artifact list plus an inventory of dependency
                      sources/revisions/features, runtime lookup, notices/SBOM
                      and symbols. Every artifact is hashed into the manifest.
* ``verify``          fail closed unless the assembled (flat) release directory
                      contains exactly the expected artifacts and provenance
                      manifests, every manifest identifies the verified tag and
                      commit, every required inventory field is present, every
                      required prerequisite succeeded, and ``SHA256SUMS.txt``
                      covers exactly the published file set.
* ``identity-write``  write a deterministic ``release-identity.json`` so a
                      source archive carries its version identity.
* ``identity-verify`` verify that identity against an expected tag/commit.

``verify`` also opens a declared source archive and checks that it carries both
the ``release-identity.json`` member (tag and commit) and the build-consumed
``src/source_identity.txt`` carrier with the substituted full commit, which is
what lets a rebuild from the archive keep version identity when ``.git`` is
absent.

The tool is Python-standard-library only, never mutates the release directory,
and prints a precise ``FAIL:`` line for every violated invariant before exiting
non-zero. Exit codes: 0 pass, 1 gate failure, 2 usage error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tarfile
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

# Manifest inventory field -> requirement name. The requirement file selects a
# subset per profile; scaffolding-only legs may require just the toolchain.
INVENTORY_FIELDS = (
    "toolchain",
    "dependencies",
    "runtime_lookup",
    "notices",
    "symbols",
)

# The one provenance-identity value every profile manifest must agree on.
IDENTITY_KEYS = ("tag", "commit")


class GateError(Exception):
    """A violated release invariant, reported with the other failures."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    try:
        with path.open("rb") as handle:
            value = json.load(handle)
    except FileNotFoundError:
        raise GateError(f"{path}: file not found")
    except json.JSONDecodeError as exc:
        raise GateError(f"{path}: invalid JSON ({exc})")
    if not isinstance(value, dict):
        raise GateError(f"{path}: expected a JSON object")
    return value


def is_nonempty_str(value: object) -> bool:
    return isinstance(value, str) and value.strip() != ""


def is_sha256(value: object) -> bool:
    return isinstance(value, str) and SHA256_RE.match(value) is not None


def validate_toolchain(value: object) -> str | None:
    """A toolchain record maps tool names to non-empty version strings.

    A boolean flag, a number, or a string is a claim that toolchain evidence
    exists, not the evidence itself, so only an object of concrete versions is
    accepted.
    """
    if not isinstance(value, dict) or not value:
        return "must be a non-empty object of tool name -> non-empty version"
    for key, item in value.items():
        if not is_nonempty_str(key) or not is_nonempty_str(item):
            return f"entry {key!r} must map to a non-empty string"
    return None


def validate_dependencies(value: object) -> str | None:
    """Each dependency must name its source, revision and enabled features."""
    if not isinstance(value, list) or not value:
        return "must be a non-empty list of dependency records"
    for index, entry in enumerate(value):
        if not isinstance(entry, dict):
            return f"entry {index} must be an object"
        if "name" in entry and not is_nonempty_str(entry.get("name")):
            return f"entry {index} name must be a non-empty string"
        if not is_nonempty_str(entry.get("source")):
            return f"entry {index} source must be a non-empty string"
        if not is_nonempty_str(entry.get("revision")):
            return f"entry {index} revision must be a non-empty string"
        features = entry.get("features")
        if not isinstance(features, list) or not features:
            return f"entry {index} features must be a non-empty list"
        if any(not is_nonempty_str(feature) for feature in features):
            return f"entry {index} features must be non-empty strings"
    return None


def validate_artifact_records(field: str):
    def validate(value: object) -> str | None:
        if not isinstance(value, list) or not value:
            return f"must be a non-empty list of {field} records"
        for index, entry in enumerate(value):
            if not isinstance(entry, dict):
                return f"entry {index} must be an object"
            if not is_nonempty_str(entry.get("path")):
                return f"entry {index} path must be a non-empty string"
            if "sha256" in entry and not is_sha256(entry.get("sha256")):
                return f"entry {index} sha256 must be a 64-hex digest"
        return None

    return validate


# Required inventory field -> typed validator. A manifest that claims a required
# inventory entry is present must carry a real record: ``false``/``0`` and
# wrong-typed or structurally incomplete records are rejected.
INVENTORY_VALIDATORS = {
    "toolchain": validate_toolchain,
    "dependencies": validate_dependencies,
    "runtime_lookup": validate_artifact_records("runtime lookup"),
    "notices": validate_artifact_records("notices"),
    "symbols": validate_artifact_records("symbols"),
}


def check_identity_shape(tag: str, commit: str) -> None:
    if not tag:
        raise GateError("release tag must not be empty")
    if not COMMIT_RE.match(commit or ""):
        raise GateError(f"release commit {commit!r} is not a full 40-hex commit")


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# identity-write / identity-verify
# ---------------------------------------------------------------------------


def cmd_identity_write(args: argparse.Namespace) -> int:
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


def cmd_record(args: argparse.Namespace) -> int:
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
        "generated_at": args.generated_at
        or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "artifacts": [],
    }

    if args.toolchain:
        manifest["toolchain"] = load_json(Path(args.toolchain))
    if args.inventory:
        inventory = load_json(Path(args.inventory))
        for field in INVENTORY_FIELDS:
            if field not in inventory:
                continue
            problem = INVENTORY_VALIDATORS[field](inventory[field])
            if problem is not None:
                raise GateError(f"inventory field {field!r} is invalid: {problem}")
            manifest[field] = inventory[field]

    artifacts = []
    for name in args.artifact:
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
    manifest["artifacts"] = sorted(artifacts, key=lambda item: item["path"])
    write_json(Path(args.out), manifest)
    return 0


# ---------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------


def substitute_tag(value: str, tag: str) -> str:
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
        if not SHA256_RE.match(digest):
            raise GateError(f"{path}:{number}: {digest!r} is not a sha256 digest")
        if name in entries:
            duplicates.append(name)
            continue
        entries[name] = digest
    if duplicates:
        raise GateError(
            f"{path}: duplicate checksum entries for {sorted(set(duplicates))}"
        )
    return entries


def verify_manifest_identity(
    manifest_path: Path,
    profile_label: str,
    tag: str,
    commit: str,
    expected_target: str | None,
    expected_config: str | None,
    required_inventory: list[str],
    dist: Path,
    require_artifacts: bool = True,
) -> tuple[list[str], list[str]]:
    """Return (failures, declared artifact basenames) for one provenance manifest."""
    failures: list[str] = []
    record = load_json(manifest_path)

    if record.get("schema_version") != SCHEMA_VERSION:
        failures.append(
            f"{profile_label}: {manifest_path.name} schema_version "
            f"{record.get('schema_version')!r} is not {SCHEMA_VERSION}"
        )
    for key, expected in (("tag", tag), ("commit", commit)):
        actual = record.get(key)
        if actual != expected:
            failures.append(
                f"{profile_label}: {manifest_path.name} {key} {actual!r} does not "
                f"match verified release {expected!r}"
            )
    if expected_target is not None and record.get("target") != expected_target:
        failures.append(
            f"{profile_label}: {manifest_path.name} target {record.get('target')!r} "
            f"does not match expected {expected_target!r}"
        )
    if expected_config is not None and record.get("config") != expected_config:
        failures.append(
            f"{profile_label}: {manifest_path.name} config {record.get('config')!r} "
            f"does not match expected {expected_config!r}"
        )

    for field in required_inventory:
        if field not in INVENTORY_FIELDS:
            failures.append(f"{profile_label}: unknown required inventory field {field!r}")
            continue
        problem = INVENTORY_VALIDATORS[field](record.get(field))
        if problem is not None:
            failures.append(
                f"{profile_label}: {manifest_path.name} required inventory {field!r} "
                f"is missing or invalid: {problem}"
            )

    artifacts = record.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        if require_artifacts:
            failures.append(f"{profile_label}: {manifest_path.name} declares no artifacts")
        return failures, []

    names: list[str] = []
    for entry in artifacts:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            failures.append(f"{profile_label}: malformed artifact entry {entry!r}")
            continue
        name = entry["path"]
        names.append(name)
        artifact_path = dist / name
        if not artifact_path.is_file():
            failures.append(f"{profile_label}: declared artifact {name} is missing from dist")
            continue
        digest = entry.get("sha256")
        if not isinstance(digest, str) or not SHA256_RE.match(digest):
            failures.append(f"{profile_label}: artifact {name} has an invalid sha256 field")
            continue
        actual = sha256_file(artifact_path)
        if actual != digest:
            failures.append(
                f"{profile_label}: artifact {name} sha256 {actual} does not match manifest {digest}"
            )
        size = entry.get("size")
        if isinstance(size, int) and size != artifact_path.stat().st_size:
            failures.append(
                f"{profile_label}: artifact {name} size {artifact_path.stat().st_size} "
                f"does not match manifest {size}"
            )
    return failures, sorted(names)


def find_archive_members(archive: tarfile.TarFile, member: str) -> list[tarfile.TarInfo]:
    """Match a member by basename (``release-identity.json`` is shipped flat)."""
    return [
        item for item in archive.getmembers() if item.isfile() and Path(item.name).name == member
    ]


def find_carrier_members(archive: tarfile.TarFile, carrier: str) -> list[tarfile.TarInfo]:
    """Match the build-consumed carrier by its path relative to the tree root.

    ``git archive`` normally prefixes every entry with a top-level directory, so
    ``src/source_identity.txt`` is identified by its trailing path components
    rather than by basename alone.
    """
    wanted = [part for part in carrier.replace("\\", "/").split("/") if part]
    matches = []
    for item in archive.getmembers():
        if not item.isfile():
            continue
        parts = [part for part in item.name.replace("\\", "/").split("/") if part]
        if parts[-len(wanted):] == wanted:
            matches.append(item)
    return matches


def verify_archive_identity_member(
    archive: tarfile.TarFile, identity_member: str, tag: str, commit: str
) -> list[str]:
    """Verify the shipped ``release-identity.json`` pins tag and commit."""
    members = find_archive_members(archive, identity_member)
    if not members:
        return [
            f"source: archive does not contain {identity_member}; a rebuild would "
            "lose version identity without .git"
        ]
    failures: list[str] = []
    for member in members:
        extracted = archive.extractfile(member)
        if extracted is None:
            failures.append(f"source: could not read {identity_member} from archive")
            continue
        try:
            identity = json.loads(extracted.read().decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            failures.append(f"source: {member.name} is not readable JSON ({exc})")
            continue
        if not isinstance(identity, dict):
            failures.append(f"source: {member.name} is not a JSON object")
            continue
        for key, expected in (("tag", tag), ("commit", commit)):
            if identity.get(key) != expected:
                failures.append(
                    f"source: {member.name} {key} {identity.get(key)!r} does not match "
                    f"verified release {expected!r}"
                )
    return failures


def verify_archive_carrier(
    archive: tarfile.TarFile, carrier_member: str, commit: str
) -> list[str]:
    """Verify the identity carrier the build actually consumes without ``.git``.

    ``scripts/extern/resolve_source_identity.cmake`` reads
    ``<source_dir>/src/source_identity.txt`` when no checkout is available, so
    the archive must carry that file with the substituted full commit. An absent
    member, an unexpanded ``$Format:...$`` placeholder, a malformed value, or a
    value that conflicts with the verified release each fail closed.
    """
    members = find_carrier_members(archive, carrier_member)
    if not members:
        return [
            f"source: archive does not contain the build-consumed identity carrier "
            f"{carrier_member}; a rebuild without .git could not recover the verified "
            "revision"
        ]
    failures: list[str] = []
    for member in members:
        extracted = archive.extractfile(member)
        if extracted is None:
            failures.append(f"source: could not read {carrier_member} from archive")
            continue
        try:
            text = extracted.read().decode("utf-8")
        except UnicodeDecodeError as exc:
            failures.append(f"source: {carrier_member} is not valid UTF-8 ({exc})")
            continue
        values = [
            line.split("=", 1)[1].strip()
            for line in text.splitlines()
            if line.strip().startswith("commit=")
        ]
        if not values:
            failures.append(f"source: {carrier_member} carries no commit= line")
            continue
        for value in values:
            if "$Format:" in value:
                failures.append(
                    f"source: {carrier_member} still holds the unsubstituted "
                    f"export-subst placeholder {value!r}; git archive did not expand it"
                )
            elif not COMMIT_RE.match(value):
                failures.append(
                    f"source: {carrier_member} commit {value!r} is not a full 40-hex commit"
                )
            elif value != commit:
                failures.append(
                    f"source: {carrier_member} commit {value!r} does not match "
                    f"verified release {commit!r}"
                )
    return failures


def verify_source_archive(
    archive_path: Path,
    identity_member: str,
    carrier_member: str,
    tag: str,
    commit: str,
) -> list[str]:
    """Prove a source archive carries the verified release identity.

    The JSON identity member pins tag and commit; the build-consumed carrier
    pins the exact full commit a rebuild would resolve without ``.git``. Both
    are required so the verifier cannot pass an archive whose identity evidence
    is disconnected from what the build reads.
    """
    if not archive_path.is_file():
        return [f"source: archive {archive_path.name} is missing from dist"]
    try:
        with tarfile.open(archive_path, "r:*") as archive:
            failures = verify_archive_identity_member(archive, identity_member, tag, commit)
            failures.extend(verify_archive_carrier(archive, carrier_member, commit))
    except tarfile.TarError as exc:
        return [f"source: archive {archive_path.name} could not be read ({exc})"]
    return failures


def cmd_verify(args: argparse.Namespace) -> int:
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

    failures: list[str] = []

    # 1. Prerequisites: a green artifact set built on a failed prerequisite is
    #    not a releasable set.
    required_prereqs = requirements.get("required_prerequisites") or []
    if required_prereqs:
        if not args.prerequisites:
            failures.append(
                "prerequisites: requirements declare "
                f"{len(required_prereqs)} required prerequisite(s) but no "
                "--prerequisites results file was supplied"
            )
        else:
            results = load_json(Path(args.prerequisites))
            for job in required_prereqs:
                status = results.get(job)
                if status != "success":
                    failures.append(
                        f"prerequisites: required job {job!r} result {status!r} is not 'success'"
                    )

    # 2. Assemble the exact expected publish set.
    expected: set[str] = set()
    profiles = requirements.get("profiles") or []
    if not profiles:
        raise GateError(f"{requirements_path}: no profiles declared")

    for profile in profiles:
        target = profile["target"]
        config = profile.get("config")
        artifact_names = [substitute_tag(name, args.tag) for name in profile["artifacts"]]
        provenance_name = substitute_tag(profile["provenance"], args.tag)
        required_inventory = list(profile.get("required_inventory") or [])

        for name in artifact_names:
            expected.add(name)
        expected.add(provenance_name)

        manifest_path = dist / provenance_name
        if not manifest_path.is_file():
            failures.append(
                f"{target}: provenance manifest {provenance_name} is missing from dist"
            )
            continue
        result = verify_manifest_identity(
            manifest_path,
            target,
            args.tag,
            args.commit,
            target,
            config,
            required_inventory,
            dist,
        )
        profile_failures, declared = result
        failures.extend(profile_failures)
        declared_set = set(declared)
        if declared_set != set(artifact_names):
            missing = sorted(set(artifact_names) - declared_set)
            extra = sorted(declared_set - set(artifact_names))
            if missing:
                failures.append(
                    f"{target}: manifest does not declare expected artifact(s) {missing}"
                )
            if extra:
                failures.append(
                    f"{target}: manifest declares undeclared artifact(s) {extra}"
                )

    # 3. Source archive identity (version identity without .git).
    source = requirements.get("source")
    if source:
        archive_name = substitute_tag(source["archive"], args.tag)
        source_provenance = substitute_tag(source["provenance"], args.tag)
        expected.add(archive_name)
        expected.add(source_provenance)
        if (dist / source_provenance).is_file():
            failures.extend(
                verify_manifest_identity(
                    dist / source_provenance,
                    "source",
                    args.tag,
                    args.commit,
                    None,
                    "source",
                    [],
                    dist,
                    require_artifacts=False,
                )[0]
            )
        else:
            failures.append(
                f"source: provenance manifest {source_provenance} is missing from dist"
            )
        failures.extend(
            verify_source_archive(
                dist / archive_name,
                source.get("identity_member", "release-identity.json"),
                source.get("carrier", "src/source_identity.txt"),
                args.tag,
                args.commit,
            )
        )

    # 4. The dist directory must contain exactly the expected publish set: an
    #    undeclared file is as much a release defect as a missing one.
    checksums_name = requirements.get("checksums")
    actual_files = sorted(
        item.name
        for item in dist.iterdir()
        if item.is_file() and item.name != checksums_name
    )
    for name in sorted(expected):
        if name not in actual_files:
            failures.append(f"completeness: expected published file {name} is missing from dist")
    for name in actual_files:
        if name not in expected:
            failures.append(
                f"completeness: {name} is present in dist but not part of the expected "
                "published set"
            )

    # 5. Checksums must cover exactly that set and verify against the bytes.
    if checksums_name:
        checksums_path = dist / checksums_name
        entries: dict[str, str] = {}
        parsed = False
        try:
            entries = parse_checksums(checksums_path)
            parsed = True
        except GateError as exc:
            failures.append(str(exc))
        if parsed and not entries and expected:
            failures.append(
                f"checksums: {checksums_name} is empty but {len(expected)} file(s) are "
                "expected in the published set"
            )
        if parsed:
            for name in sorted(expected):
                if name not in entries:
                    failures.append(f"checksums: {checksums_name} does not cover {name}")
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
                            f"checksums: {name} sha256 {actual} does not match "
                            f"manifest {entries[name]}"
                        )

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
        f"files={len(expected)} profiles={len(profiles)}"
    )
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
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
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except GateError as exc:
        print(f"release-provenance: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

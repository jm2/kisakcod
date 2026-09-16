"""Typed, fail-closed validation shared by the release provenance gate."""

# A checksum file proves consistency, not completeness. The gate therefore
# needs typed records for identity, inventory and artifacts so a manifest that
# claims evidence with ``false``, ``0`` or a wrong-typed value is rejected
# instead of accepted. Keeping the predicates here lets the CLI, the manifest
# verifier and the archive verifier agree on exactly one contract.

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
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
    """Return the hex-encoded sha256 digest of a file's bytes."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    """Read a JSON object, mapping missing/malformed input to ``GateError``."""
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


def write_json(path: Path, value: dict) -> None:
    """Write deterministic, sorted, newline-terminated JSON."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def is_nonempty_str(value: object) -> bool:
    """True only for a string with non-whitespace content."""
    return isinstance(value, str) and value.strip() != ""


def is_sha256(value: object) -> bool:
    """True only for a lowercase 64-hex digest string."""
    return isinstance(value, str) and SHA256_RE.match(value) is not None


def check_identity_shape(tag: str, commit: str) -> None:
    """Reject an empty tag or a commit that is not a full 40-hex object name."""
    if not tag:
        raise GateError("release tag must not be empty")
    if not COMMIT_RE.match(commit or ""):
        raise GateError(f"release commit {commit!r} is not a full 40-hex commit")


def _validate_toolchain(value: object) -> str | None:
    """Require an object of tool name -> non-empty version string."""
    # A boolean flag, a number or a string is a claim that toolchain evidence
    # exists, not the evidence itself, so only an object of concrete versions is
    # accepted.
    if not isinstance(value, dict) or not value:
        return "must be a non-empty object of tool name -> non-empty version"
    for key, item in value.items():
        if not is_nonempty_str(key) or not is_nonempty_str(item):
            return f"entry {key!r} must map to a non-empty string"
    return None


def _validate_dependency_entry(index: int, entry: object) -> str | None:
    """Require one dependency record to carry source, revision and features."""
    if not isinstance(entry, dict):
        return f"entry {index} must be an object"
    if "name" in entry and not is_nonempty_str(entry.get("name")):
        return f"entry {index} name must be a non-empty string"
    for field_name in ("source", "revision"):
        if not is_nonempty_str(entry.get(field_name)):
            return f"entry {index} {field_name} must be a non-empty string"
    features = entry.get("features")
    if not isinstance(features, list) or not features:
        return f"entry {index} features must be a non-empty list"
    if any(not is_nonempty_str(feature) for feature in features):
        return f"entry {index} features must be non-empty strings"
    return None


def _validate_dependencies(value: object) -> str | None:
    """Require a non-empty list of complete dependency records."""
    if not isinstance(value, list) or not value:
        return "must be a non-empty list of dependency records"
    for index, entry in enumerate(value):
        problem = _validate_dependency_entry(index, entry)
        if problem is not None:
            return problem
    return None


def _validate_artifact_records(field_name: str):
    """Build a validator for a list of ``{path, sha256?}`` records."""

    def validate(value: object) -> str | None:
        if not isinstance(value, list) or not value:
            return f"must be a non-empty list of {field_name} records"
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
    "toolchain": _validate_toolchain,
    "dependencies": _validate_dependencies,
    "runtime_lookup": _validate_artifact_records("runtime lookup"),
    "notices": _validate_artifact_records("notices"),
    "symbols": _validate_artifact_records("symbols"),
}


@dataclass(frozen=True)
class ManifestExpectation:
    """The verified values one provenance manifest must agree with."""

    label: str
    tag: str
    commit: str
    target: str | None = None
    config: str | None = None
    required_inventory: tuple[str, ...] = ()
    require_artifacts: bool = True


def _identity_failures(record: dict, name: str, expected: ManifestExpectation) -> list[str]:
    """Return identity/target/config mismatches for one manifest record."""
    failures: list[str] = []
    if record.get("schema_version") != SCHEMA_VERSION:
        failures.append(
            f"{expected.label}: {name} schema_version "
            f"{record.get('schema_version')!r} is not {SCHEMA_VERSION}"
        )
    for key, wanted in (("tag", expected.tag), ("commit", expected.commit)):
        actual = record.get(key)
        if actual != wanted:
            failures.append(
                f"{expected.label}: {name} {key} {actual!r} does not match "
                f"verified release {wanted!r}"
            )
    if expected.target is not None and record.get("target") != expected.target:
        failures.append(
            f"{expected.label}: {name} target {record.get('target')!r} does not "
            f"match expected {expected.target!r}"
        )
    if expected.config is not None and record.get("config") != expected.config:
        failures.append(
            f"{expected.label}: {name} config {record.get('config')!r} does not "
            f"match expected {expected.config!r}"
        )
    return failures


def _inventory_failures(record: dict, name: str, expected: ManifestExpectation) -> list[str]:
    """Return failures for each required inventory field of a manifest."""
    failures: list[str] = []
    for inventory_field in expected.required_inventory:
        if inventory_field not in INVENTORY_FIELDS:
            failures.append(
                f"{expected.label}: unknown required inventory field {inventory_field!r}"
            )
            continue
        problem = INVENTORY_VALIDATORS[inventory_field](record.get(inventory_field))
        if problem is not None:
            failures.append(
                f"{expected.label}: {name} required inventory {inventory_field!r} "
                f"is missing or invalid: {problem}"
            )
    return failures


def _artifact_entry_failures(
    dist: Path, label: str, artifact_name: str, entry: dict
) -> list[str]:
    """Return integrity failures for one declared artifact entry."""
    artifact_path = dist / artifact_name
    if not artifact_path.is_file():
        return [f"{label}: declared artifact {artifact_name} is missing from dist"]
    digest = entry.get("sha256")
    if not isinstance(digest, str) or not SHA256_RE.match(digest):
        return [f"{label}: artifact {artifact_name} has an invalid sha256 field"]
    failures: list[str] = []
    actual = sha256_file(artifact_path)
    if actual != digest:
        failures.append(
            f"{label}: artifact {artifact_name} sha256 {actual} does not match "
            f"manifest {digest}"
        )
    size = entry.get("size")
    if isinstance(size, int) and size != artifact_path.stat().st_size:
        failures.append(
            f"{label}: artifact {artifact_name} size {artifact_path.stat().st_size} "
            f"does not match manifest {size}"
        )
    return failures


def _artifact_failures(
    record: dict, name: str, label: str, dist: Path, require_artifacts: bool
) -> tuple[list[str], list[str]]:
    """Return (failures, declared basenames) for one manifest's artifact list."""
    artifacts = record.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        if require_artifacts:
            return [f"{label}: {name} declares no artifacts"], []
        return [], []
    failures: list[str] = []
    names: list[str] = []
    for entry in artifacts:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            failures.append(f"{label}: malformed artifact entry {entry!r}")
            continue
        artifact_name = entry["path"]
        names.append(artifact_name)
        failures.extend(_artifact_entry_failures(dist, label, artifact_name, entry))
    return failures, sorted(names)


def verify_manifest_identity(
    manifest_path: Path, expected: ManifestExpectation, dist: Path
) -> tuple[list[str], list[str]]:
    """Return (failures, declared artifact basenames) for one provenance manifest."""
    record = load_json(manifest_path)
    name = manifest_path.name
    failures = _identity_failures(record, name, expected)
    failures.extend(_inventory_failures(record, name, expected))
    artifact_failures, names = _artifact_failures(
        record, name, expected.label, dist, expected.require_artifacts
    )
    failures.extend(artifact_failures)
    return failures, names

"""Source-archive identity checks for the release provenance gate."""

# scripts/extern/resolve_source_identity.cmake reads
# ``<source_dir>/src/source_identity.txt`` when no checkout is available, so a
# source archive must carry that file with the substituted full commit. An
# absent member, an unexpanded ``$Format:...$`` placeholder, a malformed value
# or a value that conflicts with the verified release each fail closed.
#
# The commit line is parsed with the resolver's own grammar: ``file(STRINGS ...
# REGEX "^commit=")`` matches ``commit=`` at column zero, so a line with leading
# whitespace is not a usable identity and must not be accepted here.

from __future__ import annotations

import json
import tarfile
from pathlib import Path

from release_provenance_common import COMMIT_RE, identity_field_failures


def posix_member_basename(name: str) -> str:
    """Return the final ``/``-separated component of a tar member path."""
    return name.rsplit("/", 1)[-1]


def find_archive_members(archive: tarfile.TarFile, member: str) -> list[tarfile.TarInfo]:
    """Match a member by basename (``release-identity.json`` is shipped flat)."""
    # Tar member names are POSIX paths, so the basename must be taken with
    # POSIX semantics; a host-native Path would read backslashes as
    # separators on Windows and diverge from what extraction produces.
    return [
        item
        for item in archive.getmembers()
        if item.isfile() and posix_member_basename(item.name) == member
    ]


def member_path_parts(name: str) -> list[str]:
    """Split a tar member's POSIX path into its non-empty components."""
    # Tar member names are POSIX paths: ``/`` is the only separator. A
    # literal backslash is an ordinary filename character on the POSIX
    # extraction host, so normalizing it to ``/`` would treat a member named
    # ``src\source_identity.txt`` as the build-consumed
    # ``src/source_identity.txt`` even though extraction keeps it a single
    # top-level file the resolver never reads.
    return [part for part in name.split("/") if part]


def _archive_layout(archive: tarfile.TarFile) -> tuple[bool, set[str]]:
    """Return whether the archive has top-level files and its top-level dirs."""
    has_top_level_file = False
    top_dirs: set[str] = set()
    for item in archive.getmembers():
        if not item.isfile():
            continue
        parts = member_path_parts(item.name)
        if len(parts) == 1:
            has_top_level_file = True
        elif parts:
            top_dirs.add(parts[0])
    return has_top_level_file, top_dirs


def _carrier_locations(archive: tarfile.TarFile, carrier: str) -> list[list[str]]:
    """Return the archive paths the resolver would read the carrier from."""
    wanted = member_path_parts(carrier)
    if not wanted:
        return []
    has_top_level_file, top_dirs = _archive_layout(archive)
    locations = [wanted]
    if not has_top_level_file and len(top_dirs) == 1:
        locations.append([next(iter(top_dirs)), *wanted])
    return locations


def _carrier_alias_members(archive: tarfile.TarFile, carrier: str) -> list[tarfile.TarInfo]:
    """
    Return members that reach a carrier location only via backslashes.

    Reading a member's backslashes as separators can make a noncanonical
    name look like the build-consumed carrier. POSIX extraction keeps the
    backslash inside the filename, so such a member never lands on the path
    ``resolve_source_identity.cmake`` reads and its value is identity
    evidence the build can never recover.
    """
    locations = _carrier_locations(archive, carrier)
    if not locations:
        return []
    aliases: list[tarfile.TarInfo] = []
    for item in archive.getmembers():
        if not item.isfile() or "\\" not in item.name:
            continue
        if member_path_parts(item.name) in locations:
            continue
        normalized = [part for part in item.name.replace("\\", "/").split("/") if part]
        if normalized in locations:
            aliases.append(item)
    return aliases


def find_carrier_members(archive: tarfile.TarFile, carrier: str) -> list[tarfile.TarInfo]:
    """Match the build-consumed carrier at the path the resolver actually reads."""
    # ``resolve_source_identity.cmake`` reads ``<source_dir>/<carrier>`` from the
    # extracted tree. A ``git archive`` either stores repository-relative paths
    # (no prefix) or places every entry under one top-level directory
    # (``git archive --prefix=<dir>/``), so the only consumable locations are
    # ``<carrier>`` and ``<top-level-dir>/<carrier>``. Matching by trailing path
    # components alone would also accept an arbitrarily nested copy such as
    # ``a/b/src/source_identity.txt``, which extraction never puts where the
    # resolver looks.
    locations = _carrier_locations(archive, carrier)
    if not locations:
        return []
    return [
        item
        for item in archive.getmembers()
        if item.isfile() and member_path_parts(item.name) in locations
    ]


def verify_archive_identity_member(
    archive: tarfile.TarFile, identity_member: str, tag: str, commit: str
) -> list[str]:
    """Verify the shipped ``release-identity.json`` pins tag and commit."""
    members = find_archive_members(archive, identity_member)
    if not members:
        message = (
            f"source: archive does not contain {identity_member}; a rebuild "
            "would lose version identity without .git"
        )
        return [message]
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
        # Reuse the CLI identity-verify field contract (schema_version, tag,
        # commit) instead of re-checking only tag/commit: an identity that the
        # CLI rejects must not pass verification just because it travelled
        # inside the source archive.
        failures.extend(identity_field_failures(identity, f"source: {member.name}", tag, commit))
    return failures


def _carrier_value_failures(carrier_member: str, value: str, commit: str) -> list[str]:
    """Return failures for one ``commit=`` value in the shipped carrier."""
    if "$Format:" in value:
        message = (
            f"source: {carrier_member} still holds the unsubstituted "
            f"export-subst placeholder {value!r}; git archive did not expand it"
        )
        return [message]
    if not COMMIT_RE.match(value):
        return [f"source: {carrier_member} commit {value!r} is not a full 40-hex commit"]
    if value != commit:
        message = (
            f"source: {carrier_member} commit {value!r} does not match "
            f"verified release {commit!r}"
        )
        return [message]
    return []


def _carrier_commit_failures(
    archive: tarfile.TarFile, carrier_member: str, member: tarfile.TarInfo, commit: str
) -> list[str]:
    """Return failures for one shipped carrier member's ``commit=`` lines."""
    extracted = archive.extractfile(member)
    if extracted is None:
        return [f"source: could not read {carrier_member} from archive"]
    try:
        text = extracted.read().decode("utf-8")
    except UnicodeDecodeError as exc:
        return [f"source: {carrier_member} is not valid UTF-8 ({exc})"]
    values = [
        line[len("commit=") :].strip()
        for line in text.splitlines()
        if line.startswith("commit=")
    ]
    if not values:
        return [f"source: {carrier_member} carries no commit= line"]
    failures: list[str] = []
    for value in values:
        failures.extend(_carrier_value_failures(carrier_member, value, commit))
    return failures


def verify_archive_carrier(
    archive: tarfile.TarFile, carrier_member: str, commit: str
) -> list[str]:
    """Verify the identity carrier the build actually consumes without ``.git``."""
    members = find_carrier_members(archive, carrier_member)
    failures: list[str] = []
    # Reject noncanonical aliases explicitly: a member only reaches a carrier
    # location when its backslashes are read as separators, but the POSIX
    # extraction host keeps the literal name, so accepting its value would
    # certify an identity the build cannot recover from the extracted tree.
    for alias in _carrier_alias_members(archive, carrier_member):
        failures.append(
            "source: member "
            f"{alias.name!r} names the identity carrier with a literal backslash; "
            "tar member paths are POSIX paths, so extraction never produces the "
            f"build-consumed path {carrier_member} the resolver reads"
        )
    if not members:
        if failures:
            return failures
        message = (
            "source: archive does not contain the build-consumed identity carrier "
            f"{carrier_member}; a rebuild without .git could not recover the "
            "verified revision"
        )
        return [message]
    for member in members:
        failures.extend(_carrier_commit_failures(archive, carrier_member, member, commit))
    return failures


def verify_source_archive(
    archive_path: Path,
    identity_member: str,
    carrier_member: str,
    tag: str,
    commit: str,
) -> list[str]:
    """Prove a source archive carries the verified release identity."""
    # The JSON identity member pins tag and commit; the build-consumed carrier
    # pins the exact full commit a rebuild would resolve without ``.git``. Both
    # are required so the verifier cannot pass an archive whose identity
    # evidence is disconnected from what the build reads.
    if not archive_path.is_file():
        return [f"source: archive {archive_path.name} is missing from dist"]
    try:
        with tarfile.open(archive_path, "r:*") as archive:
            failures = verify_archive_identity_member(archive, identity_member, tag, commit)
            failures.extend(verify_archive_carrier(archive, carrier_member, commit))
    except tarfile.TarError as exc:
        return [f"source: archive {archive_path.name} could not be read ({exc})"]
    return failures

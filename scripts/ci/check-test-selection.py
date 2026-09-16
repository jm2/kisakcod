#!/usr/bin/env python3
"""Fail-closed checker for per-profile CTest selection manifests.

The hosted jobs historically selected their tests with a single inline ``ctest
-R`` regular expression.  A name that stopped matching, a target that stopped
being built, or a test silently dropped from the expression produced no error
at all: ``ctest`` exits 0 when a filter matches nothing, and a name that no
longer exists is simply not reported.  This checker turns that silent selection
drift into a hard failure.

Inputs
------

``--inventory``
    Canonical list of portable test names the profile is expected to be able
    to discover (``scripts/ci/test-selection/portable-inventory.txt``).
    One test name per line; blank lines and ``#`` comments are ignored.

``--selected``
    Test names the profile intentionally runs.  One name per line.

``--excluded``
    Tests the profile intentionally does not run, as ``name<TAB>reason``
    lines.  Every excluded test must carry a non-empty reason: an omission
    without a justification is itself a failure.

``--discovered``
    Optional listing (a plain name list or raw ``ctest -N`` output) of the
    tests ctest actually found.  With ``--discovered-scope subset`` every
    discovered test must be classified; with ``exact`` discovery must equal
    the inventory.

``--executed``
    Optional listing (``ctest -N`` output or a real run log) of the tests the
    job actually executed.  Requires ``--discovered``.  The job passes only
    when ``executed == selected and discovered``: nothing outside the selected
    set ran, and every selected test the platform discovered executed.  A
    platform-conditional test that the platform does not register is therefore
    absent from both sides and does not mask a real omission.

``--emit-regex``
    Optional path to write the ``ctest -R`` expression built from the selected
    set, so the workflow filter and the manifest cannot drift apart.

Exit status is 0 only when every check passes; otherwise each violation is
printed as ``FAIL: ...`` and the process exits 1.
"""

from __future__ import annotations

import argparse
import re
import sys
from typing import Iterable, List, Sequence, Set, Tuple

# Matches both a plain ``ctest -N`` listing ("  Test  #4: name") and the
# per-test progress lines of a real ctest run ("4/233 Test #4: name ... Passed").
CTEST_LISTING = re.compile(r"\bTest\s+#\d+:\s*(\S+)")
# A bare test name; used to accept a plain name-list discovery file.
TEST_NAME = re.compile(r"[A-Za-z0-9_.\-]+")


def read_name_file(path: str) -> List[str]:
    """Read one test name per line, ignoring blanks and comments."""
    names: List[str] = []
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line and not line.startswith("#"):
                names.append(line)
    return names


def read_exclusions(path: str) -> List[Tuple[str, str]]:
    """Read ``name<TAB>reason`` exclusion entries."""
    entries: List[Tuple[str, str]] = []
    with open(path, encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, start=1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if "\t" not in line:
                raise ValueError(
                    "exclusion line %d has no reason column (expected "
                    "'name<TAB>reason'): %r" % (lineno, line)
                )
            name, reason = line.split("\t", 1)
            entries.append((name.strip(), reason.strip()))
    return entries


def read_discovery(path: str) -> List[str]:
    """Read a plain name list or a raw ``ctest`` listing/report."""
    names: List[str] = []
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            match = CTEST_LISTING.search(raw)
            if match:
                names.append(match.group(1))
                continue
            # A report also carries progress/header lines ("Start 1: name",
            # "Test project ...", "100% tests passed ..."). Keep only the
            # entries that are unambiguously a bare test name so those lines
            # are ignored rather than mistaken for tests.
            line = raw.strip()
            if line and not line.startswith("#") and TEST_NAME.fullmatch(line):
                names.append(line)
    return names


def duplicates(names: Sequence[str]) -> Set[str]:
    """Return the duplicated names, once each."""
    seen: Set[str] = set()
    dupes: Set[str] = set()
    for name in names:
        if name in seen:
            dupes.add(name)
        seen.add(name)
    return dupes


def report_violation(names: Iterable[str], header: str) -> int:
    """Print a sorted violation block and return 1 if anything was found."""
    items = sorted(names)
    if not items:
        return 0
    print("FAIL: %s (%d):" % (header, len(items)), file=sys.stderr)
    for name in items:
        print("      %s" % name, file=sys.stderr)
    return 1


def emit_regex(names: Sequence[str]) -> str:
    """Build a fully anchored ctest -R expression from explicit names."""
    # Test names in this tree are restricted to [A-Za-z0-9_.-], but escape
    # defensively so a future name cannot silently corrupt the expression.
    return "|".join("^%s$" % re.escape(name) for name in names)


def check_uniqueness(inventory: Sequence[str], selected: Sequence[str],
                     excluded: Sequence[str]) -> int:
    failures = report_violation(
        duplicates(inventory), "duplicate inventory entries")
    failures += report_violation(
        duplicates(selected), "duplicate selected entries")
    failures += report_violation(
        duplicates(excluded), "duplicate excluded entries")
    return failures


def check_partition(inventory: Set[str], selected: Set[str],
                    excluded: Set[str]) -> int:
    """Require selected and excluded to partition the inventory."""
    failures = report_violation(
        selected & excluded, "tests both selected and excluded")
    failures += report_violation(
        selected - inventory, "selected tests absent from the inventory")
    failures += report_violation(
        excluded - inventory, "excluded tests absent from the inventory")
    failures += report_violation(
        inventory - selected - excluded,
        "inventory tests neither selected nor justified-excluded "
        "(they would silently stop running)",
    )
    return failures


def check_discovery(scope: str, inventory: Set[str], selected: Set[str],
                    discovered: Set[str],
                    executed: Set[str]) -> int:
    """Require discovery, and any executed run, to agree with the manifest.

    The manifest describes one reference inventory that spans platforms, so a
    platform-conditional test may legitimately be absent from a platform's
    discovery or execution.  The invariant is therefore ``executed == selected
    and discovered``: nothing outside the selected set may run, and every
    selected test the platform actually discovered must have executed.
    """
    failures = report_violation(
        discovered - inventory,
        "discovered tests missing from the inventory (classify them as "
        "selected or excluded)",
    )
    if scope == "exact":
        failures += report_violation(
            inventory - discovered,
            "inventory tests not discovered by ctest (remove them from the "
            "inventory or restore the test)",
        )
    if executed is None:
        return failures
    expected = selected & discovered
    failures += report_violation(
        executed - expected,
        "executed tests outside the selected platform set",
    )
    failures += report_violation(
        expected - executed,
        "selected platform tests that did not execute (dead selection: they "
        "silently did not run)",
    )
    if not executed:
        print(
            "FAIL: the run executed no tests; a run that executes nothing "
            "proves nothing.",
            file=sys.stderr,
        )
        failures += 1
    return failures


def check_run(args: argparse.Namespace) -> int:
    """Run every check and return the number of violations found."""
    inventory_list = read_name_file(args.inventory)
    selected_list = read_name_file(args.selected)
    exclusion_entries = read_exclusions(args.excluded)
    excluded_list = [name for name, _ in exclusion_entries]

    failures = check_uniqueness(inventory_list, selected_list, excluded_list)
    failures += report_violation(
        {name for name, reason in exclusion_entries if not reason},
        "excluded tests without a reason",
    )

    inventory = set(inventory_list)
    selected = set(selected_list)
    excluded = set(excluded_list)
    failures += check_partition(inventory, selected, excluded)

    if not selected_list:
        print(
            "FAIL: the selection is empty; a run that selects nothing "
            "proves nothing.",
            file=sys.stderr,
        )
        failures += 1

    if args.discovered:
        discovered = set(read_discovery(args.discovered))
        executed = set(read_discovery(args.executed)) if args.executed else None
        if args.executed and not args.discovered:
            raise ValueError("--executed requires --discovered")
        failures += check_discovery(
            args.discovered_scope, inventory, selected, discovered, executed)
    elif args.executed:
        raise ValueError("--executed requires --discovered")

    if args.emit_regex:
        with open(args.emit_regex, "w", encoding="utf-8") as handle:
            handle.write(emit_regex(selected_list) + "\n")

    return failures


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--inventory", required=True)
    parser.add_argument("--selected", required=True)
    parser.add_argument("--excluded", required=True)
    parser.add_argument("--discovered", default=None)
    parser.add_argument("--executed", default=None)
    parser.add_argument(
        "--discovered-scope",
        choices=("subset", "exact"),
        default="subset",
        help="'subset': discovered must be a subset of the inventory and "
        "must contain every selected test. 'exact': discovered must equal "
        "the inventory.",
    )
    parser.add_argument("--emit-regex", default=None)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        failures = check_run(args)
    except (OSError, ValueError) as exc:
        print("FAIL: cannot read manifest: %s" % exc, file=sys.stderr)
        return 1
    if failures:
        print(
            "FAIL: [%s] test-selection manifest is inconsistent."
            % args.label,
            file=sys.stderr,
        )
        return 1
    print("OK: [%s] selection manifest consistent." % args.label)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

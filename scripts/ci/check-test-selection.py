#!/usr/bin/env python3
"""Fail-closed checker for per-profile CTest selection manifests."""
#
# The hosted jobs historically selected their tests with a single inline ``ctest
# -R`` regular expression.  A name that stopped matching, a target that stopped
# being built, or a test silently dropped from the expression produced no error
# at all: ``ctest`` exits 0 when a filter matches nothing, and a name that no
# longer exists is simply not reported.  This checker turns that silent selection
# drift into a hard failure.
#
# Classification
# --------------
#
# The canonical inventory is the cross-platform set of portable tests.  Each
# profile partitions it into three disjoint, explicitly written groups:
#
# ``--selected``
#     Tests the profile runs.  Every selected test must be *discovered* and
#     *executed* on the profile: this is the anti-disappearance invariant.  A
#     selected test that the platform quietly stops building fails the check.
#
# ``--excluded``
#     Tests the profile discovers but intentionally does not run, as
#     ``name<TAB>reason``.  Every entry needs a reason, and a not-enrolled test
#     must actually be discovered (otherwise it is misclassified).
#
# ``--absent``
#     Tests the profile's platform never registers at all, as
#     ``name<TAB>reason``.  Every entry needs a reason, and an absent test must
#     *not* be discovered.  Platform absence is therefore an explicit, audited
#     classification, never an accidental side effect of intersecting sets.
#
# Invariants
# ----------
#
# ``--discovered``
#     Optional listing (a plain name list or raw ``ctest`` output) of the tests
#     ctest actually found.  Discovery may never leave the inventory.  With
#     ``--discovered-scope exact`` the inventory must equal discovery, so a test
#     removed from the build fails just like an unclassified addition.
#
# ``--executed``
#     Optional raw ``ctest`` run report of the tests a real run executed.
#     Only per-test result lines with an affirmative execution status count
#     as evidence: a ``***Not Run`` (disabled, failed dependency, ...) or
#     ``***Skipped`` entry proves the test body did not run, and a result
#     line with an unrecognized status fails the check instead of being
#     trusted.  The executed set must equal the selected set: nothing
#     outside the selection may run, and a selection that runs nothing
#     fails.
#
# ``--enforce-platform-absence``
#     Turns on the full profile invariants (selected must be discovered, absent
#     must be undiscovered, excluded must be discovered, and discovery must equal
#     selected plus excluded).  The POSIX reference leg intentionally omits this
#     flag because it validates the cross-platform inventory, not one platform's
#     registrations.
#
# Exit status is 0 only when every check passes; otherwise each violation is
# printed as ``FAIL: ...`` and the process exits 1.  ``--emit-regex`` writes the
# ``ctest -R`` expression built from the selected set so a workflow filter cannot
# drift from the manifest.

from __future__ import annotations

import argparse
import re
import sys
from typing import Iterable, List, Optional, Sequence, Set, Tuple

# Matches the discovery listing of ``ctest -N`` ("  Test  #4: name").
# Discovery-only: a listing line says a test exists, never that it ran.
CTEST_LISTING = re.compile(r"\bTest\s+#\d+:\s*(\S+)")
# Matches a per-test result line of a real ctest run ("1/233 Test #1: name
# ...   Passed    0.05 sec").  Only these lines may count as execution
# evidence, and the status after the name is classified explicitly.
CTEST_RESULT = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s*(?P<name>\S+)\s+(?P<trail>.*)$"
)
# Statuses that prove the test binary actually ran.  A failed or crashed
# test still executed (and the ctest exit status already reports the
# failure); what must never pass as execution is a test that did not run.
EXECUTED_STATUSES = ("passed", "failed", "timeout", "exception")
NOT_RUN_MARKER = "***not run"
SKIPPED_MARKER = "***skipped"
# A bare test name; used to accept a plain name-list discovery file.
TEST_NAME = re.compile(r"[A-Za-z0-9_.\-]+")

Reasoned = List[Tuple[str, str]]


def read_name_file(path: str) -> List[str]:
    """Read one test name per line, ignoring blanks and comments."""
    names: List[str] = []
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line and not line.startswith("#"):
                names.append(line)
    return names


def read_reasoned(path: str, kind: str) -> Reasoned:
    """Read ``name<TAB>reason`` entries, rejecting a missing reason column."""
    entries: Reasoned = []
    with open(path, encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, start=1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if "\t" not in line:
                raise ValueError(
                    "%s line %d has no reason column (expected "
                    "'name<TAB>reason'): %r" % (kind, lineno, line)
                )
            name, reason = line.split("\t", 1)
            entries.append((name.strip(), reason.strip()))
    return entries


def read_discovery(path: str) -> List[str]:
    """Read a plain name list or a raw ``ctest -N`` listing.

    Discovery-only: this parser answers "which tests exist", never "which
    tests ran".  Use :func:`read_execution` for execution evidence.
    """
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


def read_execution(path: str) -> List[str]:
    """Read a raw ``ctest`` run report; return the tests that actually ran.

    A per-test result line counts as execution evidence only when its
    status is affirmative (``Passed``, or a ran-and-failed ``Failed`` /
    ``Timeout`` / ``Exception`` outcome).  ``***Not Run ...`` (disabled,
    failed dependency, ...) and ``***Skipped`` entries are not execution:
    their bodies never ran even though ``ctest`` may exit 0.  A result
    line with no recognized status raises, so a truncated or unknown-
    format log cannot pass as evidence.
    """
    names: List[str] = []
    with open(path, encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, start=1):
            line = raw.rstrip("\n")
            match = CTEST_RESULT.match(line)
            if not match:
                continue
            name = match.group("name")
            trail = match.group("trail").lower()
            if NOT_RUN_MARKER in trail or SKIPPED_MARKER in trail:
                continue
            if any(status in trail for status in EXECUTED_STATUSES):
                names.append(name)
                continue
            raise ValueError(
                "%s line %d carries a ctest result for '%s' with an "
                "unrecognized execution status; execution evidence must "
                "name its status: %r" % (path, lineno, name, line)
            )
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


def check_manifests(inventory: Set[str], selected: Set[str],
                    excluded: Set[str], absent: Set[str]) -> int:
    """Check the selected/excluded/absent partition of the inventory."""
    failures = report_violation(
        selected & excluded, "tests both selected and excluded")
    failures += report_violation(
        selected & absent, "tests both selected and platform-absent")
    failures += report_violation(
        excluded & absent, "tests both excluded and platform-absent")
    failures += report_violation(
        selected - inventory, "selected tests absent from the inventory")
    failures += report_violation(
        excluded - inventory, "excluded tests absent from the inventory")
    failures += report_violation(
        absent - inventory, "platform-absent tests absent from the inventory")
    failures += report_violation(
        inventory - selected - excluded - absent,
        "inventory tests neither selected, excluded, nor platform-absent "
        "(they would silently stop running)",
    )
    return failures


def check_platform_profile(selected: Set[str], excluded: Set[str],
                           absent: Set[str], discovered: Set[str]) -> int:
    """Check the platform-profile discovery invariants.

    Every selected test must be discovered (a dead selection silently did
    not run), every platform-absent test must be undiscovered, every
    excluded test must still be discovered, and nothing outside the
    selected/justified-excluded sets may be discovered.
    """
    failures = report_violation(
        selected - discovered,
        "selected tests not discovered by the platform (dead selection: "
        "they silently did not run; classify them platform-absent if the "
        "backend does not register them)",
    )
    failures += report_violation(
        absent & discovered,
        "platform-absent tests that were discovered after all (they are "
        "misclassified)",
    )
    failures += report_violation(
        excluded - discovered,
        "excluded (not-enrolled) tests that were not discovered (classify "
        "them platform-absent instead)",
    )
    failures += report_violation(
        discovered - selected - excluded,
        "discovered tests neither selected nor justified-excluded",
    )
    return failures


def check_execution(selected: Set[str], discovered: Set[str],
                    executed: Set[str], enforce_absence: bool) -> int:
    """Check the executed evidence against the selection.

    Nothing outside the selection may run and a selection that runs
    nothing proves nothing.  With --enforce-platform-absence every
    selected test is already known to be discovered, so the expected
    executed set is the whole selection; otherwise only selected tests
    that were also discovered are expected to have executed.
    """
    expected = selected if enforce_absence else (selected & discovered)
    failures = report_violation(
        executed - expected, "executed tests outside the selected set")
    failures += report_violation(
        expected - executed,
        "selected tests that did not execute (dead selection: a disabled, "
        "skipped, or not-run entry is not execution evidence)",
    )
    if not executed:
        print(
            "FAIL: the run executed no tests; a run that executes nothing "
            "proves nothing.",
            file=sys.stderr,
        )
        failures += 1
    return failures


def check_discovery(scope: str, inventory: Set[str], selected: Set[str],
                    excluded: Set[str], absent: Set[str],
                    discovered: Set[str], executed: Optional[Set[str]],
                    enforce_absence: bool) -> int:
    """Check discovery/execution against the classification."""
    failures = report_violation(
        discovered - inventory,
        "discovered tests missing from the inventory (classify them as "
        "selected, excluded, or platform-absent)",
    )
    if scope == "exact":
        failures += report_violation(
            inventory - discovered,
            "inventory tests not discovered by ctest (remove them from the "
            "inventory or restore the test)",
        )
    if enforce_absence:
        failures += check_platform_profile(selected, excluded, absent,
                                           discovered)
    if executed is None:
        return failures
    failures += check_execution(selected, discovered, executed,
                                enforce_absence)
    return failures


def read_manifests(args: argparse.Namespace) -> Tuple[
        List[str], List[str], Reasoned, Reasoned]:
    """Read every manifest named on the command line."""
    inventory_list = read_name_file(args.inventory)
    selected_list = read_name_file(args.selected)
    exclusion_entries = read_reasoned(args.excluded, "exclusion")
    absence_entries = (
        read_reasoned(args.absent, "platform-absence") if args.absent else []
    )
    return inventory_list, selected_list, exclusion_entries, absence_entries


def check_entry_quality(inventory_list: List[str], selected_list: List[str],
                        exclusion_entries: Reasoned,
                        absence_entries: Reasoned) -> int:
    """Fail closed on duplicate entries and missing reason columns."""
    failures = report_violation(
        duplicates(inventory_list), "duplicate inventory entries")
    failures += report_violation(
        duplicates(selected_list), "duplicate selected entries")
    failures += report_violation(
        duplicates([name for name, _ in exclusion_entries]),
        "duplicate excluded entries")
    failures += report_violation(
        duplicates([name for name, _ in absence_entries]),
        "duplicate platform-absent entries")
    failures += report_violation(
        {name for name, reason in exclusion_entries if not reason},
        "excluded tests without a reason",
    )
    failures += report_violation(
        {name for name, reason in absence_entries if not reason},
        "platform-absent tests without a reason",
    )
    return failures


def check_run_evidence(args: argparse.Namespace, inventory: Set[str],
                       selected: Set[str], excluded: Set[str],
                       absent: Set[str]) -> int:
    """Validate flag combinations and check discovery/execution evidence.

    Raises when the flags are mutually inconsistent (--executed without
    --discovered, platform enforcement without an --absent manifest);
    returns 0 when no discovery evidence was requested, otherwise the
    number of discovery/execution violations.
    """
    if args.executed and not args.discovered:
        raise ValueError("--executed requires --discovered")
    if args.enforce_platform_absence and not args.absent:
        raise ValueError("--enforce-platform-absence requires --absent")
    if not args.discovered:
        return 0
    discovered = set(read_discovery(args.discovered))
    executed = set(read_execution(args.executed)) if args.executed else None
    return check_discovery(
        args.discovered_scope, inventory, selected, excluded, absent,
        discovered, executed, args.enforce_platform_absence)


def check_run(args: argparse.Namespace) -> int:
    """Run every check and return the number of violations found."""
    inventory_list, selected_list, exclusion_entries, absence_entries = (
        read_manifests(args))
    excluded_list = [name for name, _ in exclusion_entries]
    absent_list = [name for name, _ in absence_entries]

    failures = check_entry_quality(
        inventory_list, selected_list, exclusion_entries, absence_entries)

    inventory = set(inventory_list)
    selected = set(selected_list)
    excluded = set(excluded_list)
    absent = set(absent_list)
    failures += check_manifests(inventory, selected, excluded, absent)

    if not selected_list:
        print(
            "FAIL: the selection is empty; a run that selects nothing "
            "proves nothing.",
            file=sys.stderr,
        )
        failures += 1

    failures += check_run_evidence(args, inventory, selected, excluded,
                                   absent)

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
    parser.add_argument("--absent", default=None)
    parser.add_argument("--discovered", default=None)
    parser.add_argument("--executed", default=None)
    parser.add_argument(
        "--discovered-scope",
        choices=("subset", "exact"),
        default="subset",
        help="'subset': discovery must stay inside the inventory. 'exact': "
        "inventory must equal discovery, so removals fail too.",
    )
    parser.add_argument(
        "--enforce-platform-absence",
        action="store_true",
        help="require every selected test to be discovered/executed and every "
        "absent test to be undiscovered (platform profile validation).",
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

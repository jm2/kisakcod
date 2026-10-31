#!/usr/bin/env python3
"""Check that a CI job built all of its dxvk-gated tests (#914).

Usage: check-dxvk-tests.py <build-dir> <expected>. It fails if fewer ctest
tests run a target that links dxvk-native's d3d9 module than the job expects.

The targets come from <build-dir>/tests/dxvk-test-targets.txt, which
tests/CMakeLists.txt writes at configure time; the tests from ctest's own
listing. Those tests only exist where pkg-config finds dxvk-d3d9, so a runner
that loses the module, or a gate that stops matching, drops the count.
"""

import json
import os
import shutil
import subprocess  # nosec B404 - runs only the resolved ctest binary with fixed arguments
import sys


def main() -> int:
    """Count the job's dxvk-gated tests and compare them with its expected number."""
    build_dir, expected = sys.argv[1], int(sys.argv[2])
    with open(os.path.join(build_dir, "tests", "dxvk-test-targets.txt"), encoding="utf-8") as listing:
        targets = {line.strip() for line in listing if line.strip()}
    ctest = shutil.which("ctest")
    if ctest is None:
        sys.exit("check-dxvk-tests.py: ctest is not on PATH")
    # Argument list, no shell, executable resolved by shutil.which; the only
    # argument from outside is the job's build directory.
    command = [ctest, "--test-dir", build_dir, "--show-only=json-v1"]
    shown = subprocess.run(command, check=True, capture_output=True, text=True).stdout  # nosec B603  # nosemgrep
    names = []
    for test in json.loads(shown)["tests"]:
        test_command = test.get("command") or []
        if test_command and os.path.splitext(os.path.basename(test_command[0]))[0] in targets:
            names.append(test["name"])
    for name in sorted(names):
        print(f"dxvk-gated test: {name}")
    print(f"{len(names)} dxvk-gated tests, {len(targets)} targets; this job expects {expected}")
    if len(names) < expected:
        print(f"::error::only {len(names)} of the {expected} dxvk-gated tests are registered: "
              "is pkg-config still finding dxvk-d3d9 (scripts/ci/install-dxvk-headers.sh)?")
        return 1
    if len(names) > expected:
        print(f"::notice::{len(names)} dxvk-gated tests, more than the {expected} expected: "
              "raise this job's count in ci.yml")
    return 0


if __name__ == "__main__":
    sys.exit(main())

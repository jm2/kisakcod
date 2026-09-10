#!/usr/bin/env bash
#
# run-local-gate.sh — local mirror of the hosted engine gates.
#
# The polecat/CI contract: a gate that fails locally must not be pushed.
# This script runs the same configure/build/test sequence the hosted
# workflow runs for the named gate, on the current host, so the
# POSIX-hosted legs of the portable suite are reproducible before push.
#
# Gates:
#   engine-linux-macOS
#       The POSIX-hosted portable gate. On a Linux host this drives the
#       linux-amd64-mp preset leg; on a macOS host it drives the
#       macos-arm64-mp preset leg. Both legs configure the utility-only
#       profile (KISAK_BUILD_MP/DEDICATED/SP OFF, BUILD_TESTING ON) that
#       selects the active platform service source sets and runs the
#       portable ctest suite, exactly like the hosted portable-tests job.
#
#       The gate also proves the Linux/macOS engine configuration gate is
#       still armed: an engine-target (KISAK_BUILD_*=ON) configure on this
#       platform must fail closed, because the Linux/macOS production
#       engine source sets stay intentionally empty until real POSIX
#       backends populate them.
#
# Usage:
#   scripts/ci/run-local-gate.sh <gate-id> [phase]
#
#   gate-id   engine-linux-macOS (the only gate currently defined)
#   phase     gate (default) — configure, armament check, build, ctest
#             configure        — configure only
#             build            — incremental build only (configures first
#                                if the build tree is absent)
#             test             — ctest only
#
# Exits 0 only when the requested phase (and everything before it) passes.
# Any other exit status means the gate is red; the failing stage is printed.
#
# The ctest phase parses the failed-test summary strictly: every summary
# line must carry an explicitly known status, crash/timeout/Not Run
# results are never tolerated, and nothing is tolerated at all while the
# tracked-defect set is empty (see run_test below). An unrecognized
# result — a new ctest status, a format change — fails the gate instead
# of passing silently. A green exit with zero executed tests also fails
# the gate: ctest exits 0 on an empty test directory ("No tests were
# found!!!"), and a gate that proves nothing must not report PASS.

set -euo pipefail

GATE_ID="${1:-}"
PHASE="${2:-gate}"

if [[ -z "$GATE_ID" ]]; then
    echo "FAIL: usage: $0 <gate-id> [phase] (gates: engine-linux-macOS)" >&2
    exit 2
fi

if [[ "$GATE_ID" != "engine-linux-macOS" ]]; then
    echo "FAIL: unknown gate '$GATE_ID' (gates: engine-linux-macOS)" >&2
    exit 2
fi

case "$(uname -s)" in
    Linux) LEG_PRESET="linux-amd64-mp" ;;
    Darwin) LEG_PRESET="macos-arm64-mp" ;;
    *)
        echo "FAIL: gate $GATE_ID runs on Linux or macOS hosts; this is $(uname -s)" >&2
        exit 2
        ;;
esac

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$SOURCE_DIR"

BUILD_DIR="${SOURCE_DIR}/build-${LEG_PRESET}"
ARMAMENT_DIR="${BUILD_DIR}-gate-armament"

log() { printf '== run-local-gate [%s/%s] %s\n' "$GATE_ID" "$LEG_PRESET" "$*"; }

# The Linux/macOS production engine source sets are intentionally empty;
# the top-level CMakeLists gate must FATAL_ERROR any engine-target
# configure on this platform. Fail closed if the gate stops firing.
check_engine_gate_armed() {
    log "engine gate armament check (KISAK_BUILD_DEDICATED=ON must fail)"
    rm -rf "$ARMAMENT_DIR"
    local arm_log
    arm_log="$(mktemp)"
    if cmake --preset "$LEG_PRESET" -B "$ARMAMENT_DIR" \
        -DKISAK_BUILD_DEDICATED=ON >"$arm_log" 2>&1; then
        echo "FAIL: the Linux/macOS engine configuration gate is disarmed:" >&2
        echo "      an engine-target configure succeeded; the production" >&2
        echo "      engine source sets must stay gated until POSIX backends" >&2
        echo "      populate them. Refusing to pass the gate." >&2
        rm -f "$arm_log"
        rm -rf "$ARMAMENT_DIR"
        return 1
    fi
    if ! grep -q "backend is not buildable yet" "$arm_log"; then
        echo "FAIL: engine-target configure failed, but not through the" >&2
        echo "      platform gate message; refusing to pass the gate." >&2
        sed -n '1,10p' "$arm_log" >&2
        rm -f "$arm_log"
        rm -rf "$ARMAMENT_DIR"
        return 1
    fi
    rm -f "$arm_log"
    rm -rf "$ARMAMENT_DIR"
    log "engine gate is armed (engine-target configure failed closed)"
}

run_configure() {
    log "configure preset $LEG_PRESET"
    cmake --preset "$LEG_PRESET"
}

run_build() {
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        run_configure
    fi
    log "build $LEG_PRESET"
    cmake --build "$BUILD_DIR" --config Release --parallel
}

run_test() {
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        echo "FAIL: test phase requested but $BUILD_DIR is not configured" >&2
        exit 2
    fi
    log "ctest $LEG_PRESET"
    local ctest_log
    ctest_log="$(mktemp)"
    local ctest_status=0
    ctest --test-dir "$BUILD_DIR" -C Release --output-on-failure \
        | tee "$ctest_log" || ctest_status=${PIPESTATUS[0]}

    # ctest exits 0 both when tests passed and when NOTHING was found, so
    # the exit status alone cannot prove the phase ran. Reject the empty
    # run outright, then parse the summary and require a nonzero executed
    # test count on the green path below.
    if grep -q "No tests were found!!!" "$ctest_log"; then
        echo "FAIL: ctest found no tests to execute; a gate run that" >&2
        echo "      executes nothing proves nothing, so it cannot pass." >&2
        rm -f "$ctest_log"
        return 1
    fi

    # Classify every result before deciding anything, and fail closed on
    # anything this script cannot classify. The summary block is parsed strictly: each
    # line must match "<index> - <name> (<status>)" with an explicitly
    # known status. Any other line — a new ctest status vocabulary, a
    # format change, a differently spelled result — is unrecognized and
    # fails the gate; nothing is silently dropped. A crash (SEGFAULT and
    # the other signal statuses), a Timeout, and a Not Run result are
    # never tolerable regardless of the test name; only a plain (Failed)
    # result is even eligible for tolerance.
    #
    # The tolerated set is EMPTY: the historical environment-sensitive
    # ki-9b13/ki-ya3t baseline (abi-sizeof-debt-tripwire,
    # abi-sizeof-scanner-fixture, security-source-regressions) was healed
    # on master and both tracking beads are closed, so today every
    # failure is unexpected. Re-add an entry only for an open, tracked
    # defect, citing its bead id in the comment next to it.
    local -a tolerated_failures=()
    local -a actual_failures=()
    local -a actual_statuses=()
    local -a unrecognized=()
    local claimed_failed=-1
    local total_tests=-1
    local in_block=0
    local line name status
    local summary_grammar='^[[:space:]]*[0-9]+[[:space:]]-[[:space:]](.+)[[:space:]]\((Failed|Timeout|Not Run|SEGFAULT|SIGSEGV|SIGILL|SIGABRT|SIGFPE|SIGBUS|Illegal|Interrupt|Other)\)$'
    local count_grammar='tests passed, ([0-9]+) tests failed out of ([0-9]+)'
    while IFS= read -r line; do
        if [[ "$line" == *"The following tests FAILED:"* ]]; then
            in_block=1
            continue
        fi
        if [[ "$line" =~ $count_grammar ]]; then
            claimed_failed="${BASH_REMATCH[1]}"
            total_tests="${BASH_REMATCH[2]}"
        fi
        if [[ "$in_block" -ne 1 ]]; then
            continue
        fi
        if [[ "$line" =~ ^[[:space:]]*$ ]]; then
            in_block=0
            continue
        fi
        if [[ "$line" =~ $summary_grammar ]]; then
            actual_failures+=("${BASH_REMATCH[1]}")
            actual_statuses+=("${BASH_REMATCH[2]}")
        else
            unrecognized+=("$line")
        fi
    done < "$ctest_log"
    rm -f "$ctest_log"

    local raw
    if [[ "${#unrecognized[@]}" -gt 0 ]]; then
        echo "FAIL: ctest reported failure-summary lines this gate does not" >&2
        echo "      recognize; refusing to guess, because crashes, timeouts," >&2
        echo "      Not Run results, and mixed known/unknown failures must" >&2
        echo "      never pass this gate:" >&2
        for raw in "${unrecognized[@]}"; do
            printf '      unrecognized summary line: %s\n' "$raw" >&2
        done
        return 1
    fi

    if [[ "$ctest_status" -eq 0 ]]; then
        # Green exit: still require positive proof that tests executed.
        # A missing "out of N" summary is a format change or a truncated
        # run — either way it is not a provable pass.
        if [[ "$total_tests" -lt 0 ]]; then
            echo "FAIL: ctest exited 0 but printed no executed-test count" >&2
            echo "      ('tests passed ... out of N'); refusing to treat an" >&2
            echo "      unparsable run as a pass." >&2
            return 1
        fi
        if [[ "$total_tests" -eq 0 ]]; then
            echo "FAIL: ctest exited 0 but executed 0 tests; a gate run" >&2
            echo "      that executes nothing proves nothing." >&2
            return 1
        fi
        if [[ "$claimed_failed" -gt 0 ]]; then
            echo "FAIL: ctest exited 0 but its own summary claims" >&2
            echo "      $claimed_failed failed test(s); refusing to pass on a" >&2
            echo "      self-inconsistent test summary." >&2
            return 1
        fi
        return 0
    fi

    # ctest exited nonzero, so something failed.
    if [[ "${#actual_failures[@]}" -eq 0 ]]; then
        echo "FAIL: ctest exited $ctest_status but no failed-test names were parsed" >&2
        return 1
    fi

    if [[ "$claimed_failed" -ge 0 && "$claimed_failed" -ne "${#actual_failures[@]}" ]]; then
        echo "FAIL: ctest counted $claimed_failed failed tests, but the summary" >&2
        echo "      block yielded ${#actual_failures[@]}; refusing to pass on a" >&2
        echo "      self-inconsistent test summary." >&2
        return 1
    fi

    local unexpected=0 i
    for i in "${!actual_failures[@]}"; do
        name="${actual_failures[$i]}"
        status="${actual_statuses[$i]}"
        if [[ "$status" != "Failed" ]]; then
            echo "FAIL: test '$name' reported status ($status); crashed," >&2
            echo "      timed out, and Not Run results are never tolerated." >&2
            unexpected=1
            continue
        fi
        if [[ "${#tolerated_failures[@]}" -gt 0 ]]; then
            local is_known=0
            local known
            for known in "${tolerated_failures[@]}"; do
                if [[ "$name" == "$known" ]]; then
                    is_known=1
                    break
                fi
            done
            if [[ "$is_known" -eq 0 ]]; then
                echo "FAIL: unexpected test failure: $name" >&2
                unexpected=1
            fi
        else
            echo "FAIL: unexpected test failure: $name" >&2
            unexpected=1
        fi
    done
    if [[ "$unexpected" -ne 0 ]]; then
        return 1
    fi
    log "NOTE: tolerating documented tracked failures:" \
        "${actual_failures[*]}"
    return 0
}

case "$PHASE" in
    configure)
        run_configure
        ;;
    build)
        run_build
        ;;
    test)
        run_test
        ;;
    gate)
        check_engine_gate_armed
        run_configure
        run_build
        run_test
        ;;
    *)
        echo "FAIL: unknown phase '$PHASE' (phases: gate, configure, build, test)" >&2
        exit 2
        ;;
esac

log "PASS"

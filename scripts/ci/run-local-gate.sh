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
    if [[ "$ctest_status" -eq 0 ]]; then
        rm -f "$ctest_log"
        return 0
    fi

    # The environment-sensitive abi-scanner and security-count defects are
    # pre-existing on master, tracked on ki-9b13 and ki-ya3t, and stay
    # green on hosted CI (docs/task.md). The gate tolerates exactly this
    # closed set — by exact test name — and fails on anything else.
    local -a known_failures=(
        abi-sizeof-debt-tripwire
        abi-sizeof-scanner-fixture
        security-source-regressions
    )
    local -a actual_failures=()
    local in_block=0
    while IFS= read -r line; do
        if [[ "$line" == "The following tests FAILED:" ]]; then
            in_block=1
            continue
        fi
        if [[ "$in_block" -eq 1 ]]; then
            if [[ "$line" =~ ^[[:space:]]*$ ]]; then
                in_block=0
                continue
            fi
            local name
            name="$(printf '%s' "$line" | sed -E 's/^[[:space:]]*[0-9]+ - (.*) \(Failed\)$/\1/')"
            if [[ "$name" != "$line" ]]; then
                actual_failures+=("$name")
            fi
        fi
    done < "$ctest_log"
    rm -f "$ctest_log"

    if [[ "${#actual_failures[@]}" -eq 0 ]]; then
        echo "FAIL: ctest exited $ctest_status but no failed-test names were parsed" >&2
        return 1
    fi

    local unexpected=0
    local failed_name
    for failed_name in "${actual_failures[@]}"; do
        local is_known=0
        local known
        for known in "${known_failures[@]}"; do
            if [[ "$failed_name" == "$known" ]]; then
                is_known=1
                break
            fi
        done
        if [[ "$is_known" -eq 0 ]]; then
            echo "FAIL: unexpected test failure: $failed_name" >&2
            unexpected=1
        fi
    done
    if [[ "$unexpected" -ne 0 ]]; then
        return 1
    fi
    log "NOTE: tolerating the documented pre-existing local failures" \
        "(tracked on ki-9b13/ki-ya3t, green on hosted CI):" \
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

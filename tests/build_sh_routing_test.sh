#!/usr/bin/env bash
# build.sh routing: each host picks its CMakePresets.json presets, unsupported
# hosts and bad options are refused, and every preset it names exists.
# Dry runs only; nothing is configured or built.
set -euo pipefail

repo_root=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
build_sh="$repo_root/build.sh"
failures=0

check() {
    local name=$1 expected_status=$2 pattern=$3 host=$4 output status
    shift 4
    status=0
    output=$(KISAK_BUILD_SH_HOST="$host" bash "$build_sh" --dry-run "$@" 2>&1) || status=$?
    if [ "$status" -ne "$expected_status" ] || ! grep -Eq -- "$pattern" <<<"$output"; then
        printf 'FAIL %s: exit %s (want %s), output:\n%s\n' "$name" "$status" "$expected_status" "$output" >&2
        failures=$((failures + 1))
    else
        printf 'ok   %s\n' "$name"
    fi
}

check mac-client 0 'cmake --preset macos-arm64-client ' 'Darwin arm64' client
check mac-server 0 'cmake --preset macos-arm64-dedi ' 'Darwin arm64' server
check mac-tests 0 'ctest --test-dir .*/build-macos-arm64-mp ' 'Darwin arm64' tests
check linux-amd64-client 0 'cmake --preset linux-amd64-client ' 'Linux x86_64' client
check linux-arm64-server 0 'cmake --preset linux-arm64-dedi ' 'Linux aarch64' server
check linux-tests 0 'cmake --preset linux-amd64-mp ' 'Linux x86_64' tests
check default-components 0 'KisakCOD-dedi' 'Linux x86_64'
check config 0 'CMAKE_BUILD_TYPE=Debug' 'Linux x86_64' server --config Debug
check run-opt-in 0 'KisakCOD-mp \+set db_unverified64 1 \+devmap mp_crash' 'Darwin arm64' client --run -- +devmap mp_crash
check mac-intel 1 'Apple silicon' 'Darwin x86_64' client
check windows 1 'build-win.ps1' 'MINGW64_NT-10.0 x86_64' client
check bad-config 2 '--config must be' 'Linux x86_64' --config Fast
check bad-jobs 2 '--jobs must be' 'Linux x86_64' --jobs 0
check args-need-run 2 'add --run' 'Linux x86_64' -- +devmap mp_crash
check clean-needs-configure 2 'needs a configure' 'Linux x86_64' --clean --no-configure
check unknown 2 'Unknown argument' 'Linux x86_64' --frobnicate

# Every preset build.sh can pick is one CMake accepts.
presets=$(cd "$repo_root" && cmake --list-presets=configure 2>&1) || {
    printf 'FAIL cmake --list-presets:\n%s\n' "$presets" >&2
    exit 1
}
for preset in macos-arm64-client macos-arm64-dedi macos-arm64-mp \
    linux-amd64-client linux-amd64-dedi linux-amd64-mp \
    linux-arm64-client linux-arm64-dedi linux-arm64-mp; do
    if ! grep -q "^ *\"$preset\"" <<<"$presets"; then
        printf 'FAIL preset %s is not in cmake --list-presets\n' "$preset" >&2
        failures=$((failures + 1))
    fi
done

[ "$failures" -eq 0 ] || { printf '%s failure(s)\n' "$failures" >&2; exit 1; }

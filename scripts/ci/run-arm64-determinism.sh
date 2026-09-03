#!/usr/bin/env bash
#
# run-arm64-determinism.sh — M10 acceptance gate for the architecture-neutral
# determinism layer.
#
# Configures, builds, and runs the portable test suite in a compiler x
# sanitizer matrix, mirroring the AArch64 hosted leg of CI locally:
#
#   compilers : gcc, clang (whichever are installed; at least one required)
#   configs   : Release, ASan+UBSan
#
# On an AArch64 host this exercises the engine semantics natively on the
# target architecture. On an x86_64 host it still validates that the
# determinism layer produces the exact same defined results the AArch64 leg
# pins in tests/runtime_scalar_determinism_tests.cpp — both architectures
# must pass the same contracts, which is the layer's whole point.
#
# The three pre-existing base failures (abi-sizeof-debt-tripwire,
# abi-sizeof-scanner-fixture, security-source-regressions) are excluded:
# they reproduce on an untouched checkout and are tracked in ki-9b13 and
# ki-ya3t. Remove the -E filter when those beads close.
#
# Usage:
#   scripts/ci/run-arm64-determinism.sh
#
# Exits non-zero when any matrix cell fails to configure, build, or test.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

COMPILERS=()
if command -v gcc >/dev/null 2>&1; then
    COMPILERS+=("gcc")
else
    echo "note: gcc not found; skipping gcc matrix cell"
fi
if command -v clang >/dev/null 2>&1; then
    COMPILERS+=("clang")
else
    echo "note: clang not found; skipping clang matrix cell"
fi
if [ "${#COMPILERS[@]}" -eq 0 ]; then
    echo "FAIL: no supported compiler (gcc/clang) found" >&2
    exit 1
fi

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

# Same tests-only shape as the linux-arm64-mp preset: engine variants stay
# off until the 64-bit runtime lands; the portable suite is what the
# determinism contracts ride on.
CONFIG_ARGS=(
    -DKISAK_PLATFORM=linux
    -DKISAK_BUILD_MP=OFF
    -DKISAK_BUILD_DEDICATED=OFF
    -DKISAK_BUILD_SP=OFF
    -DBUILD_TESTING=ON
)

# Sanitizer configuration rides CMAKE_CXX_FLAGS directly so the portable
# targets get ASan+UBSan without inventing engine-target plumbing.
declare -A CXX_FLAGS=(
    [release]=""
    [asan-ubsan]="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
)
declare -A BUILD_TYPE=(
    [release]="Release"
    [asan-ubsan]="Debug"
)

FAILED_CELLS=()
for compiler in "${COMPILERS[@]}"; do
    for cell in release asan-ubsan; do
        build_dir="build-arm64-determinism-${compiler}-${cell}"

        echo ""
        echo "=== matrix cell: ${compiler} / ${cell} (build dir: ${build_dir}) ==="
        if ! cmake -S "$REPO_ROOT" -B "$build_dir" \
                -DCMAKE_C_COMPILER="$compiler" \
                -DCMAKE_CXX_COMPILER="$compiler" \
                -DCMAKE_BUILD_TYPE="${BUILD_TYPE[$cell]}" \
                -DCMAKE_CXX_FLAGS="${CXX_FLAGS[$cell]}" \
                -DCMAKE_EXE_LINKER_FLAGS="${CXX_FLAGS[$cell]}" \
                "${CONFIG_ARGS[@]}"; then
            FAILED_CELLS+=("${compiler}/${cell} (configure)")
            continue
        fi

        if ! cmake --build "$build_dir" --parallel "$JOBS"; then
            FAILED_CELLS+=("${compiler}/${cell} (build)")
            continue
        fi

        if ! ctest --test-dir "$build_dir" --output-on-failure \
                -E "abi-sizeof|security-source-regressions"; then
            FAILED_CELLS+=("${compiler}/${cell} (test)")
        fi
    done
done

echo ""
if [ "${#FAILED_CELLS[@]}" -gt 0 ]; then
    echo "FAIL: ${#FAILED_CELLS[@]} matrix cell(s) failed:"
    for cell in "${FAILED_CELLS[@]}"; do
        echo "  - $cell"
    done
    exit 1
fi

echo "OK: arm64 determinism matrix passed (${#COMPILERS[@]} compiler(s) x 2 configs)"

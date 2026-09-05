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
if command -v g++ >/dev/null 2>&1; then
    COMPILERS+=("gcc:g++")
else
    echo "note: g++ not found; skipping gcc matrix cell"
fi
if command -v clang++ >/dev/null 2>&1; then
    COMPILERS+=("clang:clang++")
else
    echo "note: clang++ not found; skipping clang matrix cell"
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

# Per-cell compiler/linker flags, applied to BOTH languages: the portable
# targets are C++, but the memfile test subject also compiles the vendored
# zlib C sources (tests/CMakeLists.txt), and sanitizer coverage must reach
# those C translation units too — instrumenting only the C++ objects would
# report coverage the gate does not actually have.
declare -A CELL_FLAGS=(
    [release]=""
    [asan-ubsan]="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
)
declare -A BUILD_TYPE=(
    [release]="Release"
    [asan-ubsan]="Debug"
)

FAILED_CELLS=()
SANITIZER_CELLS_RUN=0
for entry in "${COMPILERS[@]}"; do
    compiler="${entry%%:*}"
    cxx_compiler="${entry##*:}"
    for cell in release asan-ubsan; do
        build_dir="build-arm64-determinism-${compiler}-${cell}"

        # A compiler whose sanitizer runtime is not installed (common on
        # dev containers with mixed toolchains) cannot configure an
        # ASan+UBSan cell. Probe once and skip loudly instead of failing
        # the whole gate; the gate still requires at least one sanitizer
        # cell to actually run somewhere in the matrix. Both language
        # frontends are probed: the matrix configures C and C++ with the
        # same flags, so a cell is only honest when BOTH runtimes work.
        if [ "$cell" = "asan-ubsan" ]; then
            # Trailing X-run templates only (portable across GNU and BSD
            # mktemp), rooted at ${TMPDIR:-/tmp} instead of a hardcoded
            # per-session directory; -x c/-x c++ keep the probe language
            # explicit without relying on a file-name suffix.
            probe_src="$(mktemp "${TMPDIR:-/tmp}/asan-probe-src-XXXXXX")"
            probe_bin="$(mktemp "${TMPDIR:-/tmp}/asan-probe-bin-XXXXXX")"
            printf 'int main() { return 0; }\n' > "$probe_src"
            probe_ok=1
            "$cxx_compiler" -x c++ -fsanitize=address,undefined "$probe_src" \
                -o "$probe_bin" >/dev/null 2>&1 || probe_ok=0
            "$compiler" -x c -fsanitize=address,undefined "$probe_src" \
                -o "$probe_bin" >/dev/null 2>&1 || probe_ok=0
            if [ "$probe_ok" -ne 1 ]; then
                echo ""
                echo "=== matrix cell: ${compiler} / ${cell} — SKIPPED:"
                echo "    ${compiler}/${cxx_compiler} have no working ASan/UBSan runtime on this host"
                rm -f "$probe_src" "$probe_bin"
                continue
            fi
            rm -f "$probe_src" "$probe_bin"
            SANITIZER_CELLS_RUN=$((SANITIZER_CELLS_RUN + 1))
        fi

        echo ""
        echo "=== matrix cell: ${compiler} / ${cell} (build dir: ${build_dir}) ==="
        if ! cmake -S "$REPO_ROOT" -B "$build_dir" \
                -DCMAKE_C_COMPILER="$compiler" \
                -DCMAKE_CXX_COMPILER="$cxx_compiler" \
                -DCMAKE_BUILD_TYPE="${BUILD_TYPE[$cell]}" \
                -DCMAKE_C_FLAGS="${CELL_FLAGS[$cell]}" \
                -DCMAKE_CXX_FLAGS="${CELL_FLAGS[$cell]}" \
                -DCMAKE_EXE_LINKER_FLAGS="${CELL_FLAGS[$cell]}" \
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

if [ "$SANITIZER_CELLS_RUN" -eq 0 ]; then
    echo "FAIL: every sanitizer cell was skipped — no ASan/UBSan coverage ran."
    echo "Install a working ASan/UBSan runtime for at least one compiler."
    exit 1
fi

echo "OK: arm64 determinism matrix passed (${#COMPILERS[@]} compiler(s) x 2 configs," \
    "$SANITIZER_CELLS_RUN sanitizer cell(s) run)"

#!/usr/bin/env bash
#
# test-run-retail-fastfile-parity.sh — acceptance-boundary gate tests for
# run-retail-fastfile-parity.sh (ki-msb).
#
# Exercised as ctest retail-fastfile-parity-driver-gates (registered on UNIX
# hosts; the parity driver is a bash instrument run on the Linux host leg).
# It verifies the driver's ACCEPTANCE BOUNDARY, not just its happy path:
#
#   - an envelope-only match is reported as ENVELOPE CONSISTENCY and can
#     never pass the --mode m5-graph runtime-graph acceptance gate. This is
#     the negative test for the returned PR #113 finding: envelope-only data
#     used to exit 0 with "OK widened-graph hash matches", advertising a
#     runtime-graph result that was never proven;
#   - success results are NAMED by mode + capture kind;
#   - --mode m5-graph fails closed on missing or foreign leg identity;
#   - minting and comparing still round-trip in instrument mode.
#
# The real driver runs against the REAL harness in an already-configured
# build tree (--skip-build), plus a stub graph-v1 harness that exercises the
# runtime-graph positive path of the gate (the widened graph walk enrolls
# with the native64 loader; its output protocol is stable, so the gate is
# testable before that lands).
#
# Exit codes:
#   0  every gate behaved as specified
#   1  a gate misbehaved (test failure; message on stderr)
#   2  usage/environment error (bad --build-dir, missing driver)
#
# Usage:
#   scripts/ci/test-run-retail-fastfile-parity.sh --build-dir <configured cmake build dir>

set -euo pipefail

BUILD_DIR=""
DRIVER="$(cd "$(dirname "$0")" && pwd)/run-retail-fastfile-parity.sh"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="${2:?}"; shift 2 ;;
        *) echo "test-run-retail-fastfile-parity: unknown option '$1'" >&2; exit 2 ;;
    esac
done
if [ -z "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "test-run-retail-fastfile-parity: --build-dir must name a configured cmake build tree" >&2
    exit 2
fi
if [ ! -f "$DRIVER" ]; then
    echo "test-run-retail-fastfile-parity: driver not found: $DRIVER" >&2
    exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/var/tmp}/ki-msb-driver-gates.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# Nonempty PLAIN TEXT on purpose: exactly the operator's P2 probe. Nothing
# about these bytes resembles a fast-file or a runtime graph, so any
# "parity" success over them would be the finding again.
FIXTURE="$WORK/fixture.ff"
printf 'ki-msb driver gate fixture: plain text, no runtime graph inside.\n' >"$FIXTURE"

OUT_FILE="$WORK/last.out"
ERR_FILE="$WORK/last.err"
PASS=0

driver() {
    "$DRIVER" --skip-build --build-dir "$BUILD_DIR" "$@"
}

# expect_status <name> <expected-exit> -- <command...>
expect_status() {
    local name="$1" expected="$2"
    shift 2
    if [ "${1:-}" = "--" ]; then
        shift
    fi
    set +e
    "$@" >"$OUT_FILE" 2>"$ERR_FILE"
    local status=$?
    set -e
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL: $name — expected exit $expected, got $status" >&2
        sed -n '1,30p' "$OUT_FILE" >&2
        sed -n '1,30p' "$ERR_FILE" >&2
        exit 1
    fi
    PASS=$((PASS + 1))
}

# expect_said <name> <fixed-string> [file] — output must contain the string.
expect_said() {
    local name="$1" needle="$2" file="${3:-$OUT_FILE}"
    if ! grep -Fq -- "$needle" "$file"; then
        echo "FAIL: $name — output missing: $needle" >&2
        sed -n '1,30p' "$file" >&2
        exit 1
    fi
    PASS=$((PASS + 1))
}

# expect_silent <name> <fixed-string> [file] — output must NOT contain it.
expect_silent() {
    local name="$1" needle="$2" file="${3:-$OUT_FILE}"
    if grep -Fq -- "$needle" "$file"; then
        echo "FAIL: $name — output must NOT contain: $needle" >&2
        sed -n '1,30p' "$file" >&2
        exit 1
    fi
    PASS=$((PASS + 1))
}

# --- 1. instrument mode mints an envelope reference, named as such ---------
ENVELOPE_REF="$WORK/envelope-ref.txt"
expect_status "instrument mint of an envelope capture exits 0" 0 -- \
    driver --mode instrument --host windows-x86 --ref linux-amd64 \
        --fastfile "$FIXTURE" --emit-reference "$ENVELOPE_REF"
expect_said "minted reference records the envelope capture kind" \
    "capture_kind=envelope-v2" "$ENVELOPE_REF"
expect_said "minted reference records the minting leg" "leg=windows-x86" "$ENVELOPE_REF"
expect_said "envelope mint output disclaims M5" "NOT an M5 runtime-graph reference"

# --- 2. instrument mode: an envelope match is reported as envelope --------
# --- consistency, never as runtime-graph parity -----------------------------
expect_status "instrument envelope match exits 0" 0 -- \
    driver --mode instrument --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$ENVELOPE_REF"
expect_said "envelope match is named envelope-consistency" \
    "OK envelope-consistency digest matches"
expect_silent "no OK line claims widened-runtime-graph parity" \
    "OK widened-runtime-graph parity"
expect_silent "the old misleading success line is gone" \
    "OK widened-graph hash matches"
expect_said "result names the acceptance gate" "--mode m5-graph"

# --- 3. THE P2 NEGATIVE: envelope-only data cannot pass the m5-graph gate --
expect_status "m5-graph gate rejects envelope-only data (exit 1)" 1 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$ENVELOPE_REF"
expect_said "gate failure names the missing runtime-graph capture" \
    "requires runtime-graph captures (graph-*)" "$ERR_FILE"
expect_said "gate failure explains what envelope captures prove" \
    "envelope consistency, NOT widened-runtime-graph parity" "$ERR_FILE"
expect_silent "failed gate emits no OK result" "OK" "$OUT_FILE"

# --- 4. m5-graph cannot mint an envelope reference (fail-closed stocking) --
M5REF_REJECT="$WORK/m5ref-rejected.txt"
expect_status "m5-graph mint of an envelope capture exits 1" 1 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --emit-reference "$M5REF_REJECT"
if [ -e "$M5REF_REJECT" ]; then
    echo "FAIL: rejected m5-graph mint must not write a reference file" >&2
    exit 1
fi
PASS=$((PASS + 1))

# --- 5. m5-graph compares two DIFFERENT legs only ---------------------------
expect_status "m5-graph same-triple is a usage error (exit 2)" 2 -- \
    driver --mode m5-graph --host linux-amd64 --ref linux-amd64 \
        --fastfile "$FIXTURE" --reference-hash "$ENVELOPE_REF"

# --- 6. unknown mode is a usage error ---------------------------------------
expect_status "unknown mode is a usage error (exit 2)" 2 -- \
    driver --mode sideways --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$ENVELOPE_REF"

# --- 7. m5-graph requires leg identity on the reference ---------------------
grep -v '^leg=' "$ENVELOPE_REF" >"$WORK/legless-ref.txt"
expect_status "m5-graph rejects a leg-less reference (exit 2)" 2 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/legless-ref.txt"
expect_said "leg-less reference failure names the fix" \
    "Re-mint it on the windows-x86 side" "$ERR_FILE"

# --- 8. leg identity is verified in instrument mode too ---------------------
sed 's/^leg=.*/leg=linux-amd64/' "$ENVELOPE_REF" >"$WORK/foreign-ref.txt"
expect_status "foreign reference leg identity fails (exit 1)" 1 -- \
    driver --mode instrument --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/foreign-ref.txt"
expect_said "foreign leg failure names the mismatch" \
    "reference declares leg=linux-amd64" "$ERR_FILE"

# --- 9. instrument mode still reports a genuine mismatch as a failure ------
sed 's/^graph_sha256=.*/graph_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/' \
    "$ENVELOPE_REF" >"$WORK/other-asset-ref.txt"
expect_status "instrument digest mismatch exits 1" 1 -- \
    driver --mode instrument --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/other-asset-ref.txt"
expect_said "mismatch is named envelope-consistency" \
    "envelope-consistency digest mismatch" "$ERR_FILE"

# --- 10. instrument mode tolerates a leg-less LEGACY reference -------------
expect_status "instrument tolerates a leg-less legacy reference" 0 -- \
    driver --mode instrument --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/legless-ref.txt"
expect_said "legacy-reference match still names the envelope kind" \
    "OK envelope-consistency digest matches"

# --- 11. the runtime-graph positive path: m5-graph passes graph-* captures -
# A stub harness emits the stable graph-v1 protocol so the gate's positive
# path is exercised before the widened walk itself lands.
STUB_BUILD="$WORK/stub-build"
mkdir -p "$STUB_BUILD"
: >"$STUB_BUILD/CMakeCache.txt"
STUB_HARNESS="$STUB_BUILD/kisakcod-retail-fastfile-parity-harness"
{
    echo '#!/usr/bin/env bash'
    echo '# test stub: emits the stable graph-v1 capture protocol.'
    echo 'leg=""'
    echo 'while [ "$#" -gt 0 ]; do'
    echo '    case "$1" in'
    echo '        --leg) leg="${2:?}"; shift 2 ;;'
    echo '        --fastfile) shift 2 ;;'
    echo '        *) shift ;;'
    echo '    esac'
    echo 'done'
    echo 'cat <<STUB'
    echo 'capture_kind=graph-v1'
    echo 'hash_domain=kisakcod/m5-widened-graph-hash/v2'
    echo "leg=\${leg}"
    echo 'fastfile_bytes=4'
    echo 'fastfile_zlib_stream=0'
    echo "graph_sha256=\${KISAK_STUB_DIGEST:-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa}"
    echo 'STUB'
} >"$STUB_HARNESS"
chmod +x "$STUB_HARNESS"

GRAPH_DIGEST="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
{
    echo "capture_kind=graph-v1"
    echo "hash_domain=kisakcod/m5-widened-graph-hash/v2"
    echo "leg=windows-x86"
    echo "graph_sha256=$GRAPH_DIGEST"
} >"$WORK/graph-ref.txt"
expect_status "m5-graph accepts verified graph-v1 legs (exit 0)" 0 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --build-dir "$STUB_BUILD" \
        --reference-hash "$WORK/graph-ref.txt"
expect_said "m5-graph success is named widened-runtime-graph parity (M5)" \
    "OK widened-runtime-graph parity (M5"
expect_said "m5-graph success records verified leg identity" "leg identity verified"

# --- 12. a graph-leg digest mismatch is still a failure --------------------
sed "s/^graph_sha256=.*/graph_sha256=${GRAPH_DIGEST%a}b/" "$WORK/graph-ref.txt" \
    >"$WORK/graph-mismatch-ref.txt"
expect_status "m5-graph digest mismatch exits 1" 1 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --build-dir "$STUB_BUILD" \
        --reference-hash "$WORK/graph-mismatch-ref.txt"
expect_said "graph mismatch is named widened-graph" \
    "widened-graph digest mismatch (graph-v1)" "$ERR_FILE"

echo "retail-fastfile-parity driver gates: $PASS check(s) passed"

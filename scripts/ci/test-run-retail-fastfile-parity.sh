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
#   - capture identity is DERIVED, not asserted: the real harness refuses a
#     --leg label that contradicts its own derived platform identity, and
#     the driver refuses to MINT from a capture that is missing, incoherent,
#     or foreign to --host. This is the negative test for the follow-up
#     finding: minting used to exit before any identity check and write
#     leg=$HOST_TRIPLE, laundering a Linux capture into a "windows-x86"
#     reference whose later compare claimed M5 parity with verified identity;
#   - minted references record the validated capture identity, and a
#     reference that disagrees with --ref (foreign leg or platform) is
#     refused on compare — minted files are coherent by construction, so
#     any relabeled reference necessarily violates a foreign-identity check;
#   - success results are NAMED by mode + capture kind;
#   - minting and comparing still round-trip in instrument mode.
#
# The real driver runs against the REAL harness in an already-configured
# build tree (--skip-build). The runtime-graph positive path additionally
# uses a stub harness that emits the stable graph-v1 protocol (the widened
# graph walk enrolls with the native64 loader; its output protocol is
# stable, so the gate is testable before that lands). The stub is a
# PROTOCOL-BOUNDARY FAKE: it stands in for the Windows tree's harness
# binary only to stock gate fixtures, asserts nothing about real platform
# provenance, and no result in this file claims a real runtime-graph
# loader or retail M5 acceptance.
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

# expect_absent <name> <path> — the file must not exist (a refused mint
# must not leave a reference behind).
expect_absent() {
    local name="$1" path="$2"
    if [ -e "$path" ]; then
        echo "FAIL: $name — file must not exist: $path" >&2
        exit 1
    fi
    PASS=$((PASS + 1))
}

# --- 0. derive THIS machine's honest identity from the real harness --------
# The harness DERIVES its platform identity (compile-time target
# cross-checked against the running kernel), so the honest local leg label
# is whatever THIS executable emits — linux-amd64 here, linux-arm64 on an
# arm64 runner, and so on. Every case below that speaks for this machine
# passes --host "$DERIVED_PLATFORM"; a fixture that hardcoded linux-amd64
# for its honest-mint case failed on arm64 runners exactly because the new
# identity validation correctly refuses a foreign label there. Deliberately
# foreign labels (the windows-x86 reference-leg stand-in, --host windows-x86
# refusal probes, stub harnesses with fixed identities) stay hardcoded on
# purpose — those exercise refusals, and their refusal must not depend on
# which machine runs the gate.
REAL_HARNESS="$(find "$BUILD_DIR" -maxdepth 3 -type f \
    -name 'kisakcod-retail-fastfile-parity-harness*' | head -1)"
if [ -z "$REAL_HARNESS" ] || [ ! -e "$REAL_HARNESS" ]; then
    echo "test-run-retail-fastfile-parity: real harness binary not found under '$BUILD_DIR'" >&2
    exit 2
fi
DERIVED_PLATFORM="$("$REAL_HARNESS" --fastfile "$FIXTURE" | sed -n 's/^platform=//p')"
if [ -z "$DERIVED_PLATFORM" ]; then
    echo "test-run-retail-fastfile-parity: real harness emitted no platform identity" >&2
    exit 1
fi
PASS=$((PASS + 1))

# --- 1. instrument mode mints an HONEST envelope reference ------------------
# The REAL harness derives its own identity, so a mint from this tree records
# the derived platform/leg — whatever this runner is, never a hardcoded
# triple. Minting "as windows-x86" from this tree is the laundered reference
# the driver must refuse — covered below.
LINUX_ENV_REF="$WORK/linux-env-ref.txt"
expect_status "instrument mint of an envelope capture exits 0" 0 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --emit-reference "$LINUX_ENV_REF"
expect_said "minted reference records the envelope capture kind" \
    "capture_kind=envelope-v2" "$LINUX_ENV_REF"
expect_said "minted reference records the derived platform" \
    "platform=$DERIVED_PLATFORM" "$LINUX_ENV_REF"
expect_said "minted reference records the validated leg" \
    "leg=$DERIVED_PLATFORM" "$LINUX_ENV_REF"
expect_said "envelope mint output disclaims M5" "NOT an M5 runtime-graph reference"

# Boundary fixture: the same reference RELABELED to windows-x86. This is a
# deliberate forgery built by the test itself — well-formed (coherent
# identity, true fixture digest) but not minted by a windows tree. It
# stocks the envelope-negative and legacy-tolerance cases; the gate must
# accept it ONLY as what it claims (an envelope reference for the
# windows-x86 leg), never as runtime-graph parity, and the mint path that
# could have produced it dishonestly is refused in its own tests below.
WIN_ENV_REF="$WORK/win-env-ref.txt"
sed -e 's/^platform=.*/platform=windows-x86/' -e 's/^leg=.*/leg=windows-x86/' \
    "$LINUX_ENV_REF" >"$WIN_ENV_REF"

# --- 2. instrument mode: an envelope match is reported as envelope --------
# --- consistency, never as runtime-graph parity -----------------------------
expect_status "instrument envelope match exits 0" 0 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WIN_ENV_REF"
expect_said "envelope match is named envelope-consistency" \
    "OK envelope-consistency digest matches"
expect_silent "no OK line claims widened-runtime-graph parity" \
    "OK widened-runtime-graph parity"
expect_silent "the old misleading success line is gone" \
    "OK widened-graph hash matches"
expect_said "result names the acceptance gate" "--mode m5-graph"

# --- 3. THE P2 NEGATIVE: envelope-only data cannot pass the m5-graph gate --
expect_status "m5-graph gate rejects envelope-only data (exit 1)" 1 -- \
    driver --mode m5-graph --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WIN_ENV_REF"
expect_said "gate failure names the missing runtime-graph capture" \
    "requires runtime-graph captures (graph-*)" "$ERR_FILE"
expect_said "gate failure explains what envelope captures prove" \
    "envelope consistency, NOT widened-runtime-graph parity" "$ERR_FILE"
expect_silent "failed gate emits no OK result" "OK" "$OUT_FILE"

# --- 4. m5-graph cannot mint an envelope reference (fail-closed stocking) --
M5REF_REJECT="$WORK/m5ref-rejected.txt"
expect_status "m5-graph mint of an envelope capture exits 1" 1 -- \
    driver --mode m5-graph --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --emit-reference "$M5REF_REJECT"
expect_absent "rejected m5-graph mint must not write a reference file" "$M5REF_REJECT"

# --- 5. m5-graph compares two DIFFERENT legs only ---------------------------
# (same-triple is refused before any capture, so this holds for any local
# identity)
expect_status "m5-graph same-triple is a usage error (exit 2)" 2 -- \
    driver --mode m5-graph --host "$DERIVED_PLATFORM" --ref "$DERIVED_PLATFORM" \
        --fastfile "$FIXTURE" --reference-hash "$WIN_ENV_REF"

# --- 6. unknown mode is a usage error ---------------------------------------
expect_status "unknown mode is a usage error (exit 2)" 2 -- \
    driver --mode sideways --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WIN_ENV_REF"

# --- 7. m5-graph requires full identity on the reference --------------------
grep -v -e '^leg=' -e '^platform=' "$WIN_ENV_REF" >"$WORK/legless-ref.txt"
expect_status "m5-graph rejects an identity-less reference (exit 2)" 2 -- \
    driver --mode m5-graph --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/legless-ref.txt"
expect_said "leg-less reference failure names the fix" \
    "reference file has no leg identity" "$ERR_FILE"

grep -v '^platform=' "$WIN_ENV_REF" >"$WORK/platformless-ref.txt"
expect_status "m5-graph rejects a platform-less reference (exit 2)" 2 -- \
    driver --mode m5-graph --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/platformless-ref.txt"
expect_said "platform-less reference failure names the fix" \
    "reference file has no platform identity" "$ERR_FILE"

# --- 8. reference identity is verified in instrument mode too ---------------
# Minted references are coherent by construction, so a reference that
# disagrees with --ref on EITHER recorded field is a relabeled file, and
# the foreign-field checks refuse it. The relabeled fields below use
# "linux-amd64" as a DELIBERATELY FOREIGN label against --ref windows-x86
# (a forged reference is fixture text; its fake label never interacts with
# this machine's identity).
sed 's/^leg=.*/leg=linux-amd64/' "$WIN_ENV_REF" >"$WORK/foreign-leg-ref.txt"
expect_status "foreign reference leg identity fails (exit 1)" 1 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/foreign-leg-ref.txt"
expect_said "foreign leg failure names the mismatch" \
    "reference declares leg=linux-amd64" "$ERR_FILE"

sed 's/^platform=.*/platform=linux-amd64/' "$WIN_ENV_REF" >"$WORK/foreign-platform-ref.txt"
expect_status "foreign reference platform identity fails (exit 1)" 1 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/foreign-platform-ref.txt"
expect_said "foreign platform failure names the mismatch" \
    "platform=linux-amd64, invocation claims --ref windows-x86" "$ERR_FILE"

# --- 9. instrument mode still reports a genuine mismatch as a failure ------
sed 's/^graph_sha256=.*/graph_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/' \
    "$WIN_ENV_REF" >"$WORK/other-asset-ref.txt"
expect_status "instrument digest mismatch exits 1" 1 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/other-asset-ref.txt"
expect_said "mismatch is named envelope-consistency" \
    "envelope-consistency digest mismatch" "$ERR_FILE"

# --- 10. instrument mode tolerates a leg-less LEGACY reference -------------
expect_status "instrument tolerates an identity-less legacy reference" 0 -- \
    driver --mode instrument --host "$DERIVED_PLATFORM" --ref windows-x86 \
        --fastfile "$FIXTURE" --reference-hash "$WORK/legless-ref.txt"
expect_said "legacy-reference match still names the envelope kind" \
    "OK envelope-consistency digest matches"

# --- 11. the runtime-graph positive path: m5-graph passes graph-* captures -
# A stub harness emits the stable graph-v1 protocol with a fixed
# linux-amd64 identity so the gate's positive path is exercised before the
# widened walk itself lands (protocol-boundary fake; see header). The fixed
# identity is INTENTIONAL and architecture-independent: this stub stands in
# for the Windows harness, the driver validates only the stub's own emitted
# identity against the invocation, and no case here touches the local
# machine's derived identity.
STUB_BUILD="$WORK/stub-build"
mkdir -p "$STUB_BUILD"
: >"$STUB_BUILD/CMakeCache.txt"
STUB_HARNESS="$STUB_BUILD/kisakcod-retail-fastfile-parity-harness"
cat >"$STUB_HARNESS" <<'STUB'
#!/usr/bin/env bash
# test stub: protocol-boundary fake emitting the stable graph-v1 capture
# protocol with a FIXED linux-amd64 identity (host-leg stand-in only).
cat <<'FIELDS'
capture_kind=graph-v1
hash_domain=kisakcod/m5-widened-graph-hash/v2
platform=linux-amd64
leg=linux-amd64
fastfile_bytes=4
fastfile_zlib_stream=0
graph_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
FIELDS
STUB
chmod +x "$STUB_HARNESS"

GRAPH_DIGEST="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
{
    echo "capture_kind=graph-v1"
    echo "hash_domain=kisakcod/m5-widened-graph-hash/v2"
    echo "platform=windows-x86"
    echo "leg=windows-x86"
    echo "graph_sha256=$GRAPH_DIGEST"
} >"$WORK/graph-ref.txt"
expect_status "m5-graph accepts verified graph-v1 legs (exit 0)" 0 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --build-dir "$STUB_BUILD" \
        --reference-hash "$WORK/graph-ref.txt"
expect_said "m5-graph success is named widened-runtime-graph parity (M5)" \
    "OK widened-runtime-graph parity (M5"
expect_said "m5-graph success records verified identity" "platform+leg identity verified"

# --- 12. a graph-leg digest mismatch is still a failure --------------------
sed "s/^graph_sha256=.*/graph_sha256=${GRAPH_DIGEST%a}b/" "$WORK/graph-ref.txt" \
    >"$WORK/graph-mismatch-ref.txt"
expect_status "m5-graph digest mismatch exits 1" 1 -- \
    driver --mode m5-graph --host linux-amd64 --ref windows-x86 \
        --fastfile "$FIXTURE" --build-dir "$STUB_BUILD" \
        --reference-hash "$WORK/graph-mismatch-ref.txt"
expect_said "graph mismatch is named widened-graph" \
    "widened-graph digest mismatch (graph-v1)" "$ERR_FILE"

# --- 13. MINT REFUSES FOREIGN OR MISSING CAPTURE IDENTITY -------------------
# The follow-up finding: the mint branch used to exit before any identity
# check and write leg=$HOST_TRIPLE, so a capture from the wrong executable
# was laundered into a reference for whichever leg the invocation named.

# 13a. END-TO-END with the REAL harness: this Linux tree cannot mint for
# --host windows-x86, because the harness refuses to stamp a leg its own
# derived identity contradicts. (This is the operator's native-foreign
# mint probe, which used to exit 0 with a forged windows-x86 reference.)
FORBIDDEN_MINT="$WORK/forbidden-mint.txt"
expect_status "real-harness mint under a foreign --host is refused (exit 2)" 2 -- \
    driver --mode instrument --host windows-x86 --ref linux-amd64 \
        --fastfile "$FIXTURE" --emit-reference "$FORBIDDEN_MINT"
expect_absent "refused foreign-identity mint writes no reference" "$FORBIDDEN_MINT"

# 13b. Driver-side refusal for a harness that cannot validate itself: a
# stub emitting an honest linux-amd64 identity while the invocation claims
# --host windows-x86 must be refused BEFORE any reference is written.
STUB_LINUX_ENV="$WORK/stub-linux-env"
mkdir -p "$STUB_LINUX_ENV"
: >"$STUB_LINUX_ENV/CMakeCache.txt"
cat >"$STUB_LINUX_ENV/kisakcod-retail-fastfile-parity-harness" <<'STUB'
#!/usr/bin/env bash
# test stub: honest linux-amd64 identity, envelope kind (mint-refusal fixture).
cat <<'FIELDS'
capture_kind=envelope-v2
hash_domain=kisakcod/m5-widened-graph-hash/v2
platform=linux-amd64
leg=linux-amd64
fastfile_bytes=4
fastfile_zlib_stream=0
graph_sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
FIELDS
STUB
chmod +x "$STUB_LINUX_ENV/kisakcod-retail-fastfile-parity-harness"
STUB_FOREIGN_MINT="$WORK/stub-foreign-mint.txt"
expect_status "mint from a capture foreign to --host is refused (exit 1)" 1 -- \
    driver --mode instrument --host windows-x86 --ref linux-amd64 \
        --fastfile "$FIXTURE" --build-dir "$STUB_LINUX_ENV" \
        --emit-reference "$STUB_FOREIGN_MINT"
expect_said "foreign mint refusal names the capture identity" \
    "platform=linux-amd64 leg=linux-amd64, invocation claims --host windows-x86" "$ERR_FILE"
expect_absent "foreign-identity mint writes no reference" "$STUB_FOREIGN_MINT"

# 13c. Driver-side refusal for MISSING identity fields at mint: a capture
# without the leg or platform line cannot be attributed, so nothing is
# minted from it in any mode.
make_incomplete_stub() {
    local dir="$1" drop="$2"
    mkdir -p "$dir"
    : >"$dir/CMakeCache.txt"
    cat >"$dir/kisakcod-retail-fastfile-parity-harness" <<'STUB'
#!/usr/bin/env bash
# test stub: capture missing one identity line (mint-refusal fixture).
cat <<'FIELDS'
capture_kind=envelope-v2
hash_domain=kisakcod/m5-widened-graph-hash/v2
platform=windows-x86
leg=windows-x86
fastfile_bytes=4
fastfile_zlib_stream=0
graph_sha256=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
FIELDS
STUB
    if [ "$drop" = "platform" ]; then
        sed -i '/^platform=/d' "$dir/kisakcod-retail-fastfile-parity-harness"
    else
        sed -i '/^leg=/d' "$dir/kisakcod-retail-fastfile-parity-harness"
    fi
    chmod +x "$dir/kisakcod-retail-fastfile-parity-harness"
}
make_incomplete_stub "$WORK/stub-noleg" leg
STUB_NOLEG_MINT="$WORK/stub-noleg-mint.txt"
expect_status "mint from a capture without a leg line is refused (exit 2)" 2 -- \
    driver --mode instrument --host windows-x86 --ref linux-amd64 \
        --fastfile "$FIXTURE" --build-dir "$WORK/stub-noleg" \
        --emit-reference "$STUB_NOLEG_MINT"
expect_said "leg-less mint refusal names the missing field" \
    "host leg produced no leg identity" "$ERR_FILE"
expect_absent "leg-less mint writes no reference" "$STUB_NOLEG_MINT"

make_incomplete_stub "$WORK/stub-noplatform" platform
STUB_NOPLATFORM_MINT="$WORK/stub-noplatform-mint.txt"
expect_status "mint from a capture without a platform line is refused (exit 2)" 2 -- \
    driver --mode instrument --host windows-x86 --ref linux-amd64 \
        --fastfile "$FIXTURE" --build-dir "$WORK/stub-noplatform" \
        --emit-reference "$STUB_NOPLATFORM_MINT"
expect_said "platform-less mint refusal names the missing field" \
    "host leg produced no platform identity" "$ERR_FILE"
expect_absent "platform-less mint writes no reference" "$STUB_NOPLATFORM_MINT"

# --- 14. the REAL harness derives and enforces its own identity -------------
# These probes run the built binary directly, using the identity already
# derived from it up front (section 0), so they hold on any leg platform:
# a foreign --leg must be refused at capture time, and the accepted leg must
# appear alongside the derived platform in the protocol.
FOREIGN_LABEL="not-the-$DERIVED_PLATFORM-tree"
expect_status "real harness refuses a foreign --leg (exit 2)" 2 -- \
    "$REAL_HARNESS" --fastfile "$FIXTURE" --leg "$FOREIGN_LABEL"
expect_said "foreign-leg refusal names both labels" \
    "does not match this executable's platform identity" "$ERR_FILE"

expect_status "real harness accepts the identity it derives (exit 0)" 0 -- \
    "$REAL_HARNESS" --fastfile "$FIXTURE" --leg "$DERIVED_PLATFORM"
expect_said "accepted capture records the derived platform" \
    "platform=$DERIVED_PLATFORM" "$OUT_FILE"
expect_said "accepted capture records the validated leg" \
    "leg=$DERIVED_PLATFORM" "$OUT_FILE"

echo "retail-fastfile-parity driver gates: $PASS check(s) passed"

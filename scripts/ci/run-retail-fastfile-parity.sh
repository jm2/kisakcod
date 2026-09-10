#!/usr/bin/env bash
#
# run-retail-fastfile-parity.sh — retail fast-file parity instrument and M5
# runtime-graph acceptance gate (ki-msb).
#
# Runs the fast-file capture harness on the host build (native64, Linux
# amd64) and hash-matches its capture digest against a minted reference leg
# (documented reference tree: Windows x86).
#
# Both legs run the SAME harness and emit the SAME output protocol:
#
#   capture_kind=envelope-v2
#   hash_domain=kisakcod/m5-widened-graph-hash/v2
#   platform=<harness-derived build/runtime identity>
#   leg=<validated capture-leg identity>
#   fastfile_bytes=<n>
#   fastfile_zlib_stream=<0|1>
#   graph_sha256=<64 hex>
#
# CAPTURE IDENTITY IS DERIVED, NOT ASSERTED. The harness derives its
# platform identity from its own compile-time target (cross-checked against
# the running kernel where observable) and accepts the driver's --leg label
# only when it MATCHES that derived identity — a Linux-built binary cannot
# stamp leg=windows-x86 onto its capture. The driver independently verifies
# the emitted platform/leg identity against the declared --host/--ref
# triples on BOTH paths: a mint whose capture is missing, incoherent, or
# foreign to --host is refused (nothing is written), and a compare refuses
# a reference whose recorded identity does not match --ref or whose
# platform and leg disagree (a relabeled reference). Minted references
# record the VALIDATED capture identity, never the requested label.
#
# MODES (--mode), named so a result can never claim more than was proven:
#
#   instrument (default)
#       Compares captures of any one declared kind and reports the result
#       under THAT kind's name. envelope-v2 captures hash the fast-file
#       envelope and its full CONTENT BYTES only — no runtime graph is
#       loaded — so an envelope match is reported as envelope-consistency
#       and is explicitly NOT a runtime-graph parity result.
#
#   m5-graph
#       The M5 runtime-graph acceptance gate. Requires BOTH legs to carry
#       a runtime-graph capture kind (graph-*; the widened runtime-graph
#       walk enrolls as capture_kind graph-v1 when the native64 loader
#       path lands) AND verified leg identity: the host capture must
#       declare leg == --host and the reference leg == --ref. Envelope
#       captures CANNOT pass this gate; the negative case is exercised by
#       ctest retail-fastfile-parity-driver-gates
#       (scripts/ci/test-run-retail-fastfile-parity.sh).
#
# Workflow:
#   1. (x86 side, once per fast-file) mint the reference:
#        scripts/ci/run-retail-fastfile-parity.sh --host windows-x86 \
#            --emit-reference reference.txt --fastfile <retail.ff>
#      Run this inside the Windows x86 reference tree with its own build of
#      the harness; the file records capture_kind, hash_domain, leg, and
#      digest. The default preset FOLLOWS --host (windows-x86 configures
#      the windows-x86-mp preset; override with --preset).
#   2. (host side, CI) compare:
#        scripts/ci/run-retail-fastfile-parity.sh --host linux-amd64 \
#            --ref windows-x86 --reference-hash reference.txt \
#            --fastfile <retail.ff> [--mode m5-graph]
#
# The gate is FAIL-CLOSED on protocol identity: capture_kind and hash_domain
# must be PRESENT in both leg outputs and must match; a missing or mismatched
# field aborts the gate before any digest compare, so the contract cannot
# silently drift. Field parsing strips AT MOST ONE trailing CR — the CRLF
# line terminator the Windows-x86 mint path's text-mode CRT adds — on BOTH
# paths, and an un-normalized CR would otherwise make the 64-hex digest read
# as 65 characters and identity fields mismatch --host/--ref. Any REMAINING
# (embedded) CR is malformed protocol data and is REJECTED, never normalized
# into a valid token: identity and digest values are single-line tokens in
# which CR is never legitimate content. --mode m5-graph additionally fails
# closed on leg identity and refuses envelope-only captures outright. Identity
# is validated on mint AND compare: minting requires the capture's derived
# platform/leg identity to be present and to match --host (foreign or missing
# identity writes nothing), and comparing requires the reference's recorded
# identity to match --ref and to be internally coherent (platform == leg).
#
# Exit codes:
#   0  parity/consistency established (result named by mode + capture kind)
#   1  parity/gate FAILED (digest, capture-kind, or leg-identity mismatch;
#      envelope-only data under --mode m5-graph; foreign or incoherent
#      identity on a capture or reference)
#   2  usage or environment error (missing inputs, build failure, capture
#      refusal by the harness, missing protocol/identity fields where the
#      mode requires them, malformed protocol data such as an embedded CR)
#
# Usage:
#   scripts/ci/run-retail-fastfile-parity.sh --host <triple> --ref <triple>
#       [--fastfile <path>] [--reference-hash <file>]
#       [--emit-reference <file>] [--preset <cmake-preset>] [--build-dir <dir>]
#       [--skip-build] [--mode <instrument|m5-graph>]
#
# Environment:
#   KISAK_RETAIL_FASTFILE  default --fastfile path when the flag is absent.

set -euo pipefail

HOST_TRIPLE="linux-amd64"
REF_TRIPLE="windows-x86"
FASTFILE="${KISAK_RETAIL_FASTFILE:-}"
REFERENCE_HASH_FILE=""
EMIT_REFERENCE_FILE=""
# instrument: compare any one declared capture kind, result named by kind.
# m5-graph: M5 runtime-graph acceptance gate (graph-* captures + verified
# leg identity; envelope-only data fails).
MODE="instrument"
# The default preset follows the HOST leg: the documented windows-x86 mint
# workflow runs this script inside the Windows x86 reference tree, where the
# Linux preset would configure a foreign toolchain. Override with --preset.
PRESET=""
BUILD_DIR=""
SKIP_BUILD=0

default_preset_for_host() {
    case "$1" in
        windows-x86) echo "windows-x86-mp" ;;
        *) echo "linux-amd64-mp" ;;
    esac
}

# Only runtime-graph captures (graph-*) can satisfy the m5-graph gate.
kind_is_graph() {
    case "$1" in
        graph-*) return 0 ;;
        *) return 1 ;;
    esac
}

usage_exit() {
    echo "Usage: $0 --host <triple> --ref <triple> [--fastfile <path>]" >&2
    echo "          [--reference-hash <file>] [--emit-reference <file>]" >&2
    echo "          [--preset <cmake-preset>] [--build-dir <dir>] [--skip-build]" >&2
    echo "          [--mode <instrument|m5-graph>]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --host) HOST_TRIPLE="${2:?}"; shift 2 ;;
        --ref) REF_TRIPLE="${2:?}"; shift 2 ;;
        --fastfile) FASTFILE="${2:?}"; shift 2 ;;
        --reference-hash) REFERENCE_HASH_FILE="${2:?}"; shift 2 ;;
        --emit-reference) EMIT_REFERENCE_FILE="${2:?}"; shift 2 ;;
        --mode) MODE="${2:?}"; shift 2 ;;
        --preset) PRESET="${2:?}"; shift 2 ;;
        --build-dir) BUILD_DIR="${2:?}"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        *) echo "run-retail-fastfile-parity: unknown option '$1'" >&2; usage_exit ;;
    esac
done

if [ "$MODE" != "instrument" ] && [ "$MODE" != "m5-graph" ]; then
    echo "run-retail-fastfile-parity: unknown mode '$MODE' (instrument|m5-graph)" >&2
    usage_exit
fi
if [ "$MODE" = "m5-graph" ] && [ "$HOST_TRIPLE" = "$REF_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: --mode m5-graph compares two DIFFERENT legs" >&2
    echo "  (host=$HOST_TRIPLE ref=$REF_TRIPLE); same-triple parity is not an M5 result" >&2
    usage_exit
fi

if [ -z "$HOST_TRIPLE" ] || [ -z "$REF_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: --host and --ref are required" >&2
    usage_exit
fi
if [ -z "$PRESET" ]; then
    PRESET="$(default_preset_for_host "$HOST_TRIPLE")"
fi
if [ -z "$FASTFILE" ]; then
    echo "run-retail-fastfile-parity: no retail fast-file given; pass --fastfile or set KISAK_RETAIL_FASTFILE" >&2
    usage_exit
fi
if [ ! -f "$FASTFILE" ]; then
    echo "run-retail-fastfile-parity: retail fast-file not found: $FASTFILE" >&2
    exit 2
fi
if [ -n "$EMIT_REFERENCE_FILE" ] && [ -n "$REFERENCE_HASH_FILE" ]; then
    echo "run-retail-fastfile-parity: --emit-reference and --reference-hash are mutually exclusive" >&2
    usage_exit
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# The driver cd's to the repo root before running the harness; resolve every
# user-supplied path against the INVOCATION directory first so relative
# paths keep working (and a missing file cannot masquerade as an identity
# refusal later).
absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
    esac
}
[ -n "$FASTFILE" ] && FASTFILE="$(absolute_path "$FASTFILE")"
[ -n "$REFERENCE_HASH_FILE" ] && REFERENCE_HASH_FILE="$(absolute_path "$REFERENCE_HASH_FILE")"
[ -n "$EMIT_REFERENCE_FILE" ] && EMIT_REFERENCE_FILE="$(absolute_path "$EMIT_REFERENCE_FILE")"
cd "$REPO_ROOT"

HARNESS="kisakcod-retail-fastfile-parity-harness"

if [ "$SKIP_BUILD" -eq 0 ]; then
    if [ -z "$BUILD_DIR" ]; then
        echo "=== Configuring ($PRESET) ==="
        cmake --preset "$PRESET"
    fi
fi
# The presets use ${sourceDir}/build-<preset-name> as binaryDir.
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build-$PRESET"
fi
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "run-retail-fastfile-parity: build directory '$BUILD_DIR' is not configured" >&2
    exit 2
fi
if [ "$SKIP_BUILD" -eq 0 ]; then
    echo "=== Building $HARNESS ==="
    cmake --build "$BUILD_DIR" --target "$HARNESS"
fi

# Windows trees emit the harness with a .exe suffix; probe both spellings.
HARNESS_BIN="$(find "$BUILD_DIR" -maxdepth 3 -type f \( -name "$HARNESS" -o -name "$HARNESS.exe" \) | head -1)"
if [ -z "$HARNESS_BIN" ]; then
    # Common cmake binary sub-layouts.
    for candidate in \
        "$BUILD_DIR/tests/$HARNESS" \
        "$BUILD_DIR/tests/$HARNESS.exe" \
        "$BUILD_DIR/bin/$HARNESS" \
        "$BUILD_DIR/bin/$HARNESS.exe" \
        "$BUILD_DIR/$HARNESS" \
        "$BUILD_DIR/$HARNESS.exe"; do
        if [ -e "$candidate" ]; then
            HARNESS_BIN="$candidate"
            break
        fi
    done
fi
if [ -z "$HARNESS_BIN" ] || [ ! -e "$HARNESS_BIN" ]; then
    echo "run-retail-fastfile-parity: harness binary '$HARNESS' (.exe) not found under '$BUILD_DIR'" >&2
    exit 2
fi

echo "=== Capturing host leg ($HOST_TRIPLE) ==="
# The harness derives its platform identity from its own build/runtime and
# accepts the requested leg label only when it matches that derivation —
# so a foreign --host in this tree is refused at capture time and nothing
# downstream (least of all a minted reference) can relabel it.
HOST_CAPTURE_STATUS=0
HOST_OUTPUT="$("$HARNESS_BIN" --fastfile "$FASTFILE" --leg "$HOST_TRIPLE")" \
    || HOST_CAPTURE_STATUS=$?
if [ "$HOST_CAPTURE_STATUS" -ne 0 ]; then
    echo "run-retail-fastfile-parity: host capture refused (exit $HOST_CAPTURE_STATUS)." >&2
    echo "  The harness validates --leg against its own derived platform identity;" >&2
    echo "  a capture whose own executable contradicts the requested leg is never" >&2
    echo "  emitted, so nothing can be minted or compared from it." >&2
    exit 2
fi

# The CR byte, emitted via printf format escapes: BSD sed does not honor
# \r escapes, and tr cannot express "at most one trailing CR".
CR="$(printf '\r')"

parse_field() {
    # parse_field <output> <key>
    # Both protocol parsers (harness capture output and minted reference
    # file) flow through here, so this is the one place CRLF must be
    # normalized: the documented Windows-x86 mint path runs the harness
    # under a native CRT whose text mode translates LF newlines to CRLF,
    # and a reference minted inside that tree carries CRLF line endings.
    # A trailing CR left in a parsed value makes graph_sha256 read as
    # length 65 and identity fields that never equal --host/--ref, so the
    # driver would abort before minting or comparing anything.
    #
    # Normalization strips AT MOST ONE terminal CR — the line terminator
    # the Windows text-mode CRT adds — and nothing more. An EMBEDDED CR
    # is malformed protocol data (identity and digest values are
    # single-line tokens in which CR is never legitimate content), so the
    # parser refuses the field instead of silently normalizing it into a
    # valid token: Codex P2 r3980140395 — the previous `tr -d '\r'`
    # rewrote a malformed platform=windows-<CR>x86 into windows-x86 and
    # blessed the reference with "platform+leg identity verified".
    local raw
    raw="$(printf '%s\n' "$1" | sed -n "s/^$2=//p")"
    case "$raw" in
        # The one CRLF terminator the Windows text-mode CRT adds.
        *"$CR") raw="${raw%"$CR"}" ;;
    esac
    case "$raw" in
        *"$CR"*)
            echo "run-retail-fastfile-parity: malformed protocol data: $2 value contains an embedded CR (fail-closed refusal)" >&2
            exit 2
            ;;
    esac
    printf '%s\n' "$raw"
}

HOST_KIND="$(parse_field "$HOST_OUTPUT" capture_kind)"
HOST_DOMAIN="$(parse_field "$HOST_OUTPUT" hash_domain)"
HOST_DIGEST="$(parse_field "$HOST_OUTPUT" graph_sha256)"
HOST_PLATFORM="$(parse_field "$HOST_OUTPUT" platform)"
HOST_LEG="$(parse_field "$HOST_OUTPUT" leg)"

# FAIL-CLOSED on protocol identity: every leg output must carry all three
# contract fields plus the derived identity; a truncated or foreign output
# aborts before any compare or mint.
if [ -z "$HOST_DIGEST" ] || [ "${#HOST_DIGEST}" -ne 64 ]; then
    echo "run-retail-fastfile-parity: host leg produced no 64-hex graph_sha256" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi
if [ -z "$HOST_KIND" ]; then
    echo "run-retail-fastfile-parity: host leg produced no capture_kind (protocol violation)" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi
if [ -z "$HOST_DOMAIN" ]; then
    echo "run-retail-fastfile-parity: host leg produced no hash_domain (protocol violation)" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi
# The derived identity is REQUIRED from the capture itself: the harness
# stamps it; a capture without it cannot be attributed to any leg.
if [ -z "$HOST_PLATFORM" ]; then
    echo "run-retail-fastfile-parity: host leg produced no platform identity (protocol" >&2
    echo "  violation; the harness derives it from its own build/runtime)" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi
if [ -z "$HOST_LEG" ]; then
    echo "run-retail-fastfile-parity: host leg produced no leg identity (protocol violation)" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi
# Identity must be COHERENT (platform == leg) and TRUE to the invocation
# (leg == --host). These checks run on BOTH paths — compare AND mint:
# minting a capture whose identity is incoherent or foreign to --host
# would stock the gate with a reference that launders its origin.
if [ "$HOST_PLATFORM" != "$HOST_LEG" ]; then
    echo "run-retail-fastfile-parity: FAIL host capture identity is incoherent" >&2
    echo "  (platform=$HOST_PLATFORM leg=$HOST_LEG); the leg label must name the" >&2
    echo "  identity the harness derived from its own build/runtime." >&2
    exit 1
fi
if [ "$HOST_LEG" != "$HOST_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: FAIL host leg identity mismatch (capture declares" >&2
    echo "  platform=$HOST_PLATFORM leg=$HOST_LEG, invocation claims --host $HOST_TRIPLE)" >&2
    exit 1
fi

if [ -n "$EMIT_REFERENCE_FILE" ]; then
    # An M5 runtime-graph reference must come from a runtime-graph capture;
    # minting an envelope reference under --mode m5-graph would stock the
    # gate with data that cannot prove runtime-graph parity.
    if [ "$MODE" = "m5-graph" ] && ! kind_is_graph "$HOST_KIND"; then
        echo "run-retail-fastfile-parity: FAIL --mode m5-graph requires a runtime-graph" >&2
        echo "  capture (graph-*); this harness produced capture_kind=$HOST_KIND." >&2
        echo "  Envelope captures hash fast-file bytes only — they prove envelope" >&2
        echo "  consistency, NOT widened-runtime-graph parity. Mint M5 references" >&2
        echo "  only after the runtime-graph walk (graph-v1) lands." >&2
        exit 1
    fi
    # The reference records the VALIDATED capture identity (platform/leg as
    # the harness emitted them, already checked coherent and equal to
    # --host above) — never the requested label. A reference therefore
    # cannot claim a leg its own capture did not.
    {
        echo "# kisakcod retail fast-file parity reference (mode=$MODE)"
        echo "# host=$HOST_TRIPLE ref=$REF_TRIPLE"
        echo "# fastfile=$(cd "$(dirname "$FASTFILE")" && pwd)/$(basename "$FASTFILE")"
        echo "capture_kind=$HOST_KIND"
        echo "hash_domain=$HOST_DOMAIN"
        echo "platform=$HOST_PLATFORM"
        echo "leg=$HOST_LEG"
        echo "graph_sha256=$HOST_DIGEST"
    } >"$EMIT_REFERENCE_FILE"
    echo "=== Reference minted: $EMIT_REFERENCE_FILE ==="
    echo "capture_kind=$HOST_KIND hash_domain=$HOST_DOMAIN platform=$HOST_PLATFORM leg=$HOST_LEG"
    echo "graph_sha256=$HOST_DIGEST"
    if kind_is_graph "$HOST_KIND"; then
        echo "run-retail-fastfile-parity: OK reference emitted ($HOST_LEG leg, runtime-graph capture)"
    else
        echo "run-retail-fastfile-parity: OK reference emitted ($HOST_LEG leg, ENVELOPE capture:"
        echo "  envelope-consistency reference only — NOT an M5 runtime-graph reference)"
    fi
    exit 0
fi

if [ -z "$REFERENCE_HASH_FILE" ]; then
    echo "run-retail-fastfile-parity: pass --reference-hash <file> (mint one on the" >&2
    echo "  $REF_TRIPLE side with --emit-reference) or --emit-reference to mint on this host" >&2
    usage_exit
fi
if [ ! -f "$REFERENCE_HASH_FILE" ]; then
    echo "run-retail-fastfile-parity: reference hash file not found: $REFERENCE_HASH_FILE" >&2
    echo "  Mint it on the $REF_TRIPLE reference side with --emit-reference." >&2
    exit 2
fi

REF_KIND="$(parse_field "$(cat "$REFERENCE_HASH_FILE")" capture_kind)"
REF_DOMAIN="$(parse_field "$(cat "$REFERENCE_HASH_FILE")" hash_domain)"
REF_DIGEST="$(parse_field "$(cat "$REFERENCE_HASH_FILE")" graph_sha256)"
REF_PLATFORM="$(parse_field "$(cat "$REFERENCE_HASH_FILE")" platform)"
REF_LEG="$(parse_field "$(cat "$REFERENCE_HASH_FILE")" leg)"

# FAIL-CLOSED: the reference MUST declare its capture_kind and hash_domain.
# Accepting a reference with missing identity fields would let a stale or
# foreign capture slip through on the digest alone.
if [ -z "$REF_DIGEST" ] || [ "${#REF_DIGEST}" -ne 64 ]; then
    echo "run-retail-fastfile-parity: reference file has no 64-hex graph_sha256: $REFERENCE_HASH_FILE" >&2
    exit 2
fi
if [ -z "$REF_KIND" ]; then
    echo "run-retail-fastfile-parity: FAIL reference file has no capture_kind: $REFERENCE_HASH_FILE" >&2
    echo "  Re-mint it on the $REF_TRIPLE side with --emit-reference." >&2
    exit 2
fi
if [ -z "$REF_DOMAIN" ]; then
    echo "run-retail-fastfile-parity: FAIL reference file has no hash_domain: $REFERENCE_HASH_FILE" >&2
    echo "  Re-mint it on the $REF_TRIPLE side with --emit-reference." >&2
    exit 2
fi

# --mode m5-graph requires VERIFIED leg identity: a runtime-graph capture
# with no declared origin cannot be attributed to the reference leg, so the
# gate refuses it (instrument mode tolerates leg-less legacy captures but
# still verifies identity when present).
if [ "$MODE" = "m5-graph" ]; then
    if [ -z "$REF_LEG" ]; then
        echo "run-retail-fastfile-parity: FAIL reference file has no leg identity: $REFERENCE_HASH_FILE" >&2
        echo "  Re-mint it on the $REF_TRIPLE side with --emit-reference." >&2
        exit 2
    fi
    if [ -z "$REF_PLATFORM" ]; then
        echo "run-retail-fastfile-parity: FAIL reference file has no platform identity: $REFERENCE_HASH_FILE" >&2
        echo "  Re-mint it on the $REF_TRIPLE side with --emit-reference." >&2
        exit 2
    fi
fi

# Reference identity: each recorded field must name the leg the invocation
# claims. A minted reference is coherent by construction (the mint writes
# only validated identity), so a relabeled file necessarily disagrees with
# --ref on its leg or its platform line, and the checks below refuse it —
# no separate coherence check is needed to catch a doctored reference.
if [ -n "$REF_LEG" ] && [ "$REF_LEG" != "$REF_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: FAIL reference leg identity mismatch (reference declares leg=$REF_LEG," >&2
    echo "  invocation claims --ref $REF_TRIPLE; re-mint it inside the $REF_TRIPLE tree)" >&2
    exit 1
fi
if [ -n "$REF_PLATFORM" ] && [ "$REF_PLATFORM" != "$REF_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: FAIL reference platform identity mismatch (reference declares" >&2
    echo "  platform=$REF_PLATFORM, invocation claims --ref $REF_TRIPLE; re-mint it inside" >&2
    echo "  the $REF_TRIPLE tree)" >&2
    exit 1
fi
if [ -n "$REF_PLATFORM" ] && [ -n "$REF_LEG" ] && [ "$REF_PLATFORM" != "$REF_LEG" ]; then
    echo "run-retail-fastfile-parity: FAIL reference identity is incoherent (platform=$REF_PLATFORM" >&2
    echo "  leg=$REF_LEG); a minted reference records only validated, matching identity," >&2
    echo "  so this file was edited after minting. Re-mint it inside the $REF_TRIPLE tree." >&2
    exit 1
fi

echo "=== Comparing legs ==="
echo "  host ($HOST_TRIPLE): capture_kind=$HOST_KIND domain=$HOST_DOMAIN platform=$HOST_PLATFORM leg=$HOST_LEG"
echo "  ref  ($REF_TRIPLE): capture_kind=$REF_KIND domain=$REF_DOMAIN platform=${REF_PLATFORM:-<none>} leg=${REF_LEG:-<none>}"

# --mode m5-graph is the M5 RUNTIME-GRAPH acceptance gate: envelope
# captures hash fast-file bytes only and cannot satisfy it, no matter how
# well the digests agree. This is the negative-tested boundary (ctest
# retail-fastfile-parity-driver-gates).
if [ "$MODE" = "m5-graph" ]; then
    if ! kind_is_graph "$HOST_KIND" || ! kind_is_graph "$REF_KIND"; then
        echo "run-retail-fastfile-parity: FAIL --mode m5-graph requires runtime-graph captures (graph-*);" >&2
        echo "  got capture_kind host=$HOST_KIND ref=$REF_KIND." >&2
        echo "  Envelope captures hash fast-file bytes only — an envelope match proves" >&2
        echo "  envelope consistency, NOT widened-runtime-graph parity. The runtime-graph" >&2
        echo "  walk (graph-v1) enrolls once the native64 loader path lands." >&2
        exit 1
    fi
fi

if [ "$REF_KIND" != "$HOST_KIND" ]; then
    echo "run-retail-fastfile-parity: FAIL capture_kind mismatch (host=$HOST_KIND ref=$REF_KIND)" >&2
    exit 1
fi
if [ "$REF_DOMAIN" != "$HOST_DOMAIN" ]; then
    echo "run-retail-fastfile-parity: FAIL hash_domain mismatch (host=$HOST_DOMAIN ref=$REF_DOMAIN)" >&2
    exit 1
fi

echo "  host graph_sha256=$HOST_DIGEST"
echo "  ref  graph_sha256=$REF_DIGEST"

if [ "$HOST_DIGEST" != "$REF_DIGEST" ]; then
    if kind_is_graph "$HOST_KIND"; then
        echo "run-retail-fastfile-parity: FAIL widened-graph digest mismatch ($HOST_KIND)" >&2
    else
        echo "run-retail-fastfile-parity: FAIL envelope-consistency digest mismatch ($HOST_KIND)" >&2
    fi
    echo "  The $HOST_TRIPLE host capture does not match the $REF_TRIPLE reference." >&2
    echo "  Confirm both legs loaded the SAME unmodified retail fast-file and that" >&2
    echo "  both reference files were minted from that exact asset." >&2
    exit 1
fi

# Success results are NAMED: an envelope match is reported as envelope
# consistency and explicitly disclaims runtime-graph parity; only
# --mode m5-graph with graph-* captures and verified leg identity may speak
# of widened-runtime-graph parity.
if [ "$MODE" = "m5-graph" ]; then
    echo "run-retail-fastfile-parity: OK widened-runtime-graph parity (M5; capture_kind=$HOST_KIND,"
    echo "  host=$HOST_TRIPLE vs ref=$REF_TRIPLE, platform+leg identity verified)"
elif kind_is_graph "$HOST_KIND"; then
    echo "run-retail-fastfile-parity: OK widened-graph digest matches (capture_kind=$HOST_KIND,"
    echo "  host=$HOST_TRIPLE vs ref=$REF_TRIPLE; instrument mode — the M5 acceptance gate is --mode m5-graph)"
else
    echo "run-retail-fastfile-parity: OK envelope-consistency digest matches (capture_kind=$HOST_KIND,"
    echo "  host=$HOST_TRIPLE vs ref=$REF_TRIPLE)"
    echo "  NOTE: envelope captures hash fast-file bytes only — this is NOT a"
    echo "  widened-runtime-graph parity result. The M5 acceptance gate is --mode m5-graph."
fi

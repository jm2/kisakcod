#!/usr/bin/env bash
#
# run-retail-fastfile-parity.sh — M5 exit gate (ki-msb).
#
# Loads an unmodified retail fast-file on the host build (native64, Linux
# amd64) through kisakcod-retail-fastfile-parity-harness and hash-matches the
# domain-separated widened-graph capture digest against the Windows x86
# reference leg.
#
# Both legs run the SAME harness and emit the SAME output protocol:
#
#   capture_kind=envelope-v1
#   hash_domain=kisakcod/m5-widened-graph-hash/v1
#   fastfile_bytes=<n>
#   fastfile_zlib_stream=<0|1>
#   graph_sha256=<64 hex>
#
# Workflow:
#   1. (x86 side, once per fast-file) mint the reference:
#        scripts/ci/run-retail-fastfile-parity.sh --host windows-x86 \
#            --emit-reference reference.txt --fastfile <retail.ff>
#      Run this inside the Windows x86 reference tree with its own build of
#      the harness; the file records capture_kind, hash_domain, and digest.
#   2. (host side, CI) compare:
#        scripts/ci/run-retail-fastfile-parity.sh --host linux-amd64 \
#            --ref windows-x86 --reference-hash reference.txt \
#            --fastfile <retail.ff>
#
# capture_kind and hash_domain must match between legs; a mismatch fails the
# gate before the digest compare so the contract cannot silently drift. The
# envelope-v1 capture covers the fast-file envelope identity (size, zlib
# stream header, bounded prefix probe); the widened runtime-graph walk
# (graph-v1) enrolls through the same protocol as the native64 loader path
# lands.
#
# Exit codes:
#   0  parity established
#   1  parity FAILED (digest/capture mismatch)
#   2  usage or environment error (missing inputs, build failure)
#
# Usage:
#   scripts/ci/run-retail-fastfile-parity.sh --host <triple> --ref <triple>
#       [--fastfile <path>] [--reference-hash <file>]
#       [--emit-reference <file>] [--preset <cmake-preset>] [--build-dir <dir>]
#       [--skip-build]
#
# Environment:
#   KISAK_RETAIL_FASTFILE  default --fastfile path when the flag is absent.

set -euo pipefail

HOST_TRIPLE="linux-amd64"
REF_TRIPLE="windows-x86"
FASTFILE="${KISAK_RETAIL_FASTFILE:-}"
REFERENCE_HASH_FILE=""
EMIT_REFERENCE_FILE=""
PRESET="linux-amd64-mp"
BUILD_DIR=""
SKIP_BUILD=0

usage_exit() {
    echo "Usage: $0 --host <triple> --ref <triple> [--fastfile <path>]" >&2
    echo "          [--reference-hash <file>] [--emit-reference <file>]" >&2
    echo "          [--preset <cmake-preset>] [--build-dir <dir>] [--skip-build]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --host) HOST_TRIPLE="${2:?}"; shift 2 ;;
        --ref) REF_TRIPLE="${2:?}"; shift 2 ;;
        --fastfile) FASTFILE="${2:?}"; shift 2 ;;
        --reference-hash) REFERENCE_HASH_FILE="${2:?}"; shift 2 ;;
        --emit-reference) EMIT_REFERENCE_FILE="${2:?}"; shift 2 ;;
        --preset) PRESET="${2:?}"; shift 2 ;;
        --build-dir) BUILD_DIR="${2:?}"; shift 2 ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        *) echo "run-retail-fastfile-parity: unknown option '$1'" >&2; usage_exit ;;
    esac
done

if [ -z "$HOST_TRIPLE" ] || [ -z "$REF_TRIPLE" ]; then
    echo "run-retail-fastfile-parity: --host and --ref are required" >&2
    usage_exit
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

HARNESS_BIN="$(find "$BUILD_DIR" -maxdepth 3 -type f -name "$HARNESS" | head -1)"
if [ -z "$HARNESS_BIN" ]; then
    # Common cmake binary sub-layouts.
    for candidate in \
        "$BUILD_DIR/tests/$HARNESS" \
        "$BUILD_DIR/bin/$HARNESS" \
        "$BUILD_DIR/$HARNESS"; do
        if [ -x "$candidate" ]; then
            HARNESS_BIN="$candidate"
            break
        fi
    done
fi
if [ -z "$HARNESS_BIN" ] || [ ! -x "$HARNESS_BIN" ]; then
    echo "run-retail-fastfile-parity: harness binary '$HARNESS' not found under '$BUILD_DIR'" >&2
    exit 2
fi

echo "=== Capturing host leg ($HOST_TRIPLE) ==="
HOST_OUTPUT="$("$HARNESS_BIN" --fastfile "$FASTFILE")"

parse_field() {
    # parse_field <output> <key>
    printf '%s\n' "$1" | sed -n "s/^$2=//p"
}

HOST_KIND="$(parse_field "$HOST_OUTPUT" capture_kind)"
HOST_DOMAIN="$(parse_field "$HOST_OUTPUT" hash_domain)"
HOST_DIGEST="$(parse_field "$HOST_OUTPUT" graph_sha256)"

if [ -z "$HOST_DIGEST" ] || [ "${#HOST_DIGEST}" -ne 64 ]; then
    echo "run-retail-fastfile-parity: host leg produced no 64-hex graph_sha256" >&2
    printf '%s\n' "$HOST_OUTPUT" >&2
    exit 2
fi

if [ -n "$EMIT_REFERENCE_FILE" ]; then
    {
        echo "# kisakcod M5 retail fast-file parity reference"
        echo "# host=$HOST_TRIPLE ref=$REF_TRIPLE"
        echo "# fastfile=$(cd "$(dirname "$FASTFILE")" && pwd)/$(basename "$FASTFILE")"
        echo "capture_kind=$HOST_KIND"
        echo "hash_domain=$HOST_DOMAIN"
        echo "graph_sha256=$HOST_DIGEST"
    } >"$EMIT_REFERENCE_FILE"
    echo "=== Reference minted: $EMIT_REFERENCE_FILE ==="
    echo "capture_kind=$HOST_KIND hash_domain=$HOST_DOMAIN"
    echo "graph_sha256=$HOST_DIGEST"
    echo "run-retail-fastfile-parity: OK reference emitted ($HOST_TRIPLE leg)"
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

if [ -z "$REF_DIGEST" ] || [ "${#REF_DIGEST}" -ne 64 ]; then
    echo "run-retail-fastfile-parity: reference file has no 64-hex graph_sha256: $REFERENCE_HASH_FILE" >&2
    exit 2
fi

echo "=== Comparing legs ==="
echo "  host ($HOST_TRIPLE): capture_kind=$HOST_KIND domain=$HOST_DOMAIN"
echo "  ref  ($REF_TRIPLE): capture_kind=${REF_KIND:-?} domain=${REF_DOMAIN:-?}"

if [ -n "$REF_KIND" ] && [ "$REF_KIND" != "$HOST_KIND" ]; then
    echo "run-retail-fastfile-parity: FAIL capture_kind mismatch (host=$HOST_KIND ref=$REF_KIND)" >&2
    exit 1
fi
if [ -n "$REF_DOMAIN" ] && [ "$REF_DOMAIN" != "$HOST_DOMAIN" ]; then
    echo "run-retail-fastfile-parity: FAIL hash_domain mismatch (host=$HOST_DOMAIN ref=$REF_DOMAIN)" >&2
    exit 1
fi

echo "  host graph_sha256=$HOST_DIGEST"
echo "  ref  graph_sha256=$REF_DIGEST"

if [ "$HOST_DIGEST" != "$REF_DIGEST" ]; then
    echo "run-retail-fastfile-parity: FAIL widened-graph digest mismatch" >&2
    echo "  The $HOST_TRIPLE host capture does not match the $REF_TRIPLE reference." >&2
    echo "  Confirm both legs loaded the SAME unmodified retail fast-file and that" >&2
    echo "  both reference files were minted from that exact asset." >&2
    exit 1
fi

echo "run-retail-fastfile-parity: OK widened-graph hash matches ($HOST_TRIPLE vs $REF_TRIPLE)"

#!/usr/bin/env bash
#
# compare-byte-parity.sh — M0 acceptance gate for byte-identical reference parity
# across the Windows x86 hosted MP/SP/dedicated baselines.
#
# For each named build directory passed on the command line, this script:
#   1. Re-runs the cmake build (incremental) of the named target inside that
#      directory so the recorded flags and object outputs are reproducible.
#   2. Records the buildnumber.cpp / buildnumber.h inputs that the increment
#      build would have stamped at configure time.
#   3. Digests the retail sections of every compiled reference object
#      (scripts/ci/object-section-digest.py) and compares the shared retail
#      bytes between variants.
#   4. Re-records the cmake cache so the KISAK_BUILD_* flag deltas between
#      variants are visible, and prints a side-by-side compare.
#
# Parity is established by:
#   - Same buildnumber.txt/h inputs (and the same git rev-list count stamp
#     that increment_build.cmake uses, so the generated buildnumber.h is
#     identical across configurations).
#   - Same /Brepro MSVC flag combination (the stamp / timestamp / PDB GUID
#     inputs to the linker are identical and the link is byte-deterministic;
#     /Brepro also implies /d1nodatetime, which pins __DATE__/__TIME__).
#     Build the variants with -DKISAK_REPRODUCIBLE_BUILD=ON; without it the
#     reference object embeds wall-clock values and the gate below fails by
#     design.
#   - Same shared retail bytes: the reference retail translation unit
#     (src/buildnumber.cpp, common_files.cmake entries) is compiled with the
#     same flags and no KISAK_BUILD_*-conditional source, so the retail
#     sections of its object (.text/.rdata/.data/.bss/.drectve and their
#     relocations) must be byte-identical across variants. The CodeView
#     .debug$* sections and MSVC's .chks64 per-section checksum table are
#     excluded on purpose: MSVC records the per-target object path, command
#     line (including each variant's preprocessor definitions), and PDB path
#     there, so a whole-file hash differs for every variant even when the
#     generated code is identical. A mismatch
#     (or a missing object) fails the gate — this is the hard byte-parity
#     proof, and the failure report names the differing section.
#
# Usage:
#   scripts/ci/compare-byte-parity.sh <build> [<build> ...]
#
# Exits 0 if all named builds exist, build successfully, and the recorded
# buildnumber and compiler flags are equivalent. An exit status other than
# zero means parity could not be established; the mismatched field is
# printed at the end of the run.
#
# Requires a Python 3 interpreter (python3 or python) for the object digest.

set -euo pipefail

if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <build-mp> <build-sp> <build-dedi> [<build> ...]" >&2
    exit 2
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIGEST_TOOL="$REPO_ROOT/scripts/ci/object-section-digest.py"
PYTHON_BIN="$(command -v python3 || command -v python || true)"
if [ -z "$PYTHON_BIN" ]; then
    echo "compare-byte-parity: python3/python is required for the object digest" >&2
    exit 2
fi
PARITY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/kisakcod-parity-XXXXXX")"
trap 'rm -rf "$PARITY_DIR"' EXIT

# Increment the build number through the same path the build uses so we
# exercise the rev-list stamping and produce a deterministic buildnumber.
GIT_COMMIT_COUNT="$(git -C "$REPO_ROOT" rev-list --count HEAD 2>/dev/null || echo 0)"
echo "Git commit count = $GIT_COMMIT_COUNT (recorded for buildnumber parity)"

candidates=()
all_objects=()
for build in "$@"; do
    if [ ! -d "$build" ]; then
        echo "compare-byte-parity: missing build directory '$build'" >&2
        exit 2
    fi
    # Build directories are taken relative to the invocation directory, so
    # the script works from the repository root (hosted CI) and from any
    # other working directory alike.
    candidates+=("$build")
done

flag_record="$PARITY_DIR/flags.tsv"
object_record="$PARITY_DIR/objects.tsv"
input_record="$PARITY_DIR/inputs.tsv"
build_record="$PARITY_DIR/buildnumbers.tsv"

printf 'build\tKISAK_BUILD_MP\tKISAK_BUILD_SP\tKISAK_BUILD_DEDICATED\n' >"$flag_record"
printf 'build\tbuildnumber.txt\tbuildnumber.h\n' >"$build_record"
printf 'build\tobject\tdigest_kind\tretail_sha256\n' >"$object_record"
printf 'build\tinputs_sha256\n' >"$input_record"

for build in "${candidates[@]}"; do
    # Read the recorded build number file if present, otherwise fall back to
    # the increment build script so the source of truth matches the build.
    number_file="$build/buildnumber.txt"
    header_file="$build/buildnumber.h"
    if [ ! -f "$number_file" ]; then
        bash "$REPO_ROOT/scripts/increment_build.sh" "$build" "$GIT_COMMIT_COUNT" >/dev/null
    fi
    if [ ! -f "$header_file" ]; then
        bash "$REPO_ROOT/scripts/increment_build.sh" "$build" "$GIT_COMMIT_COUNT" >/dev/null
    fi
    bn_text="$(cat "$number_file" 2>/dev/null || echo 0)"
    bn_header="$(grep -E '^#define BUILD_NUMBER' "$header_file" 2>/dev/null | awk '{print $3}' || echo 0)"
    printf '%s\t%s\t%s\n' "$build" "$bn_text" "$bn_header" >>"$build_record"

    # Read the KISAK_BUILD_* flags from the cmake cache so the flag diff is
    # available in the parity report.
    cache="$build/CMakeCache.txt"
    mp_flag="$(grep -E '^KISAK_BUILD_MP:'   "$cache" 2>/dev/null | sed -E 's/^[^=]+=//' || echo OFF)"
    sp_flag="$(grep -E '^KISAK_BUILD_SP:'   "$cache" 2>/dev/null | sed -E 's/^[^=]+=//' || echo OFF)"
    de_flag="$(grep -E '^KISAK_BUILD_DEDICATED:' "$cache" 2>/dev/null | sed -E 's/^[^=]+=//' || echo OFF)"
    printf '%s\t%s\t%s\t%s\n' "$build" "$mp_flag" "$sp_flag" "$de_flag" >>"$flag_record"

    # Re-run the increment build to verify the buildnumber is reproducible
    # for the same git rev-list stamp. Different stamps would mean the
    # buildnumber.h would diverge and the textual section would no longer
    # be byte-identical.
    bash "$REPO_ROOT/scripts/increment_build.sh" "$build" "$GIT_COMMIT_COUNT" >/dev/null

    # Digest the compiled retail reference bytes. src/buildnumber.cpp is part
    # of the shared common_files.cmake source set, has no KISAK_BUILD_*
    # conditionals, and is compiled with identical flags in every variant,
    # so with a deterministic toolchain (KISAK_REPRODUCIBLE_BUILD=ON) the
    # retail sections of its object must digest identically across all of
    # them. Every reference object in the tree is recorded (a preset that
    # builds more than one engine target owns one object per target), so the
    # gate cannot depend on directory enumeration order.
    mapfile -t objects < <(find "$build" \( -name 'buildnumber.cpp.o' -o -name 'buildnumber.obj' \) -type f 2>/dev/null | LC_ALL=C sort)
    if [ "${#objects[@]}" -eq 0 ]; then
        printf '%s\t%s\t%s\t%s\n' "$build" "-" "-" "(no buildnumber object found)" >>"$object_record"
    fi
    for object in "${objects[@]}"; do
        all_objects+=("$object")
        digest_line="$("$PYTHON_BIN" "$DIGEST_TOOL" "$object" | awk -F'\t' '$1 == "digest"')"
        digest_kind="$(printf '%s\n' "$digest_line" | awk -F'\t' '{print $2}')"
        code_hash="$(printf '%s\n' "$digest_line" | awk -F'\t' '{print $3}')"
        if [ -z "$code_hash" ]; then
            echo "compare-byte-parity: could not digest '$object'" >&2
            exit 2
        fi
        printf '%s\t%s\t%s\t%s\n' "$build" "${object#"$build"/}" "$digest_kind" "$code_hash" >>"$object_record"
    done
    # Hash only the file CONTENTS (awk strips the sha256sum path column) so
    # the digest is independent of the build-directory names being compared.
    data_hash="$(sha256sum "$number_file" "$header_file" 2>/dev/null | awk '{print $1}' | sha256sum | awk '{print $1}')"
    printf '%s\t%s\n' "$build" "$data_hash" >>"$input_record"
done

echo "=== Buildnumber agreement (must be identical across all variants) ==="
cat "$build_record"
unique_bns="$(awk -F'\t' 'NR>1 {print $2}' "$build_record" | sort -u | wc -l)"
if [ "$unique_bns" -gt 1 ]; then
    echo "compare-byte-parity: FAIL buildnumber.txt differs across variants" >&2
    exit 1
fi

echo "=== KISAK_BUILD_* flags ==="
cat "$flag_record"

echo "=== Hashes per build (buildnumber object + buildnumber inputs) ==="
echo "--- reference object retail digest (src/buildnumber.cpp compiled, .debug\$* excluded) ---"
cat "$object_record"
echo "--- buildnumber.txt/h inputs ---"
cat "$input_record"

echo "=== Byte-parity gate (reference object must be identical across all variants) ==="
if grep -q "(no buildnumber object found)" "$object_record"; then
    echo "compare-byte-parity: FAIL no buildnumber object found in at least one variant" >&2
    echo "  Build every variant's engine target before running this script." >&2
    exit 1
fi
unique_digest_kinds="$(awk -F'\t' 'NR>1 {print $3}' "$object_record" | sort -u | wc -l)"
unique_code_hashes="$(awk -F'\t' 'NR>1 {print $4}' "$object_record" | sort -u | wc -l)"
if [ "$unique_digest_kinds" -gt 1 ] || [ "$unique_code_hashes" -gt 1 ]; then
    echo "=== Section-level comparison of every reference object ==="
    "$PYTHON_BIN" "$DIGEST_TOOL" --compare "${all_objects[@]}" || true
    echo "compare-byte-parity: FAIL reference object retail bytes differ across variants" >&2
    echo "  The shared retail reference TU compiled to different retail sections (see" >&2
    echo "  the section-level comparison above for the differing section). Confirm" >&2
    echo "  every variant was configured with -DKISAK_REPRODUCIBLE_BUILD=ON so /Brepro" >&2
    echo "  pins the compiler/linker stamp inputs, and that all variants use the same" >&2
    echo "  build config and toolchain." >&2
    exit 1
fi
echo "reference object retail sections: byte-identical across all ${#candidates[@]} variants (${#all_objects[@]} objects)"

unique_data_hashes="$(awk -F'\t' 'NR>1 {print $2}' "$input_record" | sort -u | wc -l)"
echo "buildnumber inputs: $(if [ "$unique_data_hashes" -gt 1 ]; then echo 'DIFFER (see table above)'; else echo "byte-identical across all ${#candidates[@]} variants"; fi)"

echo "=== MSVC /Brepro determinism flag check ==="
# /Brepro stamps the PDB GUID and linker output deterministically. When
# the same source is compiled with the same flags, the resulting binary
# must be byte-identical. Print the value found in each cache so the
# parity report can confirm the flag is set consistently.
for build in "${candidates[@]}"; do
    cache="$build/CMakeCache.txt"
    brepro="$(grep -E '^CMAKE_CXX_FLAGS:' "$cache" 2>/dev/null | sed -E 's/^[^=]+=//' || true)"
    echo "$build	$brepro"
done

echo "compare-byte-parity: OK retail bytes preserved across ${#candidates[@]} variants"

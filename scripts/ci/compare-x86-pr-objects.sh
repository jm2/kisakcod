#!/usr/bin/env bash
#
# compare-x86-pr-objects.sh: AGENTS.md rule 16's x86 check for one PR.
#
# Compares every Release object of two /Brepro Windows x86 build trees (the
# PR's base and the PR) and sorts each object into one of:
#   identical      whole file equal
#   metadata-only  file differs, but object-section-digest.py finds the retail
#                  sections (code, data, relocations) equal: only CodeView
#                  debug metadata moved, e.g. a shifted __LINE__ or include
#   layout-only    the digest differs, but `dumpbin /disasm` is identical: a
#                  header edit renumbered sections or symbols in an includer,
#                  which the digest's relocation resolution counts
#   CODE           the disassembly differs (or no dumpbin to check with)
#   added/removed  the object exists in only one tree
#
# Set DUMPBIN to dumpbin.exe for the layout-only check; CI finds it with
# vswhere. Without it, every digest difference is reported as CODE.
#
# Writes a Markdown report to stdout (CI appends it to the job summary).
# Exit 0 when nothing is CODE/added/removed; 1 otherwise. A PR whose body
# declares its x86 change may still merge (rule 16); this job is advisory.
#
# Usage: scripts/ci/compare-x86-pr-objects.sh <base-build-dir> <pr-build-dir>

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <base-build-dir> <pr-build-dir>" >&2
    exit 2
fi
BASE="$1"
PR="$2"
# Section names can hold bytes a Windows console code page can't print.
export PYTHONIOENCODING=utf-8
DIGEST="$(cd "$(dirname "$0")" && pwd)/object-section-digest.py"
# The first interpreter that actually runs: on Windows, python3 can be the
# Microsoft Store alias stub, which fails instead of running.
PYTHON=""
for candidate in python3 python py; do
    if "$candidate" -c "" > /dev/null 2>&1; then
        PYTHON="$candidate"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "No working Python 3 interpreter" >&2
    exit 2
fi

list_objects() {
    (cd "$1" && find scripts -path '*/Release/*' -name '*.obj' ! -name buildnumber.obj | sort)
}

# dumpbin options are dash-style: Git Bash rewrites "/opt" arguments as paths.
# Each function becomes one line and the lines are sorted, because the order
# MSVC emits COMDAT functions in can change without changing any of them.
disasm() {
    "$DUMPBIN" -nologo -disasm:nobytes "$(cygpath -w "$1" 2> /dev/null || echo "$1")" |
        grep -v '^Dump of file' | sed -E 's/^ +[0-9A-F]{8}: *//' |
        awk 'BEGIN { RS = ""; FS = "\n" } { gsub(/\n/, " | "); print }' | LC_ALL=C sort
}

# Fails safe: no dumpbin, a dumpbin error or an empty listing keeps CODE.
same_disasm() {
    [ -n "${DUMPBIN:-}" ] || return 1
    local a b
    a="$(disasm "$1")" || return 1
    b="$(disasm "$2")" || return 1
    [ -n "$a" ] && [ "$a" = "$b" ]
}

identical=0
metadata=()
layout=()
code=()
added=()
removed=()

while IFS= read -r obj; do
    if [ ! -f "$PR/$obj" ]; then
        removed+=("$obj")
    elif cmp -s "$BASE/$obj" "$PR/$obj"; then
        identical=$((identical + 1))
    elif "$PYTHON" "$DIGEST" --compare "$BASE/$obj" "$PR/$obj" > /dev/null; then
        metadata+=("$obj")
    elif same_disasm "$BASE/$obj" "$PR/$obj"; then
        layout+=("$obj")
    else
        code+=("$obj")
    fi
done < <(list_objects "$BASE")

while IFS= read -r obj; do
    [ -f "$BASE/$obj" ] || added+=("$obj")
done < <(list_objects "$PR")

echo "## Windows x86 objects vs base"
echo
echo "identical: $identical · metadata-only: ${#metadata[@]} · layout-only: ${#layout[@]} · CODE: ${#code[@]} · added: ${#added[@]} · removed: ${#removed[@]}"
for kind in code layout metadata added removed; do
    declare -n items="$kind"
    [ "${#items[@]}" -eq 0 ] && continue
    echo
    case "$kind" in metadata) heading=metadata-only ;; layout) heading=layout-only ;; *) heading="$kind" ;; esac
    echo "### $heading"
    for item in "${items[@]}"; do
        echo "- \`$item\`"
    done
done

if [ "${#code[@]}" -ne 0 ] || [ "${#added[@]}" -ne 0 ] || [ "${#removed[@]}" -ne 0 ]; then
    echo
    echo "The PR changes x86 output. Per AGENTS.md rule 16 its body must declare and justify the change."
    exit 1
fi

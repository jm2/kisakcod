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
#   CODE           retail sections differ: the PR changes x86 code
#   added/removed  the object exists in only one tree
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

identical=0
metadata=()
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
    else
        code+=("$obj")
    fi
done < <(list_objects "$BASE")

while IFS= read -r obj; do
    [ -f "$BASE/$obj" ] || added+=("$obj")
done < <(list_objects "$PR")

echo "## Windows x86 objects vs base"
echo
echo "identical: $identical · metadata-only: ${#metadata[@]} · CODE: ${#code[@]} · added: ${#added[@]} · removed: ${#removed[@]}"
for kind in code metadata added removed; do
    declare -n items="$kind"
    [ "${#items[@]}" -eq 0 ] && continue
    echo
    echo "### ${kind/metadata/metadata-only}"
    printf -- '- `%s`\n' "${items[@]}"
done

if [ "${#code[@]}" -ne 0 ] || [ "${#added[@]}" -ne 0 ] || [ "${#removed[@]}" -ne 0 ]; then
    echo
    echo "The PR changes x86 output. Per AGENTS.md rule 16 its body must declare and justify the change."
    exit 1
fi

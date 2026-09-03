#!/usr/bin/env bash
# Aggregate SHA256SUMS builder (CI_RELEASE_WORKFLOW_AUDIT gap 8). Walks the
# downloaded artifact tree, hashes every packaged file, and fails closed when
# two files share a basename but not a SHA-256 (that would silently publish an
# ambiguous sum). Output is a sorted, flat "sha256  basename" file.
#
# Usage: aggregate-checksums.sh <artifact-root> <output-file>
# Fail closed: -e aborts on a failing `sha256sum` (an empty hash field must
# never be emitted), and the `cd` guard keeps the walk inside the artifact
# root even when the directory is missing or unreadable.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: aggregate-checksums.sh <artifact-root> <output-file>" >&2
  exit 2
fi

ROOT="$1"
OUT="$2"

# Resolve OUT against the caller's cwd BEFORE entering ROOT: a relative OUT
# (the workflow passes "SHA256SUMS.txt") must land where the caller expects,
# not inside the artifact root the script walks into below.
case "$OUT" in
  /*) ;;
  *) OUT="$(pwd)/$OUT" ;;
esac

if [ ! -d "$ROOT" ]; then
  echo "aggregate-checksums: artifact root '$ROOT' does not exist" >&2
  exit 1
fi

TMP="$(mktemp)"
FAILLOG="$(mktemp)"
trap 'rm -f "$TMP" "$FAILLOG"' EXIT

status=0
cd "$ROOT" || exit 1
# -print0 | sort -z: deterministic traversal independent of inode order.
while IFS= read -r -d '' f; do
  base="$(basename "$f")"
  [ "$base" = "SHA256SUMS.txt" ] && continue
  hash="$(sha256sum "$f" | awk '{print $1}')"
  if [ -s "$TMP" ] && grep -q "  ${base}\$" "$TMP"; then
    prev="$(grep "  ${base}\$" "$TMP" | awk '{print $1}')"
    if [ "$prev" != "$hash" ]; then
      echo "duplicate name with differing content: $base (at ${f#./})" >> "$FAILLOG"
      status=1
    fi
    # Identical duplicates are fine; the first entry already covers the name.
    continue
  fi
  printf '%s  %s\n' "$hash" "$base" >> "$TMP"
done < <(find . -type f -print0 | sort -z)

if [ "$status" -ne 0 ]; then
  echo "aggregate-checksums: refusing to emit ambiguous sums:" >&2
  cat "$FAILLOG" >&2
  exit 1
fi

sort -k2 "$TMP" > "$OUT"
echo "aggregate-checksums: wrote $(wc -l < "$OUT") entries to $OUT"

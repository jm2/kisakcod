#!/usr/bin/env bash
# Architecture truth gate for POSIX targets (CI_RELEASE_WORKFLOW_AUDIT gap 16):
# a runner that silently fell back to emulation must not upload a binary
# labeled for another architecture. Fails closed unless EVERY named binary
# reports the expected architecture token from `file`.
#
# Usage: verify-artifact-arch.sh <expected-token> <binary> [<binary>...]
#   expected tokens as reported by `file`:
#     linux amd64 -> x86-64     linux arm64 -> aarch64     macOS arm64 -> arm64
set -uo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: verify-artifact-arch.sh <expected-token> <binary> [<binary>...]" >&2
  exit 2
fi

EXPECTED="$1"
shift

status=0
for path in "$@"; do
  if [ ! -f "$path" ]; then
    echo "FAIL: $path does not exist" >&2
    status=1
    continue
  fi
  actual="$(file -b "$path" 2>/dev/null || true)"
  case "$actual" in
    *"$EXPECTED"*)
      echo "ok: $path reports $EXPECTED"
      ;;
    *)
      echo "FAIL: $path does not report '$EXPECTED' (got: ${actual:-unreadable})" >&2
      status=1
      ;;
  esac
done
exit "$status"

# ADR-0008: Engine-TU tests and CI hygiene

Status: accepted
Date: 2026-10-25

## Context

The 2026-10-25 issue sweep fixed engine bugs with tests that link production
TUs. Some of those tests passed vacuously, and some CI failures were noise.

## Decision

- An engine fix ships with a test that runs the production `.cpp`, with
  `WEAK` stubs at its boundary, `-ffunction-sections` and `--gc-sections`
  (`-dead_strip` for a local macOS run). These tests are clang-only, so
  configure with `CC=clang CXX=clang++`: under gcc their targets don't exist.
- A strong definition in a linked TU beats the test's weak stub
  (`g_spawn_mp.cpp`'s `Scr_AddEntity`), so observe the lowest stubbed call and
  check with `nm`. `iassert` compiles out there.
- Prove the test fails on the old code: swap in master's TU. A check that
  passes on master, even under ASan, is dropped.
- A server change that adds a game call needs a weak stub in
  `wire_identity_tests.cpp`, or only the ASan leg fails to link.
- Rerun only these known flakes: the OpenAL TSan
  `StatusAfter(sample, 400) == SMP_DONE` timing check, and the census
  "apt attempt N failed or stalled". Codacy flags `memcpy`/`strlen`
  (CWE-120/126): use `std::copy_n`, four-iterator `std::equal`, `strnlen`
  or `snprintf`, and move function-local statics to file scope.
- GitHub reads mergeability as UNKNOWN just after another merge: re-gate.
  Sibling PRs get their own cmake and test-file anchors; trial-merge them
  in a scratch worktree before pushing. A conflict becomes a
  -v2 branch cherry-picked onto fresh master; check its cmake for a dropped
  `endif()`. Merge with `gh pr merge --match-head-commit <sha>`, never
  chained to a grep.

## Consequences

Each fix carries proof that it fixes something, and a red CI job is either a
real failure or one of the two named flakes.

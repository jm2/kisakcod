# ADR-0009: Retail Steam 1.8 is the behaviour reference

Status: accepted (amended 2026-10-28)
Date: 2026-10-25

## Context

Code that looks wrong is often what retail does. The 2026-10-25 sweep found
four "bugs" that matched retail; #895, #882, #876 and #877 then changed
retail behaviour without reading the exe, and #897-#900 reverted them.

## Decision

- Read the retail 1.8 `iw3mp.exe` before any change that can alter what a
  player, a peer or a script sees ([ADR-0004](0004-reverse-engineering-scope.md)):
  objdump or `dumpbin /disasm`, on a host-local copy. Find the function by
  an `.rdata` string, a dvar global, or a constant (`0x32`, `0x64`).
- Retail does the same: keep it, label the issue `matches-retail`, and
  close the item with the address and instruction. Never quote bytes.
- Ours differs by a decompile error: fix it and cite the address.
- Exempt only fixes that can't change retail-visible behaviour: memory
  safety, invalid input, debug, build, test or port-only code. Name the
  reason.
- Every PR that touches `src/` has a `Retail check:` line with the address
  or the exempt reason. `scripts/ci/pr_gate.py` blocks without one, and
  blocks a PR on a `matches-retail` issue unless it cites a retail address.
- A test pins retail's behaviour, not the fix author's guess.

## Consequences

Fidelity is settled from the exe. Reopening a settled item needs new retail
evidence.

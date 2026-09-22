# ADR-0003: Testing gates and interim vehicles

Status: accepted
Date: 2026-09-22

## Context

After 890 commits the fork has no native 64-bit target that configures, and
the x86 build has never run on retail data. Plans required the full release
matrix before any live test, so nothing was ever tested live. Progress was
reported as ledgers and source-text tests instead.

## Decision

- Gates G0-G6 are defined in [ROADMAP.md](../ROADMAP.md). Each is a live
  test on real binaries with evidence a reviewer can re-run.
- These vehicles count as test milestones, never as deliverables:
  - Wine/Proton + DXVK for the x86 build on Linux
  - Windows-on-ARM x86 emulation
  - CrossOver on macOS (headless only)
  - a Win64 client on the existing D3D9 renderer
  - dxvk-native on Linux and macOS
  - optionally, a 32-bit Linux headless server, only if the POSIX layer is
    ready well before the loader
- The full Steam 1.8 matrix on all five targets is the release gate (G6), not
  a precondition for testing.

## Consequences

- Fork-peer evidence counts for G3, never G4; x86 evidence counts for G4a,
  never G4b ([CHARTER.md](../CHARTER.md)).
- Wire work (G4a) starts on x86 in parallel with the loader (G2).
- Vehicle code that only serves a vehicle is time-boxed and deleted when the
  real path lands.

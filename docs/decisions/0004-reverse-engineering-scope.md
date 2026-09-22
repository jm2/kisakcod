# ADR-0004: Reverse-engineering scope

Status: accepted
Date: 2026-09-22

## Context

The fork is a 1.0 decompile; Steam 1.8 differs in at least the protocol
number and the handshake. Unbounded reverse engineering (RE) of later exes
would re-open the whole game. Transliterated code would also raise licensing
and review problems.

## Decision

- RE is allowed only in service of Steam 1.8 wire compatibility.
- Capture first. RE is only for behaviour that shows up in captures and that
  decoding and diffing can't explain.
- RE is allowed on the Steam 1.8 `iw3mp.exe`. The 1.7 exe may be used only as
  a diff aid, to locate a change.
- Each RE task is its own bead, naming the capture and the bytes it can't
  explain.
- The output is a spec note in [NET_STEAM18.md](../design/NET_STEAM18.md) plus
  a test fixture, never transliterated code.
- Out of scope:
  - PunkBuster
  - 1.1-1.7 gameplay or balance changes that are invisible on the wire and in
    the prediction check
  - CoD4x protocol 21
  - anti-cheat
  - UI and menus
  - single-player

## Consequences

- Beads that start with "RE" and no capture are rejected by the reviewer.
- A `bg_*` gameplay delta is in scope only if the prediction check finds it.

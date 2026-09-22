# ADR-0001: Steam 1.8 is the only network reference

Status: accepted
Date: 2026-09-22

## Context

The code is the CoD4 1.0 PC decompile and speaks protocol 1, inherited from
upstream. The Steam release's 2018 exe-only update reports `1.8.13620`;
community sources put it at protocol 7 against retail 1.7's protocol 6, with
unchanged data files. Two commercial profiles doubled the evidence burden,
and nothing was ever captured for either.

## Decision

- The only network reference is the original Steam 1.8 release, `1.8.13620`.
  Its protocol number is confirmed by capture, not assumed.
- Retail 1.7 is no longer a required profile. Its exe may be used offline as a
  diff aid only.
- The release gate (G6) needs both directions: a native KisakCOD client joins
  an unmodified Steam 1.8 server, and an unmodified Steam 1.8 client joins a
  KisakCOD server.

## Consequences

- `tests/net_capture_certification.cpp` requires one profile, `steam-1.8`.
- The server gets a per-instance protocol profile with `steam18` as the
  default ([NET_STEAM18.md](../design/NET_STEAM18.md)).
- G4a runs on the x86 build as soon as the owner's captures exist; G4b repeats
  it on every native server.
- CoD4x servers and clients are not references ([ADR-0005](0005-cod4x-compatibility.md)).

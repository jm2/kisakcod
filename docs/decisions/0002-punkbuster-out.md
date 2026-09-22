# ADR-0002: PunkBuster is out

Status: accepted
Date: 2026-09-22

## Context

The decompile carries PunkBuster (PB) hooks, and every `Pb*` call site is
commented out. PB is proprietary, and whether Steam 1.8 still uses it is
unconfirmed. Emulating it would mean reverse engineering anti-cheat, which
[ADR-0004](0004-reverse-engineering-scope.md) excludes.

## Decision

- No PunkBuster emulation and no PB authorize path.
- The commented-out PB hooks stay dead. Don't delete them in bulk (that is
  churn), and don't revive them.
- If a Steam 1.8 capture shows PB traffic is required to join, that finding
  goes to the owner as a blocker. It is not solved by emulation.

## Consequences

- The `getinfo` `pb` key is kept or dropped to match what the Steam 1.8
  capture shows, and nothing more.
- Security findings about PB code paths are out of scope.

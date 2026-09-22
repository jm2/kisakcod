# Charter

Owner: operator. Changes need an ADR (`decisions/`).

## Goal

Ship KisakCOD as a native 64-bit Call of Duty 4 multiplayer client and
headless dedicated server on five desktop targets. It reads unmodified retail
content and plays on the wire with the original Steam 1.8 release in both
directions. Progress is measured only by code that compiles, links, runs and
plays (the KPIs in [NOW.md](NOW.md)) and by the testing gates in
[ROADMAP.md](ROADMAP.md).

## Targets and roles

| Target | MP client | Headless server | Notes |
| --- | --- | --- | --- |
| Windows amd64 | required | required | First 64-bit target (G1) |
| Windows ARM64 | required | required | Same sources as Win64 |
| Linux amd64 | required | required | First POSIX target |
| Linux arm64 | required | required | Same sources as Linux amd64 |
| macOS arm64 | required | required | Signed and notarized at G6 |
| Windows x86 | baseline | baseline | Keeps building; not a release target |

Single-player is deferred. The cell levels live in
[capability/manifest.json](capability/manifest.json) (KPI K6).

## Network reference

The only reference is the original Steam 1.8 release, `1.8.13620`
([ADR-0001](decisions/0001-network-reference-steam-1-8.md)). Release needs
both directions: a native KisakCOD client joins an unmodified Steam 1.8
server, and an unmodified Steam 1.8 client joins a KisakCOD server. Retail
1.7 is not a required profile; its exe may be used only as a diff aid.

## Retail content

- Retail `.ff` files are read unmodified, through disk32 mirrors plus
  load-time widening ([design/FASTFILE_LOADER.md](design/FASTFILE_LOADER.md)).
- No content is rebaked. Derived caches (shaders) are content-addressed
  sidecars; the originals stay untouched ([design/CLIENT.md](design/CLIENT.md)).
- Retail data never enters the repository or CI artifacts.

## Fixed decisions

- Native 64-bit on all five targets above.
- Roles: MP client plus headless dedicated server.
- Release renderer: Vulkan, with MoltenVK on macOS.
- Native clients use OpenAL Soft and FFmpeg instead of Miles and Bink.
- Speex stays, gated on decoder interop with Steam 1.8 peers rather than
  encoder byte equality.
- The Windows x86 baseline keeps building.

## Non-goals

- PunkBuster ([ADR-0002](decisions/0002-punkbuster-out.md)).
- CoD4x protocol or auth, pending [ADR-0005](decisions/0005-cod4x-compatibility.md).
- Single-player.
- Retail 1.7 as a required profile.
- Rebaked or converted content.
- Fork-only auth (Steam ticket or `cl_guid`) as the default handshake.
- Wine, Proton or CrossOver as a deliverable. They are test vehicles
  ([ADR-0003](decisions/0003-testing-gates-and-vehicles.md)).

## Gate evidence

A gate passes only on evidence a reviewer can re-run or inspect.

- Fork-peer evidence (a KisakCOD client with a KisakCOD server) counts for G3,
  never for G4.
- Windows x86 evidence counts for G4a, never for G4b.
- Evidence from a vehicle (Wine, emulation, the Win64 D3D9 client) counts as a
  test milestone, never as delivery.
- "Blocked", "pending" or "not run" is never a pass.
- Licensed evidence records the exe hash and the depot manifest, never the
  data itself.

## KPIs

K1-K3 are defined in [design/NATIVE64.md](design/NATIVE64.md), K4 in
[design/FASTFILE_LOADER.md](design/FASTFILE_LOADER.md) and K5 in
[design/PLATFORM_POSIX.md](design/PLATFORM_POSIX.md).

K6 is the number of required target x role cells at or above each gate level:
`compiles`, `links`, `boots_map`, `fork_peer`, `steam18_peer`, `packaged`. It
comes from `capability/manifest.json`, which changes only with gate evidence.

The CI `native64-census` and `KPI summary` jobs render the current values.

## Decisions

| ADR | Decision | Status |
| --- | --- | --- |
| [0001](decisions/0001-network-reference-steam-1-8.md) | Steam 1.8 is the only network reference | accepted |
| [0002](decisions/0002-punkbuster-out.md) | PunkBuster is out | accepted |
| [0003](decisions/0003-testing-gates-and-vehicles.md) | Gates G0-G6 and interim test vehicles | accepted |
| [0004](decisions/0004-reverse-engineering-scope.md) | Reverse engineering only for Steam 1.8 wire gaps | accepted |
| [0005](decisions/0005-cod4x-compatibility.md) | CoD4x compatibility | proposed |
| [0006](decisions/0006-docs-and-process-policy.md) | Doc set, budgets and agent rules | accepted |

## Doc map

`scripts/ci/docs_budget.py` enforces the budgets; `docs/` totals 100 KB or
less. Nothing else lives in `docs/`.

| Doc | Budget | Owner | Purpose |
| --- | --- | --- | --- |
| `AGENTS.md` (root) | 6 KB | operator | Operating rules |
| `CHARTER.md` | 8 KB | operator | This file |
| `ROADMAP.md` | 15 KB | operator | Gates, vehicles, workstreams |
| `NOW.md` | 6 KB | mayor | KPIs, queue, owner blockers |
| `decisions/NNNN-*.md` | 2 KB each | operator | ADRs |
| `design/NATIVE64.md` | 15 KB | WS-1/WS-3 | Layouts, hazards, K1-K3 |
| `design/FASTFILE_LOADER.md` | 12 KB | WS-2 | Two-layout loader, K4 |
| `design/NET_STEAM18.md` | 12 KB | WS-5 | Steam 1.8 wire, captures |
| `design/PLATFORM_POSIX.md` | 8 KB | WS-4 | POSIX headless platform, K5 |
| `design/DETERMINISM.md` | 5 KB | WS-3 | RNG, floating point, comparisons |
| `design/CLIENT.md` | 10 KB | WS-6 | Client route, shaders, audio, voice |
| `UPSTREAM.md` | 3 KB | operator | Upstream sync policy |
| `ARCHIVE.md` | 1 KB | operator | Where the old docs went |
| `capability/manifest.json` | 8 KB | operator | K6 cells |

Workstream-owned design docs are edited by beads in that workstream; the
reviewer checks them against the budget and the no-history rule.

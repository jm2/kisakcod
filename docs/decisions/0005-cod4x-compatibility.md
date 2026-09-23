# ADR-0005: CoD4x compatibility

Status: proposed (decide after G4a)
Date: 2026-09-22

## Context

Almost all public CoD4 play is on CoD4x (protocol 21). Its servers reply
`challengeResponse <ch> <clch> 0 xproto 18`, and they redirect any client at
protocol 7 or lower to `cod4update.cod4x.ovh:27953`, which pushes the CoD4x
installer. CoD4x clients join plain protocol-6 servers in legacy mode and
can't see protocol-7-only servers. Snapshot 2026-09-22:

| Source | Figure |
| --- | --- |
| SteamCharts, app 7940 | 410 playing now; 30-day avg 239, peak 550 (incl. SP) |
| cod4xtracker.com | 464 servers; 613 humans plus 2,430 bots |
| varq.net | 153 servers, 374 players; ~25 servers / 33 players "1.7a" |
| Populated unmodded Steam 1.8 servers | about 0 ("1.8" on trackers means CoD4x) |

CoD4x_Server is AGPLv3: reading it is fine, copied code brings the network
source obligation. The CoD4x client isn't free software; copy nothing.

## Options

- **A (current):** Steam 1.8 only.
- **B-cheap:** also serve stock protocol 6 so CoD4x clients join in legacy
  mode. The server protocol is already a per-instance profile, so this is a
  `proto6` flag, not a redesign.

In every option the KisakCOD client refuses the CoD4x auto-update: on
`xproto` in a `challengeResponse` it downloads and runs nothing.

## Data that settles it

- A 14-day UDP census of both masters by protocol and `xproto`/
  `shortversion`, humans and bots counted separately.
- Whether protocol 7 differs from 6 beyond the number (from G4a captures).
- The number of `sv_noauth` servers.
- The CoD4x team's stance on third-party clients.

# NOW

Last reviewed: 2026-10-24 by kisakcod-mac (mbp16-m1max), acting as mayor at the owner's request
Current gates: G2 (the formal proof) and G5 (native clients); G4a waits on the owner's captures; G0 and G1 are done
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 23/25 (`db_graphhash`, 3 legs; list in `IsK4Verified`) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 37 | K4 digest pipeline: per-family `db::graph_hash` walkers on x86 and 64-bit | G2 | K4 | a family's 64-bit graph hash equals its x86 hash on the owner's data; its fail-closed guard is then removed | L | 23/25 with GfxWorld (#736); FX write-up #739; left: Fx/ImpactFx walkers (win-arm64) |
| 53 | 1.8 gameplay parity: executable-side 1.1-1.8 changes inventoried; pmove and weapon state match the 1.8 reference server (#692) | G4a | gate | #692's done-test | L | started: imports (linux-arm64), per-frame parity (linux); scope questions in #692 |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | Linux amd64 passes all 4 retail pairings (dedi and listen, both ways; reconnect, map change); left: the windows-x86 leg (win-arm64, yoga) |

Other `burndown` issues are off these gates or wait on the owner.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Steamworks legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- Retail data is on every agent host, for local manual runs only; never in CI or the repo. The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x; subagent limits are AGENTS.md rule 23 (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows through vcpkg, 2026-10-22.
- Version 0.1.0 is the first display version (#688); 64-bit builds load unverified asset families by default, `+set db_unverified64 0` restores the guard (#689); the developer autoexec runs only with `+set com_devAutoexec 1` (#693), 2026-10-22.
- A declared x86 change that matches the other builds is pre-approved (rule 16 ESCALATE); x86-only divergence, Windows-only refactors (rule 10) and security fixes still go to the owner, 2026-10-24.

## Done since last review

Merge SHAs are in `git log`.

- Row 47 (G3 first target): an x86 client played a round, two map changes and a reconnect on the Win64 headless server with 8 test clients, on antec-win.
- 1.8 parity: official IWDs only, and 1.8's fatal error with none (#735, #742). 64-bit fixes #738, #743, #744, #747, #749. openal-soft pinned to 1.24.3 for a Windows arm64 hang (#746, #745).
- Row 51: OpenAL Soft audio on every client (#669-#679); 3D start offset in ms (#682); Linux TSan leg (#681). Fixes #714 (pure checksums), #715 (advisory published).
- Row 52, the map sweep: all 21 stock MP maps load and rotate in one 64-bit ASan dedicated server (#709-#712). LAN join, reconnect and map_restart pass on all five 64-bit platforms (#695-#697, #707).
- K4 23/25: RawFile through GfxWorld (#633-#736; headers #644, #656, #659; renderer families and LoadedSound headless-only). Per-symbol x86 identity check (#647).
- G2 soak (row 45) passes on lin64 and arm64 (`scripts/g2_run.py`, #636; #639).
- G5: all five 64-bit clients in-game (rows 44, 49); cinematics through FFmpeg (row 41, #654, #660); screenshot regression set (row 50, #673); Linux client exports no symbols (#655).
- G0 passed (row 46, #178): the x86 client and headless server played 3 maps of rounds on antec-win, then the same binaries ran under Wine on antec (#635, #637).

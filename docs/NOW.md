# NOW

Last reviewed: 2026-10-18 by the coordinator session on antec, acting as mayor (host order under Decided)
Current gates: G2 (the formal proof) and G5 (native clients); G4a waits on the owner's captures; G1 is done
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 0/25 verified (with `db_unverified64`, every client and server zone loads; no digest pipeline yet) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 37 | K4 digest pipeline: per-family `db::graph_hash` walkers on x86 and 64-bit | G2 | K4 | a family's 64-bit graph hash equals its x86 hash on the owner's data; its fail-closed guard is then removed | L | queued |
| 45 | G2 run: the 64-bit headless server under ASan/UBSan boots `mp_shipment` (`db_unverified64`), answers `getinfo`/`getstatus`, rotates the 5 G2 maps, runs 30 min | G2 | gate | console text and a `getstatus` reply recorded; no sanitizer report | M | queued |
| 44 | Linux amd64 client in-game on Steam 1.8 data | G5 | gate | `+devmap mp_crash` on master runs in-game frames with no error | M | queued |
| 41 | Native client cinematics: Bink is a silent stub on 64-bit; play `.bik` through FFmpeg ([CLIENT](design/CLIENT.md)) | G5 | gate | a retail intro cinematic plays on a 64-bit client | L | queued |
| 46 | G0 evidence: x86 client and headless server on Steam 1.8 data, 3 maps, one fork round, the same binaries under Wine | G0 | gate | console text per map and at round end, on antec-win and under Wine on antec | M | queued |
| 48 | macOS client: build dxvk-native for the macOS 26 target the client claims | G5 | gate | `vtool -show-build` reads minos 26.0 on KisakCOD-mp and the dxvk dylibs; no link warning | S | in progress (kisakcod-mac) |
| 49 | Windows ARM64 client in-game on Steam 1.8 data (CI already links it) | G5 | gate | `+devmap mp_shipment` on master runs in-game frames with no error on yoga-slim7x | M | queued |
| 47 | G3 first target: an x86 fork client plays a round, a map change and a reconnect on the Win64 headless server, with 8 test clients | G3 | K6 | the round's console text on both ends | L | after 45 |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | blocked-owner |

Other `burndown` issues are off these gates or wait on the owner.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Miles/Bink legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- Retail data is on every agent host, for local manual runs only; never in CI or the repo. The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x; subagent limits are AGENTS.md rule 23 (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).

## Done since last review

Merge SHAs are in `git log`.

- G5 clients on Steam 1.8 data: Win64 in-game on mp_shipment at about 188 fps (rows 35, 43; #626); macOS in-game on mp_shipment and mp_crash (row 34); Linux arm64 in-game on mp_crash (row 31; #621); Linux amd64 draws its main menu (row 33). Under dxvk-native, screenshots read the backbuffer before `Present` and resolve MSAA (#624).
- Census scope (row 42, #623); AGENTS.md rules 16-23 (#602); x86 identity CI job (#603); `scripts/ci/pr_gate.py` (#604).
- Row 40 (macOS menu at ~60 fps) closed as retail behaviour (`SCR_ShouldSkipUpdateScreen`).

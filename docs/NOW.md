# NOW

Last reviewed: 2026-10-20 by the coordinator session on antec, acting as mayor (host order under Decided)
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
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 14/25 (`db_graphhash` on x86, Win64, lin64 ASan; families in FASTFILE_LOADER) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 37 | K4 digest pipeline: per-family `db::graph_hash` walkers on x86 and 64-bit | G2 | K4 | a family's 64-bit graph hash equals its x86 hash on the owner's data; its fail-closed guard is then removed | L | in progress: Sound, XAnimParts, Image, Material, TechniqueSet, Weapon, XModel, Menu, ClipMapPvs (+XModelPieces), GfxWorld, Fx/ImpactFx |
| 45 | G2 run: the 64-bit headless server under ASan/UBSan boots `mp_shipment` (`db_unverified64`), answers `getinfo`/`getstatus`, rotates the 5 G2 maps, runs 30 min | G2 | gate | console text and a `getstatus` reply recorded; no sanitizer report | M | lin64 passes with #639 (UBSan found the profiling overflow); arm64 rerun on master pending |
| 41 | Native client cinematics: Bink is a silent stub on 64-bit; play `.bik` through FFmpeg ([CLIENT](design/CLIENT.md)) | G5 | gate | a retail intro cinematic plays on a 64-bit client | L | queued |
| 46 | G0 evidence: x86 client and headless server on Steam 1.8 data, 3 maps, one fork round, the same binaries under Wine | G0 | gate | console text per map and at round end, on antec-win and under Wine on antec | M | blocked-owner (antec-win display; antec `wine-common`); x86 server runs under Wine (#635); no-Steam client artifact (#637) |
| 51 | Native client audio: Miles is a silent stub on 64-bit; play sound aliases through OpenAL Soft ([CLIENT](design/CLIENT.md)) | G5 | gate | in-game weapon and ambient sounds play on a 64-bit client with no sound errors | L | queued (kisakcod-mac, after 41) |
| 50 | Screenshot regression set for the native clients (`scripts/screenshot_set.py`, after openETQW's): fixed views per map, master vs PR on one host, per-view diff numbers; images stay on the host | G5 | gate | main vs main on mp_crash and mp_shipment prints near-zero diffs on one native client; a deliberate render change shows up in the numbers | S | in progress (win-arm64) |
| 47 | G3 first target: an x86 fork client plays a round, a map change and a reconnect on the Win64 headless server, with 8 test clients | G3 | K6 | the round's console text on both ends | L | pre-check (arm64 server, 8 test clients) on macmini |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | blocked-owner |

Other `burndown` issues are off these gates or wait on the owner.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Miles/Bink legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| A display for the x86 client on antec-win (`tscon` or an RDP session); `wine-common` on antec | 46, G0 |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- Retail data is on every agent host, for local manual runs only; never in CI or the repo. The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x; subagent limits are AGENTS.md rule 23 (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).

## Done since last review

Merge SHAs are in `git log`.

- K4 14/25 (#633, #640, #642, #643, #646, #648, #650, #653, #657; headers #644, #656, #659). FFmpeg cinematics (#654). G2 run on lin64 with #639; harness `scripts/g2_run.py` (#636 pending). Windows ARM64 client in-game on mp_shipment, row 49 (#632, #634). Wine root fix (#635); no-Steam x86 nightly artifact (#637).
- G5 clients on Steam 1.8 data: Win64 in-game on mp_shipment at about 188 fps (rows 35, 43; #626); macOS in-game on mp_shipment and mp_crash (row 34); Linux arm64 in-game on mp_crash (row 31; #621); Linux amd64 in-game on mp_crash at about 250 fps with no code change (rows 33, 44).
- macOS dxvk dylibs for macOS 26 (row 48, #629); census scope (row 42); rules 16-23, identity job, pr_gate (#602-#604).

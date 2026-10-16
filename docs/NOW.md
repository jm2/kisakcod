# NOW

Last reviewed: 2026-10-16 by the coordinator session on antec, acting as mayor (host order under Decided)
Current gates: G5 (Linux and macOS client launch) and G4a (stock Steam 1.8 clients join); G1 and the G1 queue are done
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · lin64 272/276 · a64 272/276 (before #623 dropped 4 POSIX client files from the headless set) | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 0/25 verified (with the `db_unverified64` opt-in, every client and server zone loads; no digest pipeline yet) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | not re-measured this review | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe yet) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 31 | Linux arm64 client in-game on Steam 1.8 data | G5 | gate | `+devmap mp_crash` on master runs in-game frames with no error | M | done (#621): in-game on mp_crash at about 225 main-loop frames/s; macOS verified too |
| 33 | Linux amd64 client draws its main menu on Steam 1.8 data | G5 | gate | a TGA `screenshot` sent at the menu is mostly non-black ([CLIENT](design/CLIENT.md) bring-up) | M | done: the menu capture is 93% non-black; earlier black captures fired during zone loading (`+wait` runs while loading) |
| 34 | macOS client in-game (jm2/dxvk on KosmicKrisp) | G5 | gate | `+devmap mp_shipment` on master runs in-game | L | done: in-game on master with no patches, about 120 fps, zones in under 1 s |
| 41 | Native client cinematics: Bink is a silent stub on 64-bit; play `.bik` through FFmpeg ([CLIENT](design/CLIENT.md)) | G5 | gate | a retail intro cinematic plays on a 64-bit client | L | queued |
| 42 | Census scope: the headless K1 set includes 4 POSIX client files that need SDL3 and dxvk-native | G1 | K1 | K1 reads 100% on lin64 and a64 | S | done (#623) |
| 35 | Win64 client in-game on Steam 1.8 data | G4b | gate | `+devmap mp_shipment` runs in-game | M | done: in-game on mp_shipment on the antec-win console session (capture 99.7% non-black) |
| 43 | Win64 in-game at ~40 fps: cgame ~25 ms/frame (macOS ~120 fps); check whether a profiling build or dvar is on | G5 | gate | Win64 in-game FPS within ~25% of macOS on the same map | S | queued (win) |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects to the fork's x86 server under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | queued |
| 37 | K4 digest pipeline: per-family `db::graph_hash` walkers on x86 and 64-bit | G2 | K4 | a family's 64-bit graph hash equals its x86 hash on the owner's data; its fail-closed guard is then removed | L | queued |

Other `burndown` issues are off these gates or wait on the owner. Future bead: a total-order Huffman tie-break ([DETERMINISM](design/DETERMINISM.md)).

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Miles/Bink legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner since last review

- `steam18` auth policy: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07. Landed in #532.
- Retail data is on every agent host for local manual runs only; never in CI or the repo.
- Steam 1.8 reference: app 7940 public build 2737681 (built 2018-05-01); `iw3mp.exe` 1.8, SHA-256 b8e1a973…92e3aacf; Windows depots 7941 (manifest 485990305878906444), 7942 (645298708590566320), 7967 (6005281766938853887); English depots 7944, 7945, 7946, 7962.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x (2026-10-15).
- Subagents: DeepSeek/Xiaomi sessions up to four at once; other models one agent plus a no-context review pass (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).

## Done since last review

Merge SHAs are in `git log`.

- 2026-09-29 queue: rows 9-14, 17 and 20-29 (#313, #316, #318, #320, #321, #324, #328-#330, #333, #338, #340, #341, #344, #345, #372, #386).
- Retail load stops: #475, #478, #479, #497, #498, #512, #513, #518. x86 load path in CI: #522.
- G4a wire identity: #531, #532. 64-bit D3DX replacement: #520, #537, #539, #541.
- G5 client launch: #543-#557, #559, #562, #567-#569, #571-#573, #576, #581, #586-#588, #590, #592, #593, #596, #598, #597, #610-#617, #621.
- 2026-10-15 backlog: #591, #600, #601; docs #605, #607, #609, #618, #619.
- Row 40 (macOS menu at ~60 fps): closed as retail behaviour; `SCR_ShouldSkipUpdateScreen` redraws menus at most every 16 ms until the client is active.

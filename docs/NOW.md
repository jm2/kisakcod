# NOW

Last reviewed: 2026-10-15 by kisakcod-linux (coordinator and mayor, AGENTS.md host order)
Current gates: G5 (Linux and macOS client launch) and G4a (stock Steam 1.8 clients join); G1 and the G1 queue are done
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · lin64 272/276 · a64 272/276 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 0/25 verified (with the `db_unverified64` opt-in, every client and server zone loads; no digest pipeline yet) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | not re-measured this review | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe yet) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 31 | Linux arm64 client in-game on Steam 1.8 data | G5 | gate | `+devmap mp_crash` on master runs in-game frames with no error | M | in progress (linux-arm64): GPU detection #581; renders mp_crash; its last crash is fixed by #616, so re-run on master |
| 33 | Linux amd64 client draws its main menu on Steam 1.8 data | G5 | gate | gdb reads `UI_GetActiveMenu(0)` = 1 and a TGA `screenshot` taken after the menu is up is mostly non-black ([CLIENT](design/CLIENT.md) bring-up) | M | in progress (antec): zones load, menu active at about 230 fps, but captures are black even with a magenta clear |
| 34 | macOS client in-game (jm2/dxvk on KosmicKrisp) | G5 | gate | `+devmap mp_shipment` on master runs in-game | L | done: in-game on master with no patches, about 120 fps, zones in under 1 s |
| 40 | macOS main menu capped near 60 fps (FIFO-only present on KosmicKrisp) | G5 | gate | the main menu exceeds 60 fps with `com_maxfps 0` | S | queued (mac) |
| 41 | Native client cinematics: Bink is a silent stub on 64-bit; play `.bik` through FFmpeg ([CLIENT](design/CLIENT.md)) | G5 | gate | a retail intro cinematic plays on a 64-bit client | L | queued |
| 35 | Win64 client main-menu run on Steam 1.8 data | G4b | gate | the owner's manual run reaches the main menu; evidence as text | M | blocked (needs an active console session on antec-win) |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects to the fork's x86 server under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | queued |
| 37 | K4 digest pipeline: per-family `db::graph_hash` walkers on x86 and 64-bit | G2 | K4 | a family's 64-bit graph hash equals its x86 hash on the owner's data; its fail-closed guard is then removed | L | queued |

Other `burndown` issues are off these gates or wait on the owner. Future bead: a total-order Huffman tie-break ([DETERMINISM](design/DETERMINISM.md)).

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| An active desktop session on antec-win for the Win64 client run | 35, G4b |
| Hash the Steam 1.8 `iw3mp.exe`; record the depot manifest | G0, G4a |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Miles/Bink legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner since last review

- `steam18` auth policy: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07. Landed in #532.
- Retail data is on every agent host for local manual runs only; never in CI or the repo.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x (2026-10-15).
- Subagents: DeepSeek/Xiaomi sessions up to four at once; other models one agent plus a no-context review pass (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).

## Done since last review

Merge SHAs are in `git log`.

- 2026-09-29 queue: rows 9-14, 17 and 20-29 (#313, #316, #318, #320, #321, #324, #328-#330, #333, #338, #340, #341, #344, #345, #372, #386).
- Retail load stops: #475, #478, #479, #497, #498, #512, #513, #518. x86 load path in CI: #522.
- G4a wire identity: #531, #532. 64-bit D3DX replacement: #520, #537, #539, #541.
- G5 client launch: #543-#557, #559, #562, #567-#569, #571-#573, #576, #581, #586-#588, #590, #592, #593, #596, #598, #597, #610-#617.
- 2026-10-15 backlog: #591, #600, #601; docs #605, #607, #609, #618, #619.

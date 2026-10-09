# NOW

Last reviewed: 2026-10-09 by kisakcod-linux, acting as mayor at the owner's direction (coordinator: kisakcod-win)
Current gates: G5 (Linux and macOS client launch) and G4a (stock Steam 1.8 clients join); G1 and the G1 queue are done
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census at 30bd69bc. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 272/272 · lin64 271/271 · a64 271/271 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 79/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 0/25 verified (with the `db_unverified64` opt-in, all families load and mp_crash runs on macOS arm64; no digest pipeline yet) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | not re-measured this review | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | not re-measured this review | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 30 | POSIX client SDL3 entry point (`main`, `Sys_GetEvent`, clipboard) | G5 | gate | the Linux client builds with `KISAK_EXPERIMENTAL_POSIX_CLIENT` and `KISAK_CLIENT_SDL3` and starts | S | done (#567, d0322d4c) |
| 31 | POSIX client GPU detection; `r_init`/`r_screenshot` SDL paths | G5 | gate | every renderer TU compiles in the Linux client build; the client creates a dxvk-native device | M | in progress (linux-arm64): window, dxvk device and GPU detection work; the arm64 client reaches the main menu on Steam 1.8 data (300 menus, about 190 fps) |
| 32 | POSIX client line-neutral renderer/UI fixes; D3DX9 subset definitions | G5 | gate | the Linux client links | M | done (#565, #582) |
| 33 | Linux amd64 windowed client run to the main menu on Steam 1.8 data | G5 | gate | the owner's manual run (agent on the owner's machine) reaches the main menu; evidence as text | M | in progress (antec): all five base zones load and the client idles cleanly; menu not yet seen |
| 38 | disk32 renderer hooks for client builds: the 64-bit converters don't call `Load_BuildVertexDecl`, the shader creates, `Load_Texture`, `Load_PicmipWater` or `Load_VertexBuffer`; add them | G5 | gate | the client loads the code_post_gfx and localized zones to the main menu on Steam 1.8 data | M | in progress (linux-arm64): techniqueset #586 merged; image and water #590 in review; GfxWorld VBs next |
| 34 | macOS client: jm2/dxvk MoltenVK fork (upstream doitsujin/dxvk#5962, #5963) | G5 | gate | the macOS client creates a device | L | in progress (mac) |
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
| Miles/Bink legal decision; release packaging (#263, #290) | WS-8, G6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner since last review

- `steam18` auth policy: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07. Landed in #532 (950f8f91).
- Retail data is on every agent host for local manual runs only; never in CI or the repo.
- Coordination and merges: kisakcod-win, 2026-10-09.

## Done since last review

Merge SHAs are in `git log`.

- 2026-09-29 queue: rows 9-14, 17 and 20-29 (#313, #316, #318, #320, #321, #324, #328-#330, #333, #338, #340, #341, #344, #345, #372, #386).
- Retail load stops: #475, #478, #479, #497, #498, #512, #513, #518. x86 load path in CI: #522.
- G4a wire identity: #531, #532. 64-bit D3DX replacement: #520, #537, #539, #541.
- G5 client launch: #543-#557, #559, #562, #567-#569, #571-#573, #576.

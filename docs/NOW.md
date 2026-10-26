# NOW

Last reviewed: 2026-10-26 by kisakcod-linux (antec), coordinator, after #824/#827; 4 open PRs
Current gates: G3 (the remaining targets) and G5 (Windows ARM64, UBSan); G4a waits on the owner; G0, G1 and G2 are done
WIP limit: 6 (at least 4 on the current gates)

The coordinator keeps this file current after every merge or gate change ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 25/25 (`db_graphhash`, 3 legs; list in `IsK4Verified`) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10, all headless: Win64 at `fork_peer`, Linux amd64 at `boots_map` (the clients play but have no CI leg or release recipe) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 61 | G3 on the Windows ARM64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS incl. 8 bots/rotations (win-arm64); client #817 |
| 63 | G3 on the Linux amd64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS at 26cf1841 per #827; client spectator #817 |
| 64 | G3 on the Linux arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS (linux-arm64); client #817 |
| 65 | G3 on the macOS arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | host LAN/unsandboxed launch blocked; client #817 |
| 60 | G5 Windows ARM64: the native client plays a round, a map change and a reconnect on a fork server (#184) | G5 | gate | console text on both ends; ASan clean | M | presets/ASan link at 2e3ba835 (#815); client #817 |
| 58 | G5 client UBSan-clean: fix the triaged client reports (flattened arrays, misaligned access, LCG overflow) | G5 | gate | a client leg logs 0 UBSan reports | M | #819/#822/#823 merged; #821 owner layout call |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | yoga: discovery, connect and 2 map changes pass; left: browser, spawn and fire, reconnect (#182) |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | 34 MB `.app` validated at 26cf1841; clean-account LAN remains |

Other `burndown` issues are off these gates or wait on the owner.

**Resume here:** #824 docs, #825 #818, #826 #816, #828 #827; #817/#821 and owner data block the gate rows. [#804](https://github.com/jm2/kisakcod/issues/804) holds host detail.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) | 36, G4a |
| Steamworks legal decision; release packaging (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve CHARTER/ADRs | all |
| Private vulnerability reporting (#293) | SECURITY.md |
| Retail-fidelity call on #208 | the sweep |
| GPU runner; Apple Developer ID | G5; G6 |
| #817 spawn route: dev/test spawn, UI-less class selection, or menu vehicle | rows 61/63/64/65 clients, 60, 36 |
| #821 shadow-arena layout semantics | row 58 |
| LAN/unsandboxed launch on mbp16 | row 65 |
| Clean-account K6 round on mbp16 | row 66 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host of antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x (2026-10-15).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows through vcpkg, 2026-10-22.
- Version 0.1.0 is the first display version (#688); 64-bit builds load unverified asset families by default, `+set db_unverified64 0` restores the guard (#689); the developer autoexec runs only with `+set com_devAutoexec 1` (#693), 2026-10-22.
- 0.1.0 ships the D3D9 and dxvk-native clients; native Vulkan (#713) comes after 0.1.0 ([ADR-0010](decisions/0010-interim-renderers-ship-in-0-1-0.md)), 2026-10-25.
- Standing rules (x86 pre-approval, sanitizer bar, vulnerabilities, retail data, shared hosts) are in [CHARTER](CHARTER.md#standing-owner-rules).

## Done since last review

Merge SHAs are in `git log`.

- G0-G2 passed (rows 46, 54, 37; #178-#180); G3's Win64 target passes (row 47).
- G5 interim clients pass (rows 55-57, 59); 1.8 parity and 21-map rotation pass (rows 52-53).
- 2026-10-26: #815 ARM64 presets/ASan, #819/#822/#823 row 58, #812/#813 row 66 bundles, #824 docs ownership, #827 test-client map change.

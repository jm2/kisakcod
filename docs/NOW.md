# NOW

Last reviewed: 2026-10-29 by the coordinator
Gates: G3 (row 65), G5; G4a waits on owner; G0-G2 done
WIP limit: 6 (bug lane first, then gates)

## KPIs

From the master census; live values in the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 25/25 (`db_graphhash`, 3 legs) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10, all headless: Win64 at `fork_peer`, Linux amd64 at `boots_map` | 10 at `packaged` (G6) | manifest |

## Bug lane (before the gate rows)

Owner, 2026-10-27: bugs first. Rule-4 PRs with a test that fails without
the fix and a `Retail check:` line (ADR-0009); new crash and memory bugs
join by rank.

Drained 2026-10-29: #206 #208 #209 #214 #220 #243 #274 #287 closed (#905,
#908-#913). Left: #214 item 5 (owner); SP-only items in #195 #204 (iw3sp.exe
unreadable); #270 parked until FASTFILE_LOADER; #278 playerstate and client
deltas; #914 (17 dxvk-gated tests never run in CI).

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 65 | G3 on the macOS arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server up on mbp16 :28973; macOS firewall drops UDP to the ad-hoc-signed dedi (owner allows it); client from yoga |
| 67 | G5: a Win64 D3D9 client plays on a Steam 1.8 server (#184) | G5 | gate | console text on both ends | M | not started |
| 68 | G5: the macOS dxvk-native client plays a LAN round | G5 | gate | console text on both ends | M | needs an unlocked Mac (deferred); bundle 52bbbe70 |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under `steam18`; capture confirms protocol 7, gamename, shortversion (#531) | M | yoga: discovery, connect, 2 map changes pass; left: browser, spawn, fire, reconnect (#182) |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | licenses #907, OpenAL URL #906; clean-account round left |

Done 2026-10-29 (evidence on #181/#184): G3 rows 61, 63, 64; G5 rows 58
(0 UBSan/ASan), 60 (Windows ARM64 client via D3D9On12).

**Resume here:** row 65 once mbp16's firewall allows the dedi. Host: #804.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Allow `KisakCOD-dedi` in mbp16's firewall | 65 |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)); G4a steps at yoga's screen | 36 |
| Steamworks legal decision; release packaging (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve CHARTER/ADRs | all |
| Private vulnerability reporting (#293) | SECURITY.md |
| GPU runner; Apple Developer ID | G5; G6 |
| An unlocked Mac (clean account) | 66, 68 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host, antec → antec-win → mbp16 → macmini → yoga (2026-10-15).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows via vcpkg.
- 0.1.0 is the first display version (#688); 64-bit builds load unverified families by default (#689); dev autoexec needs `+set com_devAutoexec 1` (#693).
- 0.1.0 ships the D3D9 and dxvk-native clients; native Vulkan (#713) comes after ([ADR-0010](decisions/0010-interim-renderers-ship-in-0-1-0.md)), 2026-10-25.
- Standing rules (x86 pre-approval, sanitizer bar, vulnerabilities, retail data, shared hosts) are in [CHARTER](CHARTER.md#standing-owner-rules).

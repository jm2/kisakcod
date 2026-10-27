# NOW

Last reviewed: 2026-10-27 by the owner
Gates: G3, G5; G4a waits on owner; G0-G2 done
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

Owner, 2026-10-27: bugs before gate work. Rows 1-3 high, rest medium. Fix in
rule-4 PRs, each with a test that fails without the fix; new crash or memory bugs join by severity.

| # | Issue | Status |
| --- | --- | --- |
| 1 | #238 ODE aborts: full 4096-contact group, live asserts | merged d4e17dc7 (#880) |
| 2 | #243 physics: near-callback context, contacts, stepper | items 2-3: 3a3514e0, 0d48c880 |
| 3 | #214 game_mp vehicles: wheel overflow, strings, heli NULLs | 1-4: 9ab79d3e; 5 blocked-owner |
| 4 | #260 dynent pieces never destroyed; stale bodies on map change | merged 6b97554f (#876) |
| 5 | #258 game: hintStrings, vehicle table overflow, heli events | merged dc92ed86 (#874) |
| 6 | #219 xanim: blend nodes eval first child twice; buf_cursor | merged ef205cbb (#878) |
| 7 | #209 qcommon/server: FS_FileTell NULL, fake-lag, voice-mute | items 2-3: 13c71759; item 1 deferred |
| 8 | #198 bounds: GSC compiler, sound-alias, menu, map-name | fixed by #808; closed |
| 9 | #195 decompiler artifacts write wrong object | items 2-3 merged b8b5a73f (#883); rest sized |
| 10 | #206 #205 #204 #217 #220 #200 #207 #208 #230 | #205 fbe0e8d2 · #204 ae235688 · #206-C1 43de3a4d · #230 e5dd5771 · #220-3 7830f12a · #217-3 d7ba9119 · #208-2/3 667a06ae; #206 C2 next |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 61 | G3 on the Windows ARM64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS 8/8/8 + kick refused (#828); rerun needs Windows sync |
| 63 | G3 on the Linux amd64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | PASS at 6a2ac8de; death and reconnect not exercised |
| 64 | G3 on the Linux arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS (linux-arm64) |
| 65 | G3 on the macOS arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS b063637b (8/8/8); client blocked: no display |
| 60 | G5 Windows ARM64: the native client plays a round, a map change and a reconnect on a fork server (#184) | G5 | gate | console text on both ends; ASan clean | M | presets/ASan link at 2e3ba835 (#815) |
| 58 | G5 client UBSan-clean: fix triaged reports (flattened arrays, misaligned access, LCG overflow) | G5 | gate | a client leg logs 0 UBSan reports | M | fixes through #884; close leg 0 UBSan/0 ASan; #884 after-leg 0, no new site |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | yoga: discovery, connect, 2 map changes pass; left: browser, spawn, fire, reconnect (#182) |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | 34 MB `.app` validated at 26cf1841; clean-account LAN remains |

Other `burndown` issues wait below the bug lane.

**Resume here:** bug lane row 10 (#206 C2 next). Host: #804.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) | 36, G4a |
| Steamworks legal decision; release packaging (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve CHARTER/ADRs | all |
| Private vulnerability reporting (#293) | SECURITY.md |
| Retail-fidelity call on #208 | the sweep |
| GPU runner; Apple Developer ID | G5; G6 |
| Sync into the Windows clones (yoga 2e3ba835, antec-win 26cf1841) | rows 61/63 post-#828 legs |
| macOS host with an attached display | row 65 client half |
| Clean-account K6 round on mbp16 | row 66 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host, antec → antec-win → mbp16 → macmini → yoga (2026-10-15).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows via vcpkg.
- 0.1.0 is the first display version (#688); 64-bit builds load unverified families by default (#689); dev autoexec needs `+set com_devAutoexec 1` (#693).
- 0.1.0 ships the D3D9 and dxvk-native clients; native Vulkan (#713) comes after ([ADR-0010](decisions/0010-interim-renderers-ship-in-0-1-0.md)), 2026-10-25.
- Standing rules (x86 pre-approval, sanitizer bar, vulnerabilities, retail data, shared hosts) are in [CHARTER](CHARTER.md#standing-owner-rules).

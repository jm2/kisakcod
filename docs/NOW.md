# NOW

Last reviewed: 2026-10-27 by kisakcod-mac (mbp16), coordinator; 6 open PRs (1 draft)
Current gates: G3 and G5; G4a waits on the owner; G0-G2 done
WIP limit: 6 (at least 4 on the current gates)

The coordinator keeps this current after every merge or gate change.

## KPIs

From the master census; live values in the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 25/25 (`db_graphhash`, 3 legs; list in `IsK4Verified`) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10, all headless: Win64 at `fork_peer`, Linux amd64 at `boots_map` | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 61 | G3 on the Windows ARM64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS 8/8/8 + kick refused (#828, win-arm64); citable rerun needs the Windows sync; client #846 |
| 63 | G3 on the Linux amd64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS per #827; #828 gives 8/8/8 and refused kick on x86; client #846 |
| 64 | G3 on the Linux arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | server PASS (linux-arm64); client #846 |
| 65 | G3 on the macOS arm64 headless server, as row 47 | G3 | K6 | the round's console text on both ends | M | dedi preset verified; run needs an owner LAN host; client #846 |
| 60 | G5 Windows ARM64: the native client plays a round, a map change and a reconnect on a fork server (#184) | G5 | gate | console text on both ends; ASan clean | M | presets/ASan link at 2e3ba835 (#815); client #846 |
| 58 | G5 client UBSan-clean: fix the triaged client reports (flattened arrays, misaligned access, LCG overflow) | G5 | gate | a client leg logs 0 UBSan reports | M | #819/#822/#823/#830/#845/#839 merged; leg at 177f0c50: 0 ASan, 43 UBSan; #849 clears 27 of the physics 40 |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | yoga: discovery, connect and 2 map changes pass; left: browser, spawn and fire, reconnect (#182) |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | 34 MB `.app` validated at 26cf1841; clean-account LAN remains |

Other `burndown` issues are off these gates or wait on the owner.

**Resume here:** #824 #834 #840 #842 #845 #828 #830 #839 #846 and this docs PR merged. Merge next: #849, then #832 #844 #843 #826; retrigger by close/reopen, two CI branches at a time. Launch rows 61-65 with `+exec cfg +map`, never `map` in an exec'd cfg (#816/#826), plus `+set cg_autoJoin 1` on a scripted client (#846, merged). [#804](https://github.com/jm2/kisakcod/issues/804) holds host detail.

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
| Push the `ci.yml` fold (no token has `workflow` scope) | one macOS runner per run |
| LAN/unsandboxed launch on mbp16 | row 65 |
| Clean-account K6 round on mbp16 | row 66 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host, antec → antec-win → mbp16 → macmini → yoga (2026-10-15).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows via vcpkg.
- 0.1.0 is the first display version (#688); 64-bit builds load unverified families by default (`+set db_unverified64 0` restores the guard, #689); the dev autoexec needs `+set com_devAutoexec 1` (#693).
- 0.1.0 ships the D3D9 and dxvk-native clients; native Vulkan (#713) comes after ([ADR-0010](decisions/0010-interim-renderers-ship-in-0-1-0.md)), 2026-10-25.
- Standing rules (x86 pre-approval, sanitizer bar, vulnerabilities, retail data, shared hosts) are in [CHARTER](CHARTER.md#standing-owner-rules).

## Done since last review

Merge SHAs are in `git log`.

- G0-G2 passed (rows 46, 54, 37; #178-#180); G3's Win64 target passes (row 47).
- 2026-10-26: #815 (Windows ARM64 presets, ASan), #825 (#818 intro skip), #840 (`pr_gate` INFRA; #838 diagnosed as runner capacity).
- G5 interim clients pass (rows 55-57, 59); 1.8 parity and 21-map rotation pass (rows 52-53).
- 2026-10-26 merged: #815 ARM64 presets/ASan, #812/#813 row 66 bundles, #819/#822/#823 row 58, #825/#818 intro-cinematic connstate.

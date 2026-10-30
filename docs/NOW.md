# NOW

Last reviewed: 2026-10-29 by the coordinator
Gates: G6 (0.1.0 packaging, rows 71-74); G0-G3, G4a and G5 done; G4b open (#183)
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
| K6 | Target x role cells at `links` or above | 5/10, all headless at `fork_peer` (G3); clients rise with their row 74 legs | 10 at `packaged` (G6) | manifest |

## Bug lane (before the gate rows)

Owner, 2026-10-27: bugs first. Rule-4 PRs with a test that fails without
the fix and a `Retail check:` line (ADR-0009); new crash and memory bugs
join by rank.

Drained 2026-10-29. Left: SP-only #195 #204; #270 (parked); #278 deltas;
#914 (dxvk-gated tests not in CI).

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 69 | Alt+Enter fullscreen at desktop size (#917) | G6 | K6 | window, fullscreen, window on each OS | S | #923 merged; macOS check waits on an unlocked mbp16 |
| 71 | Windows amd64/arm64 installer and `.zip` (ADR-0012) | G6 | K6 | the ADR-0012 done-test | M | #931 merged; console checks wait on tscon |
| 73 | macOS arm64 `.dmg` (ADR-0011, ADR-0012; extends 66) | G6 | K6 | as 66, from the `.dmg` | L | #925, #926 merged; `.dmg` PR next (kisakcod-mac) |
| 74 | `release.yml` builds rows 71-73's artifacts and `SHA256SUMS` | G6 | K6 | a dry run yields all 5 targets' artifacts | M | Linux legs: antec; Windows: kisakcod-win; macOS: after 73 |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | clean-account round left |

Done 2026-10-29: rows 67 (#921), 68 (#922), 70 (#927), 72 (#929, #930); G3 (#181), G4a (#182), G5 (#184).

**Resume here:** rows 71-74 (0.1.0 packaging, ADR-0012); 69 waits on the macOS check. Host: #804.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steamworks legal decision; release packaging (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve CHARTER/ADRs | all |
| GPU runner; Apple Developer ID | G5; G6 |
| An unlocked Mac (clean account) | 66 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host, antec → antec-win → mbp16 → macmini → yoga (2026-10-15).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows via vcpkg.
- 0.1.0 is the first display version (#688); 64-bit builds load unverified families by default (#689); dev autoexec needs `+set com_devAutoexec 1` (#693).
- 0.1.0 ships the D3D9 and dxvk-native clients; native Vulkan (#713) comes after ([ADR-0010](decisions/0010-interim-renderers-ship-in-0-1-0.md)), 2026-10-25.
- Standing rules (x86 pre-approval, sanitizer bar, vulnerabilities, retail data, shared hosts) are in [CHARTER](CHARTER.md#standing-owner-rules).

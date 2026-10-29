# NOW

Last reviewed: 2026-10-29 by the coordinator
Gates: G6 (0.1.0 packaging, rows 70-74); G0-G5 and G4a done
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

Drained 2026-10-29 via #905 and #908-#913: closed #206 #208 #209 #220 and
issues #243 #274 #287, and #214 (#920; item 5 kept as retail by the owner).
Left: SP-only items in #195 #204 (iw3sp.exe unreadable); #270 parked until FASTFILE_LOADER; #278 playerstate and client
deltas; #914 (17 dxvk-gated tests never run in CI).

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 69 | Alt+Enter toggles fullscreen at the desktop resolution on every fork client (#917) | G6 | K6 | windowed to fullscreen at desktop size and back, outside developer mode, on each OS | S | #923: Windows x86/ARM64 and Linux pass; macOS waits on an unlocked mbp16 |
| 70 | Packaging paths ([ADR-0012](decisions/0012-release-packages-and-install-layout.md)): a writable per-user path (`~/.config/kisakcod` on Linux) is the default `fs_homepath` in packaged builds; profiles and zones are found there as well as in the game folder | G6 | K6 | a read-only game folder: the client keeps its profile and config in the per-user path, and loads zones placed only there | M | coordinator |
| 71 | Windows amd64/arm64: Inno Setup installer and portable `.zip`, client plus dedi (ADR-0012) | G6 | K6 | install, data beside it, play LAN as a normal user, uninstall; the same from the unpacked zip | M | kisakcod-win |
| 72 | Linux amd64/arm64: `.deb`, `.rpm`, Arch package into `/opt/kisakcod`, and a portable `.tar.xz` (ADR-0012) | G6 | K6 | install, data in `~/.config/kisakcod`, `kisakcod` launcher plays LAN; uninstall leaves user state | M | coordinator |
| 73 | macOS arm64: `.dmg` with `KisakCOD.app` and the dedi (ADR-0011, ADR-0012; extends 66) | G6 | K6 | as 66, from the mounted `.dmg` | L | kisakcod-mac |
| 74 | `release.yml` builds the client legs and every row 71-73 artifact, with `SHA256SUMS` and notes (ADR-0012) | G6 | K6 | a dry-run release produces all 5 targets' artifacts | M | after 70-73 |
| 66 | G6: the macOS `.app` bundles its runtime ([ADR-0011](decisions/0011-macos-client-bundles-its-runtime.md)) | G6 | K6 | a clean account without Homebrew plays a LAN round; `otool -L` shows no `/opt/homebrew` | L | licenses #907, OpenAL URL #906; clean-account round left |

Done 2026-10-29: rows 67 (#921), 68 (#922); G3 rows 61, 63, 64, 65 (#181; every G3 target now has
evidence); G4a row 36 (#182: the owner joined a stock client from the LAN
browser, spawned, fired, reconnected); G5 closed (#184).

**Resume here:** rows 70-74 (0.1.0 packaging, ADR-0012); 69 merges with #923. Host: #804.

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

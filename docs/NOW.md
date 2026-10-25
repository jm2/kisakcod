# NOW

Last reviewed: 2026-10-25 by kisakcod-mac (mbp16-m1max), acting as mayor, after the owner's issue sweep; sessions parked
Current gates: G3 (the remaining targets) and G5 (Windows ARM64, Vulkan); G4a waits on the owner; G0 and G2 are done, G1 waits on the owner to close
WIP limit: 6 (at least 4 on the current gates)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 273/273 · winarm64 273/273 · lin64 272/272 · a64 272/272 · mac64 273/273 | all (G1) | census |
| K2 | 64-bit headless real link | Win64: linked, 0 TUs excluded, 0 undefined | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 82/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan, with `db::graph_hash` parity | 25/25 (`db_graphhash`, 3 legs; list in `IsK4Verified`) | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 0 on lin64 and a64 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 5/10 (all headless; the Windows clients link in CI but have no release recipe) | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks. `#n` is a GitHub PR or issue. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 61 | G3: the x86 fork client plays a round, a map change and a reconnect with 8 bots on the Windows ARM64, Linux amd64/arm64 and macOS arm64 headless servers | G3 | K6 | the round's console text on both ends, per target | M | next |
| 62 | G5 Vulkan: a plan for the renderer that replaces D3D9 and dxvk (#713) | G5 | gate | the plan in [CLIENT](design/CLIENT.md), owner-approved | M | later |
| 60 | G5 Windows ARM64: the native client plays a round, a map change and a reconnect on a fork server (#184) | G5 | gate | console text on both ends; ASan clean | M | next (win-arm64) |
| 58 | G5 client UBSan-clean: fix the triaged client reports (flattened arrays, misaligned access, LCG overflow) | G5 | gate | a client leg logs 0 UBSan reports | M | started: shared-code sites (linux-arm64) |
| 36 | G4a evidence: a stock Steam 1.8 client joins the windows-x86 server | G4a | gate | a retail client connects under the `steam18` profile; capture confirms protocol 7, gamename, shortversion (#531 table) | M | blocked-owner: the yoga leg needs UAC to write the CD key under HKLM; Linux amd64 passes all 4 retail pairings |

Other `burndown` issues are off these gates or wait on the owner.

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)) to confirm the `steam18` values | 36, G4a |
| Steamworks legal decision; release packaging, including the 64-bit client release recipe (#263, #290) | WS-8, G6, K6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Close G1 (#179): its exit evidence holds | G1 |
| Turn on private vulnerability reporting (#293) | SECURITY.md |
| Retail-fidelity call on #208 (should Z_Malloc out-of-memory be fatal again); #189's question | the sweep |
| UAC on yoga for the CD key | 36 |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Decided by the owner

- `steam18` auth: option A, the CD-key hash accepted unchecked as the ban key ([NET_STEAM18](design/NET_STEAM18.md) §8), 2026-10-07.
- Retail data is on every agent host, for local manual runs only; never in CI or the repo. The Steam 1.8 reference build is in [NET_STEAM18](design/NET_STEAM18.md) §5.
- Coordination and merges: the first responsive host in order antec, antec-win, mbp16-m1max, macmini-m1, yoga-slim7x; subagent limits are AGENTS.md rule 23 (2026-10-15).
- `design/CLIENT.md` budget 15 KB (ADR-0006 amendment, #609).
- Miles and Bink are dropped on every target, x86 included: OpenAL Soft and FFmpeg everywhere, Windows through vcpkg, 2026-10-22.
- Version 0.1.0 is the first display version (#688); 64-bit builds load unverified asset families by default, `+set db_unverified64 0` restores the guard (#689); the developer autoexec runs only with `+set com_devAutoexec 1` (#693), 2026-10-22.
- A declared x86 change that matches the other builds is pre-approved (rule 16 ESCALATE); x86-only divergence, Windows-only refactors (rule 10) and security fixes still go to the owner, 2026-10-24.
- G5 client legs pass with 0 ASan reports and every UBSan report triaged on #184; row 58 makes the client UBSan-clean, 2026-10-24.

## Done since last review

Merge SHAs are in `git log`.

- Gates: G0 passed (row 46, #178). G2 passed (row 54, #180): K4 25/25 (row 37), guard on, 30 min under ASan/UBSan on Linux amd64 and ASan on Win64.
- G3's first target (row 47): an x86 client played a round, map changes and a reconnect on the Win64 server with 8 bots.
- G5's interim clients: Win64 D3D9 (row 55) and the Linux and macOS dxvk-native clients (rows 56, 57) play on a fork server and on a Steam 1.8 server (row 59), ASan clean (#184). Audio and cinematics go through OpenAL Soft and FFmpeg (rows 41, 51).
- 1.8 parity (row 53, #692): imports done, no dvar missing, per-frame play matches retail. All 21 maps rotate under ASan, and LAN play passes on five platforms (row 52).
- Owner's issue sweep, 2026-10-25: 31 PRs merged and 20 issues closed; each other open issue has a dated triage comment.

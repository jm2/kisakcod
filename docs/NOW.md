# NOW

Last reviewed: 2026-09-29 by gastown.mayor
Current gate: G1 (owner actions pending for G0 and G4a)
WIP limit: 6 (at least 4 on the current gate)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census at 4b2897d0; row 17 re-baselines K1. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 112/243 · lin64 103/236 · a64 103/236 | all (G1) | census |
| K2 | 64-bit headless real link | none; Win64 probe: 55 undefined, 4 TUs excluded | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 8/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan | 0/25 | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 103 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 0/10 | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks; headless unless noted. `#n` is a GitHub issue whose checklist is the bead's scope. Row numbers are stable IDs (design docs cite them); table order is priority. A bead copies its done-test into `now.done_test`. Finish 17 and 26 rework; start 29, then 21; feed in the rest in order.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 17 | ki-iv549 · Census fidelity: `_DEBUG`, no `-fdelayed-template-parsing`, error-by-default diagnostics on (#291, #226 flags) | G1 | K1 | the census runs with the new flags and shows the GSC table errors | S | rework (r1) |
| 23 | ki-afvhu · MSVC-compat macros and intrinsics: `ARRAYSIZE`, `_isnan`, `_BitScanReverse` (#218), `_time64`, `_TRUNCATE` | G1 | K1 | no error on these names; the `_BitScanReverse` comment and test state MSVC's actual contract | S | in review |
| 25 | ki-jddnx · `IsValidSeed` into `ui_shared.h`; `BigShort` (#231) | G1 | K1 | no error on these names | S | in review |
| 26 | ki-7xgop · Contract tests for the portable `sys_local` split (rule-4 follow-up to row 3) | G1 | K1 | ctest green including `sys-local-portable-contracts` at the PR head; the declaration pins compile and the runtime round-trips pass | S | rework (r1) |
| 27 | ki-xxg0x · Shared-file Win32 leftovers: `assertive.cpp`, `com_playerprofile.cpp`, `profile.cpp`/`timing.cpp`, `com_files.cpp`, `q_parse.cpp` `va_list` (game_mp stays row 10), `win_input.h`, `db_registry.cpp` | G1 | K1 | no error on these names | M | in progress |
| 28 | ki-ip7mg · Headless seam: `IDirect3D*` in `gfx_d3d/r_init.h`, `database/db_load.cpp` | G1 (Linux) | K1 | no error on these names | M | in review |
| 29 | ki-p1t3u · `phys_ode.cpp` 64-bit cast cleanup (K1/K2 regression from #310's WARN list) | G1 | K1, K2 | census: K1 244/214/213/241 and K2 real link linked on win64+lin64 | S | ready |
| 9 | ki-l7lly · Engine-owned MSVC-compatible RNG; `G_irand` overflow | G2 | K3 | `rand` matches MSVC for 3 seeds | S | parked (rework r1) |
| 10 | ki-tsj1q · `game_mp` hazards: `g_spawn_mp` offsets, `HIWORD`, `va_list`; game_mp layout (#216) | G2 | K3 | tests fail on the old behavior | M | queued |
| 11 | Dvars storing pointers in `int` (15 sites); enum-dvar limits | G2 | K3 | a 64-bit test round-trips them | M | queued |
| 12 | Loader design + generator spike: RawFile, StringTable, PhysPreset | G2 | K4 | the generator's disk32 mirrors pass the ONDISK asserts; hand-built disk32 fixtures for the three families load at 64-bit under ASan/UBSan in the Linux test build; K4 is counted only from the owner's manual run | L | queued |
| 20 | GSC parser and VM defects (#225, #199) | G2 | K3 | tests drive the real parser and VM | M | queued |
| 21 | ki-brzcp · xanim sizes and strides at 64-bit; `XAnimClone` clone sizes (#218) | G2 | K3 | tests fail on the old sizes | M | queued |
| 22 | Physics alias write and brush-callback contexts (#242 items 1-2) | G2 | K3 | a test runs a brush contact at 64-bit | M | queued |
| 13 | ki-tozh2 · POSIX headless entry, termios console, `NET_*` on `Sys_Socket` | G1 (Linux) | K1, K2 | Linux headless links and reaches its prompt | L | blocked (23, 25, 27, 28, 14) |
| 14 | ki-3qwla · Portable async fast-file reads (`db_file_load.cpp`) | G1 (Linux) | K1 | compiles on lin64/a64; a read test passes | M | in review |
| 15 | Steam 1.8 capture decoder; usercmd ground truth (#282) | G4a | gate | every owner capture decodes | M | blocked-owner (captures) |
| 16 | `steam18` server profile on x86 | G4a | gate | replayed captures get retail replies | M | blocked-owner (captures) |

Other `burndown` issues are off these gates (client, SP, cleanup) or wait on the owner. Future bead: a total-order Huffman tie-break ([DETERMINISM](design/DETERMINISM.md)).

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| x86 client and headless server on Steam 1.8 data (3 maps, one fork round, Wine); confirms #191, #224 | G0 |
| Hash the Steam 1.8 `iw3mp.exe`; record the depot manifest | G0, G4a |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)); `steam18` auth policy | 15-16, G4a |
| Miles/Bink legal decision; release packaging (#263, #290) | WS-8, G6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Confirm the test maps and G2 boot map ([ROADMAP](ROADMAP.md)) | G2 |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Done since last review

- 19, physics pointer-to-int casts (ki-zretw): #311 (16e06eaa).
- 18, GSC parser tables narrow 32768 into `short` (ki-pbb7c): #310 (e47a39b3).
- 24, MSVC-compat string and name helpers (ki-1h22p): #319 (530a1f87).

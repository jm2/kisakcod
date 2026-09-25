# NOW

Last reviewed: 2026-09-25 by operator
Current gate: G1 (owner actions pending for G0 and G4a)
WIP limit: 6 (at least 4 on the current gate)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

From the master census at 4b2897d0; row 1 re-baselines K1. Live values: the `Native64 census` summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 112/243 · lin64 103/236 · a64 103/236 | all (G1) | census |
| K2 | 64-bit headless real link | none; Win64 probe: 55 undefined, 4 TUs excluded | Win64, Linux amd64 (G1) | census |
| K3 | Upstream engine TUs in the Linux test build | 8/475 | rising | census |
| K4 | Server asset families loading Steam 1.8 `.ff` at 64-bit under ASan | 0/25 | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 103 | 0 (G1 Linux) | census |
| K6 | Target x role cells at `links` or above | 0/10 | 10 at `packaged` (G6) | manifest |

## Queue

S = 1-2 days, M = 3-5, L = 1-2 weeks; headless unless noted. `#n` is a GitHub issue whose checklist is the bead's scope. A bead copies its done-test into `now.done_test`. Start rows 1-6; feed in the rest in order.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Census fidelity: `_DEBUG`, no `-fdelayed-template-parsing`, error-by-default diagnostics on (#291, #226 flags) | G1 | K1 | census shows the new flags and the GSC table errors; K1 re-baselined | S | ready |
| 2 | Cut `xanim.h` -> renderer includes and the bgame `aim_assist.h` includes (#279) | G1 | K5 | census: K5 = 0 | S-M | ready |
| 3 | Split `win_local.h` into portable and Win32-only headers | G1 | K1 | no lin64/a64 first error in `win_local.h` | M | ready |
| 4 | MSVC-compat header (`ARRAYSIZE`, `_strlwr`, `_isnan`, `_time64`, `_TRUNCATE`, `basename`, `_BitScanReverse` #218), `IsValidSeed` in `ui_shared.h`, `BigShort` (#231) | G1 | K1 | no error on these names | S | ready |
| 5 | `RUNTIME_SIZE(T, n32, n64)` for the ~34 runtime-only structs | G1 | K1 | no win64 assert-only failure from them | S | ready |
| 6 | The 7 Win64 source fixes ([NATIVE64](design/NATIVE64.md)) | G1 | K1, K2 | win64 "other" failures = 0 | S | ready |
| 7 | Asset size asserts become `ONDISK`/`RUNTIME` pairs; 64-bit loads fail closed | G1 | K2 | 0 size-assert failures; a test: unconverted family raises `ERR_DROP` | M | ready |
| 8 | `windows-amd64-dedi` preset (`KISAK_ALLOW_UNSUPPORTED_64BIT` exists), Steam off (#265) | G1 | K2 | preset configures; census attempts the Win64 link | S | ready |
| 9 | GSC parser tables narrow 32768 into `short` (#226) | G1 | K1 | no narrowing error in `src/script` | S | ready |
| 10 | Physics pointer-to-int casts (#242 item 3) | G1 | K1 | no cast error in `src/physics` | S | ready |
| 11 | Engine-owned MSVC-compatible RNG; `G_irand` overflow | G2 | K3 | `rand` matches MSVC for 3 seeds | S | queued |
| 12 | Hazards: `g_spawn_mp` offsets, clone sizes, `HIWORD`, `va_list`, `XAnimClone`; game_mp layout (#216) | G2 | K3 | tests fail on the old behavior | M | queued |
| 13 | Dvars storing pointers in `int` (15 sites); enum-dvar limits | G2 | K3 | a 64-bit test round-trips them | M | queued |
| 14 | Loader design + generator spike: RawFile, StringTable, PhysPreset | G2 | K4 | load from a real `.ff` at 64-bit under ASan | L | queued |
| 15 | GSC parser and VM defects (#225, #199) | G2 | K3 | tests drive the real parser and VM | M | queued |
| 16 | xanim sizes and strides at 64-bit (#218) | G2 | K3 | tests fail on the old sizes | M | queued |
| 17 | Physics alias write and brush-callback contexts (#242 items 1-2) | G2 | K3 | a test runs a brush contact at 64-bit | M | queued |
| 18 | POSIX headless entry, termios console, `NET_*` on `Sys_Socket` | G1 (Linux) | K1, K2 | Linux headless links and reaches its prompt | L | queued |
| 19 | Portable async fast-file reads (`db_file_load.cpp`) | G1 (Linux) | K1 | compiles on lin64/a64; a read test passes | M | queued |
| 20 | Steam 1.8 capture decoder; usercmd ground truth (#282) | G4a | gate | every owner capture decodes | M | blocked-owner (captures) |
| 21 | `steam18` server profile on x86 | G4a | gate | replayed captures get retail replies | M | blocked-owner (captures) |

Other `burndown` issues are off these gates (client, SP, cleanup) or wait on the owner. Future bead: a total-order Huffman tie-break ([DETERMINISM](design/DETERMINISM.md)).

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| x86 client and headless server on Steam 1.8 data (3 maps, one fork round, Wine); confirms #191, #224 | G0 |
| Hash the Steam 1.8 `iw3mp.exe`; record the depot manifest | G0, G4a |
| Steam 1.8 captures ([NET_STEAM18](design/NET_STEAM18.md)); `steam18` auth policy | 20-21, G4a |
| Self-hosted runner vs manual licensed tests | G2, K4 |
| Miles/Bink legal decision; release packaging (#263, #290) | WS-8, G6 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)); approve `CHARTER.md` and the ADRs | all |
| Confirm the test maps and G2 boot map ([ROADMAP](ROADMAP.md)) | G2 |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Done since last review

- Native64 census CI job: #177 (4dd31d8a).
- #191, fast-file loads failed at the first script-string intern: #212 (b2f0b6f7).
- #224, headless GSC lexer hung at EOF: #228 (4b2897d0).

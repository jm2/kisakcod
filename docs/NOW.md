# NOW

Last reviewed: 2026-09-22 by operator
Current gate: G1 (owner actions pending for G0 and G4a)
WIP limit: 6 (at least 4 on the current gate)

The mayor is the only role that edits this file ([AGENTS.md](../AGENTS.md)).

## KPIs

Values are from the first census run (2026-09-22). The live values are in
the `Native64 census` job summary of the latest [master CI run](https://github.com/jm2/kisakcod/actions/workflows/ci.yml?query=branch%3Amaster).

| KPI | Measures | Value | Target | Source |
| --- | --- | --- | --- | --- |
| K1 | 64-bit headless TUs passing syntax-only | win64 112/243 · lin64 103/236 · a64 103/236 | all (G1) | census |
| K2 | 64-bit headless real link | none (131 win64 TUs don't compile); Win64 probe link: 55 undefined, 4 TUs excluded | Win64 and Linux amd64 (G1) | census |
| K3 | Upstream engine TUs compiled by the Linux test build | 8/475 | rising | census |
| K4 | Server-closure asset families loading Steam 1.8 `.ff` at 64-bit under ASan | 0/25 | 25/25 (G2) | this file |
| K5 | Headless TUs reaching `<d3d9.h>` on Linux | 103 | 0 (G1 Linux) | census |
| K6 | Required target x role cells at `links` or above | 0/10 | 10 at `packaged` (G6) | manifest |

## Queue

Sizes: S = 1-2 days, M = 3-5 days, L = 1-2 weeks. Headless-scoped unless
noted. A bead copies its row's done-test into `now.done_test`. Beads 2-7 start now; feed in 8-14 as slots free up.

| # | Bead | Gate | Moves | Done-test | Size | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Native64 census CI job | G1 | K1, K2 | CI `native64-census` job succeeds and its summary shows K1, K2, K3, K5 | S | in overhaul PR 2 |
| 2 | Cut the `xanim.h` -> `r_bsp.h`/`r_gfx.h`/`r_material.h` includes with forward declarations, so headless stops reaching `d3d9.h` | G1 | K5 (~103 TUs) | census: K5 = 0 | S-M | ready |
| 3 | Split `win_local.h` into a portable system header and a Win32-only one | G1 | K1 (first error in 20 of 39 lin64 failures) | census: no lin64/a64 TU has its first error in `win_local.h` | M | ready |
| 4 | MSVC-compat header (`ARRAYSIZE`, `_strlwr`, `_isnan`, `_time64`, `_TRUNCATE`, `basename`); fix the undeclared `IsValidSeed` call in `ui_shared.h` | G1 | K1 | census: no error on these names; `xanim.cpp` compiles without `-fdelayed-template-parsing` | S | ready |
| 5 | `RUNTIME_SIZE(T, n32, n64)` for the ~34 runtime-only structs | G1 | K1 | census: no win64 assert-only failure from a runtime-only struct | S | ready |
| 6 | The 7 Win64 source fixes ([NATIVE64](design/NATIVE64.md)) | G1 | K1, K2 | census: win64 "other" failures = 0 | S | ready |
| 7 | Asset-type size asserts become `ONDISK`/`RUNTIME` pairs; the loader fails closed at 64-bit (today the offset converters only drop above 4 GiB) | G1 | K2 | census: 0 size-assert failures; a test shows a 64-bit load of an unconverted family raises `ERR_DROP` | M | ready |
| 8 | CMake: allow `KISAK_DEDI_HEADLESS` on 64-bit behind an experimental option; add a `windows-amd64-dedi` preset | G1 | K2 | `cmake --preset windows-amd64-dedi` configures; the census attempts the real Win64 link | S | ready |
| 9 | Engine-owned MSVC-compatible RNG behind `rand`/`random`/`G_*rand`; fix the `G_irand` overflow | G2 | K3 | a test: engine `rand` matches MSVC for 3 seeds; `G_irand` stays in range on glibc | S | queued |
| 10 | Hazards: `g_spawn_mp` offsets -> `offsetof`; clone size table -> real `sizeof` (incl. ClipMapPvs); the `HIWORD` bug; the 5 `va_list` misuses; the `XAnimClone` size | G2 | K3 | tests compile each fixed TU and fail on the old behavior; K3 rises | M | queued |
| 11 | Dvars storing pointers in `int` (15 sites); enum-dvar limits packing | G2 | K3 | a 64-bit test round-trips every pointer-bearing dvar; K3 rises | M | queued |
| 12 | Loader design plus a generator spike on RawFile, StringTable and PhysPreset, loading a real `.ff` at 64-bit | G2 | K4 | the three families load from a real `.ff` at 64-bit under ASan (K4 >= 3/25) | L | queued |
| 13 | POSIX headless entry point and termios console; `NET_*` on `Sys_Socket` | G1 (Linux) | K1, K2 | Linux headless links (K2 lin64) and starts to its console prompt | L | queued |
| 14 | Portable async fast-file reads (`db_file_load.cpp`) | G1 (Linux) | K1 | census: `db_file_load.cpp` compiles on lin64/a64; an async-read test passes | M | queued |
| 15 | Steam 1.8 capture decoder and diff report | G4a | gate | the diff report decodes every owner capture with no unexplained field | M | blocked-owner (captures) |
| 16 | `steam18` server profile on x86: protocol 7, stock challenge/connect, 1.8 `getinfo` keys | G4a | gate | replayed Steam 1.8 `getinfo`/`getchallenge`/`connect` captures get retail-shaped replies | M | blocked-owner (captures) |

Open item for a future bead: make the Huffman tie-break a total order and
make its test fail on host disagreement ([DETERMINISM](design/DETERMINISM.md)).

## Blocked on owner

| Action | Unblocks |
| --- | --- |
| Run the x86 client and headless server on Steam 1.8 data (3 maps, one fork round, Wine) | G0 |
| Hash the Steam 1.8 `iw3mp.exe` and record the depot manifest | G0, G4a |
| Steam 1.8 captures per [NET_STEAM18](design/NET_STEAM18.md) | beads 15-16, G4a |
| Self-hosted runner vs manual licensed tests | G2 evidence, K4 |
| `steam18` auth policy (CD-key hash unchecked vs authorize first) | bead 16 |
| Legal decision on Miles/Bink in the repo | WS-8 |
| CoD4x census ([ADR-0005](decisions/0005-cod4x-compatibility.md)) | ADR-0005 |
| Approve `CHARTER.md` and the ADRs | all |
| Confirm the test-map list and G2 boot map ([ROADMAP](ROADMAP.md)) | G2 |
| Later: GPU runner; Apple Developer ID | G5; G6 |

## Done since last review

(none yet)

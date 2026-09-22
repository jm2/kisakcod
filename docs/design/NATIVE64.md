# NATIVE64: engine code at 64-bit

Workstreams: WS-1 (compile closure, [G1](../ROADMAP.md)) and WS-3 (silent hazards, G2, shared with [DETERMINISM.md](DETERMINISM.md)).
The loader lives in [FASTFILE_LOADER.md](FASTFILE_LOADER.md), and the POSIX platform layer in [PLATFORM_POSIX.md](PLATFORM_POSIX.md).
Queue and owners: [NOW.md](../NOW.md). Evidence rules: [CHARTER.md](../CHARTER.md) and [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md).

## Why 64-bit is hard

The source is a decompile of a 32-bit (ILP32) binary, and it encodes that ABI:

- Every reconstructed struct is pinned to its 4-byte-pointer size. A struct with a pointer member stops compiling as soon as pointers become 8 bytes.
- Fast-file zones store 4-byte pointer slots. A 64-bit engine can only read retail data through a separate on-disk layout and a load-time widening pass (see [FASTFILE_LOADER.md](FASTFILE_LOADER.md)).
- The script VM packs pointers and 32-bit handles into one `VariableUnion`. The production headers now make it native-width (`RUNTIME_SIZE(VariableUnion, 0x4, 0x8)`), but no engine target compiles the VM at 64-bit yet.
- Memory management and asset walkers do address arithmetic through `int`.
- `long` is 32 bits on Windows (LLP64) and 64 bits on Linux and macOS (LP64).

Convert **by layout class, not file by file**.

## Layout classes

All macros live in `universal/kisak_abi.h`. `Ptr32<T>` has exactly one definition, in `database/db_disk32.h` (namespace `disk32`); don't add a parallel one.

| Class | What | Rule | Assert |
| --- | --- | --- | --- |
| Wire-frozen | Networked POD: `entityState_s` 0xF4, `archivedEntity_s` 0x118, `playerState_s` 0x2F64, `usercmd_s` 0x20, `hudelem_s` 0xA0, `objective_t` 0x1C, `clientState_s` 0x64 | Pointer-free and the same size on every target. The delta codec reads every netfield through an `int *`, so each netfield member stays exactly 32 bits. `net_wire_contract_tests` pins these on all portable targets | `ONDISK_SIZE` (`RUNTIME_SIZE(T, n, n)` is equivalent) |
| Disk32 mirror | On-disk records of fast-file assets | Packed mirror whose pointer fields are `disk32::Ptr32<T>`. It keeps the original 32-bit size and offsets on every target. Mirrors exist for `XAsset`, `XAssetList`, `ScriptStringList` and the FX family | `ONDISK_SIZE`, `ONDISK_OFFSET` |
| Runtime | Everything else, including the in-memory asset structs the loader builds | Native pointers. Assert both widths so drift is caught on each build | `RUNTIME_SIZE(T, n32, n64)`, `RUNTIME_OFFSET(T, f, n32, n64)` |

An asset type that is both loaded and used at runtime gets a pair: an `ONDISK_*` mirror plus a `RUNTIME_SIZE` runtime struct. Until its family is converted, the loader refuses that family at 64-bit.

## Conventions

- Target facts come only from `kisak_abi.h`: `KISAK_ARCH_64BIT`, `KISAK_PTR_BITS`, `KISAK_OS_*`, `KISAK_ARCH_*`. Don't use `_WIN32` as shorthand for "x86"; it is also defined on Win64 and WinARM64.
- Use exact-width integers wherever width matters. `long` and `unsigned long` are banned there.
- Address math uses `uintptr_t`. Page masks are `~(uintptr_t)0xFFF`, not `& 0xFFFFF000` on an `int`.
- Replace `(int)&arr[i]` stride walks with typed pointer loops, so the compiler recomputes strides.
- Don't write new raw `static_assert(sizeof(T) == N)`; use the macros. The sizeof tripwire rejects new raw asserts.
- A change must not alter the Windows x86 build's behaviour. `RUNTIME_SIZE`'s `n32` arm is the x86 contract.

## The 64 failing size asserts

The same 64 `static_assert(sizeof(T) == N)` fail on win64, lin64 and a64. Bead numbers refer to the [NOW.md](../NOW.md) seed queue.

| Header | Asserts | Class | Bead |
| --- | --- | --- | --- |
| `bgame/bg_local.h` | 17 | runtime (`bgs_t`, `clientInfo_t`, `animScriptData_t`, `pml_t`, …) | 5 |
| `xanim/xanim.h` | 10 | asset (`XAsset`, `XAssetList`, `ScriptStringList`, `XSurface`, `XAnimParts`, `WeaponDef`, `RawFile`, …) | 7 |
| `xanim/xmodel.h` | 5 | asset (`XModel`, `XModelPieces`, `XModelSurfs`, …) | 7 |
| `game_mp/g_public_mp.h` | 5 | runtime (`scr_data_t`, `corpseInfo_t`, `BuiltinFunctionDef`, …) | 5 |
| `sound/snd_public.h` | 4 | asset (`LoadedSound`, `SndCurve`, `snd_alias_t`, `snd_alias_list_t`) | 7 |
| `DynEntity/DynEntity_client.h` | 4 | `DynEntityDef` asset; 3 runtime | 7 / 5 |
| `gfx_d3d/r_gfx.h`, `gfx_d3d/r_material.h` | 3 each | asset (`GfxImage`, `GfxWorldDpvsStatic`, `Material`, `MaterialTechniqueSet`, …) | 7 |
| `universal/q_parse.h`, `game/game_public.h` | 3 each | runtime | 5 |
| `game_mp/g_main_mp.h` | 2 | runtime (`level_locals_t`, `entityHandler_t`) | 5 |
| `gfx_d3d/r_bsp.h`, `gfx_d3d/r_font.h`, `game/pathnode.h` | 1 each | asset (`GfxWorld`, `Font_s`, `pathnode_tree_nodes_t`) | 7 |
| `game/enthandle.h`, `aim_assist/aim_assist.h` | 1 each | runtime | 5 |

That's about 30 asset types (bead 7: `ONDISK`/`RUNTIME` pairs, and the loader fails closed) and about 34 runtime-only types (bead 5: `RUNTIME_SIZE`).

## Win64 source fixes (bead 6)

With the asserts neutralised, win64 fails in only 4 TUs. The 7 real fixes are:

| Fix | Where |
| --- | --- |
| `CPUSTRING` is defined only under `_M_IX86` (2 sites: release and debug) | `universal/q_shared.h` |
| `longjmp((int*)value, -1)`: `jmp_buf` isn't `int*` at 64-bit | `qcommon/common.cpp` |
| Missing `<cstring>` for `std::strlen` | `_platform/win32/sys_process.cpp` |
| 3 variables typed as pointers to enums that are never defined (forward-referenced `enum`) | `database/db_load.cpp` |
| `alignas` written after `const`, where clang rejects it | `script/scr_yacc_structs.h` |
| Inline `__asm` in `DoStackTrace`; MSVC x64 has no inline asm | `universal/assertive.cpp` |

The other `__asm` blocks in the headless set are dead code: `win_configure.cpp` is under `#if 0`, and `common.cpp` is under a `_M_X686` guard that no compiler defines.

## Hazard catalogue

Two kinds of hazard, both fixed by beads 9–11 (WS-3, G2):
- **Silent:** compiles at 64-bit but misbehaves.
- **Compile error on LP64 only:** passes Win64 but fails on Linux or macOS.

| Hazard | Where | At 64-bit | Fix | Bead | Status |
| --- | --- | --- | --- | --- | --- |
| Clone size table aliases, inherited from the original compiler folding identical size functions | `DB_GetXAssetSizeHandler` in `database/db_assetnames.cpp`, which feeds the `memcpy` in `DB_CloneXAssetInternal` | 5 entries go wrong. PhysPreset (56) and LoadedSound (64) use `sizeof(GameWorldSp)` (88), an over-read. ClipMap and ClipMap PVS (480) use `sizeof(menuDef_t)` (360), a truncated copy. LightDef (32) uses `sizeof(StringTable)` (24), also truncated | One real `sizeof` per asset type | 10 | open |
| Hardcoded entity field offsets | `fields_1` in `game_mp/g_spawn_mp.cpp` | 8 of 10 move: classname, model, spawnflags, target, targetname, health, dmg, count. origin and angles stay | `offsetof(gentity_s, …)` | 10 | open |
| `HIWORD`/`_WORD` taken of the *address* of a local `scr_entref_t` copy | `PlayerCmd_DeactivateReverb`, `PlayerCmd_DeactivateChannelVolumes` in `game_mp/g_client_script_cmd_mp.cpp` | Wrong on every width; reads different pointer bits at 64-bit | Use `entref.entnum` / `entref.classnum` | 10 | open |
| `XAnimClone` allocates 88 bytes, then copies `sizeof(XAnimParts)` | `xanim/xanim.cpp` | Heap overflow: the copy is 136 bytes. The comment in the test-only `xanim_native.h` describes a fix that isn't in engine code | Allocate `sizeof(XAnimParts)` | 10 | open |
| `va_list` passed as `char *` | 2 in `universal/q_parse.cpp`, 3 in `qcommon/com_playerprofile.cpp` | Compile error on SysV x86-64 and AArch64 | Pass `va_list` | 10 | open |
| Pointers stored in dvar `int` fields | `universal/dvar.cpp`: 15 pointer→int stores (string values in `DvarValue.integer`, the enum string table at registration) and the matching int→pointer reads | Pointer truncation | Use the union's pointer members | 11 | open |
| The enum-dvar domain overlays `enumeration.strings` on `integer.max` | `DvarLimits` in `universal/q_shared.h`, read both ways in `dvar.cpp` | The alias only holds at 32-bit, where the pointer sits at offset 4 | Separate fields; no punning | 11 | open |
| `RAND_MAX` hardcoded as 32768 | `random` in `universal/com_math.cpp`; `G_random`, `G_flrand`, `G_irand` in `game_mp/g_utils_mp.cpp` | glibc and macOS `rand()` reach 2³¹−1, so results leave their range and `G_irand` overflows | Engine-owned MSVC LCG ([DETERMINISM.md](DETERMINISM.md)) | 9 | open |
| 4-byte pointer slots and `(T*)-1` markers | `database/db_load.cpp`, `db_stream_load.cpp` | Loader writes half-pointers | Two-layout loader ([FASTFILE_LOADER.md](FASTFILE_LOADER.md)) | 7, 12 | fails closed after 7 |
| Pointer↔int casts | ≈240 in 37 headless files: `com_files.cpp` 57, `xanim_load_obj.cpp` 42, `dvar.cpp` 30, `db_load.cpp` 25 | Truncation | Policy below | per file | open |
| `__rdtsc` | `scr_vm.cpp`, `sv_main_mp.cpp` | Doesn't exist on arm64 | See [PLATFORM_POSIX.md](PLATFORM_POSIX.md) | 13 | open |
| Huffman tie-break uses the host `qsort` | `qcommon/huffman.cpp` | The code book may differ per host | Total order ([DETERMINISM.md](DETERMINISM.md)) | open item | open |

A row reaches **fixed** only when the fix is merged *and* a test executes the fixed code at 64-bit.

## Script VM

- `VariableUnion` is native-width in the production headers, and `scr_readwrite.cpp` (save/load) is SP-only.
- The native-width VM runs only in portable tests, which exercise a small part of `scr_vm.cpp` (5,240 lines).
- `scr_compiler2`, `scr_evaluate`, `scr_parser`, `scr_main` and `scr_variable` hold 17 pointer↔int casts.
- The VM reaches G2 only through a real map boot running stock scripts.

## Pointer-cast policy

- Add no new pointer↔int casts. Fix the existing ones in the bead that touches the file; there are no sweep-only beads.
- Carry handles as 32-bit indices or offsets. If a pointer must pass through an integer, use `uintptr_t` and a typed accessor.
- Wire and disk values stay 32 bits. The widening happens at the loader or codec boundary, never inside the struct.
- The census counts casts with clang's `-Wpointer-to-int-cast`, `-Wint-to-pointer-cast`, `-Wvoid-pointer-to-int-cast` and `-Wshorten-64-to-32`.

## Tripwires, not KPIs

These allowlists fail on *new* debt, and on stale entries once a site is fixed. They read source text, so they say nothing about closure. Never report them as progress.

| Allowlist | ctest | Tracks |
| --- | --- | --- |
| `tests/abi_sizeof_debt.allow` (+ `abi_sizeof_formula_debt.allow`) | `abi-sizeof-debt-tripwire` | 183 raw sizeof asserts (+ 8 formula asserts) |
| `tests/pointer_truncation.allow` | `pointer-truncation-tripwire` | 24 `(int)&` and page-mask sites. The regex misses most casts: the census finds ≈240 |
| `tests/headless_include_debt.allow` | `dedi-headless-client-media-include-debt` | 21 direct client/media includes. Transitive reach is K5 |

## KPIs

| KPI | Definition | Source | Review baseline 2026-09-22 | Target |
| --- | --- | --- | --- | --- |
| **K1** 64-bit headless compile closure | Headless TUs that pass clang `-fsyntax-only` on each target (win64, lin64, a64). win32 is the control | `native64-census` | win64 112/243 (127 assert-only, 4 other); lin64 103/236 (94 assert-only, 39 other); a64 103/236 | all TUs on all three (G1) |
| **K2** 64-bit headless link | Per target: the headless server links with 0 undefined symbols and no neutralised asserts (a *real link*). The census also reports a labelled *probe link*, with size asserts neutralised by a force-included header, and its undefined-symbol count. The probe never gates | `native64-census` | real link: none. Probe: win64 PE32+ with 0 undefined symbols, after 4 TU workarounds | Win64 and Linux amd64 real link (G1) |
| **K3** engine code under 64-bit test | Upstream engine TUs that the Linux amd64 test build compiles, either as a TU in `compile_commands.json` or `#include`d as a `.cpp` by a test TU. Denominator: `.c`/`.cpp` under `src/` at the upstream merge-base, excluding `src/radiant/` and vendored ODE and Speex | `native64-census` | 8/475 | rises every G2 bead |

- **K1 control:** clang with mingw-w64 headers passes 235/243 on win32. 3 failures are the `db_load`, `sys_process` and `scr_yacc` fixes above. The other 5 are mingw calling-convention mismatches in `sys_sync`, `sys_thread`, `assertive`, `win_net_debug` and `win_syscon`.
- **K4–K6:** K4 (loader closure) is defined in [FASTFILE_LOADER.md](FASTFILE_LOADER.md), K5 (D3D reach) in [PLATFORM_POSIX.md](PLATFORM_POSIX.md), and K6 (delivery cells) in [CHARTER.md](../CHARTER.md).

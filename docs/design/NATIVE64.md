# NATIVE64: engine code at 64-bit

Workstreams: WS-1 (compile closure, [G1](../ROADMAP.md)) and WS-3 (silent hazards, G2, shared with [DETERMINISM.md](DETERMINISM.md)).
The loader lives in [FASTFILE_LOADER.md](FASTFILE_LOADER.md), and the POSIX platform layer in [PLATFORM_POSIX.md](PLATFORM_POSIX.md).
Queue and owners: [NOW.md](../NOW.md). Evidence rules: [CHARTER.md](../CHARTER.md) and [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md).

## Why 64-bit is hard

The source is a decompile of a 32-bit (ILP32) binary, and it encodes that ABI:

- Every reconstructed struct is pinned to its 4-byte-pointer size. A struct with a pointer member stops compiling as soon as pointers become 8 bytes.
- Fast-file zones store 4-byte pointer slots. A 64-bit engine can only read retail data through a separate on-disk layout and a load-time widening pass (see [FASTFILE_LOADER.md](FASTFILE_LOADER.md)).
- The script VM packs pointers and 32-bit handles into one `VariableUnion`. The production headers make it native-width (`RUNTIME_SIZE(VariableUnion, 0x4, 0x8)`), and the 64-bit clients run it in-game; the headless server runs no GSC until its map boot (NOW row 45).
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

## G1 closure (done)

G1 is done: the headless set compiles on every leg and Win64 links for real (K1, K2).

- **Size asserts:** the 64 `static_assert(sizeof(T) == N)` that failed on every 64-bit leg are gone. About 30 asset types became `ONDISK`/`RUNTIME` pairs with a fail-closed loader, and about 34 runtime-only types use `RUNTIME_SIZE`. The census reads 0 size-assert sites.
- **Win64-only fixes:** `CPUSTRING` outside `_M_IX86`, `longjmp` on a real `jmp_buf`, forward-referenced enums in `db_load.cpp`, `alignas` placement for clang, and no inline `__asm` in `DoStackTrace`. The other `__asm` blocks in the headless set are dead code (`#if 0`, or the `_M_X686` guard nothing defines).

## Hazard catalogue

Two kinds: **silent** (compiles at 64-bit but misbehaves) and **LP64-only compile errors** (pass Win64, fail on Linux or macOS). Rows are 2026-09-29 queue rows; their PRs are in `git log`.

| Hazard | Where | At 64-bit | Fix | Status |
| --- | --- | --- | --- | --- |
| Clone size table aliases from the original compiler folding identical size functions | `DB_GetXAssetSizeHandler` (`db_assetnames.cpp`), feeding `DB_CloneXAssetInternal` | Over-read or truncated copies for PhysPreset, LoadedSound, ClipMap, ClipMap PVS, LightDef | One real `sizeof` per type (row 21) | fixed |
| Hardcoded entity field offsets | `fields_1` in `g_spawn_mp.cpp` | 8 of 10 move | `offsetof(gentity_s, …)` (row 10) | fixed |
| `HIWORD`/`_WORD` of the *address* of a local `scr_entref_t` | `PlayerCmd_DeactivateReverb`, `…ChannelVolumes` | Wrong on every width | `entref.entnum` / `classnum` (rows 10, 20) | fixed |
| `XAnimClone` allocates 88 bytes, copies `sizeof(XAnimParts)` | `xanim.cpp` | Heap overflow (136 bytes) | Allocate `sizeof(XAnimParts)` (row 21) | fixed |
| `va_list` passed as `char *` | `q_parse.cpp`, `com_playerprofile.cpp` | Compile error on SysV x86-64 and AArch64 | Pass `va_list` (row 27) | fixed |
| Pointers stored in dvar `int` fields; the enum domain punned onto `integer.max` | `dvar.cpp`, `DvarLimits` in `q_shared.h` | Pointer truncation | Pointer members (row 11). Two assert-only reads (`Dvar_EnumToString`, `Dvar_IndexStringToEnumString`) still test `integer.max` for `enumeration.strings` | fixed; 2 assert puns left |
| `RAND_MAX` hardcoded as 32768 | `com_math.cpp`, `g_utils_mp.cpp` | Out-of-range results off Windows | Engine-owned MSVC LCG `Kisak_rand` ([DETERMINISM.md](DETERMINISM.md), row 9) | fixed |
| 4-byte pointer slots and `(T*)-1` markers | `db_load.cpp`, `db_stream_load.cpp` | Loader writes half-pointers | Two-layout loader ([FASTFILE_LOADER.md](FASTFILE_LOADER.md)) | fails closed; K4 |
| Pointer↔int casts | 103 in 17 headless files (MSVC x64 and clang, 2026-09-30), mostly `xanim_load_obj.cpp` (raw-asset path) and `db_load.cpp` (32-bit loader) | Truncation | Policy below | open, per file |
| `__rdtsc` | `scr_vm.cpp`, `sv_main_mp.cpp` | Doesn't exist on arm64 | `Sys_CycleCounter` ([PLATFORM_POSIX.md](PLATFORM_POSIX.md)) | fixed |
| Huffman tie-break uses the host `qsort` | `huffman.cpp` | Code book differs per C library | Total-order `nodeCmp` ([DETERMINISM.md](DETERMINISM.md)) | fixed |
| Parser tables write 32768 as the `short` YYPACT_NINF sentinel | `scr_yacc_structs.h`, `scr_yacc.cpp` | Ill-formed narrowing; MSVC only warns | Write −32768; the census keeps `-Werror=c++11-narrowing` | fixed |
| Byte counts frozen at their x86 size | `R_GetCommandBuffer` callers | Commands overran the next | `sizeof`/`offsetof`, rounded to 4 or 8 | fixed |
| `obj[1].member` reaching the struct after `obj` | `dc[1]…` in `ui_main_mp.cpp` | Lands on other fields | Map to the named member | fixed |

A row reaches **fixed** only when the fix is merged *and* a test executes the fixed code at 64-bit. Pin an x86 identity with `RUNTIME_SIZE`/`RUNTIME_OFFSET` (n32 = old bytes); a formula `static_assert` fails the abi-sizeof tripwire.

More patterns from the client bring-up: [CLIENT.md](CLIENT.md#64-bit-patterns-the-client-hit).

## Script VM

- `VariableUnion` is native-width in the production headers, and `scr_readwrite.cpp` (save/load) is SP-only.
- The native-width VM runs stock gametype scripts in the 64-bit clients' in-game runs (`db_unverified64`); portable tests exercise a small part of `scr_vm.cpp`.
- `scr_compiler2`, `scr_evaluate`, `scr_parser`, `scr_main` and `scr_variable` hold 17 pointer↔int casts.
- The VM's G2 evidence is the headless map boot and rotation (NOW row 45).

## Pointer-cast policy

- Add no new pointer↔int casts. Fix the existing ones in the bead that touches the file; there are no sweep-only beads.
- Carry handles as 32-bit indices or offsets. If a pointer must pass through an integer, use `uintptr_t` and a typed accessor.
- Wire and disk values stay 32 bits. The widening happens at the loader or codec boundary, never inside the struct.
- The census counts casts with clang's `-Wpointer-to-int-cast`, `-Wint-to-pointer-cast`, `-Wvoid-pointer-to-int-cast` and `-Wshorten-64-to-32`.

## Tripwires, not KPIs

These allowlists fail on *new* debt, and on stale entries once a site is fixed. They read source text, so they say nothing about closure. Never report them as progress.

| Allowlist | ctest | Tracks |
| --- | --- | --- |
| `tests/abi_sizeof_debt.allow` (+ `abi_sizeof_formula_debt.allow`) | `abi-sizeof-debt-tripwire` | 91 raw sizeof asserts (+ 8 formula asserts) |
| `tests/pointer_truncation.allow` | `pointer-truncation-tripwire` | 20 `(int)&` and page-mask sites. The regex misses most casts: MSVC and clang flag 103 |
| `tests/headless_include_debt.allow` | `dedi-headless-client-media-include-debt` | 29 direct client/media includes. Transitive reach is K5 |

## KPIs

| KPI | Definition | Source | Census | Target |
| --- | --- | --- | --- | --- |
| **K1** 64-bit headless compile closure | Headless TUs that pass clang `-fsyntax-only` on each leg: win64, winarm64, lin64, a64, mac64. win32 is the control | `native64-census`; mac64 from `Native64 census / mac64` | win64 273/273; winarm64 273/273; lin64 272/272; a64 272/272; mac64 273/273 (census 2026-10-16; 0 assert-only, 0 other on each) | all TUs on win64, lin64, a64 (G1); winarm64 and mac64 before G3 |
| **K2** 64-bit headless link | Per target: the headless server links with 0 undefined symbols and no neutralised asserts (a *real link*). The census also reports a labelled *probe link*, with size asserts neutralised by a force-included header, and its undefined-symbol count. The probe never gates | `native64-census` | win64 real link: linked, 0 undefined symbols, 0 TUs excluded; the probe links too | Win64 and Linux amd64 real link (G1) |
| **K3** engine code under 64-bit test | Upstream engine TUs that the Linux amd64 test build compiles, either as a TU in `compile_commands.json` or `#include`d as a `.cpp` by a test TU. Denominator: `.c`/`.cpp` under `src/` at the upstream merge-base, excluding `src/radiant/` and vendored ODE and Speex | `native64-census` | 82/475 (2026-10-16) | rises every G2 bead |

- **K1 control:** clang with mingw-w64 headers passes 270/273 on win32. The 3 failures are mingw calling-convention mismatches in `sys_sync`, `sys_thread` and `sys_console` (its console control handler); MSVC x86 builds all three.
- **K1 ARM64 legs (owner 2026-09-29):** `win64` is Windows x64 only, so a clean win64 says nothing about Windows ARM64 or macOS arm64. `winarm64` checks the Win64 set for `aarch64-w64-mingw32` with clang 18 and a sha256-pinned llvm-mingw (LLVM 18.1.8). `mac64` checks the POSIX set plus `src/_platform/macos` with Apple clang 21 on the hosted `xcode-27` runner. Both measure and never gate. Off Windows, `sound/snd_msstypes.h` stands in for Miles `mss.h`, and ODE takes `alloca` from `<alloca.h>` on macOS.
- **Census flags:** `-D_DEBUG` so asserts compile, no `-fdelayed-template-parsing` so two-phase lookup errors surface, and no `-Wno-everything` so DefaultError diagnostics fail a TU; `-Werror=c++11-narrowing` re-promotes the one group `-fms-extensions` demotes. `census.md` prints the flag set and the narrowing sites per header.
- **K4–K6:** K4 (loader closure) is defined in [FASTFILE_LOADER.md](FASTFILE_LOADER.md), K5 (D3D reach) in [PLATFORM_POSIX.md](PLATFORM_POSIX.md), and K6 (delivery cells) in [CHARTER.md](../CHARTER.md).

## Lessons from the ports

- **Hard-coded Win32 struct sizes** (`dwSize`, `cbSize`, `cb`) are 32-bit
  values; use `sizeof` (#632, #634). Check a new one with a `sizeof` probe
  for both arches; `mmsystem.h` packs to 1.
- **Decompiler aliases** into a neighbouring array can be wrong at both
  widths when the struct grew later (#667: 69 bytes off on x86). Rederive
  the offset from the client-side twin, and check x86 offsets without
  Windows: `clang++ --target=i686-linux-gnu -fsyntax-only` with
  `template<int> struct Show;` on `offsetof`.
- **Truncating casts** hide in decompiled calls through
  `(int(__cdecl*)(…))` (MSS_Alloc, #666). A sanitizer run finds them.
- **Headless includes.** Headless TUs may not include client, gfx_d3d,
  sound or ui headers (include tripwire). Move a record definition-only into
  `universal/` first (#644, #656, #659), spelling `__int64`/`__int32` as
  standard types, and repoint the configure-time header slicers in
  `tests/renderer_*_contracts.cmake`, which fail configure when a type moves.
- **x86 identity.** Declare any CODE object in the PR body. A reviewer checks
  a declared delta with a local MSVC x86 `/Brepro` compile of base and head;
  an inlining change shows as CODE with the same callees and data. The job
  compares data per symbol (#647).

## G2 run

`scripts/g2_run.py` drives row 45. Run it on an ASan+UBSan headless build
(clang, RelWithDebInfo, `-fsanitize=address,undefined
-fno-sanitize-recover=undefined -fno-omit-frame-pointer -g` in C, C++ and
link flags, otherwise the CI `linux-headless` options); it defaults
`halt_on_error=1` for both sanitizers, so the first report ends the run. The server's configs
overwrite a command-line `sv_mapRotation`, so the script sets the rotation
on stdin after the first map, and waits for `Headless dedicated loading map
'<name>'` then `Huffman Took` (first map only) or `Hitch warning` before querying.

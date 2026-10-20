# Fast-file loader: two layouts

| Owner | Gate | KPI | Workstream |
|---|---|---|---|
| WS-2 beads | G2 | K4 | [ROADMAP](../ROADMAP.md) |

Retail `.ff` files are read unmodified ([CHARTER](../CHARTER.md)). The disk format is the 32-bit
(ILP32) in-memory image of every asset, so a 64-bit loader has to translate every record it
reads. Layout classes and conventions (`ONDISK_*`, `RUNTIME_SIZE`) are defined in
[NATIVE64.md](NATIVE64.md). This doc covers the loader.

## Status

All 25 server-closure families have generated disk32 loaders, and so does
every family the client zones reach. The fail-closed guard (design step 6)
stays on for each family until K4 counts it (`IsK4Verified`). The developer
dvar `db_unverified64 1` (#513) admits every family with a disk32 loader
(`db::asset_layout::HasDisk32Loader`) and warns once. It doesn't count toward
K4. SndDriverGlobals is listed in `code_post_gfx_mp` but has no loader, so the
envelope skips it (#473).

Retail data broke these synthetic-fixture assumptions; each now has a test:

- **Block-4 pointer slots.** An offset token can name any block-4 slot where
  a pointer was stored, not only a header slot: the IW linker reuses the
  first place a pointer landed. Every pointer step records its slot (#497,
  x86 #512).
- **Stub assets.** A name starting with `,` is a stub: a SoundCurve with no
  knots (`,helicopter1`) loads as-is on both paths (#498, x86 #518).
- **Empty values.** A 0x0 StringTable may carry an inline values token
  (#478); an FX lifespan may start at 0 (#479).
- **Shader versions.** Renderer 0 ships SM1 tokens with no length field;
  their bytecode is checked by comment blocks and END (#475).

## Design

1. **Schema once per family.** Describe each family's disk32 layout once, in a declarative
   schema: records, field offsets, scalars, fixed arrays, counted pointers (count expression),
   inline strings, script strings, asset references (alias kind), discriminated unions, stream
   block push/pop and delayed streams (`Load_DelayStream`).
2. **Generate the 64-bit loader.** The 32-bit `Load_*` in `db_load.cpp` stay as they are, so
   Windows x86 does not change; each converted `Load_<F>Ptr` calls its 64-bit loader under
   `#if KISAK_ARCH_64BIT`. The schema is `src/database/db_disk32.schema`, which documents the
   format and includes one file per family (`src/database/disk32/<NN>-<family>.schema`):
   retail sizes, offsets and field kinds.
   `scripts/gen_disk32.py` checks it against the ILP32 rules, and CMake runs it at build time
   for 64-bit targets and the Linux loader tests only, so Windows x86 needs no Python. Generated
   code is not committed ([AGENTS.md](../../AGENTS.md) rule 8). It emits the mirrors with their
   `ONDISK_*` asserts plus `RUNTIME_*` asserts on the native structs, and `db_disk32_loaders.h`.
   A record's `asset` line gives its family's loader facts: the `XAssetHeader` member, the pool
   call, the alias kind and protocol, the name field and a noun for errors. From them the
   generator emits the header-slot entry and the pointer step. `inserted` streams the record
   into the temp block for the pool to copy. `completed` keeps the block-4 record as the alias
   identity and resolves aliases to its native twin. For a flat family it also emits the record
   body from the field kinds: scalars, `bool` as `!= 0`, fixed scalar arrays, xstrings,
   terminated byte arrays with a checked count, and arrays of nested records (`array of=`),
   converted into native storage, such as ComWorld's primary lights. `check=custom` adds the
   32-bit loader's further rules as a hand-written `Check<Family>`, such as SoundCurve's knot
   checks. A `body=custom` family hand-writes only the body, such as StringTable's value
   array. Each family's TU forwards `DB_Load<Family>PtrDisk32` to the generated entry.
   Custom bodies also get `pointer`/`rawptr`, `struct of=`, dotted nested fields and
   `copy=scalars` (the record's scalar copies).

   **Asset references**, such as LightDef → Image or Font → Material. A `pointer` field `asset=<Family>` holds a token to another
   family's header. The body calls that family's generated pointer step, `Load<Family>Ptr`, which
   pushes the temp block itself as its 32-bit `Load_<Family>Ptr` does, so the child streams
   where x86 streams it and the slot ends with the native pointer. While the child has no
   `asset` line, a non-null token raises `ERR_DROP` naming it.
3. **Relocation map.** Every materialized record registers `(block, disk offset, disk stride) →
   (native base, native stride)`. Offset tokens resolve through this map, including interior
   pointers into arrays and to named fields, so no pointer points into raw stream bytes. A
   token that lands on an unmapped offset raises `ERR_DROP`. Aliases (`-2`) keep using the
   side table in `db_relocation.cpp`, which already stores native pointers without widening
   the packed slot.
4. **Layout-invariant bytes stay put.** Vertex, index, pixel, raw-file and string bytes are the
   same at both widths. They stay in the zone blocks, and native pointers point straight at
   them. Only records that change layout go through the arena.
5. **Native arenas.** Each zone owns arenas that mirror its block lifetimes: runtime blocks live
   until the zone unloads, and temp blocks are freed after the load. Arena exhaustion is an
   error, not a fallback. Top-level asset headers need none: `DB_AddXAsset` copies the native
   header into the pool. At 64-bit a header slot holds the zero-extended disk32 token on entry
   to the family loader and the native pointer on return. The 64-bit envelope
   (`db_disk32_envelope.cpp`) writes it so: it streams the retail `XAssetList` root, script
   strings and 8-byte `XAsset` records through the `db_xasset_disk32.h` iterators at their
   retail offsets, checks every asset type first, builds the native 16-byte array in the zone's
   native storage (`DB_AllocZoneNative`) and dispatches each asset through `Load_XAsset`.
6. **Fail closed per family.** At 64-bit, the `Load_XAssetHeader` dispatch refuses any family
   whose generated loader is not yet enabled. It raises `ERR_DROP` naming the family before it
   reads any of the family's bytes. The whole zone fails, since the stream cannot be skipped.
7. **FX.** The hand-written FX converter becomes the oracle for the generated FX loader. Diff
   the two on the same bytes, then keep one.

## Server-closure families

A headless server loads `code_post_gfx_mp`, `localized_code_post_gfx_mp`, `common_mp`,
`localized_common_mp`, `mod` when present (`CL_InitDedicated`), then the map zone. It skips
`ui_mp`. Because nothing can be skipped, every family the MP build admits
(`RequirementForAssetType` in `db_asset_mode.h`) is in the closure. That is **N = 25**. Every
header record in the table below changes size at 64-bit.

Consumer: **server** means server code reads the asset. **parse** means the asset must still be
materialized because other assets point at it, but only client code consumes it.

| Id | Family | Header 32→64 | Consumer | Wave |
|---|---|---|---|---|
| 0x1F | RawFile | 12→24 | server | 1 |
| 0x20 | StringTable | 16→24 | server | 1 |
| 0x01 | PhysPreset | 44→56 | server | 1 |
| 0x16 | LocalizeEntry | 8→16 | server | 2 |
| 0x0F | MapEnts | 12→24 | server | 2 |
| 0x0E | GameWorldMp | 4→8 | server | 2 |
| 0x0C | ComWorld | 16→24 | server | 2 |
| 0x08 | SoundCurve | 72→80 | parse | 2 |
| 0x11 | LightDef | 16→32 | parse | 2 |
| 0x13 | Font | 24→40 | parse | 2 |
| 0x14 | MenuList | 12→24 | parse | 2 |
| 0x1A | ImpactFx | 8→16 | parse | 2 |
| 0x07 | Sound | 12→24 | parse | 3 |
| 0x09 | LoadedSound | 44→64 | parse | 3 |
| 0x06 | Image | 36→48 | parse | 3 |
| 0x04 | Material | 80→104 | parse | 3 |
| 0x05 | TechniqueSet | 148→296 | parse | 3 |
| 0x19 | Fx | 32→40 | parse | 3 |
| 0x17 | Weapon | 2168→2832 | server | 3 |
| 0x02 | XAnimParts | 88→136 | server | 4 |
| 0x00 | XModelPieces | 12→24 | server | 4 |
| 0x03 | XModel | 220→280 | server | 4 |
| 0x0B | ClipMapPvs | 284→480 | server | 4 |
| 0x10 | GfxWorld | 732→1032 | parse | 4 |
| 0x15 | Menu | 284→360 | parse | 4 |

The MP build rejects the other eight families: ClipMap and GameWorldSp are SP-only, and UiMap,
SndDriverGlobals, AiType, MpType, Character and XModelAlias are unavailable. XModelPieces has no
top-level dispatch; `Load_DynEntityDef` reaches it.

## Clone sizes

`DB_GetXAssetSizeHandler` takes one `sizeof` per header type (row 21), so
`DB_CloneXAssetInternal` copies native sizes at 64-bit.

## K4: loader closure

**K4 = the number of server-closure families (out of N = 25) that load from unmodified Steam 1.8
`.ff` at 64-bit under ASan and UBSan.** Baseline 0/25. Target 25/25 (G2). Until the loader tests
report K4 themselves, [NOW.md](../NOW.md) records it by hand. K1–K3 are defined in
[NATIVE64.md](NATIVE64.md).

A family counts toward K4 when all hold:

- every asset of that family in the test zones loads with no sanitizer report;
- its digest (`db_graphhash <family>`, `db_graph_digest.cpp`) equals the Windows x86 load's
  on the same zones;
- the family's fail-closed guard goes only in the PR that makes it count.

K4 is a server gate. A client builds renderer objects from Image, Material,
TechniqueSet and GfxWorld (`kCreatesRendererObjects`) and keeps LoadedSound
samples, which no client leg has compared with x86, so `IsK4Verified` lifts
each only under `KISAK_DEDI_HEADLESS` (Image, Material, LoadedSound so far).

## Test plan

| Layer | What runs | Where |
|---|---|---|
| Generator | The emitted mirrors pass the `ONDISK_*`/`RUNTIME_*` asserts; a broken generator fails the family tests | Linux test build, every PR |
| Family | Hand-built disk32 byte fixtures per family, including malformed tokens, unmapped offsets and arena exhaustion | Linux test build, ASan/UBSan leg |
| Zone | One synthetic `.ff` holding all 25 families, loaded through `DB_LoadXAssets` and the real dispatch, pools and unload, its cross-family references checked; each family's guard, left on, fails it closed (`db_disk32_zone_tests.cpp`) | Linux and macOS test builds, ASan/UBSan leg |
| Real data | Load the four code and common zones plus the boot map at 64-bit under ASan/UBSan, and compare the graph digest against x86 | Manual owner runs only; retail data never enters CI |

Only the real-data layer moves K4.

## Retail runs (manual)

Retail data stays on the agent hosts and never enters CI or the repo
([AGENTS.md](../../AGENTS.md)); evidence is text. A 64-bit headless run:

```sh
cmake -S . -B build-headless -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DKISAK_BUILD_MP=OFF -DKISAK_BUILD_DEDICATED=ON -DKISAK_BUILD_SP=OFF \
  -DKISAK_DEDI_HEADLESS=ON -DKISAK_ENABLE_STEAM=OFF -DKISAK_ALLOW_UNSUPPORTED_64BIT=ON
build-headless/bin/KisakCOD-dedi +set fs_basepath <install> +set fs_homepath <scratch> \
  +set db_unverified64 1 +map mp_crash +db_graphhash rawfile
```

Use a scratch `fs_homepath`; afterwards nothing under `fs_basepath` may be
newer than the run. Read the console for:

- `db_unverified64: admitting …` once, then `Loaded zone '<zone>'` per zone;
- `Waited N msec for asset '<name>'`: normal; names the asset in flight;
- `db_graphhash <family>: N assets, sha256 …`: must equal the x86 run's;
  `list` adds per-asset digests;
- the first `Invalid fast-file …` or `KisakCOD fatal error` line: the stop.
  `no 64-bit on-disk/runtime layout pair` means the guard (no opt-in);
  `alias offset: unregistered slot` means an unrecorded pointer slot.

**K4 legs** compare headless servers (clients also load UI); Windows
legs run the PR's CI artifacts, one `+db_graphhash` per verified family on
the same command line. `list` lines name retail assets: post only family lines. Walker rules
head `db_graph_digest.cpp`.

**Registry sessions.** A sequence that holds `db_hashCritSect` while its
assets intern or mark script strings (user 4 or 8) must hold it through
`DB_BeginRegistrySession`/`DB_EndRegistrySession`, not `Sys_LockWrite`. The
bridge can't take the hash its own thread holds, so the mark fails. This
covers unloads, shutdown and the post-load override link (#600). Sessions
don't nest, and an error is reported only after the hash is released.

# Fast-file loader: two layouts

| Owner | Gate | KPI | Workstream |
|---|---|---|---|
| WS-2 beads | G2 | K4 | [ROADMAP](../ROADMAP.md) |

Retail `.ff` files are read unmodified ([CHARTER](../CHARTER.md)). The disk format is the 32-bit
(ILP32) in-memory image of every asset, so a 64-bit loader has to translate every record it
reads. Layout classes and conventions (`ONDISK_*`, `RUNTIME_SIZE`) are defined in
[NATIVE64.md](NATIVE64.md). This doc covers the loader.

## Problem

- **Pointer slots are 4 bytes on disk.** Each `Load_*Ptr` in `db_load.cpp` streams 4 bytes
  into a native pointer field (`Load_Stream(..., 4)`), then tests the value: `-1` means inline
  data follows, `-2` means shared-inline (an alias the loader must register), and anything else
  is a block/offset token. Comparisons such as `== (SoundFile *)-1` bake in 32-bit pointers.
- **Records are read in place.** `Load_Stream` copies disk bytes over the runtime struct. That
  only works while `sizeof(runtime) == sizeof(disk)`.
- **The offset converters narrow.** `DB_ConvertOffsetToPointer`, `DB_ConvertOffsetToAlias` and
  `DB_ConvertOffsetToCString` in `db_stream_load.cpp` resolve a token and then write back 4
  bytes. They raise `ERR_DROP` ("does not fit the 32-bit runtime") only when the target is above
  4 GiB. A 64-bit load whose blocks land below 4 GiB passes that check and leaves the upper half
  of every pointer holding stale stream bytes. **That is not failing closed.**
- **Most records change layout.** 129 of the 202 record types `db_load.cpp` walks through `var*`
  cursors change size between i686 and x86_64. 92 of those 129 have no size assert of any kind.
  Examples: `WeaponDef` 2168→2832, `clipMap_t` 284→480, `GfxWorld` 732→1032, `XAnimParts` 88→136,
  `MaterialTechniqueSet` 148→296.
- **Few disk32 mirrors exist.** Mirror structs exist only for `XAsset`, `XAssetList` and
  `ScriptStringList` (`db_xasset_disk32.h`, with fail-closed iterators) and for FX
  (`fx_fastfile_disk32.h` with a native converter and arena). The FX path has zero production
  callers. Headless excludes `EffectsCore`, and `db_fx_zone_adapter_wiring_headless.cpp`
  returns null, so FX is **not wired for headless**. `db_disk32.h` otherwise has only the token
  grammar (`PointerToken`, `Ptr32`, `DecodeOffset`) and named byte extents.
- **Nothing can be skipped.** An `XAssetList` stream has no per-asset lengths. To reach byte
  *n* the loader must walk every asset before it, so a zone loads only if every family in it
  parses.

## Design

1. **Schema once per family.** Describe each family's disk32 layout once, in a declarative
   schema: records, field offsets, scalars, fixed arrays, counted pointers (count expression),
   inline strings, script strings, asset references (alias kind), discriminated unions, stream
   block push/pop and delayed streams (`Load_DelayStream`).
2. **Generate both loaders.** From the schema, the generator emits:
   - the disk32 mirror struct, with `ONDISK_SIZE` and `ONDISK_OFFSET` asserts;
   - the 32-bit `Load_*`, which keeps today's in-place semantics so Windows x86 behaviour does
     not change;
   - the 64-bit `Load_*`, which reads the mirror, allocates the runtime record in a native
     arena, converts it field by field and resolves tokens.

   Generated code is not committed ([AGENTS.md](../../AGENTS.md) rule 8). It is built from the
   schema at configure or build time.
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
   error, not a fallback.
6. **Fail closed per family.** At 64-bit, the `Load_XAssetHeader` dispatch refuses any family
   whose generated loader is not yet enabled. It raises `ERR_DROP` naming the family before it
   reads any of the family's bytes. The whole zone fails, since the stream cannot be skipped.
   Bead 7 in [NOW.md](../NOW.md) installs this guard before any family converts.
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

## Order

- **Wave 1 (bead 12 spike): RawFile, StringTable, PhysPreset.** Together they cover inline
  strings, a pointer array of strings, invariant byte buffers and aliases. The spike converts
  all three and loads them from a real `.ff` at 64-bit.
- **Wave 2:** flat records with a few pointers.
- **Wave 3:** medium graphs: the sound chain, the material chain, FX, and Weapon (large but
  flat, with many asset references).
- **Wave 4:** deep graphs: XAnimParts, XModel, ClipMapPvs (collision, DynEnt, physics), GfxWorld
  and Menu (expression trees).

G2 needs all 25 families, because the boot map zone plus the four code and common zones touch
most of them.

## Clone size table

`DB_CloneXAssetInternal` in `db_registry.cpp` does
`memcpy(to, from, DB_GetXAssetTypeSize(type))`. The size comes from `DB_GetXAssetSizeHandler`
in `db_assetnames.cpp`. That table inherited the original compiler's identical-function folding,
so 11 entries call another type's size function. The sizes match at 32-bit, but five of them
diverge at 64-bit:

| Family | Table uses | Size at 64-bit | Effect |
|---|---|---|---|
| PhysPreset | `sizeof(GameWorldSp)` | 88, real 56 | over-read and over-write |
| LoadedSound | `sizeof(GameWorldSp)` | 88, real 64 | over-read and over-write |
| ClipMap | `sizeof(menuDef_t)` | 360, real 480 | partial copy |
| ClipMapPvs | `sizeof(menuDef_t)` | 360, real 480 | partial copy (MP) |
| LightDef | `sizeof(StringTable)` | 24, real 32 | partial copy |

**Fix:** one `sizeof` per asset type, taken from the `XAssetHeader` member type, with a
`static_assert` per entry. Add the missing header types to `XAssetSize` so its assert covers
the largest. This fix is independent of the generator, so do it first (bead 10).

## K4: loader closure

**K4 = the number of server-closure families (out of N = 25) that load from unmodified Steam 1.8
`.ff` at 64-bit under ASan and UBSan.** Baseline 0/25. Target 25/25 (G2). Until the loader tests
report K4 themselves, the value is recorded by hand in [NOW.md](../NOW.md). K1–K3 are defined in
[NATIVE64.md](NATIVE64.md).

A family counts toward K4 when all of these hold:

- every asset of that family in the test zones loads with no sanitizer report;
- its `db::graph_hash` digest (`db_graph_hash.cpp`) equals the Windows x86 load of the same
  zone;
- the family's fail-closed guard is removed only in the PR that makes it count.

## Test plan

| Layer | What runs | Where |
|---|---|---|
| Generator | The emitted mirror passes the `ONDISK_*` asserts. The 32-bit output matches today's `Load_*` behaviour on synthetic streams | Linux test build, every PR |
| Family | Hand-built disk32 byte fixtures per family, including malformed tokens, unmapped offsets and arena exhaustion | Linux test build, ASan/UBSan leg |
| Real data | Load the four code and common zones plus the boot map at 64-bit under ASan/UBSan, and compare the graph digest against x86 | Needs owner data: a licensed runner or manual owner runs |

The real-data layer is the only one that moves K4. It needs owner-provided Steam 1.8 files
(`blocked-owner` in [NOW.md](../NOW.md)). `tests/retail_fastfile_parity_harness.cpp` today
hashes only the file envelope. Extending it to hash the loaded graph is part of bead 12.
Platform file I/O for async reads belongs to [PLATFORM_POSIX.md](PLATFORM_POSIX.md).

## Open questions

- Which families actually occur in the four zones and the boot map? Measure this on the first
  owner data run. The answer can only reorder waves; N stays 25 while nothing can be skipped.
- Schema format: a C++ constexpr DSL or an external file plus a Python generator. Decide in
  bead 12.

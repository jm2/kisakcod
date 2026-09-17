# Native ABI and asset closure ledger (production MP capability)

Ledger for [fork issue #129](https://github.com/jm2/kisakcod/issues/129) (roadmap
item A07). It inventories every registered asset family, its transitive
pointer-bearing subobjects, the frozen Disk32 schema surface, the native runtime
owner, the reader/converter and publication/unload paths, and the current
production-caller/enrollment state — so the remaining M4/M5 queue ("remaining
runtime structures", "broader asset relocation") can be scheduled per family
instead of as one opaque block.

**Basis tree:** `a1ca543b28391f347d4127797d971fdc0bc5a199` (`origin/master`,
post-#138/#139). File and line references in §1–§8 are exact at that commit;
treat later drift the way `docs/CODEBASE_AUDIT.md` does — the tree wins.

**Corrective revision (ki-shmhh, 2026-09-17):** sections §9–§13 are the
corrective evidence supplement for issue #129 acceptance criteria 1–3 (the
grouped family rows in §4 do not by themselves enumerate every transitive
pointer-bearing subobject, per-family invariants, or exact test receipts).
Every citation in §9–§13 was verified at `ba508d1521f832701fee73dd6adf8514ad9b8ff5`
(`origin/master`, post-#141-merge `df654591`, post-#146/#147/#154); the local
test receipt in §11.1 is from this exact tree. The §4 narrative stays as
merged; where a §4 citation is superseded by a tighter §9 citation, the §9
citation wins.

**Governance:** this ledger records capability and enrollment state only. It is
governed by [#122](https://github.com/jm2/kisakcod/issues/122) and
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). It makes **no**
compatibility claim: both commercial references (original 1.7 and Steam 1.8)
remain unpinned, no commercial interop cell has passed, and missing
reference/runner evidence stays a blocker to certification. It does not retire
any native64 gate.

---

## 1. Status vocabulary

Each family row uses these labels; they are deliberately narrower than "done".

| Label | Meaning |
|---|---|
| **Frozen** | Wire/disk layout pinned by `ONDISK_SIZE`/`ONDISK_OFFSET` assertions or exact byte constants; independent of native `sizeof`. |
| **Widened** | Native runtime struct follows `RUNTIME_SIZE(T, n32, n64)` dual contract (or explicit `uintptr_t`/fixed-width fields) and passes cross-width layout tests. |
| **Raw-width** | Native runtime struct still relies on ILP32 layout assumptions; appears in `tests/abi_sizeof_debt.allow` or equivalent debt. |
| **Converted reader** | Bounded, token-validated Disk32 reader/converter exists (iterator/preflight style), distinct from the decompiled fixup reader. |
| **Decompiled reader** | Production `db_load.cpp`-style `Load_*` pointer-fixup path; portable-compiling, audited in subpaths, not a bounded rewrite. |
| **Enrolled** | A real production call path executes this code in a shipping build profile. |
| **Build-enrolled, zero-caller** | Compiled into production targets and sealed, but no production call path consumes it (production-neutral by design). |
| **Deferred (SP)** | SP-only persistence surface; tracked separately, not on the MP critical path. |

## 2. Cross-family invariants (apply to every row)

### 2.1 Pointer-token grammar (`src/database/db_disk32.h`)

All fast-file pointer fields are 4-byte `disk32::PointerToken` values:

| Token | Meaning | Current enforcement |
|---|---|---|
| `0x00000000` | Null | Accepted only where the retail format allows absence. |
| `0x0???????`–offset | One-based offset; high nibble = zone block, low 28 bits = offset (`kOffsetMask = 0x0FFFFFFF`) | `DecodeOffset()` validates block index and `offset + requiredBytes <= blockSize` before any use. |
| `0xFFFFFFFF` (`kInline`) | Inline data follows in the stream | Reader allocates in stream/zone storage. |
| `0xFFFFFFFE` (`kSharedInline`) | Shared-inline back-reference | **Rejected** where the retail walk has no shared-inline protocol (script-string token walk, `db_xasset_disk32.h` "UnsupportedSharedInline"); honored only where retail inserts pointers (MssSound shared data, §4.4). |

`Ptr32<T>` is the single canonical packed pointer field type ("do not stand up a
parallel `Ptr32<T>`" — `docs/PORTING.md` §8). Resolution goes through
`DecodeOffset` + the zone block table; widening to native pointers happens only
in the load-time relocation pass. High-address behavior (tokens at the top of
the 32-bit space, nonzero high bytes, block-4 tail) is covered by
`tests/disk32_tests.cpp` and the FX Disk32 suites.

### 2.2 Frozen-layout contracts (`src/universal/kisak_abi.h`)

- `ONDISK_SIZE`/`ONDISK_OFFSET` freeze the **original 32-bit** size/offset.
- `RUNTIME_SIZE(T, n32, n64)` pins the ILP32 value and the native64 value per
  target.
- Serialization never uses native `sizeof`; every Disk32 extent is a named
  constant (e.g. `kMaterialPassBytes = 20`, `kGfxWorldBytes = 732`,
  `kPathNodeBytes = 128`, `kSndAliasBytes = 92`) in `db_disk32.h`.
- Wire/disk enum values are pinned by `static_assert` against the engine enum
  (`src/database/database.h` ↔ `db::asset_mode`), so the IW3 fast-file asset-type
  contract cannot drift.

This is the mechanical form of the #122 rule: native pointers, host padding and
widened runtime sizes never become wire/disk data.

### 2.3 Atomic seven-site script-string enrollment (`db_load_legacy_bridge`)

The only **production-enrolled** portion of the runtime-ownership stack. All
seven legacy script-string/registry sites were cut over atomically in `0d5a7558`
through the five-method `DbLoadLegacyBridge` (begin facade access → begin
standalone registry ownership → typed operation → finish registry ownership →
finish facade access):

| # | Site | File |
|---|---|---|
| 1 | temporary `SL_GetStringOfSize` user-4 claim (`DbLoadLegacyBridge::TryInternUser4StringOfSize`) | `src/database/db_stream_load.cpp:244` |
| 2 | temporary `SL_GetStringOfSize` user-4 claim (`DbLoadLegacyBridge::TryInternUser4StringOfSize`) | `src/database/db_stream_load.cpp:315` |
| 3 | `SL_AddUser` user-4 reference (`DbLoadLegacyBridge::TryAddUser4`) | `src/database/db_stringtable_load.cpp:27` |
| 4 | dynamic-default `SL_GetString(name, 4)` intern (`DbLoadLegacyBridge::TryInternUser4String`) | `src/database/db_registry.cpp:1858` |
| 5 | dynamic-default `SL_GetString(name, 4)` re-intern (`DbLoadLegacyBridge::TryInternUser4String`) | `src/database/db_registry.cpp:3557` |
| 6 | `SL_TransferSystem(4, 8)` global sweep (`DbLoadLegacyBridge::TryTransferUsers4To8`) | `src/database/db_registry.cpp:3524` (in `DB_FreeUnusedResources`) |
| 7 | `SL_ShutdownSystem(8)` global sweep (`DbLoadLegacyBridge::TryShutdownUser8`) | `src/database/db_registry.cpp:3581` (in `DB_FreeUnusedResources`) |

Source contracts require **zero raw sites** and freeze the exact bridge-call
counts, so partial enrollment fails the seals
(`tests/db_load_legacy_bridge_production_seal_tests.cpp`,
`tests/db_load_legacy_bridge_tests.cpp`). **Invariant: this enrollment must stay
atomic — no family may partially re-open a raw site, and no new runtime-ownership
consumer may bypass the facade.** The subsequent MSVC `/WX` repairs are recorded
in `docs/task.md` (commit `4859c9ee`, CI runs 30367496573 / 30369149465).

### 2.4 Native64 gates still armed

| Gate | File | Current count | Meaning |
|---|---|---|---|
| Pointer-truncation tripwire | `tests/pointer_truncation_test.cmake` + `pointer_truncation.allow` | 26 tracked sites | Fails on new pointer→narrow-int truncations; allowlist burndown is mandatory as sites are fixed. It is an **inventory, not completion proof** (`docs/task.md`). |
| Direct-literal sizeof ABI debt | `tests/abi_sizeof_debt_test.cmake` + `abi_sizeof_debt.allow` | 238 entries | Every raw `sizeof(X)==N` assertion is keyed; M1/M4 work removes entries, additions require debt review. Formula-form entries are separated (`abi_sizeof_formula_debt.allow`, 13 entries). |
| Headless include debt | `tests/headless_include_debt.allow` + `headless_profile_test.cmake` | 28 entries | Client/media leakage into the headless profile fails the gate. |
| Engine configuration gate | `CMakeLists.txt` fail-closed platform gate | — | `KISAK_BUILD_*` engine configs fail configure until the POSIX/Win64 engine composition lands; one test executable linking does not retire it. |

No gate may be relaxed for a capability that has not demonstrated a real
production path (roadmap completion rules; #129 acceptance criterion 6).

## 3. Asset-type registry (the 33 fast-file families)

Enum: `src/xanim/xanim.h` (`XAssetType`, values frozen "Accurate to SP/MP
(Win32)"). Build-mode admission: `src/database/db_asset_mode.h`
(`RequirementForAssetType`), enforced at load with `ERR_DROP` in
`Mark_XAssetHeader` (`src/database/db_load.cpp`) and pinned by
`tests/db_asset_mode_tests.cpp`.

| # | Type | Mode requirement | Headless MP relevance |
|---:|---|---|---|
| 0x00 | XModelPieces | Shared | Required (model closure) |
| 0x01 | PhysPreset | Shared | Required (physics) |
| 0x02 | XAnimParts | Shared | Required (model closure) |
| 0x03 | XModel | Shared | Required (model closure) |
| 0x04 | Material | Shared | Parse/ownership required; render consumers client-only |
| 0x05 | TechniqueSet | Shared | Parse/ownership required; render consumers client-only |
| 0x06 | Image | Shared | Parse/ownership required; realization is null in headless |
| 0x07 | Sound | Shared | Parse/ownership required; playback client-only |
| 0x08 | SoundCurve | Shared | Parse/ownership required; playback client-only |
| 0x09 | LoadedSound | Shared | Parse/ownership required; playback client-only |
| 0x0A | ClipMap | SinglePlayer | Not loaded in MP builds (admission-rejected) |
| 0x0B | ClipMapPvs | Multiplayer | Required (collision) |
| 0x0C | ComWorld | Shared | Required (world closure) |
| 0x0D | GameWorldSp | SinglePlayer | Not loaded in MP builds (admission-rejected) |
| 0x0E | GameWorldMp | Multiplayer | Required (world closure) |
| 0x0F | MapEnts | Shared | Required (world closure) |
| 0x10 | GfxWorld | Shared | Parse/ownership required; render consumers client-only |
| 0x11 | LightDef | Shared | Parse/ownership required; render consumers client-only |
| 0x12 | UiMap | Unavailable | Rejected in all current builds |
| 0x13 | Font | Shared | Parse required; glyph consumers client-only |
| 0x14 | MenuList | Shared | Full-MP client UI only; headless loads zones without menus |
| 0x15 | Menu | Shared | Full-MP client UI only |
| 0x16 | LocalizeEntry | Shared | Required for string resolution |
| 0x17 | Weapon | Shared | Required (MP gameplay) |
| 0x18 | SndDriverGlobals | Unavailable | Rejected in all current builds |
| 0x19 | Fx | Shared | Required (FX closure) |
| 0x1A | ImpactFx | Shared | Required (FX closure) |
| 0x1B | AiType | Unavailable | Rejected in all current builds |
| 0x1C | MpType | Unavailable | Rejected in all current builds |
| 0x1D | Character | Unavailable | Rejected in all current builds |
| 0x1E | XModelAlias | Unavailable | Rejected in all current builds |
| 0x1F | RawFile | Shared | Required (scripts/config/pure) |
| 0x20 | StringTable | Shared | Required |
| — | (virtual) String 0x21 / AssetList 0x22 | — | In-memory only; not fast-file types |

Envelope schemas for the serialized container itself are Frozen + Converted
Reader: `XAssetListDisk32` (0x10), `XAssetDisk32` (0x08),
`ScriptStringListDisk32` (0x08), `ScriptStringTokenDisk32` (0x04) with
fail-closed iterators (`src/database/db_xasset_disk32.h/.cpp`; tests
`db_xasset_disk32_tests.cpp` + source seal).

## 4. Family ledgers

### 4.1 Zone runtime foundation (script strings, streams, PMem, durable table)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Frozen: script-string list/token records, script-string walker (`kInline` preserved verbatim, shared-inline rejected, unaligned reads checked) | `db_xasset_disk32.*`, `db_script_string_disk32_*` tests |
| Native runtime owner | Widened ownership stack, **build-enrolled zero-caller except §2.3**: 33-slot durable runtime table (fixed layouts frozen), script-string OwnershipBatch/journal/transaction, PMem checked scopes + serialized runtime, stream ownership, pending-copy ledger (2,048 records / 8 generations), registry coordinator, process-lifetime facade | `src/database/db_zone_*`, `db_registry_ownership_coordinator.*`, `src/universal/physicalmemory*`; per-component `*_production_seal_tests.cpp` |
| Reader/converter | Converted reader for the container + string list; per-asset bodies still decompiled readers (§4.2–4.7) | `db_xasset_disk32.cpp` |
| Publication/unload | Bridge-enrolled `SL_TransferSystem`/`SL_ShutdownSystem` sweeps in `DB_FreeUnusedResources`; retry-safe Live-unload and terminal-reset adapters exist production-neutral | `db_registry.cpp`; `db_zone_runtime_table` adapters |
| Production enrollment | **Seven sites only** (§2.3). Table/facade/PMem/stream/pending have zero production callers by design. **Missing enrollment = every downstream asset family's staged load/commit routing through the durable table/facade.** The one enrolled exception runs outside this seam: the FX lease-bound archive restore is reached from the client archive paths (`cl_cgame.cpp:1220`, `cl_cgame_mp.cpp:1287` via `FX_Archive`) — client-side, not headless-server-reachable — while fast-file FX/impact adapter conversion is itself still zero-caller (§4.3) | `docs/task.md` M5 rows |
| Requirement | Headless MP: required (script strings feed every family) | — |

### 4.2 Model & animation (XModel 0x03, XAnimParts 0x02, XModelPieces 0x00)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Partial: model pieces/stringtable/string extents frozen in `db_disk32.h` (`kXModelPieceBytes`…); surfaces/collision via `kXSurfaceCollision*Bytes`; **no full portable XModel/XAnim body schema module** — bodies are read by the decompiled fixup readers | `db_disk32.h`; `db_load.cpp` `Load_XAnim*`/`Load_XSurface*` |
| Native runtime owner | XAnim/DObj/model-surface streams widened (merged M1/M4 work); **MP pose, BreakablePiece, DynEntity physics ownership still raw-width** — `sizeof(BreakablePiece)==0xC` sits at the top of the sizeof-debt ledger; **XAnimParts/XAnimIndices payload consumers still raw-width on native64**: production has not adopted `XAnimPartsNative` — `XAnimClone` allocates a hardcoded 88 bytes (`src/xanim/xanim.cpp:152`), the load-object path asserts `sizeof(XAnimParts)==88` (`src/xanim/xanim_load_obj.cpp:998`), and `xanim_native.h:37-43` records consumer migration as follow-up (64-bit runtime view is 0x88) | `docs/task.md` M4; `tests/abi_sizeof_debt.allow`; **owner: PR #99 / `ki-v4m` (open)**; XAnim payload consumer migration: no separate owner bead — tracked in §6.3 per `docs/task.md` |
| Reader/converter | Load-object route bounded via `buf_cursor` with nested cursor ownership + checked second-pass rewind **in flight** (PR #140 / `ki-okmr`, production stage of #124); fast-file route is the decompiled reader; `fuzz_fastfile` is a primitive harness, not production-parser coverage | `src/xanim/xmodel_load_obj.cpp`, `src/xanim/buf_cursor.*`; **#124/`ki-ym2r`, #125 (A03) owners** |
| Publication/unload | `Load_XModelAsset`/`Mark_XModelAsset` registration; DObj create/clone/unarchive failure-atomic transaction (merged P2 work); unload = zone free + DObj pool accounting | `db_registry.cpp`; `dobj_management.cpp` |
| Test evidence | `xmodel_load_test.cpp`, `xanim_load_test.cpp`, `xanim_parts_split_test.cpp`, `model_surface_stream_tests.cpp`, `skel_memory_atomic_tests.cpp`, nested-cursor suite on #140 | tests/ |
| Requirement | Headless MP: required (skeleton/anim closure); rendering consumers client-only | — |

### 4.3 Effects (Fx 0x19, ImpactFx 0x1A) — most-complete Disk32 family

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Frozen: archive effect/system/buffer/body mirrors, fast-file effect/visual/trail/impact schemas, native64 `fx_archive_disk32` stride contracts | `src/EffectsCore/fx_archive_*_disk32.*`, `fx_fastfile_*` |
| Native runtime owner | Widened: zero native atomics in EffectsCore, exact-width runtime records, native FX arena inside the zone-runtime slab plan | `fx_runtime.h`; PRs #2–#4, #33, #59 lineage |
| Reader/converter | Converted reader: bounded two-pass effect/impact converters, frozen resolver transactions, retained-extent overlap checks, callback-free materialization, publish-after-materialize ordering. **Two distinct consumers, not one seam:** (a) the lease-bound `FX_Restore` archive reader/candidate path, enrolled through the client archive paths; (b) the fast-file adapter conversion `TryWireEffectDefThroughActiveFxZoneAdapter`/`TryWireImpactTableThroughActiveFxZoneAdapter`, which converts only under an active zone-adapter binding. Restore-side native64 guard/raw parser removed | `fx_convert.cpp`, `fx_archive_restore_*`, `db_fx_zone_adapter_wiring.*`; `docs/task.md` M5 row |
| Publication/unload | Zone-adapter wiring with fail-closed headless bridge; physics batch control + rollback recipes sealed | `db_fx_zone_adapter_wiring*.cpp` (+ headless variant), `fx_archive_physics_batch_control.*`, `tests/db_fx_zone_adapter_wiring_production_call_site_tests.cpp` |
| Production enrollment | **Two seams with opposite states — do not conflate.** (a) Lease-bound `FX_Restore` archive restore: **enrolled** through the client archive paths (`cl_cgame.cpp:1220`; `cl_cgame_mp.cpp:1287` via `FX_Archive`) — client-side, not headless-server-reachable. (b) Fast-file FX/impact conversion: `Load_FxEffectDef` (`src/database/db_load.cpp:7126`) and `Load_FxImpactTable` (`src/database/db_load.cpp:8928`) call the `TryWire*` adapters, but these return null unless a binding is active; bindings are enrolled only by the zone runtime-table controller (`db_zone_runtime_table.cpp:2347`, `:4358`) after a `ZoneRuntimeFacade::TryBindStorage` receipt (`db_zone_runtime_facade.cpp:674`), which has **zero production callers** — so every shipping profile executes the decompiled fallback readers and this seam remains **build-enrolled, zero-caller**. Physics/impact **live** enrollment still gated on the open FX/impact path item | `docs/task.md` "Enroll the guarded native FX/impact path…" (unchecked) |
| Test evidence | ~30 `fx_*` test/suite files (archive, reader, native, fastfile, restore, visibility, sidecar) | tests/ |
| Requirement | Headless MP: required (FX is a shared asset type; headless realizes null but must parse/own) | — |

### 4.4 Sound (0x07/0x08/0x09)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Frozen extents: `kSoundFileBytes=12`, `kSpeakerMapBytes=408`, `kSndAliasBytes=92`; MssSound header read as fixed 40 bytes | `db_disk32.h`; `db_load.cpp` `Load_MssSound` |
| Alias/shared-inline semantics | **Inserted-pointer family**: `MssSound.data < 0xFFFFFFFE` → alias to zone bytes (`DB_ConvertOffsetToAlias`, `DBAliasKind::SoundData`); `0xFFFFFFFE` → fresh allocation **registered as inserted pointer** for later sharing; `0xFFFFFFFF` → fresh allocation, unshared. Every `LoadedSound` owns and frees its processed buffer (no cross-asset aliasing) | `db_load.cpp:2529` (`Load_MssSound`) |
| Native runtime owner | Sound family runtime structures remain Miles-shaped; playback is a client-only dependency (Miles) — replacement tracked by A10/upstream #76, not this ledger | `src/sound/`; **A10/#132 owner** |
| Reader/converter | Decompiled reader (`Load_snd_alias_list_t`, `Load_StreamedSound`, `Load_SoundFileRef`); no portable sound schema rewrite | `db_load.cpp` |
| Publication/unload | `Load_/Mark_snd_alias_list_Asset`; unload via `DB_FreeUnusedResources` sweeps (bridge-enrolled §2.3) + zone free | `db_registry.cpp` |
| Test evidence | `sound_dry_send_source_test.cmake` (focused); no sound loader runtime suite | tests/ |
| Requirement | Headless MP: parse/ownership required; output devices client-only | — |

### 4.5 Collision & physics (ClipMapPvs 0x0B, ComWorld 0x0C, GameWorldMp 0x0E, PhysPreset 0x01)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Frozen extents: `kBrushWrapperBytes=80`, `kCBrushBytes=80`, `kCBrushSideBytes=12`, `kCPlaneBytes=20`, `kPhysGeomInfoBytes=68`, `kPhysGeomListBytes=44`, `kGameWorldSpBytes=44`, `kPathDataBytes=40`, `kPathNode*` family | `db_disk32.h` |
| Alias semantics | Brush-side plane tokens: null and shared-inline (`0xFFFFFFFE`) are **hard errors** (`ERR_DROP`); inline allocates in block-4 with `DB_IsStreamRangeValid` check; offsets decode normally. This is the audited inserted-pointer rejection pattern for physics subobjects | `src/database/db_load.cpp:5224-5258` (`Load_BrushWrapper` side-plane token walk) |
| Native runtime owner | ODE user-data + physics pools widened (merged); **DynEntity/BreakablePiece/pose ownership raw-width pending #99**; physics sidecar authority sealed macro-off | `tests/abi_sizeof_debt.allow`; **owner: PR #99 / `ki-v4m`** |
| Reader/converter | Decompiled readers with audited token checks (above); pathdata/AI nodes shared-extent constants exist but no converted walker | `db_load.cpp` |
| Publication/unload | `Load_/Mark_PhysPresetAsset`; collision registration on map load; rollback recipes sealed on the physics sidecar | `tests/physics_rollback_recipe_source_test.cmake` |
| Test evidence | `phys_resource_pair_tests.cpp`, `phys_user_geom_storage_tests.cpp`, `ode_fixed_pool_occupancy_tests.cpp`, `physics_transaction_source_test.cmake`, `disk32_tests.cpp` | tests/ |
| Requirement | Headless MP: **required** (collision/path data are server-reachable) | — |

### 4.6 World & renderer-facing (GfxWorld 0x10, LightDef 0x11, MapEnts 0x0F, Material 0x04, TechniqueSet 0x05, Image 0x06)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | Frozen extents for world subobjects: `kGfxWorldBytes=732`, `kGfxCellBytes=56`, `kGfxBrushModelBytes=56`, `kGfxAabbTreeBytes=44`, `kGfxPortalBytes=68`, `kGfxCullGroupBytes=32`, `kGfxReflectionProbeBytes=16`, `kGfxTextureBytes=4`, `kDpvsPlaneBytes=20`, material family (`kMaterialVertexDeclarationBytes=100`, `kMaterialPassBytes=20`, …) | `db_disk32.h` |
| Native runtime owner | Renderer worker queue/DObj/model-surface widened (merged P1/P4); gfx_d3d itself remains the x86 decompile — **not** a headless-MP blocker, is a full-client one (A09/#131) | `docs/task.md`; **A09 owner** |
| Reader/converter | Decompiled readers; material texture/constant/state inline tokens handled in the fast-file path (`db_load.cpp` `Load_GfxTextureLoad` et al.); no bounded world-graph rewrite — actual graph walking is exactly the missing instrument below | `db_load.cpp` |
| Graph walker | **Missing production walker.** `ki-msb` #113 provides the capture/protocol envelope only; no real x86/native64 graph walks over licensed assets exist. Hash/decoder instruments must not be presented as graph parity | **owner: PR #113 / `ki-msb`**; roadmap A07 row |
| Publication/unload | `DB_RemoveGfxWorld` / `DB_RemoveComWorld` unregistration; `DB_GetVertexBufferAndOffset`/index helpers for zone handles | `db_registry.cpp` |
| Test evidence | `renderer_reservation_atomic_tests.cpp`, `renderer_value_encoding_tests.cpp` (client-side); no world-parse runtime suite | tests/ |
| Requirement | Headless MP: parse/ownership of world assets required; render realization client-only. Full MP: complete renderer path | — |

### 4.7 UI, script data & localization (MenuList 0x14, Menu 0x15, LocalizeEntry 0x16, Font 0x13, Weapon 0x17, RawFile 0x1F, StringTable 0x20)

| Aspect | State | Evidence / owner |
|---|---|---|
| Frozen Disk32 schema | `kStringTableBytes=16` frozen; weapon bounce-sound table fixed at 29 entries (`kWeaponBounceSoundCount`); menu/font/localize bodies read by decompiled readers | `db_disk32.h`; `db_load.cpp` |
| Native runtime owner | Weapon family widened where network-visible (`weapon_input_safety`, `missile_layout` contracts); UI safety contracts merged; menu dynamic-clone path unchanged | `tests/weapon_*`, `tests/ui_safety_*` |
| Reader/converter | Decompiled readers; stringtable entries flow through the bridge-enrolled `SL_AddUser` site (§2.3 #3) | `db_stringtable_load.cpp` |
| Publication/unload | `DB_DynamicCloneMenu`, `DB_RemoveWindowFocus` (client-only paths); RawFile/StringTable published into asset pools for script/pure consumption | `db_registry.cpp` |
| Test evidence | `ui_safety_tests.cpp`, `hudelem_sort_*`, `upstream_command_dispatch_tests.cpp` | tests/ |
| Requirement | Menus/fonts: full-MP client only (headless loads zones without UI realization). Weapon/RawFile/StringTable/Localize: **headless-MP required** | — |

### 4.8 Save/demo records (separate from fast-file persistence)

| Aspect | State | Evidence / owner |
|---|---|---|
| tagInfo save record | **Converted and merged on GitHub master (PR #89 / `ki-f0w`). Do not recreate.** Pointer-bearing 112-byte Disk32 record (`tagInfo_s`, 0x70 x86 / 0x78 x64 host shape) converted via entity-map arena; production-path coverage through the `g_save.cpp` `SF_TYPE_TAG_INFO` branches | `src/game/g_save.cpp:780`, `:830`, `:1044`; `tests/save_taginfo_tests.cpp`, `save_taginfo_production_tests.cpp` |
| Remaining save family | **Deferred (SP) by scope**: full SP save/load sizing debt (`g_save.cpp` array sizing), MP-required script VM persistence tracked with the VM owner below; demo decode is the A05 oracle input, not proof of simulation parity | `docs/CODEBASE_AUDIT.md` SP debt; **A05/#127, #119 owners** |
| Requirement | Headless MP: no save path. Full MP client: profile/config only. SP persistence: deferred | — |

### 4.9 Script VM (GSC runtime)

| Aspect | State | Evidence / owner |
|---|---|---|
| Native runtime owner | **Raw-width in-tree.** Script-value/native-layout widening, production VM stack execution and MP-required archive paths are the open PR #119 scope (now review-ready; its body contains an older survey checkpoint — reconcile acceptance with its final diff) | **owner: PR #119 / `ki-n1et`**; roadmap row |
| Script strings | Widened + bridge-enrolled (§2.3, §4.1); memory-tree lease + OwnershipBatch merged production-neutral | `src/script/scr_stringlist.cpp`, `scr_memorytree.cpp` |
| Reader/converter | `scr_readwrite` MP archive path unchanged; full SP save/load tracked separately (deferred) | `src/script/scr_readwrite.cpp` |
| Test evidence | `script_string_atomic_tests.cpp`, `script_string_ownership_tests.cpp`, `script_memorytree_*`, `runtime_scalar_determinism_*` (utility-level, not engine parity) | tests/ |
| Requirement | Headless MP: **required** (GSC drives server gameplay) | — |

### 4.10 Graph parity instrument (cross-family evidence tooling)

| Aspect | State | Evidence / owner |
|---|---|---|
| Walker | Canonical graph-hash/capture **protocol** exists (envelope, self-tests); **no real x86 or native64 walker has run over licensed assets** — envelope evidence is not M5 graph parity | **owner: PR #113 / `ki-msb`**; roadmap A07 row |
| Consumption | The M5 exit ("load an unmodified retail fast-file on native64 and hash-match its widened runtime graph against the Windows x86 reference") is blocked on this instrument plus per-family conversion rows above | `docs/task.md` M5 |

## 5. Production-caller map (summary)

| Surface | Production callers today |
|---|---|
| Seven script-string/registry sites via `DbLoadLegacyBridge` | **7 (atomic, sealed)** |
| FX lease-bound archive restore (`FX_Restore` reader/candidate) | **1 path** — client archive-restore callers only (`cl_cgame.cpp:1220`; `cl_cgame_mp.cpp:1287` via `FX_Archive`); client-side, not headless-reachable |
| FX fast-file adapter conversion (`TryWire*` / `TryBindStorage` binding) | **0** — `Load_FxEffectDef`/`Load_FxImpactTable` take the decompiled fallback readers in every shipping profile |
| Durable runtime table / facade / coordinator / PMem checked scopes / stream ownership / pending-copy ledger / script-string OwnershipBatch / Live-unload & terminal-reset adapters | **0** (build-enrolled, source-sealed, deliberately production-neutral) |
| `buf_cursor` bounded load-object reads | Enrolled on the x86 load-object route; nested-ownership correction in flight (#140) |
| Native64 production engine | **None** (configuration gate armed; M6+ targets undelivered) |

Anything claiming "converted helper in use" must name its row here. Rows with
zero callers are foundations, not enrolled capabilities.

## 6. Headless-MP closure map (what blocks a real headless server)

Ordered, per-family blockers with owners — this is the "next integrated
checkpoint" queue from `docs/task.md`, refined to ledger rows:

1. **Runtime ownership enrollment beyond the seven sites** — staged
   load/commit/unload routing per family through the durable table/facade
   (foundation exists, zero callers). *M5 work; no dedicated bead yet — schedule
   per family after #99/#119 land.*
2. **Physics ownership conversion** (#99 / `ki-v4m`, open): pose,
   BreakablePiece, DynEntity — required before DynEnt-bearing maps load native64.
3. **XAnimParts/XAnimIndices payload consumer migration** (server-reachable
   native64 blocker; no dedicated owner bead — #129 scope per `docs/task.md`):
   production consumers still allocate and assert the retail 88-byte payload
   (`src/xanim/xanim.cpp:152`, `src/xanim/xanim_load_obj.cpp:998`) while the
   64-bit runtime view is 0x88; `xanim_native.h` stages the split but records
   consumer adoption as follow-up. XAnim closure is headless-MP required
   (§3), so this blocks a real headless server.
4. **Script VM widening** (#119 / `ki-n1et`): server gameplay requires the VM;
   serialized formats must not change (Roadmap: "preserving serialized
   formats").
5. **Model cursor production correction** (#140 / `ki-okmr` → #124): nested
   ownership + checked rewind; then real cold/warm loader tests (#124
   acceptance).
6. **World/collision parse hardening + real graph walks** (#113 / `ki-msb`
   instrument over licensed assets): needed for the M5 hash-match exit.
7. **Production fuzz coverage** (#125 / A03): fast-file harness currently models
   selected reads, not the production DB/XModel/FX composition.
8. **Guarded native FX/impact live path** (`docs/task.md` open item): rollback,
   high-address, alias, unload, slot-reuse coverage — including fast-file
   adapter binding enrollment (§4.3 seam b, zero-caller today).
9. **Engine composition** (A08 / #130, consuming `ki-eudd` sockets + `ki-vuj`
   console, merged): first real Win64/Linux headless link, then retail-map
   closure per this ledger, then commercial sessions (#122).

SP-only persistence (full save/load), client rendering (A09), audio/cinematics
(A10) are explicitly **not** headless blockers.

## 7. Owner cross-reference

| Family / row | Owner bead | PR | State at basis |
|---|---|---|---|
| MP pose / BreakablePiece / DynEntity ownership | `ki-v4m` | [#99](https://github.com/jm2/kisakcod/pull/99) | Open |
| Script VM widening / persistence | `ki-n1et` | [#119](https://github.com/jm2/kisakcod/pull/119) | Open (review-ready) |
| Graph parity instrument | `ki-msb` | [#113](https://github.com/jm2/kisakcod/pull/113) | Open |
| Model nested cursor + second pass | `ki-okmr` (stage of `ki-ym2r`/#124) | [#140](https://github.com/jm2/kisakcod/pull/140) | Open |
| tagInfo save conversion | `ki-f0w` | [#89](https://github.com/jm2/kisakcod/pull/89) | **Merged — credited, do not recreate** |
| Scalar determinism | `ki-jgz` | [#116](https://github.com/jm2/kisakcod/pull/116) | Merged |
| Portable UDP service | `ki-eudd` | [#106](https://github.com/jm2/kisakcod/pull/106) | Open |
| Console/POSIX preparation | `ki-vuj` | [#108](https://github.com/jm2/kisakcod/pull/108) | **Merged 2026-09-10** |
| CI/release scaffolding | `ki-yvj` | [#107](https://github.com/jm2/kisakcod/pull/107) | Draft |
| Recursive deletion | `ki-3iv` | [#114](https://github.com/jm2/kisakcod/pull/114) | Draft |
| Filesystem NT ABI members / directory identity | `ki-9650` | [#139](https://github.com/jm2/kisakcod/pull/139) | **Merged (in basis tree)** |
| This ledger | `ki-mtmw` | (#129) | This document |

Schedule new work against these owners; do not assign duplicate implementations.

## 8. Explicit non-claims

- **No compatibility claim.** Perfect interoperation with original commercial
  1.7 and Steam commercial 1.8 (#122) is unproven; reference manifests are
  pending; community CoD4x/fork-only tests are not substitutes
  (`NETWORK_COMPATIBILITY.md`).
- **No native64 retirement.** The pointer-truncation tripwire (26 sites),
  sizeof-debt ledger (238 + 13 entries) and the engine configuration gate stay
  armed. They are inventories and fail-closed gates, not completion evidence.
- **No enrollment inflation.** "Build-enrolled, zero-caller" foundations are not
  delivered production capabilities; helper-only coverage does not complete a
  milestone (`docs/task.md`, roadmap completion rules).
- **Wire/disk preservation.** Nothing in this ledger authorizes widening any
  serialized format; #122's byte-level rules govern every future family change.

---

## 9. Transitive pointer-bearing subobject inventory (corrective, criterion 1)

This section enumerates, per family, **every pointer-bearing subobject walk**
executed by the production fast-file readers in `src/database/db_load.cpp`
(the `Load_*` decompiled fixup readers) and their `Mark_*` twins. Columns:
the subobject field; the loader symbol; the exact site (file:line at
`ba508d15`); the allocation/Disk32 schema; the alias/inline grammar; and the
failure path. "zone bump" means `DB_AllocStreamPos` into the active zone
stream block (`src/database/db_stream.cpp:274-291`; `ERR_DROP` on bad
alignment state `:278` / block overrun `:286`) through the decompile's
misnomer shims — `AllocLoad_raw_byte` = unaligned bump
(`db_load.cpp:1934-1937`), `AllocLoad_XBlendInfo` = 2-byte bump
(`db_load.cpp:1903-1906`), `AllocLoad_FxElemVisStateSample` = 4-byte bump
(`db_load.cpp:5049-5052`), `AllocLoad_GfxPackedVertex0` = 16-byte bump
(`db_load.cpp:3110-3113`), `AllocLoad_raw_uint128` = 128-byte bump
(`db_load.cpp:1848-1851`). None of these allocates the struct its name
suggests; they are IW-decompile artifacts and the names must not be read as
type information.

Shared failure grammar used by every row below: pointer-field tokens are
4-byte values compared against `-1` (inline) / `-2` (shared-inline) — the
`disk32::kInline`/`kSharedInline` sentinels (`db_disk32.h:10-11`), usually
spelled as literal `-1`/`-2` casts in the decompiled readers; offset tokens
resolve via `DB_ConvertOffsetToAlias` (`db_stream_load.cpp:131-166`,
`ERR_DROP` `:138/:152-155/:160`), `DB_ConvertOffsetToPointer`
(`db_stream_load.cpp:168-209`), `DB_ConvertOffsetToCString`
(`db_stream_load.cpp:211-227`), or deferred `DB_ResolveDirectPointer`;
inserted-pointer publication is `DB_InsertPointer` (`db_stream.cpp:324-346`)
+ `DB_SetInsertedPointer` (`db_stream.cpp:348-409`); completed shared
objects are `DB_RegisterPointerSlot` (`db_stream.cpp:304-322`) +
`DB_CompleteObject` (`db_stream.cpp:411+`, per-kind schema validation
`:428-482`). `DBAliasKind` values: `db_relocation.h:36-85`; exact-start
publication classes: `db_relocation.h:87-114`.

### 9.0 Container envelope (every zone)

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| asset list container | `XAssetListDisk32` | `db_xasset_disk32.h:44-48`, `ONDISK_SIZE 0x10` `:85` | Frozen | fail-closed iterator | iterator status, atomic |
| per-asset header | `XAssetHeaderDisk32` | `ONDISK_SIZE 0x04` `db_xasset_disk32.h:55` | Frozen | — | — |
| asset pointer + type | `XAssetDisk32` | `ONDISK_SIZE 0x08` `:62` | Frozen | `Ptr32<XAssetDisk32>` | — |
| string list / token | `ScriptStringListDisk32` / `ScriptStringTokenDisk32` | `ONDISK_SIZE 0x08` `:70` / `0x04` `:78` | Frozen | `kInline` preserved verbatim; shared-inline rejected (`UnsupportedSharedInline`) | fail-closed iterator |
| script-string index → value | `Load_ScriptStringCustom` | `db_stringtable_load.cpp:4-22` | — | 16-bit index into `stringList.strings` | `ERR_DROP` bad index `:10`; `ERR_DROP` exceeds 16-bit runtime `:18` |

### 9.1 XAnimParts (0x02) — walk `Load_XAnimParts` (`db_load.cpp:2342-2405`), entry `Load_XAnimPartsPtr` (`:2407-2441`), dispatch `:11342-11345`

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 88-byte header | `Load_Stream` | `db_load.cpp:2344` | zone bump; **no `disk32::` extent constant** (88 literal; runtime assert `sizeof(XAnimParts)==88` in `xanim_load_obj.cpp:998`, tests) | — | — |
| `name` | `Load_XString` | `:2346-2347` | zone C-string | inline `-1` / block-4 offset (`Load_XString` `:1979-1997`) | — |
| `names[boneCount]` (script strings) | `Load_ScriptStringArray` | `:2350-2352` | zone bump (2-byte) | — | — |
| `notify[notifyCount]` | `Load_XAnimNotifyInfoArray` → `Load_XAnimNotifyInfo` | `:2356-2358`, `:2320-2340` | zone bump (4-byte) | notify name script string `:2323-2324` | — |
| `deltaPart` | `Load_XAnimDeltaPart` | `:2362-2364` | zone bump (4-byte) | — | — |
| `deltaPart->trans` | `Load_XAnimPartTrans` | `:2184-2186`, hdr `:2304-2318` | zone bump | — | `MyAssertHandler` `:2307`, `:2310-2315` |
| `trans->u` frames/indices | `Load_XAnimPartTransFrames` / `Load_XAnimDynamicIndicesTrans` | `:2272-2288` / `:2196-2232` | in-place stream | — | `MyAssertHandler` `:2275`, `:2278-2283`, `:2205`, `:2208-2213`, `:2220`, `:2222-2228` |
| `deltaPart->quat` | `Load_XAnimDeltaPartQuat` | `:2190-2192`, hdr `:2170-2177` | zone bump | — | iassert `:2172`, `:2174` |
| `quat->u` frames | `Load_XAnimDeltaPartQuatDataFrames` | `:2133-2154` | zone bump (4-byte) `AllocLoad_FxElemVisStateSample` `:2142` | — | iassert `:2135`, `:2137` |
| quat frame indices | `Load_XAnimDynamicIndicesDeltaQuat` | `:2109-2131` | in-place stream | — | iassert `:2117`, `:2119`, `:2125`, `:2127` |
| `dataByte[dataByteCount]` | `Load_byteArray` | `:2368-2370` | zone bump (unaligned) | — | — |
| `dataShort[dataShortCount]` | `Load_shortArray` | `:2374-2376` | zone bump (2-byte) | — | — |
| `dataInt[dataIntCount]` | `Load_intArray` | `:2380-2382` | zone bump (4-byte) | — | — |
| `randomDataShort` | `Load_shortArray` | `:2386-2388` | zone bump (2-byte) | — | — |
| `randomDataByte` | `Load_byteArray` | `:2392-2394` | zone bump (unaligned) | — | — |
| `randomDataInt` | `Load_intArray` | `:2398-2400` | zone bump (4-byte) | — | — |
| `indices` (≥0x100 frames / below) | `Load_XAnimIndices` | `:2402-2403`, def `:2090-2107` | ushort path `AllocLoad_XBlendInfo` `:2096`; byte path `AllocLoad_raw_byte` `:2103` | — | — |
| parts pointer token | `Load_XAnimPartsPtr` | `:2407-2441` | inline alloc `:2419`; shared-inline `-2` → `DB_InsertPointer(DBAliasKind::XAnimParts)` `:2422`, publish `DB_SetInsertedPointer` `:2428-2431`; offset → `DB_ConvertOffsetToAlias(…, XAnimParts)` `:2435-2437` | registration `Load_XAnimPartsAsset` `:2426` (`db_registry.cpp:907-910`) | alias-converter `ERR_DROP`s |

### 9.2 XModel (0x03) — walk `Load_XModel` (`db_load.cpp:5762-6013`), entry `Load_XModelPtr` (`:6015-6054`), dispatch `:11346-11349`; validation `DB_ValidateLoadedXModel` `:5474-5760`

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 220-byte header (incl. inline lodInfo) | `Load_Stream` | `:5764` | zone bump; **no `disk32::` extent** (220 literal) | — | collision-layout precheck `ERR_DROP` `:5778`; checked-count helpers `:5781-5812` |
| `name` | `Load_XString` | `:5814-5815` | zone C-string | — | — |
| `boneNames[numBones]` | `Load_ScriptStringArray` | `:5820-5822` | zone bump (2-byte) | offset→ptr `DB_ConvertOffsetToPointer` `:5826-5830` | — |
| `parentList[nonRootBoneCount]` | `Load_byteArray` | `:5837-5839` | zone bump (unaligned) | convert `:5843-5847` | — |
| `quats[nonRootTransformCount]` | `Load_shortArray` | `:5854-5856` | zone bump (2-byte) | convert `:5860-5864` | — |
| `trans[nonRootTransformCount]` | `Load_floatArray` | `:5871-5873` | zone bump (4-byte) | convert `:5877-5881` | — |
| `partClassification[numBones]` | `Load_byteArray` | `:5888-5890` | zone bump (unaligned) | convert `:5894-5898` | — |
| `baseMat[numBones]` | `Load_DObjAnimMatArray` (def `:2492-2495`) | `:5905-5907` | zone bump (4-byte) | convert `:5911-5915` | — |
| `surfs[numsurfs]` | `Load_XSurfaceArray` (def `:3438`) | `:5920-5922` | zone bump (4-byte) | — | false → `:5922-5926` |
| `materialHandles[numsurfs]` | `Load_MaterialHandleArray` | `:5930-5932` | zone bump (4-byte) | — | — |
| `collSurfs[numCollSurfs]` | `Load_XModelCollSurfArray` (def `:5162`; elem walk `Load_XModelCollSurf` `:5090-5160`, 44-byte header `:5092`, `collTris` `:5110-5114`, 48 B/tri `:5087`) | `:5936-5943` | zone bump (4-byte) | — | `ERR_DROP` `:5105`, `:5128`, `:5136`, `:5144`, `:5155` |
| `boneInfo[numBones]` | `Load_XBoneInfoArray` (def `:2487-2490`, 40 B) | `:5951-5953` | zone bump (4-byte) | — | — |
| `physPreset` | `Load_PhysPresetPtr` | `:5955-5956` | — | PhysPreset alias (§9.6) | — |
| `physGeoms` | `Load_PhysGeomList` (def `:5426`; §9.6) | `:5961-5978`, resolve `:5991-5995` | zone bump (4-byte) | — | `ERR_DROP "Cannot allocate fast-file physics geometry list"` `:5964` |
| final validation | `DB_ValidateLoadedXModel` | `:6002-6010` | — | — | 13 `ERR_DROP` sites `:5514`, `:5602`, `:5612`, `:5637`, `:5647`, `:5664`, `:5680`, `:5689`, `:5696`, `:5727`, `:5736`, `:5748`, `:5756` |
| model pointer token | `Load_XModelPtr` | `:6015-6054` | inline alloc `:6027`; shared-inline → `DB_InsertPointer(DBAliasKind::XModel)` `:6030`, publish `:6040-6044`; offset → alias `:6048-6050` | registration `Load_XModelAsset` `:6039` (`db_registry.cpp:917-920`) | — |

`Load_XSurface` chain (`db_load.cpp:3314-3436`; called from `:5920`):

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 56-byte surface header | `Load_XSurface` (`:3322`), layout `DB_GetXSurfaceLayout` (`:289`) | `:3324`, `:3327` | zone bump; **no `disk32::` extent** (56 literal) | — | false `:3330-3333` |
| `zoneHandle` | `Load_XZoneHandle` (`:3314`) | `:3346-3347` | — | — | — |
| `vertInfo` (12 B) + `vertsBlend[blendCount]` | `Load_XSurfaceVertexInfo` (`:3264`) → `Load_XBlendInfoArray` | `:3348-3349`, `:3289-3291` | zone bump (2-byte) | direct-ptr convert `:3295-3299` | — |
| `verts0[vertCount]` (16 B/vertex) | `Load_GfxPackedVertex0Array` | `:3355-3357` | 16-byte bump `AllocLoad_GfxPackedVertex0` | convert (kDirectBlock7) `:3361-3365` | — |
| `vertList[vertListCount]` | `Load_XRigidVertListArray` (def `:3221`) | `:3374-3386` | zone bump (4-byte) | alias `:3395-3398` | `ERR_DROP "Cannot allocate fast-file rigid-vertex lists"` `:3377`; bounds `ERR_DROP` `:3235` |
| `vertList->collisionTree` | `Load_XRigidVertList` (`:3176`) → `Load_XSurfaceCollisionTree` (`:3124`) | `:3186-3198`, complete `:3200-3208`, alias `:3212-3215` | zone bump (4-byte); tree extents frozen: `kXSurfaceCollisionTreeBytes=40` / `kXSurfaceCollisionNodeBytes=16` / `kXSurfaceCollisionLeafBytes=2` (`db_disk32.h:61-63`), header `kXRigidVertListBytes=12` (`:64`) | — | `ERR_DROP "Cannot allocate fast-file surface collision tree"` `:3189`; nodes block-4 extent `ERR_DROP` `:3147`; leaves `ERR_DROP` `:3162` |
| `triIndices[indexCount]` | `Load_r_index16_tArray` | `:3406-3408` | 16-byte bump | convert (kDirectBlock8) `:3412-3416` | — |
| surface validation | `DB_ValidateLoadedXSurface` (def `:396`) | `:3420-3434` | — | — | `ERR_DROP` `:434`, `:458`, `:480` |

### 9.3 XModelPieces (0x00) — **no top-level fast-file dispatch**: `ASSET_TYPE_XMODELPIECES` has no case in `Load_XAssetHeader`/`Mark_XAssetHeader` (verified: zero occurrences in `db_load.cpp`/`db_registry.cpp` dispatch switches); type 0x0 falls through to `ERR_DROP "Unsupported fast-file asset type"` (`:11440`). The family is reached transitively: `Load_DynEntityDef` → `Load_XModelPiecesPtr` (`:7300`, def `:6143`), walk `Load_XModelPieces` (`:6114-6141`)

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 12-byte header | `Load_XStream` inline + `DB_ValidateXModelPiecesHeader` (def `:195`) | `:6116-6121` | zone bump; frozen `kXModelPiecesBytes=12` (`db_disk32.h:60`) | — | header validator |
| `name` | `Load_XString` | `:6124-6125` | zone C-string | — | — |
| `pieces[numpieces]` | `Load_XModelPieceArray` (def `:6081`) | `:6128-6135` | zone bump (4-byte); frozen `kXModelPieceBytes=16` (`db_disk32.h:59`) | — | `ERR_DROP "Cannot allocate fast-file model pieces"` `:6131`; array extent `ERR_DROP` `:6095` |
| `piece->model` | `Load_XModelPiece` (`:6071`) → `Load_XModelPtr(0)` | `:6077-6078` | streamed in `kXModelPieceBytes` | XModel alias (§9.2) | — |
| pieces pointer token | `Load_XModelPiecesPtr` | `:6143-6180` | inline alloc `:6153`; slot `DB_RegisterPointerSlot(DBAliasKind::XModelPieces)` `:6156-6158`, `DB_CompleteObject` `:6164-6172`; alias `:6176-6179` | — | alloc `ERR_DROP` `:6153` |

### 9.4 Material (0x04) — walk `Load_Material` (`db_load.cpp:4427-4637`), entry `Load_MaterialHandle` (`:4639`), dispatch `:11350-11353`

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 80-byte root | `Load_Stream` | `:4429` | zone bump; **no `disk32::` extent** (80 literal) | — | pointer-count sanity `DB_ValidatePointerCount` `:4430-4444`; extent precompute `ERR_DROP` `:4459` |
| `info` (24 B) + `info.name` | `Load_MaterialInfo` (def `:3079-3084`) | `:4463-4464` | zone bump | — | — |
| `techniqueSet` | `Load_MaterialTechniqueSetPtr` (def `:4386`; §9.5) | `:4465-4466` | — | techset alias | false `:4469` |
| `textureTable` (empty-present) | inline | `:4471-4501` | — | empty-span inline token check `disk32::kInline` `:4483-4487`; direct offset `:4494-4498`; nulled `:4500` | `ERR_DROP "Could not align empty material texture table"` `:4487` |
| `textureTable` (inline) | `Load_MaterialTextureDefArray` (def `:4243`) | `:4502-4534` | 4-byte bump `:4504`; per-def 12 B (`kMaterialTextureDefBytes`, `db_disk32.h:27`) | — | alloc `ERR_DROP` `:4507`; slot fail `:4514-4518`; complete fail `:4525-4533`; unordered `ERR_DROP` `:4261` |
| `textureTable` (offset) | `DB_ConvertOffsetToAlias(…, MaterialTextureTable)` | `:4538-4541` | frozen schema kind | shared alias | converter `ERR_DROP`s |
| `textureTable[i].u.image` | `Load_GfxImagePtr` (via `Load_MaterialTextureDef` `:4223`, info walk `:4160-4218`) | `:4212-4213` | — | image alias (§9.6) | `ERR_DROP "Fast-file material texture has no image"` `:4216` |
| `textureTable[i].u.water` (semantic 11) | `Load_MaterialTextureDefInfo` water branch + `Load_water_t` (def `:3588`; §9.6) | `:4169-4207` | 4-byte bump `:4172`; slot `MaterialWater` `:4175-4177`; `DB_CompleteObject(kMaterialWaterBytes)` `:4192-4200` | alias fallback `:4204-4207` | `ERR_DROP "Fast-file water texture has no definition"` `:4166`; alloc fail `:4173-4174` |
| `constantTable` (empty-present) | inline | `:4544-4571` | — | empty-span `disk32::kInline` check `:4553-4557`; direct 16-byte `:4564-4568`; nulled `:4570` | `ERR_DROP "Could not align empty material constant table"` `:4557` |
| `constantTable` (inline) | `Load_MaterialConstantDefArray` (def `:4269`, 32 B stride `:4271`) | `:4572-4577` | 16-byte bump `:4574` | — | — |
| `constantTable` (offset) | `DB_ConvertOffsetToPointer` | `:4580-4584` | — | direct block-4 | — |
| `stateBitsTable` (empty-present) | inline | `:4587-4614` | — | empty-span check `:4596-4600`; direct `:4607-4611`; nulled `:4613` | `ERR_DROP "Could not align empty material state table"` `:4600` |
| `stateBitsTable` (inline) | `Load_GfxStateBitsArray` (def `:4007`, 8 B `:4009`) | `:4615-4620` | 4-byte bump `:4617` | — | — |
| `stateBitsTable` (offset) | `DB_ConvertOffsetToPointer` | `:4623-4627` | — | direct block-4 | — |
| semantics check | `DB_ValidateMaterialSemantics` | `:4630-4634` | — | — | false `:4633` |

### 9.5 TechniqueSet (0x05) + shaders — walk `Load_MaterialTechniqueSet` (`db_load.cpp:4332-4384`), entry `Load_MaterialTechniqueSetPtr` (`:4386-4425`), dispatch `:11354-11357`

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| techset pointer token | `Load_MaterialTechniqueSetPtr` | `:4386-4425` | inline alloc `:4398`; shared-inline → `DB_InsertPointer(MaterialTechniqueSet)` `:4400-4401`, publish `:4410-4414`; alias `:4418-4420` | — | load fail pop+false `:4404-4408` |
| 148-byte root | `Load_Stream` | `:4334` | zone bump; **no `disk32::` extent** (148 literal) | — | `ERR_DROP "Invalid fast-file material technique set header"` `:4341` |
| `name` | `Load_XString` | `:4347-4348` | zone C-string | — | `ERR_DROP` no name `:4380` |
| `techniques[34]` pointer array | `Load_MaterialTechniquePtrArray` (def `:4315`) / `Load_MaterialTechniquePtr` (def `:4274`) | `:4349-4353`, per-slot `:4322-4328` | per-tech inline alloc `:4281`; slot `MaterialTechnique` `:4282-4284`; `DB_CompleteObject(kMaterialTechniqueSchema)` `:4294-4299`; alias `:4306-4309` | — | array fail `:4353`; slot/complete fails `:4285-4286`, `:4301` |
| technique header (8 B) + validation | `Load_MaterialTechnique` (def `:4112`) | `:4119-4131` | frozen `kMaterialTechniqueHeaderBytes=8` (`db_disk32.h:18`) | — | `ERR_DROP` `:4116`, `:4128`, `:4154` |
| `passArray[passCount]` (inline) | `Load_MaterialPassArray` (def `:4091`) | `:4132-4134` | frozen `kMaterialPassBytes=20` (`db_disk32.h:19`); schema pair `kMaterialTechniqueSchema` (`:20-21`) | — | false `:4134`; renderer-variant mix `ERR_DROP` `:4144-4146` |
| pass root + validation | `Load_MaterialPass` (def `:4012`) | `:4014-4033` | — | — | `ERR_DROP "Invalid fast-file material pass header"` `:4031` |
| `pass->vertexDecl` (inline) | `Load_MaterialVertexDeclaration` (def `:3897`) | `:4034-4058` | 4-byte bump `:4038`; slot `MaterialVertexDeclaration` `:4039-4041`; `DB_CompleteObject` `:4050-4058`; frozen `kMaterialVertexDeclarationBytes=100` (`db_disk32.h:17`) | alias `:4062-4065` | slot `:4042-4043`; decl `:4046`; complete `:4057` |
| `pass->vertexShader` / `pixelShader` | `Load_MaterialVertexShaderPtr` (def `:3790`) / `Load_MaterialPixelShaderPtr` (def `:3855`) | `:4068-4073` | inline allocs `:3797`/`:3862`; `DB_CompleteObject(kMaterialVertexShaderBytes/kMaterialPixelShaderBytes)` `:3806-3811` (frozen 16 B, `db_disk32.h:22-23`); on complete-fail COM release `:3813-3817` (pixel mirror `:3878-3882`) | alias `:3823-3826` (pixel mirror) | variant mix `ERR_DROP` `:4077`; false `:4070`, `:4073` |
| shader root + program | `Load_MaterialVertexShader` (`:3767`) → `Load_MaterialVertexShaderProgram` (`:3729`) → `Load_GfxVertexShaderLoadDef` (`:3673`); pixel mirrors `:3832`/`:3748`/`:3701` | `:3769-3744` | program payload = `programSize` DWORDs after `DB_ValidateMaterialShaderLoadDef` `:3679-3686`, alloc `:3688-3690`, `Load_DWORDArray` `:3691-3692`, validate `:3693-3698`; frozen `kMaterialShaderProgramBytes=12` / `kMaterialShaderLoadDefBytes=8` (`db_disk32.h:24-25`) | — | validate `:3685`; alloc `:3690`; program `:3693-3698`; headless null `:3743-3744` |
| `pass->args[argCount]` | `Load_MaterialShaderArgumentArray` (def `:3992`) → `Load_MaterialArgumentDef` (def `:3934`) | `:4080-4087` | 4-byte bump `:4082`; 8 B/arg (`:3987`) | literal types 1/7: null `ERR_DROP` `:3942`; inline `-1` alloc `:3947` + `Load_floatArray(1,4)` `:3949`; offset `:3953-3957` | arg validation `:4085-4086` |
| techset-wide consistency | inline | `:4355-4376` | — | — | `ERR_DROP` `:4370-4372` |

### 9.6 Image (0x06) + water — walk `Load_GfxImage` (`db_load.cpp:3532-3541`), entry `Load_GfxImagePtr` (`:3543-3577`), dispatch `:11358-11361`

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| image pointer token | `Load_GfxImagePtr` | `:3543-3577` | inline alloc `:3555`; shared-inline → `DB_InsertPointer(GfxImage)` `:3557-3558`, publish `:3563-3567`; alias `:3571-3573` | — | — |
| 36-byte root | `Load_Stream` | `:3534` | zone bump; **no `disk32::` extent** (36 literal; runtime `static_assert` `r_gfx.h:231`) | — | — |
| `name` | `Load_XString` | `:3536-3537` | zone C-string | — | — |
| `texture` union (4 B) | `Load_GfxTextureLoad` (def `:3455`) | `:3538-3539` | frozen `kGfxTextureBytes=4` (`db_disk32.h:41`) | inline `-1/-2` test `:3466` | — |
| `texture.loadDef` (inline) | `Load_GfxImageLoadDef` (def `:3515`) | `:3468-3488` | 16-byte header `:3520` (guarded by `OFFSET_TO_GfxImageLoadDef_DATA == 16` iassert `:3519`) + `resourceSize` payload bytes `:3528-3529`, streamed in place | publish `DB_SetInsertedPointer(…, mapType)` `:3483-3488` | asserts `:3518-3527`; headless finalize `DB_FinalizeHeadlessTextureLoad` `:3480-3481` |
| `texture` (offset) | `DB_ConvertOffsetToAlias(…, GfxTexture, mapType)` | `:3492-3495` | — | shared alias + COM `AddRef()` `:3500` / headless null `:3503` | converter `ERR_DROP`s |
| `water_t` (68 B) + H0/wTerm samples | `Load_water_t` (def `:3588-3660`), `DB_ValidateWaterHeader` (def `:720-742`) | header `:3595-3601`, `H0` `:3604-3611`, `wTerm` `:3613-3620`, spans `:3622-3651`, `image` `:3652-3658` | frozen `kMaterialWaterBytes=68` (`db_disk32.h:26`) | — | header `ERR_DROP` `:736`; span/address/finite checks `:3622-3651` |
| raw texture arrays (world lightmaps/probes) | `Load_GfxRawTextureArray` (def `:3510-3513`, 4 B stride) | `:10868`, `:10912`, `:10920` | zone bump | plain stream — **no** per-texture alias walk | — |

Mark twins: `Mark_MaterialTextureDefInfo` `:4694-4709`, `Mark_GfxImagePtr` `:3579-3586`, `Mark_water_t` `:3662-3666`.

### 9.7 Sound family (0x07 Sound, 0x08 SoundCurve, 0x09 LoadedSound) — dispatch `:11362-11373` (load) / `:11494-11505` (mark)

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| `LoadedSound` root (44 B) + `name` | `Load_LoadedSound` (def `:2579`) | `:2581`, `:2583-2584` | zone bump; **no `disk32::` extent** (44 literal; `static_assert` `snd_public.h:110`) | C-string inline/offset via `Load_XString` | — |
| `LoadedSound->sound` (MssSound) | `Load_MssSound` (def `:2529`; see alias row) | `:2585-2586` | 40-byte header `:2536` (literal) | **inserted-pointer family** — below | — |
| `MssSound.data` (offset `<0xFFFFFFFE`) | `DB_ConvertOffsetToAlias(…, SoundData, data_len)` | `:2541-2550` | alias to zone bytes (`data_len` metadata match) | shared back-reference to an earlier producer | `ERR_DROP` alias `db_stream_load.cpp:152-155` |
| `MssSound.data` (`0xFFFFFFFE` shared producer) | `AllocLoad_raw_byte` + `DB_InsertPointer(SoundData)` + `DB_SetInsertedPointer` | `:2551-2568` (insert `:2557-2558`, publish `:2563-2568`) | payload = `info.data_len` bytes `:2561` | registered inserted pointer for later sharing | zone-range `ERR_DROP "Fast-file sound alias source is outside zone memory"` `db_stream.cpp:362-367` |
| `MssSound.data` (`0xFFFFFFFF` inline) | same, `inserted = {}` | `:2557-2560` | fresh allocation, unshared | none published | — |
| processed playback buffer | `Load_SetSoundData` → `SND_SetData` (`src/sound/snd.cpp:4834-4883`) | `:2519-2527`, call `:2549`/`:2562` | `MSS_Alloc` `snd.cpp:4867`/`:4878`; headless no-op + `DB_ClearHeadlessSoundRuntimeData` (`:768-776`, call `:2575`) | — | — |
| LoadedSound pointer token | `Load_LoadedSoundPtr` (def `:2590`) | `:2590-2620` | inline alloc `:2602`; shared-inline `DB_InsertPointer(LoadedSound)` `:2605`, publish `:2611-2614`; alias `:2618-2620` | — | — |
| `snd_alias_list_t` root (12 B) | `Load_snd_alias_list_t` (def `:2842`) | `:2844` | zone bump; **no `disk32::` extent** (12 literal; `static_assert` `snd_public.h:210`) | — | span `ERR_DROP` `:51` (from `:2845`); count `ERR_DROP` `:64` (from `:2849`); present-empty `ERR_DROP` `:2858`; no name `ERR_DROP` `:2866` |
| `head[count]` (snd_alias_t array) | `Load_snd_alias_tArray` (def `:2821`) | `:2870-2898` | inline alloc `:2874`; slot `SndAliasArray` `:2875-2877`; `DB_CompleteObject` `:2889-2894` (exact-start kind); alias `:2902-2905` | — | register/load/complete fails → pop+return `:2878-2888`, `:2895-2898` |
| `snd_alias_t` element (92 B = `kSndAliasBytes`) | `Load_snd_alias_t` (def `:2738`) | header `:2740-2743` | frozen `kSndAliasBytes=92` (`db_disk32.h:34`; `static_assert` `snd_public.h:202`) | — | completed-alias `ERR_DROP "Invalid completed fast-file sound alias"` `:641` (from `:2816`) |
| `aliasName`/`subtitle`/`secondaryAliasName`/`chainAliasName` | `Load_XString` ×4 | `:2744-2751` | zone C-string | — | — |
| `soundFile` (12 B = `kSoundFileBytes`) | `Load_SoundFile` (def `:2647`) | `:2752-2782` | inline alloc `:2756`; slot `SoundFile` `:2757-2759`; `DB_CompleteObject` `:2765-2770` (exact-start kind); alias `:2777-2780` | — | header `ERR_DROP "Invalid fast-file sound-file header"` `:2657`; stage fails → false `:2760-2773` |
| `soundFile->u.loadSnd` / streamed | `Load_SoundFileRef` (`:2633`) → `Load_LoadedSoundPtr` / `Load_StreamedSound` (`:2626`) → `Load_StreamFileName` (`:2512`) → `Load_StreamFileInfo` (`:2506`) → `Load_StreamFileNameRaw` (`:2497`, dir+name XStrings `:2500-2503`) | `:2635-2644`, `:2626-2631`, `:2512-2517`, `:2497-2504` | — | — | — |
| `volumeFalloffCurve` (SndCurve, 72 B) | `Load_SndCurvePtr` (def `:2685`) / `Load_SndCurve` (def `:2665`) | `:2783-2784`, walk `:2665-2670`, token `:2685-2723` (insert `:2700`, publish `:2709-2713`, alias `:2717-2719`) | zone bump; **no `disk32::` extent** (72 literal; `static_assert` `snd_public.h:149`) | — | `ERR_DROP "Invalid fast-file sound falloff curve"` `:2677` (knotCount ∈ [2,8], normalized knots) |
| `speakerMap` (408 B = `kSpeakerMapBytes`) | `Load_SpeakerMap` (def `:2725`) | `:2785-2815` | inline alloc `:2789`; slot `SpeakerMap` `:2790-2792`; `DB_CompleteObject` `:2798-2803` (exact-start kind); alias `:2810-2813` | — | map validation `ERR_DROP` `:577`, `:585`, `:590`, `:605`, `:621` (from `:2733`) |
| by-name alias reference | `Load_SndAliasCustom` (def `:2947`) → `Load_snd_alias_list_name` (def `:2962`) | `:2947-2966`, `:2962-2981` | name via `Load_XStringPtr` `:2952` | `DB_FindXAssetHeader(ASSET_TYPE_SOUND, name)` `:2958` | `ERR_DROP "Fast-file sound alias reference has no name"` `:2955` |
| sound pointer token | `Load_snd_alias_list_ptr` (def `:2911`) | `:2911-2945` | inline alloc `:2923`; shared-inline `DB_InsertPointer(SndAliasList)` `:2926`; publish `:2931-2935`; alias `:2939-2941` | registration `Load_snd_alias_list_Asset` `:2930` (`db_registry.cpp:959-962`) | — |

No `iassert` exists in any sound loader (`:2497-2982` verified); all failures are `ERR_DROP` or ordered boolean false returns.

### 9.8 Collision & physics (0x01 PhysPreset, 0x0A/0x0B ClipMap(Pvs), 0x0C ComWorld, 0x0D/0x0E GameWorld(Sp/Mp), 0x0F MapEnts, PathData, DynEntities, BrushWrapper/PhysGeomList)

PhysPreset — walk `Load_PhysPreset` (`db_load.cpp:4955-4964`), entry `Load_PhysPresetPtr` (`:4966-5000`), dispatch `:11338-11341`:

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 44-byte header | `Load_Stream` | `:4957` | zone bump; **no `disk32::` extent** (44 literal) | — | — |
| `name`, `sndAliasPrefix` | `Load_XString` ×2 | `:4959-4962` | zone C-string | — | — |
| pointer token | `Load_PhysPresetPtr` | `:4966-5000` | inline alloc `:4978`; `DB_InsertPointer(PhysPreset)` `:4981`; publish `:4987-4990`; alias `:4994-4996` | registration `Load_PhysPresetAsset` `:4985` (`db_registry.cpp:897-900`) | — |

ClipMap/ClipMapPvs — shared walk `Load_clipMap_t` (`db_load.cpp:7722-8095`), entry `Load_clipMap_ptr` (`:8097-8142`), dispatch `:11374-11378`. **Loads directly into the `cm` singleton** (`db_registry.cpp:649-650`).

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 284-byte header | `Load_Stream` | `:7724` | zone bump; **no `disk32::` extent** (284 literal) | — | brush-layout `ERR_DROP` `:7726-7731`; checked derived counts `:7734-7751` |
| `name` | `Load_XString` | `:7753-7754` | zone C-string | — | — |
| `planes[numPlanes]` (20 B = `kCPlaneBytes`) | `Load_cplane_tArray` (def `:5019`) | `:7757-7781` | 4-byte bump `:7759`; direct resolve `:7772-7781` | — | extent `ERR_DROP` `:7765`; resolve fail `:7778-7780` |
| `staticModelList` (80 B, per-elem `xmodel` XModelPtr `:7425-7426`) | `Load_cStaticModel_tArray` (def `:7429`) | `:7783-7788` | 4-byte bump `:7785` | XModel alias | — |
| `materials` (72 B) | `Load_dmaterial_tArray` (def `:2066`) | `:7789-7805` | 4-byte bump | — | extent `ERR_DROP` `:7797` |
| `brushsides` (12 B = `kCBrushSideBytes`) | `Load_cbrushside_tArray` (def `:5054`) → `Load_cbrushside_t` (def `:5028`) | `:7806-7826`; side plane token `:5041-5046` | 4-byte bump | per-side plane direct resolve `DB_ResolveDirectPointer` | **null / `-1` / `-2` all hard errors** `ERR_DROP "Invalid fast-file clipmap brush-side plane token"` `:5038`; extent `ERR_DROP` `:7814` |
| `brushEdges` | `Load_cbrushedge_tArray` (def `:5080`) | `:7827-7843` | raw byte | — | extent `ERR_DROP` `:7835` |
| `nodes` (8 B, per-node plane) | `Load_cNode_tArray` (def `:7466`) → `Load_cNode_t` (def `:7444`) | `:7844-7849`; plane `:7449-7461` | inline plane alloc + `Load_cplane_t(1)` `:7449-7454`; offset convert `:7457-7461` | — | — |
| `leafs` (44 B) / `leafbrushes` / `leafbrushNodes` / `leafsurfaces` | `Load_cLeaf_tArray` (`:7481`), `Load_LeafBrushArray` (`:7717`), `Load_cLeafBrushNode_tArray` (`:7541`) → leaf `:7486-7510` | `:7850-7873` | leaf brushes inline alloc `:7495-7500` / convert `:7503-7508` | — | — |
| `verts` / `triIndices` (`3*triCount`) / `triEdgeIsWalkable` | `Load_vec3_tArray`, `Load_UnsignedShortArray`, `Load_byteArray` | `:7874-7891` | checked products `:7734-7737` | — | — |
| `borders` / `partitions` (per-elem `borders` ptr) / `aabbTrees` / `cmodels` | `Load_CollisionBorderArray` (`:7561`), `Load_CollisionPartitionArray` (`:7592`) → partition `:7566-7590`, `Load_CollisionAabbTreeArray` (`:7607`), `Load_cmodel_tArray` (`:7612`) | `:7892-7915`; partition borders `:7575-7588` | inline borders alloc `:7575-7580` / convert `:7583-7588` | — | — |
| `brushes` (80 B = `kCBrushBytes`, block-15 16-aligned) | `Load_cbrush_tArray` (def `:7696`) → `Load_cbrush_t` (def `:7617`) | `:7916-7934`; sides `:7627-7650`, adjacency `:7658-7690` | 16-byte bump `AllocLoad_GfxPackedVertex0`; `DB_IsStreamRangeValid` `:7920-7922` | side/adjacency direct resolves `:7642-7650`, `:7674-7683` | extent `ERR_DROP` `:7924`; side-count `:7627`; inline-sides `:7639`; resolve `:7642-7650`; adjacency `:7658`, `:7663`, `:7671`, `:7674-7682`, `:7690` |
| `visibility` | `Load_byteArray(visibilityByteCount)` | `:7935-7940` | raw byte | — | — |
| `mapEnts` | `Load_MapEntsPtr` | `:7941-7942` | — | MapEnts alias | — |
| `box_brush` (alias-slot object) | inline `:7946-7971` / `DB_ResolveCompletedPointer(…, ClipMapBoxBrush, kCBrushBytes)` `:7973-7981`; completion `DB_CompleteObject` `:8021-8032` | `:7944-8032` | slot `DBAliasKind::ClipMapBoxBrush` `:7948-7950` | exact-start kind | alloc `ERR_DROP` `:7954`; graph `ERR_DROP` `:8017` |
| final graph validation | `DB_ValidateMaterializedBlock4Span` + `ClipMapBrushGraphValid` + `DB_ValidateClipMapBoxBrush` | `:7984-8020` | — | — | `ERR_DROP "Invalid completed fast-file clipmap brush graph"` `:8017` |
| `dynEntDefList[2]` (96 B; per-def `xModel`/`destroyFx`/`destroyPieces`/`physPreset` ptrs `:7292-7304`) | `Load_DynEntityDefArray` (def `:7306`) | `:8033-8044` | 4-byte bump | XModel/FX/PhysPreset aliases; `destroyPieces` → XModelPieces nested walk (§9.3) | — |
| `dynEntPoseList[2]` / `dynEntClientList[2]` / `dynEntCollList[2]` | `Load_DynEntityPoseArray` (`:7326`), `Load_DynEntityClientArray` (`:7331`), `Load_DynEntityCollArray` (`:7321`) | `:8045-8092` | 4-byte bump | — | — |
| clipmap pointer token | `Load_clipMap_ptr` | `:8097-8142` | inline alloc `:8112`; `DB_InsertPointer(ClipMap)` `:8118`; registration `Load_ClipMapAsset` `:8127` (`db_registry.cpp:989-996`, MP registers as CLIPMAP_PVS); alias `:8136-8138` | — | alloc `ERR_DROP` `:8112` |

ComWorld (0x0C) — walk `Load_ComWorld` (`db_load.cpp:8217-8230`), entry `Load_ComWorldPtr` (`:8232-8266`): 16-byte header `:8219` (no `disk32::` extent), `name` `:8221-8222`, `primaryLights[primaryLightCount]` (68 B each, per-light `defName` XString `:8195-8200`) `:8223-8228`; token inline/insert/alias `:8247-8262`; singleton `&comWorld` (`db_registry.cpp:651`).

GameWorldSp (0x0D) — walk `Load_GameWorldSp` (`db_load.cpp:6735-6752`), entry `Load_GameWorldSpPtr` (`:6763-6820`): frozen `kGameWorldSpBytes=44` header `:6737-6740` (`db_disk32.h:49`), `name` `:6742-6743`, `path` → `Load_PathData` `:6744-6749`; token `:6789-6816` (insert `:6789`, publish `:6806-6810`, alias `:6814-6816`; alloc-range `ERR_DROP` `:6781`).

GameWorldMp (0x0E) — **name-only struct** (`g_bsp.h:10-13`, `sizeof=0x4`): walk = 4-byte header `:6756` + `name` XString `:6758-6759`; token `:6837-6852`. **No path data is loaded for MP** in this codebase; §4.5's "path data are server-reachable" refers to the SP `PathData` shape and any future MP path surface — there is no MP path graph loader today.

MapEnts (0x0F) — walk `Load_MapEnts` (`db_load.cpp:7362-7375`), entry `Load_MapEntsPtr` (`:7377-7411`): 12-byte header `:7364` (no extent), `name` `:7366-7367`, `entityString` blob (`numEntityChars` bytes) `:7368-7373`; token insert `:7392` / alias `:7405-7407`.

PathData (SP world; shared walker `Load_PathData`, `db_load.cpp:6573-6733`):

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 40-byte header (`kPathDataBytes`) | `Load_Stream` | `:6575-6578` | frozen (`db_disk32.h:50`) | — | layout `ERR_DROP` `:6582` |
| `nodes[nodeCount]` (128 B = `kPathNodeBytes`) | `Load_pathnode_tArray` (def `:6353`) → `Load_pathnode_t` (def `:6329`) → `Load_pathnode_constant_t` (def `:6268`) | `:6587-6595`, node type `:6346`, constants `:6289-6298` | 4-byte bump; frozen `kPathNodeConstantBytes=68` (`db_disk32.h:52`) | 5 script strings per node | node type `ERR_DROP` `:6346`; nodes extent `ERR_DROP` `:6593` |
| `nodes[].constant.links` | `Load_pathlink_tArray` (def `:6263`, 12 B = `kPathLinkBytes`) | `:6307-6323` | 4-byte bump; completion | — | links extent `ERR_DROP` `:6307`; completed `ERR_DROP` `:6323` |
| `basenodes[baseNodeCount]` (16 B = `kPathBaseNodeBytes`) | `Load_pathbasenode_tArray` (def `:6374`) | `:6605-6621` | **block-1** push `:6604` | — | extent `ERR_DROP` `:6613` |
| `chainNodeForNode` / `nodeForChainNode` | `Load_UnsignedShortArray` ×2 | `:6623-6654` | 2-byte bump | — | extent `ERR_DROP` `:6631`, `:6647` |
| `pathVis` | `Load_byteArray(visibilityBytes)` | `:6655-6668` | raw byte | — | extent `ERR_DROP` `:6663` |
| `nodeTree` (16 B = `kPathTreeBytes`) | `Load_pathnode_tree_tArray` (def `:6518`) → `Load_pathnode_tree_t` (`:6508`) → info `:6491` → leaf `Load_pathnode_tree_nodes_t` (`:6383`) or split `Load_pathnode_tree_ptrArray` (`:6474`) → `Load_pathnode_tree_ptr` (`:6436`) | `:6671-6677`; leaf `:6414`, `:6430`; child token `:6448`, resolve `:6451-6456`, ownership `:6468` | leaf alloc block-4 | children are direct pointers into the same array | split `ERR_DROP` `:6501`; child token `ERR_DROP` `:6448`; not-owned `ERR_DROP` `:6468`; tree extent `ERR_DROP` `:6677` |
| final graph validation | spans + `PathNodesRuntimeValid` + `PathChainMapsRuntimeValid` + `PathTreeGraphValid` | `:6684-6731` | — | — | `ERR_DROP "Invalid completed fast-file path graph"` `:6729` |

BrushWrapper/PhysGeomList (reached from XModel `physGeoms` §9.2 and DynEntityDef):

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| 80-byte wrapper header (`kBrushWrapperBytes`) | `Load_BrushWrapper` (def `:5179`) | `:5181-5201` | frozen (`db_disk32.h:65`) | — | layout `ERR_DROP` `:5189-5201` |
| side-plane token table | per-side 4-byte token snapshot | `:5203-5238` | `kCBrushSideBytes` sides `:5218-5222` | **null (0) and shared-inline (`0xFFFFFFFE`) are hard errors** | `ERR_DROP "Invalid fast-file physics brush-side plane token"` `:5234-5238` |
| inline side plane (`0xFFFFFFFF`) | `AllocLoad_FxElemVisStateSample` + `DB_IsStreamRangeValid(kCPlaneBytes)` + `Load_cplane_t(1)` | `:5239-5252` | 4-byte bump | — | extent `ERR_DROP` `:5245-5248` |
| deferred side plane (offset) | `DB_ResolveOffsetBytes(…, kDirectBlock4)` | `:5304-5328` | — | deferred direct | `ERR_DROP` `:5304-5328` |
| `baseAdjacentSide` (edges) | `Load_cbrushedge_tArray` | `:5261-5273` | raw byte | — | extent `ERR_DROP` `:5261-5271` |
| `planes[numsides]` | `Load_cplane_tArray` / convert | `:5275-5300` | 4-byte bump | inline or direct | extent `ERR_DROP` `:5284-5287` |
| wrapper validation | `DB_ValidateMaterializedBlock4Span` + `BrushWrapperRuntimeValid` | `:5330-5349` | — | — | `ERR_DROP "Invalid completed fast-file physics brush graph"` `:5330-5349` |
| geomInfo brush (68 B = `kPhysGeomInfoBytes`) | `Load_PhysGeomInfo` (def `:5353`) | `:5370-5401` | inline alloc + slot `BrushWrapper` `:5370-5372` + `DB_CompleteObject` `:5376-5386`; resolve `:5388-5395` | exact-start kind | `ERR_DROP "Invalid completed fast-file physics geometry"` `:5397-5401` |
| geom list (44 B = `kPhysGeomListBytes`) | `Load_PhysGeomList` (def `:5426`) | `:5428-5470` | `geoms` alloc + range `:5441-5452`; completion `:5461-5470` | — | extent `ERR_DROP` `:5441-5452`; completed `ERR_DROP` `:5461-5470` |

### 9.9 World & renderer (0x10 GfxWorld, 0x11 LightDef)

LightDef — walk `Load_GfxLightDef` (`db_load.cpp:4782-4791`), entry `Load_GfxLightDefPtr` (`:4793-4827`): 16-byte header `:4784` (no extent), `name` `:4786-4787`, `attenuation.image` GfxImagePtr `:4788-4789`; token insert `:4808` / publish `:4813-4817` / alias `:4821-4823`. This entry doubles as a nested token: `Load_GfxLight` re-enters it for `GfxWorld.sunLight->def` (`varGfxLightDefPtr = &varGfxLight->def; Load_GfxLightDefPtr(0)`, `:4835-4836`; sole `Load_GfxLight` call site is the GfxWorld walk, `:10809`), so the 64-byte `kGfxLightBytes` GfxLight payload carries one LightDef asset-pointer token — and, when that token is inline/shared-inline, the `attenuation.image` traversal above rides with it (mark path mirrors the edge: `Mark_GfxLight` `:4862-4866`).

GfxWorld — walk `Load_GfxWorld` (`db_load.cpp:10637-11084`), entry `Load_GfxWorldPtr` (`:11086-11143`), dispatch `:11395-11398`; singleton `&s_world` (`db_registry.cpp:660`). Frozen root extent `kGfxWorldBytes=732` (`db_disk32.h:39`), streamed `:10639-10642`; pre-validation `:10650-10766` (cell layout `ERR_DROP` `:10650-10665`, lookup arrays `ERR_DROP` `:10670-10700`, pointer counts `:10712-10734`).

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| `name`/`baseName` | `Load_XString` ×2 | `:10768-10771` | zone C-string | — | — |
| `indices` / `skyStartSurfs` | `Load_r_index_tArray` (`:3304`), `Load_intArray` | `:10772-10783` | 2-byte / 4-byte bump | — | — |
| `skyImage` / `outdoorImage` | `Load_GfxImagePtr` | `:10784-10785`, `:10950-10951` | — | image alias | — |
| `sunLight` (64 B = `kGfxLightBytes`) | `Load_GfxLight` (def `:4829`) | `:10786-10833` | inline alloc + range `:10796`; slot `GfxLight` `:10800-10802`; `DB_CompleteObject(kGfxLightBytes)` `:10815-10824` | **the one alias carrying an explicit extent**: `DB_ConvertOffsetToAlias(…, GfxLight, kGfxLightBytes)` `:10828-10831` | extent `ERR_DROP` `:10796` |
| `sunLight.def` (nested LightDef asset token) | `Load_GfxLightDefPtr` re-entered from the `Load_GfxLight` tail (def `:4829-4837`); same grammar as the standalone LightDef entry above | `:4835-4836` (`varGfxLightDefPtr = &varGfxLight->def; Load_GfxLightDefPtr(0)`); token walk `:4798-4826` | 4-byte token `:4798`; inline `-1`/`-2` → `AllocLoad_FxElemVisStateSample` bump + `Load_GfxLightDef(1)` + `Load_LightDefAsset` publication `:4805-4812`; LightDef body `:4782-4791` | `-2` inserted pointer `DB_InsertPointer(DBAliasKind::GfxLightDef)` → `DB_SetInsertedPointer` `:4807-4817`; offset → `DB_ConvertOffsetToAlias(DBAliasKind::GfxLightDef)` `:4821-4823`; the loaded LightDef then owns `attenuation.image` GfxImage alias `:4788-4789` — **`GfxLight` is not a pointer-free 64-B payload** | — (no extent validation or `ERR_DROP` in the token walk or LightDef body) |
| `reflectionProbes` (16 B = `kGfxReflectionProbeBytes`) | `Load_GfxReflectionProbeArray` (def `:9775`; per-probe `reflectionImage` `:9771-9772`) | `:10834-10850` | 4-byte bump + range | image alias per probe | extent `ERR_DROP` `:10842` |
| `reflectionProbeTextures` | `Load_GfxRawTextureArray` | `:10851-10872` | block-1 push `:10851`, 4-byte bump | plain stream | extent `ERR_DROP` `:10860-10862` |
| `dpvsPlanes` (16 B) | `Load_GfxWorldDpvsPlanes` (def `:10586`) | `:10873-10874`; planes `:10606-10618`, nodes `:10621-10626`, cellBits `:10627-10634` | inline planes / convert (20 B literal) | — | — |
| `cells[cellCount]` (56 B = `kGfxCellBytes`) | `Load_GfxCellArray` (def `:9999`) → `Load_GfxCell` (def `:9901`) | `:10880-10898` | alloc + range `:10884-10886` | — | layout `ERR_DROP` `:9910`; extent `ERR_DROP` `:10888`; count `:10875-10879` |
| `cells[].aabbTree[treeCount]` (44 B = `kGfxAabbTreeBytes`) | `Load_GfxAabbTreeArray` (def `:9880`) → `Load_GfxAabbTree` (def `:9819`) | `:9913-9929`; smodelIndexes `:9838-9862`, validation `:9874` | inline smodelIndexes / `DB_ResolveDirectPointer` | — | tree extent `ERR_DROP` `:9921`; smodel extent `ERR_DROP` `:9846-9848`; bad index `ERR_DROP` `:9874` |
| `cells[].portals[portalCount]` (68 B = `kGfxPortalBytes`) | `Load_GfxPortalArray` (def `:10095`) → `Load_GfxPortal` (def `:10020`) | `:9930-9944`; cell token `:10031`, resolve `:10034-10042`, target `:10052`, verts `:10061-10076`, completion `:10089` | vertex alloc block-4 | portal→cell direct pointer | token `ERR_DROP` `:10031`; target `ERR_DROP` `:10052`; layout `ERR_DROP` `:10061`; extent `ERR_DROP` `:10076`; completed `ERR_DROP` `:10089` |
| `cells[].cullGroups` / `reflectionProbes` (indices) | `Load_intArray` / `Load_byteArray` | `:9945-9972` | — | — | extent `ERR_DROP` `:9953`, `:9967` |
| `lightmaps` (8 B pairs) / lightmap textures | `Load_GfxLightmapArrayArray` (def `:4898`) → `Load_GfxLightmapArray` (`:4889`); `Load_GfxRawTextureArray` | `:10899-10922` | 4-byte bump; block-1 pushes | image aliases inside pairs | — |
| `lightGrid` (56 B) | `Load_GfxLightGrid` (def `:10208`) | `:10905-10906`; walk `:10208-10248` | rowDataStart/rawRowData/entries/colors sub-allocs `:10216-10247` | — | axes `ERR_DROP` `:10213` |
| `models` (56 B = `kGfxBrushModelBytes`) | `Load_GfxBrushModelArray` (def `:3096`) | `:10923-10937` | 56 B stride + range | — | extent `ERR_DROP` `:10931` |
| `materialMemory` | `Load_MaterialMemoryArray` (def `:10142`) | `:10938-10943` | 4-byte bump | material alias per elem `:10137-10140` | — |
| `vd` (vertices + `worldVb`) / `vld` | `Load_GfxWorldVertexData` (def `:10157`), `Load_GfxWorldVertexLayerData` (def `:10186`) | `:10944-10947` | 44 B/vertex product `:10160-10167`; `Load_VertexBuffer` D3D or headless null `:10174-10183` | — | — |
| `sun` (sunflare_t, 96 B) | `Load_sunflare_t` (def `:9748`) | `:10948-10949` | — | sprite/flare material aliases | — |
| `cellCasterBits` / `sceneDynModel` / `sceneDynBrush` / shadow-vis arrays | `Load_raw_uintArray`, scene-dyn arrays `:10250-10258`, raw arrays `:10988-11019` | `:10952-10987` | block-1 pushes | — | caster extent `ERR_DROP` `:10961-10963` |
| `shadowGeom` / `lightRegion` | `Load_GfxShadowGeometryArray` (def `:10282`), `Load_GfxLightRegionArray` (def `:10339`) | `:11020-11031` | sortedSurfIndex/smodelIndex + hulls/axes sub-allocs `:10265-10326` | — | — |
| `dpvs` (104 B) | `Load_GfxWorldDpvsStatic` (def `:10440`) | `:11032-11033`; walk `:10440-10584` | root `Load_Stream(…, 104)` `:10442`; visData/lodData block-1 `:10467-10522`; sortedSurfIndex validate+range `:10523-10543`; smodelInsts `Load_GfxStaticModelInstArray` (28 B stride) `:10544-10549`; **surfaces** `Load_GfxSurfaceArray` (def `:4874`, 48 B, per-surface material) `:10550-10555`; **cullGroups** `Load_GfxCullGroupArray` (def `:10116`, 32 B) `:10556-10561`; `smodelDrawInsts` / `surfaceMaterials` / `surfaceCastsSunShadow` enumerated in the three rows below | material aliases per surface; XModel token per `smodelDrawInsts[].model` (rows below) | counts `ERR_DROP` `:10450`, `:10456`; sorted index `ERR_DROP` `:10541` |
| `dpvs.smodelDrawInsts[smodelCount]` (76 B/inst) | `Load_GfxStaticModelDrawInstArray` (def `:9708`, stride 76 `:9713`) → `Load_GfxStaticModelDrawInst` (def `:9701`, 76 B `:9703`) | `:10562-10567` | alloc `AllocLoad_FxElemVisStateSample` `:10564` | **nested asset reference per element**: `varXModelPtr = &drawInst->model; Load_XModelPtr(0)` (`:9704-9705`) — the full XModel pointer-token grammar (§9.2 entry, `:6015-6054`) applies to every instance | — |
| `dpvs.surfaceMaterials[staticSurfaceCount]` | `Load_GfxDrawSurfArray` (def `:10260`, 8 B stride) | block-1 push `:10568-10575` | alloc `AllocLoad_FxElemVisStateSample` `:10571` | plain 8-B `GfxDrawSurf` data — no nested token | — |
| `dpvs.surfaceCastsSunShadow[surfaceVisDataCount]` | `Load_raw_uint128Array` | block-1 push `:10576-10583` | alloc `AllocLoad_raw_uint128` `:10579` | plain `uint32` data — no nested token | — |
| `dpvsDyn` (48 B) | `Load_GfxWorldDpvsDynamic` (def `:10354`) | `:11034-11035`; walk `:10354-10438` | eight block-1 pushes (`dynEntCellBits[2]` + `dynEntVisData[2][3]`) `:10374-10437` | — | — |
| final validation | spans + `GfxWorldCellGraphValid(kGfxCellBytes)` + `DB_ValidateWorldAabbTrees` | `:11036-11081` | — | — | `ERR_DROP "Invalid completed fast-file world cell graph"` `:11078` |
| world pointer token | `Load_GfxWorldPtr` | `:11086-11143` | alloc + range `:11104`; insert `:11112`; registration `Load_GfxWorldAsset` `:11128`; alias `:11137-11139` | — | alloc `ERR_DROP` `:11104` |

### 9.10 UI, script data & localization (0x13 Font, 0x14 MenuList, 0x15 Menu, 0x16 LocalizeEntry, 0x17 Weapon, 0x1F RawFile, 0x20 StringTable)

| Family | Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|---|
| Font | 24-byte header + glyph-table sanity | `Load_Font` (`:11228`), `CountInRange(96,65536)` | `:11229-11238` | zone bump; **no `disk32::` extent** (24 literal, glyph stride 24) | — | `ERR_DROP "Invalid fast-file font glyph table"` `:11232`; span `ERR_DROP` `:51` |
| Font | `fontName` / `material` / `glowMaterial` | `Load_XString` / `Load_MaterialHandle` ×2 | `:11240-11245` | — | material alias | — |
| Font | `glyphs[glyphCount]` (24 B) | `Load_GlyphArray` (def `:11221`) | `:11246-11260` | 4-byte bump `:11250` | offset convert `:11256-11260` | — |
| Font | pointer token | `Load_FontHandle` (def `:11266`) | `:11266-11300` | inline alloc `:11278`; insert `:11281`; publish `:11287-11290`; alias `:11294-11296` | registration `Load_FontAsset` `:11285` (`db_registry.cpp:1088-1091`) | — |
| MenuList | 12-byte header + `name` + `menus[]` | `Load_MenuList` (`:8672`), `Load_menuDef_ptrArray` (def `:8655`) | `:8672-8681` | 4-byte bump `:8678` | menu alias per entry | — |
| Menu | 284-byte menuDef + `window` (156 B, name/group XStrings + background material `:8431-8436`) | `Load_menuDef_t` (`:8582`), `Load_Window` (`:8439`) | `:8582-8585` | zone bump; **no `disk32::` extent** (284/156 literals) | material alias | — |
| Menu | `font`/`onOpen`/`onClose`/`onESC`/`allowedBinding`/`soundName` XStrings | `Load_XString` ×6 | `:8586-8605` | zone C-string | — | — |
| Menu | `onKey` handler chain (recursive) | `Load_ItemKeyHandler` (def `:8446`) | `:8594-8599` | 4-byte bump `:8596`; `action` XString `:8449-8450` | recursive `next` `:8451-8456` | — |
| Menu | menu-level `visibleExp`/`rectXExp`/`rectYExp` statements | `Load_statement` ×3 | `:8601`, `:8607`, `:8609` | statement chain (two rows below) | — | — |
| Menu | itemDef statements (`visibleExp`/`textExp`/`materialExp`/`rectXExp`/`rectYExp`/`rectWExp`/`rectHExp`/`forecolorAExp`) | `Load_statement` ×8 | `:8536-8551` | statement chain (next row) | — | — |
| Menu | statement root — 8-byte (`entries` ptr + `numEntries`) | `Load_statement` (def `:8361-8370`) | non-null `entries` → arena alloc `:8366` + `Load_expressionEntry_ptrArray(1, numEntries)` `:8366-8368` | 4-byte token-array bump | — | span `ERR_DROP` `:51` |
| Menu | `entries[numEntries]` token array → one 12-byte `expressionEntry` per non-null token | `Load_expressionEntry_ptrArray` (def `:8346-8359`) → `Load_expressionEntry_ptr` (def `:8335-8344`) | array stream `:8351`; per-entry arena alloc `:8340` + `Load_expressionEntry` `:8340-8342` | 4-byte bump per entry | — | — |
| Menu | `expressionEntry->data` union | `Load_entryInternalData` (def `:8314-8326`) | `type != 0` → `Load_Operand` `:8316-8320`; `type == 0` → `Load_Operator` 4-byte `:8321-8325` | — | — | — |
| Menu | `Operand` (8-byte) + `internals` union | `Load_Operand` (def `:8302-8307`) → `Load_operandInternalDataUnion` (def `:8277-8300`) | `VAL_FLOAT` → floatVal stream `:8281-8288`; **`VAL_STRING` → `Load_XString` zone C-string `:8289-8293`**; `0` → int stream `:8295-8299` | the `VAL_STRING` leaf is the expression chain's only pointer-bearing step | — | span `ERR_DROP` `:51` |
| Menu | `items[itemCount]` (372-byte itemDef, 11 XStrings `:8502-8523`) | `Load_itemDef_ptrArray` (def `:8565`) → `Load_itemDef_t` (`:8499`) | `:8610-8615` | 4-byte bump `:8612` | sound alias `focusSound` `:8533`; `enableDvar` XString | — |
| Menu | `typeData` dispatch by item type | `Load_itemDefData_t` (def `:8466-8495`): type 6 → listBox `:8470-8473`; types 4/9/0x10/0x12/0xB/0xE/0xA/0/0x11 → editField `:8474-8485`; type 0xC → multiDef `:8486-8489`; type 0xD → string `:8490-8493` | call `:8535` | per-type token arena allocs `:8386`, `:8402`, `:8422` | — | — |
| Menu | listBox `typeData` → `listBoxDef_t` (340-byte) | `Load_listBoxDef_ptr` (def `:8381-8390`) → `Load_listBoxDef_t` (def `:8372-8379`) | arena alloc `:8386`; root `:8374` | **`doubleClick` XString `:8375-8376` (zone C-string); `selectIcon` `:8377-8378` material alias** | — | — |
| Menu | editField `typeData` → `editFieldDef_s` (32-byte, plain) | `Load_editFieldDef_ptr` (def `:8397-8406`) → `Load_editFieldDef_t` (def `:8392-8395`) | arena alloc `:8402`; root `:8394` | **no nested pointers** | — | — |
| Menu | multiDef `typeData` → `multiDef_s` (392-byte) | `Load_multiDef_ptr` (def `:8417-8426`) → `Load_multiDef_t` (def `:8408-8415`) | arena alloc `:8422`; root `:8410` | **two 32-entry XString arrays `:8411-8414` → 64 zone C-strings** | — | — |
| Menu | pointer token | `Load_menuDef_ptr` (def `:8619`) | `:8619-8653` | inline alloc `:8631`; insert `:8634`; publish `:8640-8643`; alias `:8647-8649` | registration `Load_MenuAsset` (`db_registry.cpp:1108-1120`, re-parents items `:1116-1117`) | — |
| LocalizeEntry | 8-byte header + `value`/`name` | `Load_LocalizeEntry` (`:8846`), entry `Load_LocalizeEntryPtr` (def `:8855`) | `:8846-8889` | zone C-strings; **no `disk32::` extent** (8 literal) | insert `:8870` / alias `:8883-8885` | — |
| Weapon | 2168-byte header | `Load_WeaponDef` (def `:9028`) | `:9028` | zone bump; **no `disk32::` extent** (2168 literal) | — | accuracy-graph `ERR_DROP` `:985`, `:1001` (validators `:978-1007`) |
| Weapon | names + `szXAnims[33]` + `szModeName` | `Load_XString`/`Load_XStringArray` | `:9041-9054` | zone C-string | — | — |
| Weapon | `gunXModel[16]`/`handXModel`/`worldModel*`/projectile models | `Load_XModelPtr(Array)` | `:9047-9050`, `:9203-9212`, `:9233-9234` | — | XModel alias | — |
| Weapon | `hideTags[8]`, `notetrackSoundMap{Keys,Values}[16]` | `Load_ScriptStringArray` → `Load_ScriptStringCustom` | `:9055-9060` | 16-bit script-string indices | — | `ERR_DROP` bad index `db_stringtable_load.cpp:10`; 16-bit overflow `:18` |
| Weapon | `viewFlashEffect` … 38 named sounds | `Load_FxEffectDefHandle` / `Load_snd_alias_list_name` ×38 | `:9061-9153`, `:9191-9248` | — | FX/name alias | — |
| Weapon | `bounceSound[29]` | inline `:9155-9181` / alias `:9185-9188` | `:9155-9190` | 4-byte bump; **frozen `kWeaponBounceSoundCount=29`** (`db_disk32.h:35`, table bytes `:36-37`); slot `WeaponBounceSoundTable` `:9160-9167`; `DB_CompleteObject` `:9172-9181` | exact-start kind | slot fail silent return `:9163-9167`; complete fail `:9179-9181` |
| Weapon | icons/materials | `Load_MaterialHandle` ×8 | `:9199-9230` | — | material alias | — |
| Weapon | `accuracyGraph[2]` + knot arrays | inline `:9249-9322` | `:9255-9316` | 4-byte bumps + `Load_vec2_tArray` | offset convert `:9261`, `:9278`, `:9299`, `:9316` | knots `ERR_DROP` `:1021` (validator `:1009-1025`) |
| Weapon | pointer token | `Load_WeaponDefPtr` (def `:9338`) | `:9338-9372` | inline alloc `:9350`; insert `:9353`; alias `:9366-9368` | registration `Load_WeaponDefAsset` (`db_registry.cpp:1172-1175`) | — |
| RawFile | 12-byte header + `name` + `buffer[len+1]` | `Load_RawFile` (def `:9549`), entry `Load_RawFilePtr` (def `:9566`) | `:9551-9562`, token `:9578-9596` | unaligned bump `:9555`; `DB_CheckedCountSum(len,1)` `:9557-9560` | insert `:9581` / alias `:9594-9596` | `ERR_DROP` derived count `:1185` |
| StringTable | 16-byte header (`kStringTableBytes`) + product/count checks | `Load_StringTable` (def `:9613`) | `:9613-9637` | frozen `kStringTableBytes=16` (`db_disk32.h:58`) | — | product `ERR_DROP` `:1196`; count `ERR_DROP` `:64`; present-empty `ERR_DROP` `:9630`; no name `ERR_DROP` `:9637` |
| StringTable | `values[valueCount]` cell strings | `Load_XStringArray` | `:9640-9645` | 4-byte bump `:9642` | cell strings zone-allocated via `Load_XString`/`Load_XStringCustom` (`db_stream_load.cpp:262-298`) | — |
| StringTable | pointer token — **inline `-1` only, no shared-inline** | `Load_StringTablePtr` (def `:9649`) | `:9649-9690` | slot `StringTable` `:9658-9662`; `DB_CompleteObject(kStringTableBytes)` `:9666-9674` | alias `:9684-9687`; **the `SL_AddUser` bridge site lives in the mark twin**: `Mark_ScriptStringCustom` → `DbLoadLegacyBridge::TryAddUser4` (`db_stringtable_load.cpp:27-30`) | registration check `ERR_DROP` `:9676-9680` |

### 9.11 FX (0x19) & ImpactFx (0x1A) — dispatch `:11423-11430` (load) / `:11555-11562` (mark)

Decompiled fallback walks (the shipping-profile path; the converted adapters are the §4.3 seam-b zero-caller path):

| Subobject | Loader symbol | Site | Alloc/Disk32 | Alias/inline | Failure |
|---|---|---|---|---|---|
| FxEffectDef 32-byte root | `Load_FxEffectDef` (def `:7122`) | `:7124` | frozen `kFxEffectDefDisk32Bytes=32` (`db_load.cpp:31`; `ONDISK_SIZE` `fx_fastfile_disk32.h:406`) | adapter seam first: `TryWireEffectDefThroughActiveFxZoneAdapter` `:7125-7134` — success rebinds + pops, no legacy walk; null → decompiled walk | — |
| `name` | `Load_XString` | `:7136-7137` | zone C-string | — | — |
| `elemDefs[elementCount]` (252 B stride) | alloc `:7138-7141` + `Load_FxElemDefArray` (def `:7107`) | `:7150` | 4-byte bump; count = checked sums `:7142-7149` | — | derived count `ERR_DROP` `:1180-1189` |
| `elemDefs[].velSamples[velIntervalCount+1]` (96 B stride) | `Load_FxElemVelStateSampleArray` (def `:7018`) | `:7075-7082` | 4-byte bump | — | count `ERR_DROP` "effect velocity samples" |
| `elemDefs[].visSamples[visStateIntervalCount+1]` (48 B stride) | `Load_FxElemVisStateSampleArray` (def `:7013`) | `:7083-7090` | 4-byte bump | — | count `ERR_DROP` "effect visual samples" |
| `elemDefs[].visuals` (mark array 8 B / array 4 B / single union) | `Load_FxElemDefVisuals` (def `:7025`): mark `:6950-6970`, array `:6998-7011`, single `Load_FxElemVisuals` (def `:6972`): elemType 5→XModel `:6976`, 0xA→effect ref `:6980`, 8→XString `:6984`, 6/7 skip `:6989`, default→material `:6991` | `:7025-7047` | 4-byte bumps | XModel/material/effect aliases | — |
| `effectOnImpact`/`effectOnDeath`/`effectEmitted` | `Load_FxEffectDefRef` ×3 | `:7093-7098` | — | nested effect name refs (`Load_FxEffectDefFromName` → `DB_FindXAssetHeader`, `db_registry.cpp:1192-1196`) | — |
| `trailDef` (28 B) + verts/inds | `Load_FxTrailDef` (def `:7055`); verts stride 20 `:7052`, inds `AllocLoad_XBlendInfo` `:7066` | `:7099-7104` | frozen `FxTrailDefDisk32 0x1C` (`fx_fastfile_disk32.h:141-150,351`) | — | — |
| FxImpactTable 8-byte root | `Load_FxImpactTable` (def `:8924`) | `:8926` | frozen `kFxImpactTableDisk32Bytes=8` (`db_load.cpp:32`; `fx_fastfile_disk32.h:428`) | adapter seam `:8927-8936` (same null-fallback grammar) | — |
| `name` + `table[12]` (132 B entries) | `Load_XString` + `Load_FxImpactEntryArray` (def `:8909`) | `:8938-8944` | 4-byte bump; entry = `nonflesh[29]` `:8903-8904` + `flesh[4]` `:8905-8906` effect handles | FX handle aliases | — |
| effect/impact pointer tokens | `Load_FxEffectDefHandle` (def `:6892`), `Load_FxImpactTablePtr` (def `:8949`) | `:6892-6926`, `:8949-8987` | insert `DBAliasKind::FxEffectDef` `:6907` / `FxImpactTable` `:8964`, `:8972`; alias `:6920`, `:8972` region | registrations `Load_FxEffectDefAsset`/`Load_FxImpactTableAsset` (`db_registry.cpp:1182-1201`) | — |

### 9.12 Save/tagInfo records (non-fastfile persistence; credited merged work)

| Subobject | Loader symbol | Site | Notes |
|---|---|---|---|
| tagInfo 112-byte Disk32 record | `SF_TYPE_TAG_INFO` branches | `src/game/g_save.cpp:780`, `:830`, `:1044` | Converted via entity-map arena; merged PR #89 / `ki-f0w` — **credited, do not recreate**; production tests `tests/save_taginfo_tests.cpp`, `save_taginfo_production_tests.cpp` |

---

## 10. Per-family ownership / alias / failure invariants (corrective, criterion 2)

Shared invariants that hold for **every** family below unless a row says
otherwise:

- **Zone-block ownership.** Every loader allocation is a `DB_AllocStreamPos`
  bump inside one of the nine zone blocks (`XZoneMemory::blocks[9]`,
  `db_zone_memory.h:13-20`; backing allocator `DB_AllocXZoneMemory`,
  `db_memory.cpp:104-167`, PMem-based; blocks 7/8 carry GPU vertex/index
  mirrors `db_memory.cpp:50-72`, `:148-166`). There is **no per-subobject
  free**: family data dies with the zone's PMem blocks
  (`DB_UnloadXZoneMemory` → `DB_FreeXZoneMemory` + `PMem_Free`,
  `db_registry.cpp:3399-3405`, `DB_ReleaseGeometryBuffers`
  `db_memory.cpp:75-102`). Per-asset removers exist only where a runtime
  resource escapes zone memory (ClipMap `CM_Unload`, ComWorld
  `Com_UnloadWorld`, GfxWorld `DB_MediaUnloadGfxWorld`, LoadedSound
  `DB_RemoveLoadedSound`, TechniqueSet/Image media release).
- **Failure grammar (corrected: per-object seal ordering is not whole-zone
  atomicity).** All load-path validation failures are
  `Com_Error(ERR_DROP, …)` (long-jump zone-load abort) or an ordered boolean
  false return that unwinds to the same abort. What the sources demonstrate
  is **per-object seal ordering**: within one object's reader, the pointer
  token is registered (slot) before children are read, and that object's own
  publication — inserted pointer (`DB_SetInsertedPointer`,
  `db_load.cpp:4410-4414`) or completion seal (`DB_CompleteObject`,
  `db_load.cpp:4525-4530`) — happens only after its body walk succeeds. A
  zone load is **not** demonstrated to be transactional, on three counts.
  (1) *Prior child publications survive a later parent failure:* a nested
  asset whose walk completes is registered into the global asset pool
  mid-parent-walk — `Load_Material` reaches `Load_MaterialTechniqueSetPtr`
  (`db_load.cpp:4465-4469`), whose reader calls
  `Load_MaterialTechniqueSetAsset` →
  `DB_AddXAsset(ASSET_TYPE_TECHNIQUE_SET)` → `DB_LinkXAssetEntry` under
  `db_hashCritSect` (`db_load.cpp:4409`, `db_registry.cpp:938-942`,
  `:2095-2118`) — while the parent Material can still fail afterwards
  aligning/allocating/loading its own texture table
  (`db_load.cpp:4483-4523`), leaving the completed child published. (2) *The
  abort is fatal, not a rollback:* the `DB_Thread` long-jump handler
  (`db_registry.cpp:2691-2699`) calls `Com_ErrorAbort()`, whose body is
  `Sys_Error("%s", com_errorMessage)` (`qcommon/common.cpp:883-886`) — the
  process dies carrying any partial publications. (3) *Cleanup is wholesale
  only:* zone data has no per-subobject free (first shared invariant above);
  it is reclaimed with the process on a load abort or with the zone's PMem
  blocks on unload, and no source mechanism repairs or unwinds in-process
  partial publication. Whole-zone rollback / "a failed zone load leaves no
  partially published asset behind" is therefore **ME** (§12 taxonomy): no
  source path and no test demonstrates it, and this ledger no longer claims
  it.
- **High-address behavior (token domain vs host width).** All pointer fields
  are 4-byte tokens in the 32-bit token space; offset decode enforces
  `kOffsetMask = 0x0FFFFFFF`, block-index validity and
  `offset + requiredBytes <= blockSize` (`db_disk32.h:12`, `DecodeOffset`);
  block-4 (`kDirectBlock4`, `db_load.cpp:34`) is the shared relocation block.
  These bounds are **32-bit-domain arithmetic**: they constrain the token
  space, not host pointer widths. `tests/disk32_tests.cpp` ("inline sentinel
  was accepted as an offset", "out-of-range block was accepted",
  "out-of-range byte span was accepted" rejection cases) and the per-kind
  extent assertions in `tests/db_relocation_tests.cpp` /
  `tests/db_validation_tests.cpp` (§11) operate on `uint32` sizes/offsets in
  that token domain — they pin top-of-token-space behavior (sentinels,
  block-4 tail) and do **not** execute >4 GiB host addresses. Full-width
  (>4 GiB) host storage/conversion coverage is pinned where a named test pins
  it — and the pins are not confined to one family (§12 keeps the evidence
  kinds distinct): `TestPointerBytesRemainNativeWidth`
  (`tests/model_surface_stream_tests.cpp:341`, ctest
  `renderer-model-surface-stream-contracts`, XModel/XSurface family —
  synthetic high-address pointer-bit preservation) and
  `TestHappyPathAndFullWidthIdentities`
  (`tests/fx_fastfile_impact_native_disk32_tests.cpp:564-635`, ctest
  `effectscore-fastfile-impact-native-disk32-conversion`, FX / ImpactFx
  family — real high-address conversion: the materialized native impact
  table's every native handle is compared to its resolved identity, and each
  expected address is asserted to exceed `UINT32_MAX` on 64-bit, `:615-624`)
  plus the cross-family static gates (`pointer-truncation-tripwire`, 24
  tracked narrow-conversion sites; `abi-sizeof-debt-tripwire`). Both fixtures
  are bounded test-process conversion/storage receipts — neither is
  production enrollment (the FX/impact native binding is still zero-caller,
  §4) nor retail parity. Every family whose row does not name one of these
  pins still carries a high-address **ME** classification (§12): token-domain
  bounds are proven, >4 GiB execution is not.

Per-family matrix:

| Family | Alias / inserted-pointer / shared-inline semantics | Memory / stream ownership | Failure / rollback invariant |
|---|---|---|---|
| Container envelope | `kInline` preserved verbatim; shared-inline token rejected in the script-string walk (`UnsupportedSharedInline`) — no shared-inline protocol at envelope level | Envelope records read through fail-closed iterators into caller-provided bounds | Iterator failure is atomic: no partial list escapes (`db_xasset_disk32.cpp`) |
| XAnimParts | Asset-level: inline alloc / `-2` inserted pointer (`DBAliasKind::XAnimParts`) / offset alias (`:2422`, `:2428-2431`, `:2435-2437`). **No sub-object aliasing inside the body** — every payload array is a fresh bump | Zone blocks; load-object route additionally hunk-persistent (`Hunk_SetDataForFile` type 4; `g_animUser` temp arena destroyed per-anim `xanim_load_obj.cpp:1650-1657`) | Body walk has iassert sentinels on stream state; a false return from any sub-walk unwinds to `ERR_DROP`; runtime-struct consumption still raw-width (XAnim payload consumer gap, §6.3) |
| XModel | Asset-level inserted-pointer/alias (`:6030`, `:6040-6044`, `:6048-6050`); sub-surface `vertList`/`collisionTree` aliases (`:3395-3398`, `:3212-3215`); physGeoms completed shared object | Zone blocks (surfs); load-object route hunk types 3/4/5 (`xmodel_load_obj.cpp:1086`, `:1292`, `:1914`) | 13-point post-walk validation `DB_ValidateLoadedXModel`; collision-layout precheck (`:5765-5780`) runs after `Load_Stream` consumes the 220-byte header (`db_load.cpp:5764`) but before any sub-object allocation — a malformed header aborts with the header bytes already charged to the zone stream, not before zone bytes are consumed |
| XModelPieces | Nested-only family (§9.3): alias-slot object with `DB_CompleteObject` publication (`:6156-6172`); piece models are XModel aliases | Zone blocks | Array-extent `ERR_DROP` before per-piece walk (`:6086-6097`) |
| Material | **Per-table grammars differ — not a shared four-way grammar** (`:4471-4628`; authoritative walk §9.4). **textureTable** is the only completion-sealed table: inline `-1` → fresh bump + `DB_RegisterPointerSlot(MaterialTextureTable)` + `DB_CompleteObject` seal (`:4502-4534`); offset → `DB_ConvertOffsetToAlias` shared alias (`:4538-4541`). **constantTable** / **stateBitsTable** are never sealed and never shared: inline `-1` → plain bump loads (`:4572-4577`, `:4615-4620`); offset → `DB_ConvertOffsetToPointer` direct block-4, alignment 16 / 4 respectively (`:4580-4584`, `:4623-4627`) | Zone blocks; technique-set/image members owned by their own families; **material root has no remove handler** (`db_registry.cpp:3306-3343`) | Empty-present spans accept **either** `kInline` (alignment-check only) **or** a validated direct offset (`:4494-4498`, `:4564-4568`, `:4607-4611`), then canonicalize the field to null (`:4500`, `:4570`, `:4613`) — `kInline` is not the only accepted token; the `ERR_DROP`s (`:4487`, `:4557`, `:4600`) fire only when the alignment allocation fails; per-table extent precompute with `ERR_DROP` before reads; completion-seal schema validation (`db_stream.cpp:428-482`) applies to the textureTable walk only (its nested water defs complete separately) |
| TechniqueSet + shaders | techset/technique/vertexDecl/shader inserted-pointer + alias grammar (`:4400-4420`, `:4282-4309`, `:4034-4065`, `:3797-3826`); shader program payload sized by validated `programSize` DWORDs, never native `sizeof` | Zone blocks; GPU program release on complete-fail (`:3813-3817`, pixel `:3878-3882`); media release via `DB_MediaReleaseTechniqueSet` (`db_registry.cpp:3294-3298`) | Renderer-variant mixing is a hard `ERR_DROP` at pass, technique and techset levels; `flags &= 0x3F` canonicalization `:4131` |
| Image + water | loadDef inline (published inserted pointer with `mapType` metadata `:3483-3488`) vs shared alias + `AddRef()` (`:3492-3503`); water samples are plain bumps, `water->image` is an image alias | Zone blocks + GPU texture (`Image_Release` `r_image.cpp:188-210`, `totalMemory` accounting); headless stub `DB_MediaFreeImage` no-op (`db_registry.cpp:85`) | `resourceSize` payload streamed in place behind the 16-byte loadDef header with position asserts (`:3518-3527`); water span/finite validation before publish |
| Sound family | **The inserted-pointer family**: `MssSound.data` offset = alias to zone bytes with `data_len` metadata match (`:2541-2550`); `0xFFFFFFFE` = fresh bump registered as inserted pointer for later sharing (`:2557-2568`); `0xFFFFFFFF` = unshared inline (`:2557-2560`). Alias lists/curves/speaker maps: completed shared objects or aliases; StringTable-style inline-only grammar does **not** apply here | Raw block-4 payload stays zone memory for alias provenance; the **processed** playback buffer is a separate Miles allocation owned by the LoadedSound (`SND_SetData`, `snd.cpp:4834-4883`) and freed by `DB_RemoveLoadedSound` (`db_registry.cpp:1007-1011`) — the only sound-family remover | Headless builds never allocate playback data (`DB_ClearHeadlessSoundRuntimeData` `:768-776`) so the remover's `Z_Free` cannot run on headless; alias-array/sound-file/speaker-map exact-start publication enforced by `RequiresExactStartPublication` (`db_relocation.h:98-100`) |
| PhysPreset / LightDef / ComWorld / GameWorldMp / MapEnts | Simple asset-level inline/insert/alias tokens; **no sub-object aliasing** beyond per-element XStrings and (ComWorld) light `defName`s; LightDef attenuation image is an image alias | Zone blocks; ComWorld/GameWorldMp live in singletons (`&comWorld`, `&cm` region `db_registry.cpp:649-660`); removers: `DB_RemoveComWorld` → `Com_UnloadWorld` (`com_bsp.cpp:59-64`) | Minimal walks; the only hard validations are the game-world header range check (`:6775-6785`) and XString grammar |
| ClipMap(Pvs) | Box brush is an alias-slot completed object (`ClipMapBoxBrush`, exact-start `:7948-7981`, `:8021-8032`); mapEnts/dynEnt references are family aliases; **brush-side plane tokens reject null AND shared-inline as hard errors** (`:5034-5040`) — the audited inserted-pointer rejection pattern | Loads **directly into the `cm` singleton** (not pool-allocated); freed by `DB_RemoveClipMap` → `CM_Unload` (`cm_load.cpp:92-97`, `Sys_Error` if still in use) | Full block-4 span + brush-graph + box-brush validation after the walk (`:7984-8020`); per-brush side/adjacency resolves fail closed with 8 distinct `ERR_DROP`s |
| GameWorldSp + PathData | Path-tree children are direct pointers into the same array with an owned-node check (`:6468`); no cross-family aliasing; basenodes live in block 1 (`:6604`) | Zone blocks (SP-only family; admission-rejected in MP by `db_asset_mode`) | Path-chain bijection + tree-cycle + visibility-matrix validation before publish (`:6684-6731`) |
| GfxWorld | `sunLight` is the **only** alias carrying an explicit extent (`kGfxLightBytes`, `:10828-10831`); portal→cell and aabbTree→smodelIndexes are validated direct pointers; sky/outdoor/probe/lightmap images are image aliases; per-surface materials are material aliases; each `dpvs.smodelDrawInsts[].model` carries the full XModel pointer-token grammar (`:9704-9705`, §9.2); `sunLight` is itself pointer-bearing — its `def` member is a LightDef asset-pointer token (`Load_GfxLightDefPtr(0)`, `:4835-4836`) with the standard inline/insert/alias grammar (`:4793-4827`) and, when inline/shared-inline, a nested `attenuation.image` GfxImage alias (`:4788-4789`) | Zone blocks + GPU vertex buffers (`vd`/`vld`, headless null `:10174-10183`); removed via `DB_RemoveGfxWorld` → `DB_MediaUnloadGfxWorld` (headless no-op `db_registry.cpp:72`) | Cell-graph + aabb-tree + span validation after the walk (`:11036-11081`); every per-cell sub-array has its own block-4 extent `ERR_DROP` |
| Font / Menu / LocalizeEntry | Simple token grammar; menu handler chains are recursive inline allocations; menu item typeData is type-dispatched | Zone blocks; `DB_DynamicCloneMenu`/`DB_RemoveWindowFocus` exist (`db_registry.cpp:1127-1160`) but have **zero production callers** (verified: def+decl only) — dynamic menu cloning is not an enrolled path | No dedicated family validation beyond glyph-count and XString grammar — **evidence gap** (§12) |
| Weapon | bounce-sound table is an exact-start completed object (29×4 B, frozen count); everything else is per-field alias (models/FX/materials/sounds) or plain XStrings/script-string indices | Zone blocks; **no per-weapon struct free** — hunk-resident `bg_weaponDefs[128]`; script-string refs released by `BG_FreeWeaponDefStrings` (`bg_weapons.cpp:156-181`) | Accuracy graphs validated twice (pre-check `:9029-9039` + knot validation `:9285`/`:9323`); runtime conversion is the separate LoadObj route (`bg_weapons_load_obj.cpp:1223-1378`) with its own `ERR_DROP`s — the fast-file route stores the retail struct verbatim |
| RawFile / StringTable | StringTable is **inline-only at the asset token** (no `-2` shared-inline, `:9654`) and exact-start published; RawFile is a simple inline blob | Both zone blocks; cell strings registered as stream C-strings (`DB_RegisterStreamCString`, `db_stream_load.cpp:286-288`) | StringTable registration `ERR_DROP` if the completed object was not registered (`:9676-9680`) |
| FX / ImpactFx | Effect/impact tokens use the standard insert/alias grammar with `mapType`-less handle kinds; nested effect refs resolve by name through the registry (`DB_FindXAssetHeader`) | Zone blocks + FX arena (converted path) — the converted zone-adapter publication commits arena storage **before** the publish callback and rejection strands the committed storage (`fx_fastfile_zone_adapter_disk32.cpp:1136-1240`, test `TestPublicationRejectionStrandsCommittedStorage`) | Converted path: plan validates graph + alias/overlap **before** any workspace mutation; frozen resolver journal; callback-free materialization; publish-after-materialize; 17 failure statuses (§9.11 sources). Decompiled fallback: derived-count and per-array `ERR_DROP`s. Restore path (client-only): two lease revalidations, rollback snapshot under `CRITSECT_FX_ALLOC`, `Sys_Error`+`std::abort` (not `ERR_DROP`) when the runtime cannot be recovered safely (`fx_archive.cpp:2783-2784`, `:2807-2811`, `:2926-2927`) — longjmp would release archive ownership |
| Script strings (cross-family) | The only production-enrolled runtime-ownership consumers are the §2.3 seven bridge sites; `Mark_ScriptStringCustom`'s `SL_AddUser` (`db_stringtable_load.cpp:27-30`) is site #3 | String data lives in the script-string systems, not zone blocks; lease + OwnershipBatch machinery is build-enrolled zero-caller outside the bridge | Bridge rejection (`LegacyBridgeStatus != Success`) is `ERR_DROP`; raw-site re-opening is prevented by source seals |

---

## 11. Graph walkers, fixtures and exact test receipts (corrective, criterion 3)

### 11.1 Local run receipt at the corrective basis

Command: `ctest --test-dir build-tests -C Release --output-on-failure`
(after `cmake -S . -B build-tests -DKISAK_BUILD_MP=OFF
-DKISAK_BUILD_DEDICATED=OFF -DKISAK_BUILD_SP=OFF -DCMAKE_BUILD_TYPE=Release
-DBUILD_TESTING=ON` and a full `cmake --build build-tests --config Release
--parallel`), run on `ba508d1521f832701fee73dd6adf8514ad9b8ff5` with this
document as the only pending edit:

> **100% tests passed, 0 tests failed out of 238** (total 222.30 s).

This supersedes the "217/217" figure quoted from the 2026-09-11 self-review
of #141's head — the suite has grown to 238 registered ctest tests at this
head. Named receipts below are quoted from this run. The full 238-test
roster was additionally re-discovered and re-executed green at the
corrective head `73b995dc` with this §9–§13 revision as the only pending
edit (rework of review `31965d30` findings). It was re-executed green again
with the review-`b3cd8218` rework edits (per-table Material grammars,
completed GfxWorld `dpvs` inventory) as the only pending edit (196.42 s).

### 11.2 What "the graph walker" is, per family

For the fast-file route, **the decompiled `Load_*` recursion is the
production graph walker**: `DB_LoadXFile` → `Load_XAsset`/`Load_XAssetHeader`
(`db_load.cpp:11320`, dispatch §9) → per-family `Load_<Type>Ptr` → nested
`Load_<Subobject>` walks (§9.1–§9.11), with `Mark_XAssetHeader` (`:11452`)
as the mark-phase twin that re-walks the same graph for reference marking
during `DB_FreeUnusedResources`. There is no separate "walker" abstraction
to enroll; criterion 3's walker column below names the exact functions that
perform each family's graph walk today, plus the converted walkers where
they exist. The **missing** walker is the cross-family parity instrument
(graph hash/capture over licensed assets, §4.10, owner #113/`ki-msb`) — no
per-family converted walker substitutes for it.

| Family | Production graph walker (exact symbols) | Converted walker (state) |
|---|---|---|
| Envelope + script strings | `Load_XAsset`/`Load_XAssetHeader`/`Mark_XAssetHeader`; `Load_ScriptString(Custom)`/`Mark_ScriptStringCustom` (bridge site) | `XAssetListDisk32`/`ScriptStringListDisk32` fail-closed iterators (`db_xasset_disk32.cpp`) — **enrolled at the container layer** |
| XAnimParts | `Load_XAnimPartsPtr` → `Load_XAnimParts` → `Load_XAnimDeltaPart*`/`Load_XAnimIndices` | none for the fast-file body; load-object route bounded by `buf_cursor` (`xanim_load_obj.cpp`, `XAnimLoadFile`) |
| XModel | `Load_XModelPtr` → `Load_XModel` → `Load_XSurface(Array)` → collision trees; `DB_ValidateLoadedXModel` | load-object route via `buf_cursor` (`xmodel_load_obj.cpp`); nested-ownership correction in flight #140/`ki-okmr` |
| XModelPieces | `Load_XModelPiecesPtr` via `Load_DynEntityDef` (nested-only, §9.3) | none |
| Material / TechniqueSet / Image | `Load_MaterialHandle` → `Load_Material`; `Load_MaterialTechniqueSetPtr`; `Load_GfxImagePtr` | none — the four-way table grammar is inline in the decompiled readers |
| Sound family | `Load_snd_alias_list_ptr`/`Load_LoadedSoundPtr`/`Load_SndCurvePtr` chains (§9.7) | none |
| ClipMap(Pvs)/ComWorld/GameWorld/MapEnts/PathData | `Load_clipMap_ptr` → `Load_clipMap_t`; `Load_ComWorldPtr`; `Load_GameWorldSp/MpPtr`; `Load_MapEntsPtr`; `Load_PathData` | none |
| GfxWorld / LightDef | `Load_GfxWorldPtr` → `Load_GfxWorld` (cell/portal/aabb recursion); `Load_GfxLightDefPtr` | none — real world-graph walks over licensed assets remain the #113 instrument gap |
| Weapon / Font / Menu / Localize / RawFile / StringTable | `Load_WeaponDefPtr`, `Load_FontHandle`, `Load_menuDef_ptr`/`Load_MenuListPtr`, `Load_LocalizeEntryPtr`, `Load_RawFilePtr`, `Load_StringTablePtr` | none |
| FX / ImpactFx | decompiled: `Load_FxEffectDefHandle` → `Load_FxEffectDef`, `Load_FxImpactTablePtr` → `Load_FxImpactTable`; **converted**: `TryWireEffectDefThroughActiveFxZoneAdapter`/`TryWireImpactTableThroughActiveFxZoneAdapter` → `TryBegin/Seal/PublishFx*ZoneDisk32` two-pass converters (`fx_fastfile_zone_adapter_disk32.cpp`) — **build-enrolled zero-caller** (§4.3 seam b) | converted walker exists and is sealed; **missing production enrollment** is the zero-caller binding, not the walker |
| Save/tagInfo | `g_save.cpp` `SF_TYPE_TAG_INFO` branches | converted entity-map arena (merged #89) — production-enrolled in the save path |

### 11.3 Fixtures inventory (exact names)

- **Byte-level disk32 writers**: `db_xasset_disk32_tests.cpp` `MakeList`/`MakeAsset`/`StoreU32`/`SentinelLayout`; `db_script_string_disk32_tests.cpp` `MakeList`; `fx_fastfile_disk32_tests.cpp` `StoreU8/U16/I16/U32/I32/Float` golden-bytes writers; `fx_archive_body_state_disk32_tests.cpp` `ExpectRejected(MUTATOR)`/`ExpectAccepted(MUTATOR)`.
- **FX graph fixtures**: `fx_fastfile_native_disk32_tests.cpp` `EffectFixture`/`MakeEffect`/`MinimalElem`/`AttachVisuals`/`AttachSamples`/`AttachTrail`/`FinalizeEffectTotalSize`; `fx_fastfile_zone_adapter_disk32_tests.cpp` `BuiltEffect`/`BuildEffect`/`PublishEffect`/`PublishImpact`/`WireOracle`/`RejectingOracle`; `fx_archive_reader_disk32_tests.cpp` `ImageFixture`/`PreparedFixture`.
- **Validation fixtures**: `db_validation_tests.cpp` `TestClipMapFixture`/`TestGfxWorldFixture`/`TestPathTreeFixture`/`TestPhysicsFixture` with `PopulateValid*Fixture` builders.
- **Fuzz corpus**: `fuzz_fastfile.cpp` `BuildXModelPiecesSeed` (ctest `fuzz-fastfile-corpus`); the harness covers xmodel/xanim/fx headers only and **intentionally does not link the production loaders** — no material/image/world/sound coverage.
- **No `FastFileBuilder` exists.** The closest whole-file instrument is `retail_fastfile_parity_harness.cpp` (`TestSelfTest` on synthesized fixtures; real retail fastfiles via `--fastfile`, gated POSIX-only by `retail-fastfile-parity-driver-gates`).

### 11.4 Exact test receipts per family (framework: bespoke `main()` + `CHECK`/`Expect` harnesses; no Catch2/googletest anywhere in `tests/`)

Receipt provenance: the **ctest names** column quotes registered `ctest -N`
discovery at this head (`73b995dc…`, 238 tests; full roster in §11.1), and
the §11.1 run executed every registered test green — so each named ctest
receipt below is validated at this head. The **representative exact test
names** column quotes test-function/`CHECK` symbols read from the test
sources at this head; those are file-level assertions, not ctest units, and
are provenance citations rather than independently re-run receipts.

| Family | Test files | Representative exact test names | ctest names (from the §11.1 run) |
|---|---|---|---|
| Envelope/disk32 primitives | `db_xasset_disk32_tests.cpp`, `db_script_string_disk32_tests.cpp`, `disk32_tests.cpp` | `TestExactSchemaBytes`, `TestHeaderValidationAndLimits`, `TestBuildAdmissionPolicy`, `TestUnalignedIteratorAndGuardBytes`, `TestIteratorFailureAtomicity`, `TestLateRejectionIsAtomic`, `TestMaximumAssetIteration`; `TestExactTokenSchema`, `TestTokenClassesAndRawPreservation`, `TestSharedInlinePreflightIsAtomic` | `database-xasset-disk32-envelope` (#14), `disk32-pointer-token-bounds` (#13), `database-script-string-disk32-walk` (#15) |
| Alias/relocation machinery | `db_relocation_tests.cpp` | `TestDirectResolver`, `TestDirectCString`, `"material water requires exact completed starts"`, `"sound files require exact completed starts"`, `"completed shared-object disk32 schemas remain fixed"` (pins `kSoundFileBytes==12`, `kSpeakerMapBytes==408`, `kSndAliasBytes==92`, `kGfxLightBytes==64`, `kDpvsPlaneBytes==20`, `kStringTableBytes==16`) | `database-relocation-alias-provenance` (#85) |
| Validation/checked arithmetic | `db_validation_tests.cpp`, `db_asset_mode_tests.cpp` | `"checked clipmap brush global extents accepted"`, `"complete clipmap brush graph with a shared plane accepted"`, `"path visibility uses the complete directed-pair bit matrix"`, `"path tree cycle rejected"`, `"world portal graph rejects an interior target cell pointer"`; `"MP must reject SP clipmaps"`, `"MP must accept PVS clipmaps"` | `database-checked-arithmetic` (#87), `database-build-mode-asset-policy` (#88) |
| XAnimParts / XModel / XModelPieces | `xanim_load_test.cpp`, `xmodel_load_test.cpp`, `xanim_parts_split_test.cpp`, `model_surface_stream_tests.cpp`, `skel_memory_atomic_tests.cpp` | `TestXModelPiecesParse`, `TestOverrunLatchesFailed`, `TestTransactionalRollback`, `TestReadStringBounds`, `TestReadBoneLimit`, `TestReadTriLimit`, `TestUnalignedReads`; `TestConfigFileParse`, `TestCollisionDataParse`, `TestFullLoadSequenceSync`, `TestXModelPartsParse`; `CHECK(sizeof(XAnimParts) == 88u)`; `TestFormerRigidStackBoundary`, `TestMixedStreamAndNativeAlignment`, `TestPointerBytesRemainNativeWidth`, `TestExactAndMalformedCursorBounds` | `xanim-load-bounded-cursor` (#213), `xmodel-load-bounded-cursor` (#214), `xanim-parts-split-contracts` (#215), `renderer-model-surface-stream-contracts` (#121), `skeleton-memory-atomic-protocols` (#116) |
| Material / Image / TechniqueSet | (no dedicated loader suite — **evidence gap**, §12) covered via `db_validation_tests.cpp` material sections and `shader_cache_tests.cpp` | `TestBrushWrapper`-adjacent material sections: `"one-pass material technique disk extent accepted"`, `"maximum shader load definition accepted"`, `"maximum material argument layout accepted"`, `"maximum water grid downsamples one picmip level"`; `TestSourceIdentity`, `TestSidecarRoundTrip`, `TestCorruptSidecarRegeneration` | `database-checked-arithmetic` (#87, material extent sections); `database-derived-shader-cache-contracts` (#235) |
| Sound family | `sound_dry_send_source_test.cmake` (source-contract only; **no runtime loader suite**) | source-contract pins: `SND_SetData` headless guard `#ifndef KISAK_DEDI_HEADLESS`, `DB_SetInsertedPointer(… SoundData …)` retention, `DB_ClearHeadlessSoundRuntimeData` presence | `sound-dry-send-source-invariants` |
| ClipMap / PathData / GfxWorld / physics | `db_validation_tests.cpp`, `phys_*_tests.cpp` | brush/path/world fixture checks quoted above; `TestTokenSentinels`, `TestBindResolveRelease`, `TestStaleTokenRejection`; `TestInvalidCallbacksAreRejectedBeforeAllocation`, `TestBodyAllocationFailureIsStable`, `TestUserDataFailureRollsBackBody` | inside `database-checked-arithmetic` (#87); `phys-obj-id-sidecar-contracts` (#118), `physics-resource-pair-rollback` (#103), `phys-obj-id-owner-bound-source-invariants` (#171) |
| UI/data families | `ui_safety_tests.cpp`, `hudelem_sort_tests.cpp`, `weapon_input_safety_tests.cpp`, `weapon_model_safety_tests.cpp` | `TestSavegameCountCapacity`, `TestSavegameSlotResolution`, `TestCapacityFailureIsAtomic`, `TestAppendFailureIsAtomic`, `TestNativePointerSort`; weapon mains check attack-suppression flags and model-slot bounds — **no fast-file weapon loader suite** | `ui-safety-runtime-contracts` (#3), `ui-safety-source-invariants` (#4), `hudelem-sort-mp-contracts` (#154), `hudelem-sort-sp-contracts` (#155), `hudelem-sort-source-invariants` (#200), `weapon-input-safety-contracts` (#153), `weapon-model-safety-contracts` (#152), `weapon-model-safety-source-invariants` (#206) |
| FX / ImpactFx | `fx_fastfile_disk32_tests.cpp`, `fx_fastfile_native_disk32_tests.cpp`, `fx_fastfile_impact_native_disk32_tests.cpp`, `fx_fastfile_zone_adapter_disk32_tests.cpp`, `fx_fastfile_native_arena_tests.cpp`, `fx_archive_*_tests.cpp` (12 files), `db_fx_zone_adapter_wiring*_tests.cpp` | `TestEffectDefinitionGoldenBytes`, `TestElementDefinitionGoldenBytes`, `TestImpactGoldenBytes`; `TestValidDefinitions`, `TestMaximumGraphAndResolverJournal`, `TestPointerSpanAndProvenanceFailures`, `TestFutureTokenMutationRestore`; `TestHappyPathAndFullWidthIdentities`, `TestPlanAliasChecksPrecedeCallbacks`, `TestLegacyTokenCompatibility`; `TestZeroElementEffect`, `TestAllVisualKindsEffect`, `TestPublicationRejectionStrandsCommittedStorage`, `TestArenaExhaustionFailsClosed`; `TestNoBindingReturnsNull`, `TestProductionCallSiteNoBindingFallsThrough`; `TestEveryEffectHandleRoundTrip`, `TestMaximumPhysicsCapacity`, `TestLeaseGatesAndCallbackReentry`; `TestSuccessPath`, `TestLiveRecoveryInjection`, `TestSnapshotRecoveryInjection`, `TestSafeEmptyInjection` | `effectscore-fastfile-disk32-schema` (#76), `effectscore-fastfile-native-disk32-conversion` (#77), `effectscore-fastfile-impact-native-disk32-conversion` (#78), `effectscore-fastfile-native-arena` (#79), `effectscore-fastfile-zone-adapter-disk32` (#80), `effectscore-fastfile-disk32-source-invariants` (#182), `effectscore-fastfile-zone-adapter-source-invariants` (#183); `effectscore-archive-disk32-codec` (#67), `effectscore-archive-body-state-disk32-codec` (#70), `effectscore-archive-system-disk32-codec` (#71), `effectscore-archive-buffers-disk32-codec` (#72), `effectscore-archive-native-disk32-codec` (#73), `effectscore-archive-reader-disk32` (#74), `effectscore-archive-restore-candidate-disk32` (#75), `effectscore-archive-physics-transaction` (#172), `effectscore-archive-capacity-planning` (#97), `effectscore-archive-restore-control` (#98), `effectscore-archive-physics-batch-control` (#99), `effectscore-archive-gate-control` (#101), `effectscore-archive-restore-workspace` (#102), `effectscore-archive-snapshot-publication-source-invariants` (#176), `effectscore-archive-system-disk32-source-invariants` (#177), `effectscore-archive-buffers-disk32-source-invariants` (#178), `effectscore-archive-native-disk32-source-invariants` (#179), `effectscore-archive-body-state-disk32-source-invariants` (#180), `effectscore-archive-reader-disk32-source-invariants` (#181); `effectscore-effect-table-transactional-restore` (#9), `effectscore-effect-table-bounded-save` (#10), `effectscore-effect-table-stack-usage` (#11), `effectscore-fixed-width-atomic-layouts` (#92), `effectscore-runtime-blob-layout` (#93), `effectscore-missing-effect-alias` (#94), `effectscore-physics-body-sidecar` (#95), `effectscore-physics-sidecar-production-test-access-sealed` (#96), `effectscore-effect-table-source-invariants` (#173), `effectscore-effect-table-save-source-invariants` (#174), `effectscore-live-physics-source-invariants` (#175), `effectscore-visibility-publication` (#111), `effectscore-iterator-atomic-protocol` (#112), `effectscore-snapshot-publication-coherence` (#113), `effectscore-pool-and-handle-contracts` (#114); `database-fx-zone-adapter-wiring` (#82), `database-fx-zone-adapter-wiring-production-call-site` (#83), `database-fx-zone-adapter-wiring-headless` (#84) — all green in the §11.1 run |
| Zone runtime / script strings | `db_zone_*_tests.cpp` (9 files), `db_load_legacy_bridge_tests.cpp`, `script_string_*_tests.cpp` | `TestClaimCommitAndFailureAtomicity`, `TestLiveUnloadRetryAtEveryCleanupBoundary`, `TestCompositionAuthentication`, `TestHappyPathRoundTrip`, `TestInternValidationGuards`, `TestBackToBackCyclesDoNotPoison`; `deterministicContracts`, `twoWayUserTransferContention` | `database-load-legacy-bridge` (#69); `database-zone-load-context-lifecycle` (#16), `database-zone-stream-ownership-runtime-contracts` (#17), `database-zone-pending-copy-ledger` (#19), `database-zone-script-string-ownership-controller` (#22), `database-zone-runtime-facade` (#25), `database-zone-runtime-callback-context` (#27), `database-zone-runtime-table-ownership` (#30), `database-zone-runtime-storage-layout` (#81), `database-zone-runtime-stable-context-integration` (#65), `database-zone-runtime-stable-context-forgotten-finish` (#66) — plus the `database-zone-runtime-table-*` parameter variants (#31–#64) and the `database-zone-*`-`source-invariants` set (#186–#197), all in the §11.1 roster |
| Save/tagInfo | `save_taginfo_tests.cpp`, `save_taginfo_production_tests.cpp` | `TestStaticContracts`, `TestForwardRoundTrip`, `TestWireImageBytes`; `TestNamedRecordRoundTrip`, `TestWireParityWithPinnedConverter`, `TestWriteRejectsMisalignedPointer` | `save-taginfo-disk32-converter` (#229), `save-taginfo-production-path` (#230) |
| ABI gates | `pointer_truncation_test.cmake`, `abi_sizeof_debt_test.cmake`, `headless_profile_test.cmake`, `headless_include_debt_test.cmake` | allowlist inventories (not pass/fail units): `pointer_truncation.allow` 24 tracked sites, `abi_sizeof_debt.allow` 183 entries + formula ledger 7 (+13-line file), `headless_include_debt.allow` 21 entries | `pointer-truncation-tripwire` (#163), `abi-sizeof-debt-tripwire` (#167), `dedi-headless-source-profile` (#161), `dedi-headless-client-media-include-debt` (#162) — all green in the §11.1 run |
| Parity instrument | `retail_fastfile_parity_harness.cpp`, `db_graph_hash_tests.cpp` | `TestSha256KnownAnswers`, `TestWidthParityProperty`, `TestFloatCanonicalization`; `TestSelfTest` | `retail-fastfile-parity-harness-self-test` (#237), `retail-fastfile-parity-driver-gates` (#238), `database-graph-hash-canonical` (#234) |
| Production seals | 9 `*_production_seal_tests.cpp` + object-inspection scripts | e.g. `db_load_legacy_bridge_production_seal_tests.cpp` (bridge surface pinned, test-access denied), `db_zone_runtime_table_production_seal_tests.cpp` (exact record sizes), `fx_physics_sidecar_production_seal_tests.cpp` (`!CanMutateActiveCount`), `physicalmemory_runtime_production_seal_tests.cpp` | `database-load-legacy-bridge-production-test-access-sealed` (#68), `database-zone-stream-ownership-production-test-access-sealed` (#18), `database-zone-pending-copy-production-test-access-sealed` (#20), `database-registry-ownership-production-test-access-sealed` (#24), `database-zone-runtime-facade-production-test-access-sealed` (#26), `database-zone-runtime-callback-context-production-test-access-sealed` (#28), `database-zone-runtime-callback-context-macro-off-object-symbol-sealed` (#29), `database-zone-runtime-table-production-test-access-sealed` (#64), `effectscore-physics-sidecar-production-test-access-sealed` (#96), `universal-physicalmemory-runtime-production-test-access-sealed` (#108) — all green in the §11.1 run |

**Explicit evidence gaps in this column** (missing fixtures/tests, not missing
production behavior): no runtime loader suite exists for Material/Image/
TechnSet fast-file walks, Sound fast-file walks, UI-family fast-file walks
(Font/Menu/Localize/RawFile/StringTable beyond the relocation pins), or an
end-to-end `Load_clipMap_t`/`Load_GfxWorld` execution test — coverage for
those families is at the validation/relocation/extent layer plus source
contracts. Naming these is part of criterion 3; filling them is future work
and is tracked in §12's status ledger, not claimed here.

---

## 12. Evidence-status ledger (corrective: explicit gap taxonomy)

Issue #129 requires distinguishing four different kinds of "not done".
This section labels each family with every gap kind that applies. Legend:
**ME** = missing evidence (the code exists but no exact test/fixture/receipt
pins it); **MI** = missing implementation (no converted/portable
implementation exists; the decompiled reader is the shipping behavior);
**MPE** = missing production enrollment (a converted/sealed path exists but
no production call path executes it); **SP** = deferred SP-only surface.
Absence of a label means that kind of gap does not apply to the family.

| Family | ME | MI | MPE | SP |
|---|---|---|---|---|
| Container envelope / script strings | — | — | durable-table/facade ownership stack zero-caller beyond §2.3 seven sites | — |
| XAnimParts | no Disk32 schema module for the family body (88-byte header + payload arrays are stream literals); consumer-side raw-width payloads unexercised on native64 | portable body schema does not exist | — | — |
| XModel | no Disk32 module for 220-byte header / XSurface / collSurf bodies (only collision-tree + rigid-list extents frozen) | portable body schema does not exist | model cursor production correction (#140/`ki-okmr` → #124) not landed | — |
| XModelPieces | nested-only; no dedicated tests | — | — | — |
| Material | no runtime loader suite; no Disk32 module for the 80-byte root/24-byte info/148-byte techset roots | portable reader does not exist | — | — |
| TechniqueSet + shaders | loader covered only via validation + shader-cache suites | portable reader does not exist | — | — |
| Image + water | no runtime loader suite; loadDef 16-byte header pinned by iassert, not a `disk32::` constant | portable reader does not exist | — | — |
| Sound family | no runtime loader suite (source-contract only); SndCurve/LoadedSound/list roots have no `disk32::` extents | portable reader does not exist; Miles playback replacement is A10/#132, out of scope | — | full SP streamed-sound persistence deferred with the save family |
| PhysPreset | header literal un-pinned | — | — | — |
| ClipMap(Pvs) | no end-to-end `Load_clipMap_t` execution test | portable reader does not exist | — | SP ClipMap type admission-rejected in MP by design |
| ComWorld / GameWorldMp / MapEnts | header literals un-pinned; no dedicated suites | — | — | GameWorldSp + PathData are SP-admission types (deferred with SP maps, not an MP blocker) |
| GameWorldSp + PathData | SP-only; validation-suite coverage exists (fixtures), no execution test | — | — | **SP** by type admission |
| GfxWorld / LightDef | no execution test; many subobject extents unpinned (dpvs/surface/light-grid/shadow families) | portable world-graph reader does not exist; real graph walks over licensed assets blocked on #113/`ki-msb` | — | — |
| Font / Menu / LocalizeEntry | no dedicated suites; header literals un-pinned | — | `DB_DynamicCloneMenu`/`DB_RemoveWindowFocus` are dead code (zero callers) — recorded, not enrolled | menus are full-MP-client-only (not a headless gap) |
| Weapon | no fast-file weapon loader suite | runtime conversion exists only on the LoadObj route | — | — |
| RawFile / StringTable | covered by relocation pins only | — | — | — |
| FX / ImpactFx | — | decompiled fallback remains the shipping reader by design | **fast-file adapter conversion is build-enrolled zero-caller**: `TryBindStorage` (`db_zone_runtime_facade.cpp:674`) has zero production callers; enrollment gated on `docs/task.md` "Enroll the guarded native FX/impact path…" (unchecked) | — |
| Save/tagInfo | — | remaining `g_save.cpp` SP sizing debt | tagInfo conversion **is** enrolled (merged #89) | full SP save/load **SP** |
| Script VM | — | raw-width VM is **fixed**: script runtime pointers were widened to native width by merged PR #119 (`f0b4157a`, "abi: widen script runtime pointers while preserving serialized formats" — a merge commit already in this basis's ancestry, so it is landed work, not pending scope); serialized script formats were preserved unchanged. Remaining raw-width debt is the 24 `pointer_truncation.allow` tracked narrow-conversion sites (cross-family, §2.4 counts) | — | full SP script persistence **SP** |
| World-graph parity instrument | — | — | n/a (instrument itself missing: #113/`ki-msb`) | — |

Reading guide: a family can be fully *implemented* (decompiled reader is
correct and validated) yet still carry **ME** rows — criterion 3's fixture
receipts are the missing artifact there, and adding them is documentation/
test work, not implementation. **MPE** rows are the only gaps that require
production wiring before a capability can be claimed. **MI** rows are the
honest "the portable rewrite has not happened" entries and must not be
closed by documentation.

**High-address (>4 GiB) evidence, per family.** Four evidence kinds are kept
distinct and must not be conflated: (a) **synthetic pointer-bit
preservation** — `TestPointerBytesRemainNativeWidth` (XModel/XSurface family,
via ctest `renderer-model-surface-stream-contracts`); (b) **real high-address
conversion** — `TestHappyPathAndFullWidthIdentities`
(`tests/fx_fastfile_impact_native_disk32_tests.cpp:564-635`, FX / ImpactFx
family, via ctest `effectscore-fastfile-impact-native-disk32-conversion`): it
materializes the native impact table and compares every native handle to its
resolved identity, asserting each expected address exceeds `UINT32_MAX` on
64-bit (`:615-624`); (c) **token-domain arithmetic** — the `uint32`
sentinel/block/span tests, all families; (d) **production execution** — a
shipping profile exercising >4 GiB host addresses: **none**. Beyond (a) and
the cross-family static gates (`pointer-truncation-tripwire`,
`abi-sizeof-debt-tripwire`), only the FX / ImpactFx family currently holds a
(b)-class receipt; the other FX fixture cases that touch high addresses are
synthetic and failure-path-only (forced wrapped asset identities,
`fx_fastfile_impact_native_disk32_tests.cpp:820-822`; a limit-valued output
pointer rejected by the materialization preflight, `:1256-1262`), and (d)
remains open for **every** family — the FX/impact production binding is still
zero-caller (§4). Every family row above that does not name one of these pins
therefore carries an implicit high-address **ME** entry on top of any
explicitly listed gaps.

---

## 13. Corrective revision record

- **Scope.** Docs/evidence only. No serialization/layout change, no enum
  change, no native64 gate retirement, no compatibility claim. The merged
  PR #141 ledger (§1–§8) is preserved verbatim except for the basis note;
  this revision appends §9–§13 and tightens citations.
- **Provenance corrections vs the merged ledger** (supersessions, not
  contradictions):
  - §4.3's "cl_cgame.cpp:1220 … via FX_Archive" is now precise: SP client
    `cl_cgame.cpp:1220` calls **`FX_Restore(0, memFile)`** (save side is
    `FX_Save` at `:1209`); MP client `cl_cgame_mp.cpp:1287` calls
    **`FX_Archive`**, which dispatches save/restore (`fx_archive.cpp:3256-3267`).
  - §4.5's "`db_load.cpp:5224-5258` (`Load_BrushWrapper` side-plane token
    walk)" is now exact: walk def `:5179`, token snapshot `:5203-5238`,
    hard errors `:5234-5238`, inline/deferred resolution `:5239-5252`,
    `:5304-5328`. Also: the same rejection pattern exists clipmap-side in
    `Load_cbrushside_t` (`:5034-5040`).
  - §3's XModelPieces row now records that type 0x0 has **no top-level
    fast-file dispatch case**; it is reachable only nested inside
    DynEntityDef (`:7300`).
  - The test-count receipt grows from 217 (#141 era) to **238/238** at this
    head (§11.1).
- **Rework of review `31965d30` findings** (four P2 corrections; citations
  re-verified against the recorded basis `ba508d15` before each edit):
  - §9.10 menu statement walks are now enumerated to the leaves: the
    `Load_statement` chain (`:8361-8370` → `Load_expressionEntry_ptrArray`
    `:8346-8359` → `Load_expressionEntry_ptr` `:8335-8344` →
    `Load_expressionEntry` `:8328-8333` → `Load_entryInternalData`
    `:8314-8326` → `Load_Operand` `:8302-8307` →
    `Load_operandInternalDataUnion` `:8277-8300`, whose `VAL_STRING` branch
    follows the pointer through `Load_XString`) and the `typeData` interiors
    (`Load_listBoxDef_t` `doubleClick`/`selectIcon`, `Load_editFieldDef_t`
    plain 32-byte body, `Load_multiDef_t` 2×32 XString arrays). §9.1–§9.9
    were audited for the same grouped-row pattern; their nested walks were
    already enumerated (XAnim delta frames, XSurface collision trees,
    brush-side plane tokens, pathnode tree splits, GfxWorld portals), so no
    other rows changed.
  - §11.4's ctest column now quotes registered `ctest -N` discovery names
    exactly (e.g. `database-build-mode-asset-policy`,
    `phys-obj-id-sidecar-contracts`, `physics-resource-pair-rollback`,
    `ui-safety-runtime-contracts`, `weapon-input-safety-contracts`,
    `save-taginfo-disk32-converter`, `database-graph-hash-canonical`, the
    `effectscore-*` fastfile/archive/runtime sets); the prior shorthand
    (`database-asset-mode`, `phys-obj-id`, `ui-safety`, `fx-fastfile-*`,
    `save-taginfo variants`, …) mixed historical shorthand with registered
    names and is replaced. A provenance note separates validated-at-this-head
    ctest receipts from file-level test-function citations.
  - §10's high-address bullet now separates 32-bit token-domain bounds
    (sentinels, block index, span — pinned by `uint32`-domain
    `disk32_tests.cpp`) from >4 GiB host storage/conversion coverage, which
    exists only via `TestPointerBytesRemainNativeWidth` (XModel/XSurface) and
    the static truncation/ABI gates; every other family records an explicit
    high-address **ME** entry (§12 note). *(High-address wording superseded
    by the `e59c95d7` rework entry below, which credits the FX / ImpactFx
    conversion fixture.)*
  - §12's Script VM row credits the **merged** PR #119 (`f0b4157a`, verified
    ancestor of basis `ba508d15` via `git merge-base --is-ancestor`) instead
    of describing it as pending scope, and restates remaining raw-width debt
    as the 24 `pointer_truncation.allow` tracked sites plus SP script
    persistence.
- **Rework of review `e59c95d7` finding** (one P2 evidence correction;
  citations re-verified against this head before each edit; the review
  `31965d30` UI expression/listBox and PR #119 fixes are retained unchanged):
  - §10's and §12's high-address statements no longer classify every
    non-XModel family as lacking >4 GiB execution evidence. The FX / ImpactFx
    family holds a **real high-address conversion** receipt —
    `TestHappyPathAndFullWidthIdentities`
    (`tests/fx_fastfile_impact_native_disk32_tests.cpp:564-635`, ctest
    `effectscore-fastfile-impact-native-disk32-conversion`, green at this
    head): it materializes the native impact table and compares every
    materialized native handle to its resolved identity, asserting each
    expected address exceeds `UINT32_MAX` on 64-bit (`:615-624`). §12 now
    separates the four evidence kinds (synthetic pointer-bit preservation /
    real high-address conversion / token-domain arithmetic / production
    execution). A related-FX-fixture audit found no additional family-level
    >4 GiB execution receipt: the file's other high-address touches are
    synthetic failure-path inputs (wrapped forced asset identities
    `:820-822`; a limit-valued output pointer rejected by the
    materialization preflight `:1256-1262`). The legitimate gaps stand
    unchanged: production enrollment (the FX/impact binding is still
    zero-caller, §4) and retail parity remain missing for every family.
- **Rework of review `91337ca4` finding** (one P2 failure/rollback-evidence
  correction; citations re-verified against this head before each edit; the
  `31965d30` UI/test-name and `e59c95d7` high-address corrections are
  retained unchanged):
  - §10's failure-grammar bullet no longer claims whole-zone publication
    atomicity. It now separates per-object seal ordering (slot registration
    before children; `DB_SetInsertedPointer`/`DB_CompleteObject` after the
    object's own body walk) from what is *not* demonstrated: nested assets
    register into the global pool mid-parent-walk
    (`Load_MaterialTechniqueSetAsset` → `DB_AddXAsset` →
    `DB_LinkXAssetEntry`, `db_load.cpp:4409`, `db_registry.cpp:938-942`,
    `:2095-2118`) before the parent can still fail
    (`db_load.cpp:4483-4523`); the `DB_Thread` long jump ends in
    `Com_ErrorAbort` → `Sys_Error` (`db_registry.cpp:2691-2699`,
    `qcommon/common.cpp:883-886`), a fatal abort rather than a rollback;
    and cleanup is wholesale-only zone reclamation. Whole-zone rollback is
    marked **ME** instead of claimed. §10's XModel row no longer says
    malformed headers abort before zone bytes are consumed: `Load_Stream`
    reads the 220-byte header (`db_load.cpp:5764`) before the collision
    precheck (`:5765-5780`), so the header bytes are consumed first; the
    precheck still precedes any sub-object allocation. Documentation only —
    no production code, serialization, or gate change.
- **Rework of review `b3cd8218` findings** (two P2 inventory/grammar
  corrections; citations re-verified against this head before each edit; the
  `31965d30`, `e59c95d7`, and `91337ca4` corrections are retained unchanged):
  - §10's Material row no longer describes a shared four-way
    inline/pointer/alias/empty grammar across all three tables — the
    grammars differ per table (`:4471-4628`): only **textureTable** is
    completion-sealed (`DB_RegisterPointerSlot`/`DB_CompleteObject`,
    `:4511-4534`) and alias-capable (`DB_ConvertOffsetToAlias`,
    `:4538-4541`); **constantTable** and **stateBitsTable** are plain-bump
    inline loads or `DB_ConvertOffsetToPointer` direct block-4 tokens
    (alignment 16 / 4, `:4580-4584`, `:4623-4627`), never sealed, never
    shared. Empty-present spans accept `kInline` **or** a validated direct
    offset (`:4494-4498`, `:4564-4568`, `:4607-4611`) before null
    canonicalization — `kInline` is not the only accepted token. §9.4
    (already per-table) is unchanged; §10 is now consistent with it.
  - §9.9's GfxWorld `dpvs` inventory is complete to the end of
    `Load_GfxWorldDpvsStatic` (`:10584`): new explicit rows enumerate
    `dpvs.smodelDrawInsts[]` — a 76-byte-stride array
    (`Load_GfxStaticModelDrawInstArray` `:9708-9721`) each of whose elements
    loads a nested XModel pointer token (`varXModelPtr = &drawInst->model;
    Load_XModelPtr(0)`, `:9704-9705`) — plus the previously omitted block-1
    `dpvs.surfaceMaterials` (`Load_GfxDrawSurfArray` def `:10260`, count
    `staticSurfaceCount`) and `dpvs.surfaceCastsSunShadow`
    (`Load_raw_uint128Array`, count `surfaceVisDataCount`) at `:10568-10583`.
    The grouped-row audit also corrected `dpvsDyn` from six to **eight**
    block-1 pushes (`dynEntCellBits[2]` + `dynEntVisData[2][3]`,
    `:10374-10437`) and re-verified the scene-dyn (`:10250-10258`),
    shadow-geometry/light-region (`:10265-10326`) and per-surface
    (`Load_GfxSurface` → `Load_MaterialHandle`, `:4867-4871`) grouped rows as
    plain-stream/sub-alloc walkers with no additional hidden nested tokens.
    §10's GfxWorld row now credits the per-instance XModel tokens.
- **Criteria mapping.** Criterion 1 (exhaustive inventory): §9.0–§9.12
  enumerate every pointer-bearing subobject walk of all 33 registered
  families (26 dispatchable types + XModelPieces nested-only + the 6
  Unavailable admission-rejected types + the virtual String/AssetList
  in-memory types) with exact symbols and lines. Criterion
  2 (per-family invariants): §10. Criterion 3 (walker/fixture/test
  receipts): §11, including the explicit missing-fixture list. Criteria 4–6
  (owner links, layout preservation, gate integrity) were already satisfied
  by §4–§8 and are unchanged; this revision adds no owner and retires no
  gate. §2.4's gate counts were exact at `a1ca543b`; at this head the
  non-comment allowlist entries are pointer-truncation 24 (was 26),
  sizeof-debt 183 + formula 7 (was 238 + 13), headless-include 21 (was 28)
  — all gates still armed; the decreases reflect landed debt burndown, not
  gate relaxation.
- **Validation.** Local gates at this head: cmake configure clean, full
  Release build clean, `ctest` **238/238 passed** (§11.1). Docs-only diff
  (`git diff --stat` = this file only). Refinery review of the exact head
  is required before any issue-acceptance receipt; GitHub issue closure
  alone remains non-acceptance (parent `ki-mtmw` stays open until all six
  criteria are evidenced).

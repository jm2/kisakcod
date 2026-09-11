# Native ABI and asset closure ledger (production MP capability)

Ledger for [fork issue #129](https://github.com/jm2/kisakcod/issues/129) (roadmap
item A07). It inventories every registered asset family, its transitive
pointer-bearing subobjects, the frozen Disk32 schema surface, the native runtime
owner, the reader/converter and publication/unload paths, and the current
production-caller/enrollment state — so the remaining M4/M5 queue ("remaining
runtime structures", "broader asset relocation") can be scheduled per family
instead of as one opaque block.

**Basis tree:** `a1ca543b28391f347d4127797d971fdc0bc5a199` (`origin/master`,
post-#138/#139). File and line references are exact at this commit; treat later
drift the way `docs/CODEBASE_AUDIT.md` does — the tree wins.

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

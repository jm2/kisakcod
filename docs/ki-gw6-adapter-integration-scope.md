# ki-gw6: Native FX/impact enrollment — adapter integration scope

This file documents the scope of work for `ki-gw6` (Native FX/impact
enrollment: wire guarded adapter into production route behind the explicit
legacy-x86 boundary). It is captured as part of the analysis performed
when the bead was first claimed.

## Bead description

> Wire the guarded adapter into the production route behind the explicit
> legacy-x86 boundary. Preserve retail bytes and writer.
>
> Affected files: src/fx/, src/database/db_load.cpp, src/effects/
> Build: cmake --build build-tests --target fx_archive_test fx_effect_def_test fx_impact_test
> Test: ctest --test-dir build-tests --output-on-failure

## Architecture

The adapter `fx::fastfile::FxFastFileZoneAdapterDisk32Workspace` and its
companion arena `fx::fastfile::FxFastFileNativeArena` already exist on
master (introduced by PR #33 "Add zone-owned FX fast-file arena and
guarded zone adapter"). They are exercised by
`kisakcod-fx-fastfile-zone-adapter-disk32-tests` (1742 lines, passing on
master) and a small companion suite.

What is missing is the **production wiring**: the adapter's
`TryBeginFx*ZoneDisk32` / `TryRecord*ZoneDisk32` / `TryPublish*ZoneDisk32`
entry points are currently only called from inside
`fx_fastfile_zone_adapter_disk32.cpp` itself. The production fast-file
loader (`src/database/db_load.cpp`) still reads the legacy x86 inline
asset path via `Load_Stream` for `Load_FxEffectDef`,
`Load_FxEffectDefArray`, `Load_FxImpactTable`, and
`Load_FxImpactTablePtr`, allocating transient `AllocLoad_FxElemVisStateSample()`
storage rather than reserving from the zone-owned native arena.

## Integration boundary

The adapter's contract (from `fx_fastfile_zone_adapter_disk32.h`):

> The stateful wire walk (db_load.cpp) keeps ownership of stream order and
> materialization; it reports each Disk32 extent and each externally
> resolved reference to this adapter in exact legacy wire order.

So `db_load.cpp` keeps driving `Load_Stream`/`Load_StreamArray` for the
byte walk (preserving the wire order and the retail-bytes invariant), and
for each extents and references the loader reports to the adapter:

1. `TryBeginFxEffectDefZoneDisk32(workspace, arena, cursor, &header)` —
   opens an effect transaction.
2. `TryRecordFxEffectDefNameZoneDisk32(workspace, name, nameBytes)` —
   records the name extent (validated by the cursor oracle).
3. `TryRecordFxElemDefArrayZoneDisk32(workspace, elems, count)` — records
   the element array.
4. `TryRecordFxElemSpanZoneDisk32(workspace, kind, address, count)` —
   records each per-element span (velSamples, visSamples, visuals array,
   mark visuals array, trail def, trail vertices, trail indices) in
   legacy wire order.
5. `TryRecordFxReferenceZoneDisk32(workspace, sourceField, resolution)`
   — records each resolved reference (material, model, effect handle).
6. `TryRecordFxSoundNameZoneDisk32(workspace, sourceField, name, bytes)`
   — records the sound-alias name extent for the visual slot.
7. `TrySealFxEffectDefZoneDisk32(workspace)` — closes the recording phase.
8. `TryPublishFxEffectDefZoneDisk32(workspace, publication, &outEffect)`
   — invokes the publication sink; the sink is expected to call the
   existing `Load_FxEffectDefAsset` to preserve the writer's
   `DB_AddXAsset(ASSET_TYPE_FX, ...)` registration.

`FxImpactTable` follows the same pattern with the
`TryBeginFxImpactTableZoneDisk32` / `TryRecord*FxImpactTable*ZoneDisk32`
/ `TrySealFxImpactTableZoneDisk32` / `TryPublishFxImpactTableZoneDisk32`
sibling entry points, and `Mark_FxImpactTableAsset` is the publication
sink's inner call.

## Required zone-memory additions

The loader needs:

- A `FxFastFileNativeArena *` and a
  `FxFastFileZoneAdapterDisk32Workspace *` reachable from the zone
  memory. These are wired through
  `src/database/db_zone_runtime_storage_fx_bridge.h` (already exists) and
  `src/EffectsCore/fx_zone_runtime_storage_bridge.cpp` (already exists).
  The runtime table (`db_zone_runtime_storage.cpp`) currently binds and
  authenticates the storage; the loader needs to thread the authenticated
  pointers into the inline-asset path.
- A `FxFastFileZoneAdapterCursor` whose `validateWireSpan` callback
  delegates to `DB_VerifyXBlockExtent` (or equivalent) so the adapter can
  fail closed on any reported extent that does not lie within the current
  zone's materialized XBlock.

## Sub-scope order (recommended for the integration branch)

1. Impact table header only — minimal call to
   `TryBeginFxImpactTableZoneDisk32` + `TryRecordFxImpactTableNameZoneDisk32`
   + `TrySealFxImpactTableZoneDisk32` + `TryPublishFxImpactTableZoneDisk32`
   in `Load_FxImpactTablePtr`. Validates the storage + cursor threading
   without touching the inline-sentinel matrix.
2. Effect def header only — same pattern for `Load_FxEffectDef`.
3. Effect def element array + per-element spans (the largest piece; needs
   one `TryRecordFxElemSpanZoneDisk32` per legacy span and one
   `TryRecordFxReferenceZoneDisk32` per material/model/effect
   resolution).
4. Effect def sound alias names per visual slot.
5. Impact table handle array + nested effect refs.

Each sub-scope can ship as its own polecat branch (e.g. `polecat/ki-gw6.1`,
`polecat/ki-gw6.2`, …) so the refinery sees incremental, reviewable
changes instead of one mega-commit.

## Verification

The bead's listed test targets (`fx_archive_test`, `fx_effect_def_test`,
`fx_impact_test`) are names from an older test inventory; the current
CMake target set on master is `kisakcod-fx-*` (e.g.
`kisakcod-fx-fastfile-zone-adapter-disk32-tests`,
`kisakcod-fx-fastfile-impact-native-disk32-tests`,
`kisakcod-fx-archive-disk32-tests`). The intended verification is:

```
cmake --build build-tests --target \
    kisakcod-fx-fastfile-zone-adapter-disk32-tests \
    kisakcod-fx-fastfile-impact-native-disk32-tests \
    kisakcod-fx-archive-disk32-tests
ctest --test-dir build-tests -R 'effectscore-fastfile|fx-archive' \
    --output-on-failure
```

(`kisakcod-fx-fastfile-zone-adapter-disk32-tests` already passes on
master.)

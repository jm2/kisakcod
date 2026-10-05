#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/23-gfxworld.schema
#include <database/db_gfxworld_validation.h>
#include <database/db_validation.h>

#include <cstdint>
#include <cstring>
#include <limits>

// GfxWorld, a wave-4 family (docs/design/FASTFILE_LOADER.md): the renderer's
// world, which a headless server only parses past. The body mirrors
// Load_GfxWorld: the 732-byte record streams into the temp block and the
// counts its runtime trusts are checked, then with block 4 pushed its names,
// its indices and sky surfaces (which keep their layout in block 4), its sky
// image through Image's step, its sun light (a completed object), its
// reflection probes and their runtime textures, its DPVS planes, and its
// cells: AABB trees (children offsets at the native stride), portals (each
// naming a native cell) and index arrays, under the 32-bit loader's cell,
// topology and portal rules; then its lightmaps, light grid, brush models,
// material memory, vertex data and sun flare, its runtime arrays in block 1,
// each primary light's shadow geometry and light region, and the static
// DPVS. The dynamic DPVS does not load yet, so every world still fails
// closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using Disk = disk32::GfxWorldDisk32;
using db::validation::CheckedArrayBytes;
using db::validation::CheckedCountProduct;
using db::validation::CheckedCountSum;

// The extents Load_GfxWorld derives from the record's counts.
struct Extents
{
    std::int32_t cellBytes = 0;
    std::int32_t reflectionProbeBytes = 0;
    std::int32_t cullGroupBytes = 0;
    std::int32_t modelBytes = 0;
    std::int32_t cellCasterCount = 0;
    std::int32_t sortedSurfaceCount = 0;
    std::int32_t entityShadowVisCount = 0;
    std::int32_t modelShadowVisCount = 0;
    std::int32_t brushShadowVisCount = 0;
};

// The cells' counts: 1..1024 cells, their bit rows, and static surfaces
// partitioned among the world's.
bool CellsValid(const Disk &disk, Extents *extents)
{
    const std::int32_t cellCount = disk.dpvsPlanes.cellCount;
    return db::validation::GfxWorldCellLayoutValid(!disk.cells.token.isNull(), cellCount, &extents->cellBytes)
        && db::validation::GfxWorldCellBitsValid(cellCount, disk.cellBitsCount) && disk.surfaceCount >= 0
        && db::validation::WorldAabbSurfacePartitionsValid(disk.dpvs.staticSurfaceCount, disk.dpvs.staticSurfaceCountNoDecal,
                                                           static_cast<std::uint32_t>(disk.surfaceCount));
}

// The lookup arrays: 1..254 reflection probes with both arrays, cull groups
// and a token exactly when there are any, and at least one brush model.
bool ProbesValid(const Disk &disk)
{
    const bool probes = disk.reflectionProbeCount != 0;
    return db::validation::GfxReflectionProbeCountValid(disk.reflectionProbeCount)
        && probes == !disk.reflectionProbes.token.isNull() && probes == !disk.reflectionProbeTextures.token.isNull();
}

bool LookupArraysValid(const Disk &disk, Extents *extents)
{
    const auto probeCount = static_cast<std::int32_t>(disk.reflectionProbeCount);
    std::int32_t textureBytes = 0;
    return ProbesValid(disk) && disk.cullGroupCount >= 0
        && (disk.cullGroupCount != 0) == !disk.dpvs.cullGroups.token.isNull() && disk.modelCount > 0
        && !disk.models.token.isNull()
        && CheckedArrayBytes(probeCount, disk32::kGfxReflectionProbeBytes, &extents->reflectionProbeBytes)
        && CheckedArrayBytes(probeCount, disk32::kGfxTextureBytes, &textureBytes)
        && CheckedArrayBytes(disk.cullGroupCount, disk32::kGfxCullGroupBytes, &extents->cullGroupBytes)
        && CheckedArrayBytes(disk.modelCount, disk32::kGfxBrushModelBytes, &extents->modelBytes);
}

// The cell-caster bits and the sorted surfaces, with their tokens.
bool VisibilityValid(const Disk &disk, Extents *extents)
{
    std::int32_t cellWords = 0;
    return db::validation::CheckedCountCeilDiv(disk.dpvsPlanes.cellCount, 32, &cellWords)
        && CheckedCountProduct(disk.dpvsPlanes.cellCount, cellWords, &extents->cellCasterCount)
        && CheckedCountSum(disk.dpvs.staticSurfaceCountNoDecal, disk.dpvs.staticSurfaceCount, &extents->sortedSurfaceCount)
        && db::validation::PointerCountConsistent(!disk.cellCasterBits.token.isNull(), extents->cellCasterCount)
        && db::validation::PointerCountConsistent(!disk.dpvs.sortedSurfIndex.token.isNull(),
                                                  extents->sortedSurfaceCount);
}

// The shadow-visibility extents: the primary lights past the sun, times
// 4096 entities or each dynamic model and brush.
bool ShadowVisValid(const Disk &disk, Extents *extents)
{
    std::int32_t throughSun = 0;
    std::int32_t relevant = 0;
    return CheckedCountSum(disk.sunPrimaryLightIndex, 1, &throughSun)
        && db::validation::CheckedCountDifference(disk.primaryLightCount, throughSun, &relevant)
        && CheckedCountProduct(relevant, 4096, &extents->entityShadowVisCount)
        && CheckedCountProduct(disk.dpvsDyn.dynEntClientCount[0], relevant, &extents->modelShadowVisCount)
        && CheckedCountProduct(disk.dpvsDyn.dynEntClientCount[1], relevant, &extents->brushShadowVisCount);
}

// An array that holds no pointer: any non-null token means count elements
// follow, `alignment`-aligned in the current block; they keep their layout.
template <typename T>
bool LoadArray(disk32::PointerToken token, std::int64_t count, std::uint32_t elementBytes, std::uint32_t alignment,
               T **out)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    const std::int64_t bytes = count * elementBytes;
    if (count < 0 || bytes > (std::numeric_limits<std::int32_t>::max)())
        return Drop("Invalid fast-file world array count");
    std::uint8_t *const array = DB_AllocStreamPos(static_cast<std::int32_t>(alignment - 1));
    if (!StreamBytes(array, static_cast<std::int32_t>(bytes)))
        return false;
    *out = reinterpret_cast<T *>(array);
    return true;
}

// A span: -1 streams it here, any other token names one already in block 4;
// either way it keeps its layout.
template <typename T>
bool LoadOrResolve(disk32::PointerToken token, std::uint32_t bytes, std::uint32_t alignment, T **out)
{
    if (token.isInline() || token.isNull())
        return LoadArray(token, bytes, 1, alignment, out);
    std::uintptr_t address = 0;
    if (const db::relocation::Status status = DB_ResolveOffsetBytes(
            token, bytes ? bytes : 1, alignment, db::relocation::BlockBit(kVirtualBlock), &address);
        status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<T *>(address);
    return true;
}


// An array of records that hold pointers: any non-null token means count
// records follow 4-aligned; each converts into native storage, then loads
// what it names, in order.
template <typename Disk32, typename Native, typename Convert>
bool LoadRecords(disk32::PointerToken token, std::int64_t count, Native **out, Convert convert)
{
    std::uint8_t *records = nullptr;
    *out = nullptr;
    if (!LoadArray(token, count, sizeof(Disk32), 4, &records))
        return false;
    if (!records || !count)
        return true;
    Native *const native = AllocNative<Native>(static_cast<std::int32_t>(count));
    if (!native)
        return false;
    *out = native;
    for (std::int64_t index = 0; index < count; ++index)
    {
        Disk32 disk{};
        std::memcpy(&disk, records + index * sizeof(disk), sizeof(disk));
        std::memset(&native[index], 0, sizeof(Native)); // padding too: native storage starts as junk
        if (!convert(disk, &native[index]))
            return false;
    }
    return true;
}

// A node's children offset at the native stride: the disk offset counts
// 44-byte nodes; one that does not stays unusable (-1), and the topology
// rules reject it wherever it matters.
std::int32_t NativeChildrenOffset(std::int32_t offset)
{
    constexpr std::int32_t kDiskStride = sizeof(disk32::GfxAabbTreeDisk32);
    constexpr std::int32_t kNativeStride = sizeof(GfxAabbTree);
    if (offset <= 0)
        return offset;
    if (offset % kDiskStride || offset / kDiskStride > (std::numeric_limits<std::int32_t>::max)() / kNativeStride)
        return -1;
    return offset / kDiskStride * kNativeStride;
}

// Load_GfxAabbTree: the node, its children offset at the native stride, and
// its static-model indices (-1 here, or named in block 4), each below the
// world's static-model count.
bool ConvertTree(const disk32::GfxAabbTreeDisk32 &disk, GfxAabbTree *out, std::uint32_t smodelCount)
{
    CopyGfxAabbTreeScalars(disk, out);
    out->childrenOffset = NativeChildrenOffset(disk.childrenOffset);
    if (!db::validation::PointerCountConsistent(!disk.smodelIndexes.token.isNull(), disk.smodelIndexCount))
        return Drop("Invalid fast-file pointer/count for world AABB static-model indices");
    return LoadOrResolve(disk.smodelIndexes.token, disk.smodelIndexCount * 2u, 2, &out->smodelIndexes)
        && (db::validation::AllU16Below(out->smodelIndexes, disk.smodelIndexCount, smodelCount)
            || Drop("Fast-file world AABB has an invalid static-model index"));
}

// What a portal's cell offset names: the world's cells, at their disk and
// native addresses.
struct Cells
{
    const std::uint8_t *disk = nullptr;
    std::uint32_t count = 0;
    GfxCell *native = nullptr;
};

// The cell a portal names: an offset to a whole disk cell, which names its
// native twin.
bool LoadPortalCell(disk32::PointerToken token, const Cells &cells, GfxCell **out)
{
    if (!token.isOffset())
        return Drop("Invalid fast-file world portal cell token");
    const std::uint8_t *named = nullptr;
    if (!LoadOrResolve(token, disk32::kGfxCellBytes, 4, &named))
        return false;
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(named) - reinterpret_cast<std::uintptr_t>(cells.disk);
    if (named < cells.disk || offset % disk32::kGfxCellBytes || offset / disk32::kGfxCellBytes >= cells.count)
        return Drop("Fast-file world portal target is not a cell");
    *out = cells.native + offset / disk32::kGfxCellBytes;
    return true;
}

// Load_GfxPortal: the writable state zeroed, the cell, then 3..64 vertices
// and GfxPortalRuntimeValid's rule.
bool ConvertPortal(const disk32::GfxPortalDisk32 &disk, GfxPortal *out, const Cells &cells)
{
    CopyGfxPortalScalars(disk, out);
    if (!LoadPortalCell(disk.cell.token, cells, &out->cell))
        return false;
    if (disk.vertices.token.isNull() || disk.vertexCount < db::validation::kMinGfxPortalVertices
        || disk.vertexCount > db::validation::kMaxGfxPortalVertices)
    {
        return Drop("Invalid fast-file world portal vertex layout");
    }
    return LoadArray(disk.vertices.token, disk.vertexCount, disk32::kVec3Bytes, 4, &out->vertices)
        && (db::validation::GfxPortalRuntimeValid(*out) || Drop("Invalid completed fast-file world portal"));
}

// Load_GfxCell: its layout rule, its AABB trees and their topology, its
// portals, cull-group indices and probe indices.
bool ConvertCell(const disk32::GfxCellDisk32 &disk, GfxCell *out, const Cells &cells, const GfxWorld &world)
{
    CopyGfxCellScalars(disk, out);
    db::validation::GfxCellLayoutExtents extents;
    if (!db::validation::GfxCellLayoutValid(disk.mins, disk.maxs, !disk.aabbTree.token.isNull(), disk.aabbTreeCount,
                                            !disk.portals.token.isNull(), disk.portalCount,
                                            !disk.cullGroups.token.isNull(), disk.cullGroupCount,
                                            !disk.reflectionProbes.token.isNull(), disk.reflectionProbeCount,
                                            &extents))
    {
        return Drop("Invalid fast-file world cell layout");
    }
    const auto tree = [&world](const disk32::GfxAabbTreeDisk32 &record, GfxAabbTree *native) {
        return ConvertTree(record, native, world.dpvs.smodelCount);
    };
    const auto portal = [&cells](const disk32::GfxPortalDisk32 &record, GfxPortal *native) {
        return ConvertPortal(record, native, cells);
    };
    return LoadRecords<disk32::GfxAabbTreeDisk32>(disk.aabbTree.token, disk.aabbTreeCount, &out->aabbTree, tree)
        && db::gfxworld_validation::DB_ValidateWorldAabbCell(&world, out)
        && LoadRecords<disk32::GfxPortalDisk32>(disk.portals.token, disk.portalCount, &out->portals, portal)
        && LoadArray(disk.cullGroups.token, disk.cullGroupCount, 4, 4, &out->cullGroups)
        && LoadArray(disk.reflectionProbes.token, disk.reflectionProbeCount, 1, 1, &out->reflectionProbes);
}

// Load_GfxCellArray: the cells 4-aligned, then each one's parts.
bool LoadCells(const Disk &disk, GfxWorld *out)
{
    const std::int32_t count = disk.dpvsPlanes.cellCount;
    std::uint8_t *records = nullptr;
    if (!LoadArray(disk.cells.token, count, disk32::kGfxCellBytes, 4, &records))
        return false;
    GfxCell *const cells = AllocNative<GfxCell>(count);
    if (!cells)
        return false;
    out->cells = cells;
    const Cells named{records, static_cast<std::uint32_t>(count), cells};
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::GfxCellDisk32 cell{};
        std::memcpy(&cell, records + index * sizeof(cell), sizeof(cell));
        std::memset(&cells[index], 0, sizeof(cells[index]));
        if (!ConvertCell(cell, &cells[index], named, *out))
            return false;
    }
    return true;
}

constexpr std::uint32_t kLightBytes = sizeof(disk32::GfxLightDisk32);

// DB_ValidateSunLight: a directional light, a shadow-map flag, and finite
// color, direction and origin.
bool SunLightValid(const GfxLight &light)
{
    return (light.type == GFX_LIGHT_TYPE_DIR && light.canUseShadowMap <= 1
            && db::validation::FiniteFloatArray(light.color, 12))
        || Drop("Invalid completed fast-file sun light");
}

// The sun light: -1 streams it 4-aligned here as a completed object (its
// def through LightDef's step), checked before it completes; any other token
// names an earlier one's native twin.
bool LoadSunLight(disk32::PointerToken token, GfxLight **out)
{
    *out = nullptr;
    std::uintptr_t native = 0;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        const db::relocation::Status status =
            DB_ResolveCompletedObjectNative(token, DBAliasKind::GfxLight, kLightBytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
        *out = reinterpret_cast<GfxLight *>(native);
        return true;
    }
    std::uint8_t *const record = DB_AllocStreamPos(3);
    const DBAliasHandle completed = record ? DB_RegisterPointerSlot(record, DBAliasKind::GfxLight) : DBAliasHandle{};
    GfxLight *const light = AllocNative<GfxLight>(1);
    if (!completed || !StreamBytes(record, kLightBytes) || !light)
        return false;
    disk32::GfxLightDisk32 disk{};
    std::memcpy(&disk, record, sizeof(disk));
    std::memset(light, 0, sizeof(*light));
    CopyGfxLightScalars(disk, light);
    LoadGfxLightDefPtr(disk.def.token, &light->def);
    *out = light;
    return SunLightValid(*light)
        && DB_CompleteObject(completed, DBAliasKind::GfxLight, record, kLightBytes, kLightBytes, light);
}

// Load_GfxReflectionProbeArray: any non-null token means count probes
// follow 4-aligned; each converts into native storage and names its image.
bool LoadProbes(disk32::PointerToken token, std::uint32_t count, GfxReflectionProbe **out)
{
    std::uint8_t *records = nullptr;
    *out = nullptr;
    if (!LoadArray(token, count, sizeof(disk32::GfxReflectionProbeDisk32), 4, &records))
        return false;
    if (!records)
        return true;
    GfxReflectionProbe *const probes = AllocNative<GfxReflectionProbe>(static_cast<std::int32_t>(count));
    if (!probes)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        disk32::GfxReflectionProbeDisk32 disk{};
        std::memcpy(&disk, records + index * sizeof(disk), sizeof(disk));
        std::memset(&probes[index], 0, sizeof(probes[index]));
        CopyGfxReflectionProbeScalars(disk, &probes[index]);
        LoadGfxImagePtr(disk.reflectionImage.token, &probes[index].reflectionImage);
    }
    *out = probes;
    return true;
}

// Textures the renderer fills: as on x86 any non-null token zero-fills count
// 4-byte slots in block 1, but a runtime texture is pointer-sized, so the
// native array is zeroed native storage.
bool LoadTextures(disk32::PointerToken token, std::int64_t count, GfxTexture **out)
{
    std::uint8_t *slots = nullptr;
    *out = nullptr;
    DB_PushStreamPos(1);
    if (!LoadArray(token, count, disk32::kGfxTextureBytes, 4, &slots))
        return false;
    DB_PopStreamPos();
    if (!slots || !count)
        return true;
    GfxTexture *const textures = AllocNative<GfxTexture>(static_cast<std::int32_t>(count));
    if (!textures)
        return false;
    std::memset(textures, 0, static_cast<std::size_t>(count) * sizeof(GfxTexture));
    *out = textures;
    return true;
}

// Load_GfxWorldDpvsPlanes: the planes (-1 here, or named in block 4), the
// nodes, then the scene-entity cell bits in block 1.
bool LoadDpvsPlanes(const Disk &disk, GfxWorldDpvsPlanes *out)
{
    const disk32::GfxWorldDpvsPlanesDisk32 &planes = disk.dpvsPlanes;
    CopyGfxWorldDpvsPlanesScalars(planes, out);
    std::int32_t cellBits = 0;
    if (!db::validation::PointerCountConsistent(!planes.planes.token.isNull(), disk.planeCount)
        || !CheckedCountProduct(planes.cellCount, 256, &cellBits))
    {
        return Drop("Invalid fast-file pointer/count for world planes");
    }
    if (!LoadOrResolve(planes.planes.token, disk.planeCount * 20u, 4, &out->planes)
        || !LoadArray(planes.nodes.token, disk.nodeCount, 2, 2, &out->nodes))
    {
        return false;
    }
    DB_PushStreamPos(1);
    if (!LoadArray(planes.sceneEntCellBits.token, cellBits, 4, 4, &out->sceneEntCellBits))
        return false;
    DB_PopStreamPos();
    return true;
}
bool ConvertLightmap(const disk32::GfxLightmapArrayDisk32 &disk, GfxLightmapArray *out)
{
    LoadGfxImagePtr(disk.primary.token, &out->primary);
    LoadGfxImagePtr(disk.secondary.token, &out->secondary);
    return true;
}

bool ConvertMaterialMemory(const disk32::MaterialMemoryDisk32 &disk, MaterialMemory *out)
{
    CopyMaterialMemoryScalars(disk, out);
    LoadMaterialPtr(disk.material.token, &out->material);
    return true;
}

// Load_GfxLightGrid: its axes (each below 3), then its row starts (one per
// row along the row axis), raw row data, entries and colors, which keep
// their layout in block 4.
bool LoadLightGrid(const disk32::GfxLightGridDisk32 &disk, GfxLightGrid *out)
{
    if (disk.rowAxis >= 3 || disk.colAxis >= 3)
        return Drop("Invalid fast-file light-grid axes");
    std::int32_t range = 0;
    std::int32_t rows = 0;
    if (!disk.rowDataStart.token.isNull()
        && (!db::validation::CheckedCountDifference(disk.maxs[disk.rowAxis], disk.mins[disk.rowAxis], &range)
            || !CheckedCountSum(range, 1, &rows)))
    {
        return Drop("Invalid fast-file derived count for light-grid rows");
    }
    return LoadArray(disk.rowDataStart.token, rows, 2, 2, &out->rowDataStart)
        && LoadArray(disk.rawRowData.token, disk.rawRowDataSize, 1, 1, &out->rawRowData)
        && LoadArray(disk.entries.token, disk.entryCount, 4, 4, &out->entries)
        && LoadArray(disk.colors.token, disk.colorCount, sizeof(GfxLightGridColors), 4, &out->colors);
}

// An array the renderer fills: block 1, zero-filled.
template <typename T>
bool LoadRuntime(disk32::PointerToken token, std::int64_t count, std::uint32_t elementBytes, std::uint32_t alignment,
                 T **out)
{
    DB_PushStreamPos(1);
    if (!LoadArray(token, count, elementBytes, alignment, out))
        return false;
    DB_PopStreamPos();
    return true;
}

// Load_GfxWorld's middle: lightmaps (naming images), the light grid, the
// lightmaps' runtime textures, the brush models, material memory (naming
// materials), the vertex and layer data (headless keeps no D3D buffers, as on
// x86), the sun flare's materials and the outdoor image.
bool LoadMiddle(const Disk &disk, GfxWorld *out)
{
    std::int32_t vertexBytes = 0;
    if (!CheckedCountProduct(disk.vertexCount, 44, &vertexBytes))
        return Drop("Invalid fast-file derived count for world vertex-buffer bytes");
    if (!LoadRecords<disk32::GfxLightmapArrayDisk32>(disk.lightmaps.token, disk.lightmapCount, &out->lightmaps,
                                                      ConvertLightmap)
        || !LoadLightGrid(disk.lightGrid, &out->lightGrid)
        || !LoadTextures(disk.lightmapPrimaryTextures.token, disk.lightmapCount, &out->lightmapPrimaryTextures)
        || !LoadTextures(disk.lightmapSecondaryTextures.token, disk.lightmapCount, &out->lightmapSecondaryTextures)
        || !LoadArray(disk.models.token, disk.modelCount, disk32::kGfxBrushModelBytes, 4, &out->models)
        || !LoadRecords<disk32::MaterialMemoryDisk32>(disk.materialMemory.token, disk.materialMemoryCount,
                                                       &out->materialMemory, ConvertMaterialMemory)
        || !LoadArray(disk.vd.vertices.token, disk.vertexCount, 44, 4, &out->vd.vertices)
        || !LoadArray(disk.vld.data.token, disk.vertexLayerDataSize, 1, 1, &out->vld.data))
    {
        return false;
    }
    LoadMaterialPtr(disk.sun.spriteMaterial.token, &out->sun.spriteMaterial);
    LoadMaterialPtr(disk.sun.flareMaterial.token, &out->sun.flareMaterial);
    LoadGfxImagePtr(disk.outdoorImage.token, &out->outdoorImage);
    return true;
}

// The runtime arrays in block 1: the cell-caster bits, the scene's dynamic
// models and brushes, the shadow visibility, and the primary light of each
// dynamic model.
bool LoadRuntimeArrays(const Disk &disk, const Extents &extents, GfxWorld *out)
{
    const std::int64_t dynamicModels = disk.dpvsDyn.dynEntClientCount[0];
    return LoadRuntime(disk.cellCasterBits.token, extents.cellCasterCount, 4, 4, &out->cellCasterBits)
        && LoadRuntime(disk.sceneDynModel.token, dynamicModels, sizeof(GfxSceneDynModel), 4, &out->sceneDynModel)
        && LoadRuntime(disk.sceneDynBrush.token, disk.dpvsDyn.dynEntClientCount[1], sizeof(GfxSceneDynBrush), 4,
                       &out->sceneDynBrush)
        && LoadRuntime(disk.primaryLightEntityShadowVis.token, extents.entityShadowVisCount, 4, 4,
                       &out->primaryLightEntityShadowVis)
        && LoadRuntime(disk.primaryLightDynEntShadowVis[0].token, extents.modelShadowVisCount, 4, 4,
                       &out->primaryLightDynEntShadowVis[0])
        && LoadRuntime(disk.primaryLightDynEntShadowVis[1].token, extents.brushShadowVisCount, 4, 4,
                       &out->primaryLightDynEntShadowVis[1])
        && LoadRuntime(disk.nonSunPrimaryLightForModelDynEnt.token, dynamicModels, 1, 1,
                       &out->nonSunPrimaryLightForModelDynEnt);
}

// Load_GfxShadowGeometry: the sorted-surface indices, then the static-model
// indices, each 2-aligned in block 4. The renderer reads every index the
// counts cover, the first into the static surfaces' draw keys and the second
// into the static models' draw instances, so each has its token and is below
// that count.
bool ConvertShadowGeometry(const disk32::GfxShadowGeometryDisk32 &disk, const disk32::GfxWorldDpvsStaticDisk32 &dpvs,
                           GfxShadowGeometry *out)
{
    CopyGfxShadowGeometryScalars(disk, out);
    if (!db::validation::PointerCountConsistent(!disk.sortedSurfIndex.token.isNull(), disk.surfaceCount)
        || !db::validation::PointerCountConsistent(!disk.smodelIndex.token.isNull(), disk.smodelCount))
    {
        return Drop("Invalid fast-file pointer/count for world shadow geometry");
    }
    if (!LoadArray(disk.sortedSurfIndex.token, disk.surfaceCount, 2, 2, &out->sortedSurfIndex)
        || !LoadArray(disk.smodelIndex.token, disk.smodelCount, 2, 2, &out->smodelIndex))
    {
        return false;
    }
    return (db::validation::AllU16Below(out->sortedSurfIndex, disk.surfaceCount, dpvs.staticSurfaceCount)
            && db::validation::AllU16Below(out->smodelIndex, disk.smodelCount, dpvs.smodelCount))
        || Drop("Fast-file world shadow geometry has an invalid index");
}

// Load_GfxLightRegionHull: its axes, 4-aligned in block 4. The renderer
// reads every axis the count covers, so a hull with axes has the token.
bool ConvertHull(const disk32::GfxLightRegionHullDisk32 &disk, GfxLightRegionHull *out)
{
    CopyGfxLightRegionHullScalars(disk, out);
    if (disk.axisCount && disk.axis.token.isNull())
        return Drop("Invalid fast-file pointer/count for world light-region axes");
    return LoadArray(disk.axis.token, disk.axisCount, sizeof(GfxLightRegionAxis), 4, &out->axis);
}

// Load_GfxLightRegion: its hulls, converted into native storage, each then
// loading its axes. A region with hulls has the token.
bool ConvertRegion(const disk32::GfxLightRegionDisk32 &disk, GfxLightRegion *out)
{
    CopyGfxLightRegionScalars(disk, out);
    if (disk.hullCount && disk.hulls.token.isNull())
        return Drop("Invalid fast-file pointer/count for world light-region hulls");
    return LoadRecords<disk32::GfxLightRegionHullDisk32>(disk.hulls.token, disk.hullCount, &out->hulls, ConvertHull);
}

// Each primary light's shadow geometry and light region, converted into
// native storage. The renderer indexes both by primary light, so a world
// with primary lights has both tokens.
bool LoadPrimaryLights(const Disk &disk, GfxWorld *out)
{
    const auto lights = static_cast<std::int64_t>(disk.primaryLightCount);
    if (lights && (disk.shadowGeom.token.isNull() || disk.lightRegion.token.isNull()))
        return Drop("Invalid fast-file pointer/count for world primary lights");
    const auto shadows = [&disk](const disk32::GfxShadowGeometryDisk32 &geometry, GfxShadowGeometry *native) {
        return ConvertShadowGeometry(geometry, disk.dpvs, native);
    };
    return LoadRecords<disk32::GfxShadowGeometryDisk32>(disk.shadowGeom.token, lights, &out->shadowGeom, shadows)
        && LoadRecords<disk32::GfxLightRegionDisk32>(disk.lightRegion.token, lights, &out->lightRegion,
                                                     ConvertRegion);
}

// The static DPVS's runtime arrays in block 1: three static-model and three
// surface visibility arrays, one byte per model or surface, then the LOD
// data, 16-aligned.
bool LoadStaticVisibility(const disk32::GfxWorldDpvsStaticDisk32 &disk, std::int32_t lodDataCount,
                          GfxWorldDpvsStatic *out)
{
    for (int index = 0; index < 3; ++index)
    {
        if (!LoadRuntime(disk.smodelVisData[index].token, disk.smodelCount, 1, 1, &out->smodelVisData[index]))
            return false;
    }
    for (int index = 0; index < 3; ++index)
    {
        if (!LoadRuntime(disk.surfaceVisData[index].token, disk.staticSurfaceCount, 1, 1, &out->surfaceVisData[index]))
            return false;
    }
    return LoadRuntime(disk.lodData.token, lodDataCount, 16, 16, &out->lodData);
}

bool ConvertSurface(const disk32::GfxSurfaceDisk32 &disk, GfxSurface *out)
{
    CopyGfxSurfaceScalars(disk, out);
    LoadMaterialPtr(disk.material.token, &out->material);
    return true;
}

bool ConvertDrawInst(const disk32::GfxStaticModelDrawInstDisk32 &disk, GfxStaticModelDrawInst *out)
{
    CopyGfxStaticModelDrawInstScalars(disk, out);
    LoadXModelPtr(disk.model.token, &out->model);
    return true;
}

// Draw keys the renderer fills: as on x86 any non-null token zero-fills
// count 8-byte keys 4-aligned in block 1, but a key is a 64-bit word, so the
// native array is zeroed, 8-aligned native storage.
bool LoadDrawKeys(disk32::PointerToken token, std::int64_t count, GfxDrawSurf **out)
{
    std::uint8_t *slots = nullptr;
    *out = nullptr;
    if (!LoadRuntime(token, count, sizeof(GfxDrawSurf), 4, &slots))
        return false;
    if (!slots || !count)
        return true;
    GfxDrawSurf *const keys = AllocNative<GfxDrawSurf>(static_cast<std::int32_t>(count));
    if (!keys)
        return false;
    std::memset(keys, 0, static_cast<std::size_t>(count) * sizeof(GfxDrawSurf));
    *out = keys;
    return true;
}

// The static DPVS's per-model and per-surface arrays: the renderer reads an
// element of each for every model or surface its counts cover, so each such
// array has its token.
bool DrawArraysPresent(const Disk &disk)
{
    const disk32::GfxWorldDpvsStaticDisk32 &dpvs = disk.dpvs;
    const auto present = [](bool hasCount, disk32::PointerToken token) { return !hasCount || !token.isNull(); };
    bool ok = present(dpvs.smodelCount, dpvs.smodelInsts.token) && present(dpvs.smodelCount, dpvs.smodelDrawInsts.token)
        && present(disk.surfaceCount > 0, dpvs.surfaces.token)
        && present(dpvs.staticSurfaceCount, dpvs.surfaceMaterials.token)
        && present(dpvs.smodelVisDataCount, dpvs.lodData.token)
        && present(dpvs.surfaceVisDataCount, dpvs.surfaceCastsSunShadow.token);
    for (int index = 0; index < 3; ++index)
    {
        ok = ok && present(dpvs.smodelCount, dpvs.smodelVisData[index].token)
            && present(dpvs.staticSurfaceCount, dpvs.surfaceVisData[index].token);
    }
    return ok;
}

// The sorted surfaces, 2-aligned in block 4, each below the static surface
// count.
bool LoadSortedSurfaces(const disk32::GfxWorldDpvsStaticDisk32 &dpvs, std::int32_t count, GfxWorldDpvsStatic *out)
{
    if (!LoadArray(dpvs.sortedSurfIndex.token, count, 2, 2, &out->sortedSurfIndex))
        return false;
    return db::validation::AllU16Below(out->sortedSurfIndex, static_cast<std::uint32_t>(count),
                                       dpvs.staticSurfaceCount)
        || Drop("Fast-file world has an invalid sorted surface index");
}

// The static-model instances, surfaces (naming materials), cull groups and
// draw instances (naming models), in block 4.
bool LoadDrawRecords(const Disk &disk, GfxWorldDpvsStatic *out)
{
    const disk32::GfxWorldDpvsStaticDisk32 &dpvs = disk.dpvs;
    return LoadArray(dpvs.smodelInsts.token, dpvs.smodelCount, sizeof(GfxStaticModelInst), 4, &out->smodelInsts)
        && LoadRecords<disk32::GfxSurfaceDisk32>(dpvs.surfaces.token, disk.surfaceCount, &out->surfaces,
                                                 ConvertSurface)
        && LoadArray(dpvs.cullGroups.token, disk.cullGroupCount, sizeof(GfxCullGroup), 4, &out->cullGroups)
        && LoadRecords<disk32::GfxStaticModelDrawInstDisk32>(dpvs.smodelDrawInsts.token, dpvs.smodelCount,
                                                             &out->smodelDrawInsts, ConvertDrawInst);
}

// Load_GfxWorldDpvsStatic: its static-model count (each model a 16-bit
// index) and arrays, then the visibility and LOD data; the sorted surfaces;
// the static-model instances; the surfaces (naming materials), cull groups
// and draw instances (naming models); then the draw keys and sun-shadow bits
// in block 1, the bits 16-aligned.
bool LoadDpvsStatic(const Disk &disk, const Extents &extents, GfxWorldDpvsStatic *out)
{
    const disk32::GfxWorldDpvsStaticDisk32 &dpvs = disk.dpvs;
    std::int32_t lodDataCount = 0;
    if (dpvs.smodelCount > db::validation::kMaxWorldAabbStaticModels
        || !CheckedCountProduct(2, dpvs.smodelVisDataCount, &lodDataCount))
    {
        return Drop("Invalid fast-file world static-model counts");
    }
    if (!DrawArraysPresent(disk))
        return Drop("Invalid fast-file pointer/count for world draw arrays");
    return LoadStaticVisibility(dpvs, lodDataCount, out) && LoadSortedSurfaces(dpvs, extents.sortedSurfaceCount, out)
        && LoadDrawRecords(disk, out)
        && LoadDrawKeys(dpvs.surfaceMaterials.token, dpvs.staticSurfaceCount, &out->surfaceMaterials)
        && LoadRuntime(dpvs.surfaceCastsSunShadow.token, dpvs.surfaceVisDataCount, 16, 16,
                       &out->surfaceCastsSunShadow);
}

// The record's scalars and its nested records'.
void CopyScalars(const Disk &disk, GfxWorld *out)
{
    CopyGfxWorldScalars(disk, out);
    CopyGfxWorldVertexDataScalars(disk.vd, &out->vd);
    CopyGfxWorldVertexLayerDataScalars(disk.vld, &out->vld);
    CopyGfxLightGridScalars(disk.lightGrid, &out->lightGrid);
    Copysunflare_tScalars(disk.sun, &out->sun);
    CopyGfxWorldDpvsStaticScalars(disk.dpvs, &out->dpvs);
    CopyGfxWorldDpvsDynamicScalars(disk.dpvsDyn, &out->dpvsDyn);
}

// Load_GfxWorld's start: the names, the indices, the sky surfaces and image.
bool LoadSky(const Disk &disk, GfxWorld *out)
{
    if (!LoadXString(disk.name, &out->name) || !LoadXString(disk.baseName, &out->baseName))
        return false;
    if (!out->name)
        return Drop("Fast-file world has no name"); // the asset pool hashes it
    if (!LoadArray(disk.indices.token, disk.indexCount, 2, 2, &out->indices)
        || !LoadArray(disk.skyStartSurfs.token, disk.skySurfCount, 4, 4, &out->skyStartSurfs))
    {
        return false;
    }
    LoadGfxImagePtr(disk.skyImage.token, &out->skyImage);
    return true;
}

// Then the sun light, the reflection probes and their textures, and the DPVS
// planes.
bool LoadLights(const Disk &disk, GfxWorld *out)
{
    return LoadSunLight(disk.sunLight.token, &out->sunLight)
        && LoadProbes(disk.reflectionProbes.token, disk.reflectionProbeCount, &out->reflectionProbes)
        && LoadTextures(disk.reflectionProbeTextures.token, disk.reflectionProbeCount, &out->reflectionProbeTextures)
        && LoadDpvsPlanes(disk, &out->dpvsPlanes);
}

// The parts in block 4, in Load_GfxWorld's order. The dynamic DPVS does not
// load yet, and no world publishes before it does.
bool LoadParts(const Disk &disk, const Extents &extents, GfxWorld *out)
{
    return LoadSky(disk, out) && LoadLights(disk, out) && LoadCells(disk, out) && LoadMiddle(disk, out)
        && LoadRuntimeArrays(disk, extents, out) && LoadPrimaryLights(disk, out)
        && LoadDpvsStatic(disk, extents, &out->dpvs) && Drop("Fast-file world dynamic DPVS has no 64-bit loader yet");
}
} // namespace

// The record at the temp block's position, its counts, then its parts with
// block 4 pushed.
bool LoadGfxWorld(GfxWorld *out)
{
    Disk disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    Extents extents;
    if (!CellsValid(disk, &extents))
        return Drop("Invalid fast-file world cell array");
    if (!LookupArraysValid(disk, &extents))
        return Drop("Invalid fast-file world cell lookup arrays");
    if (!VisibilityValid(disk, &extents) || !ShadowVisValid(disk, &extents))
        return Drop("Invalid fast-file world visibility counts");
    CopyScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadParts(disk, extents, out))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadGfxWorldPtrDisk32(bool atStreamStart, GfxWorld **slot)
{
    db::disk32_load::LoadGfxWorldHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

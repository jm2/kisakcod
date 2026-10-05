#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/22-clipmap.schema
#include <database/db_validation.h>

#include <cstdint>
#include <cstring>
#include <limits>

// ClipMap and ClipMapPvs, a wave-4 server family (docs/design/FASTFILE_LOADER.md).
// The body mirrors Load_clipMap_t: the 284-byte record streams into the temp
// block and its counts are checked, then with block 4 pushed its name, its
// arrays in order and its map entities (MapEnts' step), and with block 1
// pushed its dynamic entities' poses, clients and collision links. Arrays
// that hold no pointer keep their layout: any non-null token streams them
// at their retail alignment, and the planes may also name planes already
// streamed. Static models, brush sides, nodes, leaf-brush nodes,
// partitions, brushes and dynamic-entity defs do not load yet, so a clip
// map that names one fails closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using Disk = disk32::clipMap_tDisk32;

// The records kept in block 4 have one layout at both widths.
RUNTIME_SIZE(cplane_s, 0x14, 0x14);
RUNTIME_SIZE(dmaterial_t, 0x48, 0x48);
RUNTIME_SIZE(cLeaf_t, 0x2C, 0x2C);
RUNTIME_SIZE(CollisionBorder, 0x1C, 0x1C);
RUNTIME_SIZE(CollisionAabbTree, 0x20, 0x20);
RUNTIME_SIZE(cmodel_t, 0x48, 0x48);

// The extents Load_clipMap_t derives from the record's counts.
struct Extents
{
    db::validation::ClipMapBrushLayoutExtents brushes;
    std::int32_t triangleIndices = 0;
    std::int32_t walkableBytes = 0;
    std::int32_t visibilityBytes = 0;
};

bool ExtentsValid(const Disk &disk, Extents *extents)
{
    std::int32_t walkableWords = 0;
    return db::validation::ClipMapBrushLayoutValid(
               !disk.planes.token.isNull(), disk.planeCount, !disk.materials.token.isNull(), disk.numMaterials,
               !disk.brushsides.token.isNull(), disk.numBrushSides, !disk.brushEdges.token.isNull(),
               disk.numBrushEdges, !disk.brushes.token.isNull(), disk.numBrushes, &extents->brushes)
        && db::validation::CheckedCountProduct(3, disk.triCount, &extents->triangleIndices)
        && db::validation::CheckedCountCeilDiv(extents->triangleIndices, 32, &walkableWords)
        && db::validation::CheckedCountProduct(4, walkableWords, &extents->walkableBytes)
        && db::validation::CheckedCountProduct(disk.numClusters, disk.clusterBytes, &extents->visibilityBytes);
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
        return Drop("Invalid fast-file clipmap array count");
    std::uint8_t *const array = DB_AllocStreamPos(static_cast<std::int32_t>(alignment - 1));
    if (!StreamBytes(array, static_cast<std::int32_t>(bytes)))
        return false;
    *out = reinterpret_cast<T *>(array);
    return true;
}

// The planes: -1 streams them here; any other token names planes already
// in block 4.
bool LoadPlanes(const Disk &disk, std::int32_t planeBytes, cplane_s **out)
{
    if (disk.planes.token.isInline())
        return LoadArray(disk.planes.token, disk.planeCount, 20, 4, out);
    std::uintptr_t address = 0;
    if (const db::relocation::Status status = DB_ResolveOffsetBytes(
            disk.planes.token, static_cast<std::uint32_t>(planeBytes), 4,
            db::relocation::BlockBit(kVirtualBlock), &address);
        status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file direct reference for clipmap planes: %s",
                  db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<cplane_s *>(address);
    return true;
}

// The parts that hold pointers, which do not load yet.
bool NotYet(disk32::PointerToken token)
{
    return token.isNull() || Drop("Fast-file clipmap models, brushes, nodes and dynamic entities have no 64-bit loader yet");
}

// Load_clipMap_t's block-4 arrays, in its order, through the brush edges.
bool LoadFirstArrays(const Disk &disk, const Extents &extents, clipMap_t *out)
{
    return LoadPlanes(disk, extents.brushes.planeBytes, &out->planes) && NotYet(disk.staticModelList.token)
        && LoadArray(disk.materials.token, disk.numMaterials, 72, 4, &out->materials)
        && NotYet(disk.brushsides.token) && LoadArray(disk.brushEdges.token, disk.numBrushEdges, 1, 1, &out->brushEdges)
        && NotYet(disk.nodes.token) && LoadArray(disk.leafs.token, disk.numLeafs, 44, 4, &out->leafs)
        && LoadArray(disk.leafbrushes.token, disk.numLeafBrushes, 2, 2, &out->leafbrushes)
        && NotYet(disk.leafbrushNodes.token);
}

// The rest of them, in its order, and the map entities.
bool LoadLastArrays(const Disk &disk, const Extents &extents, clipMap_t *out)
{
    return LoadArray(disk.leafsurfaces.token, disk.numLeafSurfaces, 4, 4, &out->leafsurfaces)
        && LoadArray(disk.verts.token, disk.vertCount, 12, 4, &out->verts)
        && LoadArray(disk.triIndices.token, extents.triangleIndices, 2, 2, &out->triIndices)
        && LoadArray(disk.triEdgeIsWalkable.token, extents.walkableBytes, 1, 1, &out->triEdgeIsWalkable)
        && LoadArray(disk.borders.token, disk.borderCount, 28, 4, &out->borders) && NotYet(disk.partitions.token)
        && LoadArray(disk.aabbTrees.token, disk.aabbTreeCount, 32, 4, &out->aabbTrees)
        && LoadArray(disk.cmodels.token, disk.numSubModels, 72, 4, &out->cmodels) && NotYet(disk.brushes.token)
        && LoadArray(disk.visibility.token, extents.visibilityBytes, 1, 1, &out->visibility);
}

// Load_clipMap_t's tail: each dynamic-entity array of each kind in block 1.
bool LoadDynEntities(const Disk &disk, clipMap_t *out)
{
    if (!NotYet(disk.dynEntDefList[0].token) || !NotYet(disk.dynEntDefList[1].token))
        return false;
    for (int list = 0; list < 6; ++list)
    {
        const int kind = list / 2;
        const int index = list % 2;
        const std::int64_t count = disk.dynEntCount[index];
        DB_PushStreamPos(1);
        const bool loaded =
            kind == 0 ? LoadArray(disk.dynEntPoseList[index].token, count, 32, 4, &out->dynEntPoseList[index])
            : kind == 1 ? LoadArray(disk.dynEntClientList[index].token, count, 12, 4, &out->dynEntClientList[index])
                        : LoadArray(disk.dynEntCollList[index].token, count, 20, 4, &out->dynEntCollList[index]);
        if (!loaded)
            return false;
        DB_PopStreamPos();
    }
    return true;
}
} // namespace

// The record at the temp block's position, then its parts with block 4
// pushed.
bool LoadclipMap_t(clipMap_t *out)
{
    Disk disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    Extents extents;
    if (!ExtentsValid(disk, &extents))
        return Drop("Invalid fast-file clipmap brush layout");
    CopyclipMap_tScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file clipmap has no name"); // the asset pool hashes it
    if (!LoadFirstArrays(disk, extents, out) || !LoadLastArrays(disk, extents, out))
        return false;
    LoadMapEntsPtr(disk.mapEnts.token, &out->mapEnts);
    if (!NotYet(disk.box_brush.token) || !LoadDynEntities(disk, out))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadClipMapPtrDisk32(bool atStreamStart, clipMap_t **slot)
{
    db::disk32_load::LoadclipMap_tHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

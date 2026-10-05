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
// streamed. Static models, brush sides, nodes, leaf-brush nodes, partitions
// and brushes convert into native storage; the planes, brush indices,
// borders and brush edges they name stay in block 4. The box brush is a
// completed object, completed once the brush graph passes Load_clipMap_t's
// rules. Dynamic-entity defs convert too, naming models, effects and
// physics presets through their families' steps, and model pieces, a
// completed object, under DB_ValidateXModelPieces' rules.
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
RUNTIME_SIZE(cLeafBrushNodeChildren_t, 0x0C, 0x0C); // a leaf-brush node's children copy whole

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

// A span an element names: -1 streams it here, any other token names one
// already in block 4; either way it keeps its layout.
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
        Com_Error(ERR_DROP, "Invalid fast-file clipmap pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<T *>(address);
    return true;
}

// An array of records that hold pointers: any non-null token means count
// records follow 4-aligned; each converts into native storage, then loads
// what it names, in order.
template <typename Disk32, typename Native, typename Convert>
bool LoadRecords(disk32::PointerToken token, std::int64_t count, Native **out, Convert convert,
                 std::uint8_t **diskRecords = nullptr, std::uint32_t alignment = 4)
{
    std::uint8_t *records = nullptr;
    *out = nullptr;
    if (!LoadArray(token, count, sizeof(Disk32), alignment, &records))
        return false;
    if (diskRecords)
        *diskRecords = records;
    if (!records)
        return true;
    Native *const native = AllocNative<Native>(static_cast<std::int32_t>(count));
    if (count && !native)
        return false;
    for (std::int64_t index = 0; index < count; ++index)
    {
        Disk32 disk{};
        std::memcpy(&disk, records + index * sizeof(disk), sizeof(disk));
        std::memset(&native[index], 0, sizeof(Native)); // padding too: native storage starts as junk
        if (!convert(disk, &native[index]))
            return false;
    }
    *out = native;
    return true;
}

bool ConvertStaticModel(const disk32::cStaticModel_sDisk32 &disk, cStaticModel_s *out)
{
    CopycStaticModel_sScalars(disk, out);
    LoadXModelPtr(disk.xmodel.token, &out->xmodel);
    return true;
}

// Load_cbrushside_t: the plane is an offset to a plane in block 4.
bool ConvertBrushSide(const disk32::CBrushSideDisk32 &disk, cbrushside_t *out)
{
    CopyCBrushSideScalars(disk, out);
    if (!disk.plane.token.isOffset())
        return Drop("Invalid fast-file clipmap brush-side plane token");
    return LoadOrResolve(disk.plane.token, 20, 4, &out->plane);
}

bool ConvertNode(const disk32::cNode_tDisk32 &disk, cNode_t *out)
{
    CopycNode_tScalars(disk, out);
    return LoadOrResolve(disk.plane.token, 20, 4, &out->plane);
}

// Load_cLeafBrushNodeData_t: a node with brushes names its brush indices;
// one without holds children, which keep their 12 bytes.
bool ConvertLeafBrushNode(const disk32::cLeafBrushNode_sDisk32 &disk, cLeafBrushNode_s *out)
{
    CopycLeafBrushNode_sScalars(disk, out);
    if (disk.leafBrushCount > 0)
        return LoadOrResolve(disk.brushes.token, disk.leafBrushCount * 2u, 2, &out->data.leaf.brushes);
    std::memcpy(&out->data.children, reinterpret_cast<const std::uint8_t *>(&disk) + 8, sizeof(out->data.children));
    return true;
}

// What a brush's offsets name: the brush sides, at their disk and native
// addresses.
struct Sides
{
    const std::uint8_t *disk = nullptr;
    std::uint32_t count = 0;
    cbrushside_t *native = nullptr;
};

// Load_cbrush_t's sides: an offset to whole disk records of the clip map's
// brush sides, which name their native twins.
bool LoadBrushSides(const disk32::cbrush_tDisk32 &disk, const Sides &sides, cbrushside_t **out)
{
    if (!disk.sides.token.isOffset())
        return Drop("Invalid inline fast-file clipmap brush sides");
    const std::uint8_t *named = nullptr;
    if (!LoadOrResolve(disk.sides.token, disk.numsides * 12u, 4, &named))
        return false;
    const std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(named) - reinterpret_cast<std::uintptr_t>(sides.disk);
    if (!sides.disk || named < sides.disk || offset % 12 || offset / 12 + disk.numsides > sides.count)
        return Drop("Invalid fast-file clipmap brush sides");
    *out = sides.native + offset / 12;
    return true;
}

bool LoadAdjacency(const disk32::cbrush_tDisk32 &disk, cbrush_t *out);

// Load_cbrush_t: the side count, the sides, then the adjacency they imply,
// an offset into the brush edges.
bool ConvertBrush(const disk32::cbrush_tDisk32 &disk, cbrush_t *out, const Sides &sides)
{
    Copycbrush_tScalars(disk, out);
    if (disk.numsides > db::validation::kMaxClipMapBrushNonaxialSides
        || (disk.numsides != 0) == disk.sides.token.isNull())
    {
        return Drop("Invalid fast-file clipmap brush side count");
    }
    return (!disk.numsides || LoadBrushSides(disk, sides, &out->sides)) && LoadAdjacency(disk, out);
}

// The adjacency a brush's sides imply: an offset into the brush edges.
bool LoadAdjacency(const disk32::cbrush_tDisk32 &disk, cbrush_t *out)
{
    std::uint32_t adjacency = 0;
    if (!db::validation::ClipMapBrushAdjacencyPrefixExtent(*out, &adjacency))
        return Drop("Invalid fast-file clipmap brush adjacency layout");
    if (adjacency && disk.baseAdjacentSide.token.isNull())
        return Drop("Missing fast-file clipmap brush adjacency");
    if (!disk.baseAdjacentSide.token.isNull() && !disk.baseAdjacentSide.token.isOffset())
        return Drop("Invalid inline fast-file clipmap brush adjacency");
    std::uint32_t validated = 0;
    if (!disk.baseAdjacentSide.token.isNull()
        && !LoadOrResolve(disk.baseAdjacentSide.token, adjacency, 1, &out->baseAdjacentSide))
    {
        return false;
    }
    return (db::validation::ClipMapBrushAdjacencyExtentValid(*out, &validated) && validated == adjacency)
        || Drop("Invalid fast-file clipmap brush adjacency data");
}

bool ConvertPartition(const disk32::CollisionPartitionDisk32 &disk, CollisionPartition *out)
{
    CopyCollisionPartitionScalars(disk, out);
    return LoadOrResolve(disk.borders.token, disk.borderCount * 28u, 4, &out->borders);
}

// Load_clipMap_t's block-4 arrays, in its order, through the brush edges.
bool LoadFirstArrays(const Disk &disk, const Extents &extents, clipMap_t *out, Sides *sides)
{
    std::uint8_t *diskSides = nullptr;
    return LoadPlanes(disk, extents.brushes.planeBytes, &out->planes)
        && LoadRecords<disk32::cStaticModel_sDisk32>(disk.staticModelList.token, disk.numStaticModels,
                                                      &out->staticModelList, ConvertStaticModel)
        && LoadArray(disk.materials.token, disk.numMaterials, 72, 4, &out->materials)
        && LoadRecords<disk32::CBrushSideDisk32>(disk.brushsides.token, disk.numBrushSides, &out->brushsides,
                                                  ConvertBrushSide, &diskSides)
        && ((*sides = {diskSides, disk.numBrushSides, out->brushsides}), true)
        && LoadArray(disk.brushEdges.token, disk.numBrushEdges, 1, 1, &out->brushEdges)
        && LoadRecords<disk32::cNode_tDisk32>(disk.nodes.token, disk.numNodes, &out->nodes, ConvertNode)
        && LoadArray(disk.leafs.token, disk.numLeafs, 44, 4, &out->leafs)
        && LoadArray(disk.leafbrushes.token, disk.numLeafBrushes, 2, 2, &out->leafbrushes)
        && LoadRecords<disk32::cLeafBrushNode_sDisk32>(disk.leafbrushNodes.token, disk.leafbrushNodesCount,
                                                        &out->leafbrushNodes, ConvertLeafBrushNode);
}

// The rest of them, in its order, and the map entities.
bool LoadLastArrays(const Disk &disk, const Extents &extents, clipMap_t *out, const Sides &sides)
{
    const auto brush = [&sides](const disk32::cbrush_tDisk32 &record, cbrush_t *native) {
        return ConvertBrush(record, native, sides);
    };
    return LoadArray(disk.leafsurfaces.token, disk.numLeafSurfaces, 4, 4, &out->leafsurfaces)
        && LoadArray(disk.verts.token, disk.vertCount, 12, 4, &out->verts)
        && LoadArray(disk.triIndices.token, extents.triangleIndices, 2, 2, &out->triIndices)
        && LoadArray(disk.triEdgeIsWalkable.token, extents.walkableBytes, 1, 1, &out->triEdgeIsWalkable)
        && LoadArray(disk.borders.token, disk.borderCount, 28, 4, &out->borders)
        && LoadRecords<disk32::CollisionPartitionDisk32>(disk.partitions.token, disk.partitionCount, &out->partitions,
                                                          ConvertPartition)
        && LoadArray(disk.aabbTrees.token, disk.aabbTreeCount, 32, 4, &out->aabbTrees)
        && LoadArray(disk.cmodels.token, disk.numSubModels, 72, 4, &out->cmodels)
        && LoadRecords<disk32::cbrush_tDisk32>(disk.brushes.token, disk.numBrushes, &out->brushes, brush, nullptr, 16)
        && LoadArray(disk.visibility.token, extents.visibilityBytes, 1, 1, &out->visibility);
}

constexpr std::uint32_t kBrushBytes = sizeof(disk32::cbrush_tDisk32);
constexpr std::uint32_t kPiecesBytes = sizeof(disk32::XModelPiecesDisk32);

// The box brush's completed-object registration, when it streams here.
struct BoxBrush
{
    DBAliasHandle completed{};
    std::uint8_t *record = nullptr;
};

// The box brush: -1 streams it 16-aligned here as a completed object (whose
// completion waits for the graph rules); any other token names an earlier
// one's native twin.
bool LoadBoxBrush(disk32::PointerToken token, const Sides &sides, BoxBrush *box, cbrush_t **out)
{
    *out = nullptr;
    std::uintptr_t native = 0;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        const db::relocation::Status status =
            DB_ResolveCompletedObjectNative(token, DBAliasKind::ClipMapBoxBrush, kBrushBytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
        *out = reinterpret_cast<cbrush_t *>(native);
        return true;
    }
    box->record = DB_AllocStreamPos(15);
    box->completed = box->record ? DB_RegisterPointerSlot(box->record, DBAliasKind::ClipMapBoxBrush) : DBAliasHandle{};
    cbrush_t *const brush = AllocNative<cbrush_t>(1);
    if (!box->completed || !StreamBytes(box->record, kBrushBytes) || !brush)
        return false;
    disk32::cbrush_tDisk32 disk{};
    std::memcpy(&disk, box->record, sizeof(disk));
    std::memset(brush, 0, sizeof(*brush));
    *out = brush;
    return ConvertBrush(disk, brush, sides);
}

// Load_clipMap_t's brush rules on the converted graph: ClipMapBrushGraphValid,
// a box brush that is one, and none of the ordinary brushes.
bool BrushGraphValid(const clipMap_t &map)
{
    std::uint64_t index = 0;
    return (db::validation::ClipMapBrushGraphValid(map) && map.box_brush
            && db::validation::ClipMapBoxBrushValid(*map.box_brush)
            && !db::validation::ExactArrayElementIndex(map.brushes, map.numBrushes, map.box_brush, &index))
        || Drop("Invalid completed fast-file clipmap brush graph");
}

// The box brush, the brush graph's rules, then the box brush's completion.
bool LoadCheckedBoxBrush(const Disk &disk, const Sides &sides, clipMap_t *out)
{
    BoxBrush box;
    return LoadBoxBrush(disk.box_brush.token, sides, &box, &out->box_brush) && BrushGraphValid(*out)
        && (!box.completed
            || DB_CompleteObject(box.completed, DBAliasKind::ClipMapBoxBrush, box.record, kBrushBytes, kBrushBytes,
                                 out->box_brush));
}

bool ConvertPiece(const disk32::XModelPieceDisk32 &disk, XModelPiece *out)
{
    CopyXModelPieceScalars(disk, out);
    LoadXModelPtr(disk.model.token, &out->model);
    return true;
}

// DB_ValidateXModelPieces on the converted pieces: a nonempty name, and each
// piece a model at a finite offset.
bool PiecesValid(const XModelPieces &pieces)
{
    if (!pieces.name || !*pieces.name)
        return Drop("Invalid completed fast-file model-pieces identity");
    for (std::int32_t index = 0; index < pieces.numpieces; ++index)
    {
        if (!db::validation::XModelPieceRuntimeValid(pieces.pieces[index].model != nullptr, pieces.pieces[index].offset))
            return Drop("Invalid completed fast-file model piece");
    }
    return true;
}

// An earlier completed object's native twin, by its offset token.
template <typename T>
bool ResolveCompleted(disk32::PointerToken token, DBAliasKind kind, std::uint32_t bytes, T **out)
{
    std::uintptr_t native = 0;
    const db::relocation::Status status = DB_ResolveCompletedObjectNative(token, kind, bytes, &native);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<T *>(native);
    return true;
}

// DB_ValidateXModelPiecesHeader: a name, and a token exactly when there are
// pieces, at most 65535.
bool PiecesHeaderValid(const disk32::XModelPiecesDisk32 &disk)
{
    std::int32_t pieceBytes = 0;
    return db::validation::XModelPiecesLayoutValid(!disk.name.token.isNull(), !disk.pieces.token.isNull(),
                                                   disk.numpieces, &pieceBytes)
        || Drop("Invalid fast-file model-pieces header");
}

// Load_XModelPieces past its header: the name, the pieces, then
// DB_ValidateXModelPieces' rules.
bool LoadPiecesBody(const disk32::XModelPiecesDisk32 &disk, XModelPieces *pieces)
{
    std::memset(pieces, 0, sizeof(*pieces));
    CopyXModelPiecesScalars(disk, pieces);
    return LoadXString(disk.name, &pieces->name)
        && LoadRecords<disk32::XModelPieceDisk32>(disk.pieces.token, disk.numpieces, &pieces->pieces, ConvertPiece)
        && PiecesValid(*pieces);
}

// Load_XModelPiecesPtr: -1 streams the record 4-aligned here as a completed
// object, then its name and pieces; any other token names an earlier one's
// native twin.
bool LoadPieces(disk32::PointerToken token, XModelPieces **out)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    if (!token.isInline())
        return ResolveCompleted(token, DBAliasKind::XModelPieces, kPiecesBytes, out);
    std::uint8_t *const record = DB_AllocStreamPos(3);
    const DBAliasHandle completed = record ? DB_RegisterPointerSlot(record, DBAliasKind::XModelPieces) : DBAliasHandle{};
    disk32::XModelPiecesDisk32 disk{};
    if (!completed || !StreamBytes(record, kPiecesBytes))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    XModelPieces *const pieces = PiecesHeaderValid(disk) ? AllocNative<XModelPieces>(1) : nullptr;
    *out = pieces;
    return pieces && LoadPiecesBody(disk, pieces)
        && DB_CompleteObject(completed, DBAliasKind::XModelPieces, record, kPiecesBytes, kPiecesBytes, pieces);
}

// Load_DynEntityDef: its model, destroy effect, destroy pieces and physics
// preset, in that order.
bool ConvertDynEntity(const disk32::DynEntityDefDisk32 &disk, DynEntityDef *out)
{
    CopyDynEntityDefScalars(disk, out);
    LoadXModelPtr(disk.xModel.token, &out->xModel);
    LoadFxEffectDefPtr(disk.destroyFx.token, &out->destroyFx);
    if (!LoadPieces(disk.destroyPieces.token, &out->destroyPieces))
        return false;
    LoadPhysPresetPtr(disk.physPreset.token, &out->physPreset);
    return true;
}

// Load_clipMap_t's tail: both dynamic-entity def lists in block 4, then each
// dynamic-entity array of each kind in block 1.
bool LoadDynEntities(const Disk &disk, clipMap_t *out)
{
    for (int index = 0; index < 2; ++index)
    {
        if (!LoadRecords<disk32::DynEntityDefDisk32>(disk.dynEntDefList[index].token, disk.dynEntCount[index],
                                                     &out->dynEntDefList[index], ConvertDynEntity))
        {
            return false;
        }
    }
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
    Sides sides;
    if (!LoadFirstArrays(disk, extents, out, &sides) || !LoadLastArrays(disk, extents, out, sides))
        return false;
    LoadMapEntsPtr(disk.mapEnts.token, &out->mapEnts);
    if (!LoadCheckedBoxBrush(disk, sides, out) || !LoadDynEntities(disk, out))
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

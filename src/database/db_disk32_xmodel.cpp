#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/19-xmodel.schema
#include <database/db_validation.h>
#include <database/db_xmodel_validation.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

// XModel, a wave-4 server family (docs/design/FASTFILE_LOADER.md). The body
// mirrors Load_XModel: the 220-byte record streams into the temp block and its
// collision and bone counts are checked, then with block 4 pushed its name,
// bone arrays, surfaces, material handles, bone info and physics preset, and
// DB_ValidateXModelGraph checks the result. Arrays that hold no pointer keep
// their layout: -1 streams them at their retail alignment (vertices in block
// 7, indices in block 8) and any other token names bytes already there; bone
// names become interned script-string ids. Surfaces and material handles
// convert into native storage; materials and the preset load through their
// families' pointer steps. Rigid-vertex lists, collision surfaces and physics
// geometry are not converted yet and fail closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
// The extents of a model's bone arrays, from its counts.
struct BoneSpans
{
    std::uint32_t nonRootBones;
    std::uint32_t names;
    std::uint32_t parents;
    std::uint32_t quats;
    std::uint32_t trans;
    std::uint32_t classes;
    std::uint32_t baseMats;
};

BoneSpans SpansOf(const disk32::XModelDisk32 &disk)
{
    const std::uint32_t nonRoot = disk.numBones - disk.numRootBones;
    return {nonRoot,          disk.numBones * 2u, nonRoot,
            nonRoot * 4u * 2u, nonRoot * 4u * 4u, disk.numBones,
            disk.numBones * static_cast<std::uint32_t>(sizeof(DObjAnimMat))};
}

// A layout-invariant array at `token`: `bytes` streamed `alignment`-aligned
// in block 4, or an offset naming them there.
template <typename T>
bool LoadSpan(disk32::PointerToken token, std::uint32_t bytes, std::uint32_t alignment, T **out,
              std::uint32_t block = kVirtualBlock)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    std::uintptr_t address = 0;
    if (token.isInline())
    {
        std::uint8_t *const span = DB_AllocStreamPos(static_cast<std::int32_t>(alignment - 1));
        if (!span || (bytes && !StreamBytes(span, static_cast<std::int32_t>(bytes))))
            return false;
        address = reinterpret_cast<std::uintptr_t>(span);
    }
    else if (const db::relocation::Status status =
                 DB_ResolveOffsetBytes(token, (std::max)(bytes, std::uint32_t{1}), alignment,
                                       db::relocation::BlockBit(block), &address);
             status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<T *>(address);
    return true;
}

// Load_XModel's bone arrays, in its order; inline names are interned.
bool LoadBones(const disk32::XModelDisk32 &disk, const BoneSpans &spans, XModel *out)
{
    if (!LoadSpan(disk.boneNames.token, spans.names, 2, &out->boneNames))
        return false;
    for (std::uint32_t bone = 0; disk.boneNames.token.isInline() && bone < disk.numBones; ++bone)
        Load_ScriptStringCustom(&out->boneNames[bone]);
    return LoadSpan(disk.parentList.token, spans.parents, 1, &out->parentList)
        && LoadSpan(disk.quats.token, spans.quats, 2, &out->quats)
        && LoadSpan(disk.trans.token, spans.trans, 4, &out->trans)
        && LoadSpan(disk.partClassification.token, spans.classes, 1, &out->partClassification)
        && LoadSpan(disk.baseMat.token, spans.baseMats, 4, &out->baseMat);
}

// A span in another block: Load_XSurface pushes it around the span.
template <typename T>
bool LoadSpanIn(std::uint32_t block, disk32::PointerToken token, std::uint32_t bytes, T **out)
{
    DB_PushStreamPos(block);
    if (!LoadSpan(token, bytes, 16, out, block))
        return false;
    DB_PopStreamPos();
    return true;
}

// DB_GetXSurfaceLayout on the retail surface: its skinning, counts and
// pointer presence before anything streams.
bool RigidLayoutValid(const disk32::XSurfaceDisk32 &disk)
{
    if (disk.deformed)
        return !disk.vertListCount && disk.vertList.token.isNull();
    return disk.vertListCount >= 1 && disk.vertListCount <= 128 && !disk.vertList.token.isNull();
}

bool SurfaceLayoutValid(const disk32::XSurfaceDisk32 &disk, std::uint32_t *blendElements)
{
    const bool deformed = disk.deformed != 0;
    const bool rigidLayout = RigidLayoutValid(disk);
    return disk.deformed <= 1 && disk.vertCount && db::validation::XSurfaceTriangleCountValid(disk.triCount)
        && !disk.verts0.token.isNull() && !disk.triIndices.token.isNull() && rigidLayout
        && db::validation::XSurfaceSkinningLayoutValid(disk.vertInfo.vertCount, !disk.vertInfo.vertsBlend.token.isNull(),
                                                       disk.vertCount, deformed, blendElements);
}

// Load_XSurface on one retail surface: its zone handle, blend weights,
// vertices (block 7) and indices (block 8), then DB_ValidateXSurfaceGraph.
bool LoadSurface(const disk32::XSurfaceDisk32 &disk, std::uint32_t boneCount, XSurface *out)
{
    std::uint32_t blendElements = 0;
    if (!SurfaceLayoutValid(disk, &blendElements))
        return Drop("Invalid fast-file surface pointer/count layout");
    *out = {}; // native storage starts as junk
    CopyXSurfaceScalars(disk, out);
    CopyXSurfaceVertexInfoScalars(disk.vertInfo, &out->vertInfo);
    Load_GetCurrentZoneHandle(&out->zoneHandle);
    if (!LoadSpan(disk.vertInfo.vertsBlend.token, blendElements * 2u, 2, &out->vertInfo.vertsBlend)
        || !LoadSpanIn(7, disk.verts0.token, disk.vertCount * static_cast<std::uint32_t>(sizeof(GfxPackedVertex)),
                       &out->verts0))
    {
        return false;
    }
    if (!disk.vertList.token.isNull())
        return Drop("Fast-file rigid-vertex lists are not converted yet");
    return LoadSpanIn(8, disk.triIndices.token, disk.triCount * 6u, &out->triIndices)
        && db::xmodel_validation::DB_ValidateXSurfaceGraph(out, disk.deformed, boneCount);
}

// Load_XSurfaceArray: any non-null token means the retail surfaces follow,
// 4-aligned in block 4; each converts into native storage.
bool LoadSurfaces(const disk32::XModelDisk32 &disk, XModel *out)
{
    if (disk.surfs.token.isNull())
        return true;
    constexpr auto kSurfaceBytes = sizeof(disk32::XSurfaceDisk32);
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!StreamBytes(record, static_cast<std::int32_t>(disk.numsurfs * kSurfaceBytes)))
        return false;
    out->surfs = AllocNative<XSurface>(disk.numsurfs);
    if (!out->surfs)
        return false;
    for (std::uint32_t index = 0; index < disk.numsurfs; ++index)
    {
        disk32::XSurfaceDisk32 surface{};
        std::memcpy(&surface, record + index * kSurfaceBytes, sizeof(surface));
        if (!LoadSurface(surface, disk.numBones, &out->surfs[index]))
            return false;
    }
    return true;
}

// Load_MaterialHandleArray: any non-null token means numsurfs tokens follow,
// 4-aligned in block 4; each loads through Material's pointer step.
bool LoadMaterials(const disk32::XModelDisk32 &disk, XModel *out)
{
    if (disk.materialHandles.token.isNull())
        return true;
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    if (!StreamBytes(tokens, static_cast<std::int32_t>(disk.numsurfs * sizeof(disk32::PointerToken))))
        return false;
    out->materialHandles = AllocNative<Material *>(disk.numsurfs);
    if (!out->materialHandles)
        return false;
    for (std::uint32_t index = 0; index < disk.numsurfs; ++index)
    {
        disk32::PointerToken token{};
        std::memcpy(&token, tokens + index * sizeof(token), sizeof(token));
        out->materialHandles[index] = nullptr;
        LoadMaterialPtr(token, &out->materialHandles[index]);
    }
    return true;
}

// Load_XModel after the materials: collision surfaces, bone info (any
// non-null token means it follows inline), the preset and physics geometry.
bool LoadTail(const disk32::XModelDisk32 &disk, XModel *out)
{
    if (!disk.collSurfs.token.isNull())
        return Drop("Fast-file model collision surfaces are not converted yet");
    const disk32::PointerToken boneInfo{disk.boneInfo.token.isNull() ? 0u : disk32::kInline};
    if (!LoadSpan(boneInfo, disk.numBones * static_cast<std::uint32_t>(sizeof(XBoneInfo)), 4, &out->boneInfo))
        return false;
    LoadPhysPresetPtr(disk.physPreset.token, &out->physPreset);
    if (!disk.physGeoms.token.isNull())
        return Drop("Fast-file model physics geometry is not converted yet");
    return true;
}

// Load_XModel's checks before anything else streams.
bool CountsValid(const disk32::XModelDisk32 &disk)
{
    return db::validation::CountInRange(disk.numCollSurfs, 0, UINT16_MAX)
        && (disk.numCollSurfs != 0) == !disk.collSurfs.token.isNull() && disk.numRootBones <= disk.numBones;
}
} // namespace

// The record at the temp block's position and its count rules, then its name
// and bone arrays with block 4 pushed.
bool LoadXModel(XModel *out)
{
    disk32::XModelDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (!CountsValid(disk))
        return Drop("Invalid fast-file model collision or bone counts");
    CopyXModelScalars(disk, out);
    for (std::uint32_t lod = 0; lod < 4; ++lod)
        CopyXModelLodInfoScalars(disk.lodInfo[lod], &out->lodInfo[lod]);
    const BoneSpans spans = SpansOf(disk);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name) || !LoadBones(disk, spans, out))
        return false;
    if (!LoadSurfaces(disk, out) || !LoadMaterials(disk, out) || !LoadTail(disk, out))
        return false;
    if (!db::xmodel_validation::DB_ValidateXModelGraph(out, spans.names, spans.parents, spans.quats, spans.trans,
                                                       spans.classes, spans.baseMats))
    {
        return false;
    }
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadXModelPtrDisk32(bool atStreamStart, XModel **slot)
{
    db::disk32_load::LoadXModelHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

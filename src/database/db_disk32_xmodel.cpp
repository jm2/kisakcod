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
// collision and bone counts are checked, then with block 4 pushed its name and
// bone arrays. The bone arrays hold no pointer, so they keep their layout: -1
// streams them into block 4 and any other token names bytes already there;
// bone names become interned script-string ids, as Load_ScriptStringArray
// makes them. Surfaces are not converted yet and fail closed; every model has
// surfaces, so every model does.
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
bool LoadSpan(disk32::PointerToken token, std::uint32_t bytes, std::uint32_t alignment, T **out)
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
                                       db::relocation::BlockBit(kVirtualBlock), &address);
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
    if (!disk.surfs.token.isNull())
        return Drop("Fast-file model surfaces are not converted yet");
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

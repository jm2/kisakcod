#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/18-material.schema
#include <database/db_material_validation.h>
#include <database/db_validation.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

// Material, a wave-3 parse family (docs/design/FASTFILE_LOADER.md). The body
// mirrors Load_Material: the record streams into the temp block and its table
// rules run, then with block 4 pushed its name, its technique set (through
// TechniqueSet's pointer step), its texture table, its constants and its state
// bits, and DB_ValidateMaterialSemantics checks the result. Constants and
// state bits keep their layout: -1 streams them into block 4 (constants
// 16-aligned) and any other token names bytes already there; a present but
// empty table only aligns or checks its token, then becomes null. Texture
// tables are not converted yet and fail closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using namespace db::material_validation;

constexpr std::uint32_t kConstantBytes = 32;
constexpr std::uint32_t kStateBitsBytes = 8;

// A layout-invariant table at `token`: `count` elements of `stride` bytes
// streamed `alignment`-aligned in block 4, or an offset naming them there.
template <typename T>
bool LoadTable(disk32::PointerToken token, std::uint32_t count, std::uint32_t stride, std::uint32_t alignment,
               T **out)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    std::uintptr_t address = 0;
    if (token.isInline())
    {
        std::uint8_t *const table = DB_AllocStreamPos(static_cast<std::int32_t>(alignment - 1));
        if (!table || (count && !StreamBytes(table, static_cast<std::int32_t>(count * stride))))
            return false;
        address = reinterpret_cast<std::uintptr_t>(table);
    }
    else if (const db::relocation::Status status = DB_ResolveOffsetBytes(
                 token, (std::max)(count * stride, std::uint32_t{1}), alignment,
                 db::relocation::BlockBit(kVirtualBlock), &address);
             status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    // A present but empty table has no consumer; it only aligns or checks.
    *out = count ? reinterpret_cast<T *>(address) : nullptr;
    return true;
}

// Load_Material's rules on the retail record before anything else streams.
bool TablesValid(const disk32::MaterialDisk32 &disk)
{
    std::uint32_t textureBytes = 0;
    return db::validation::PointerCountConsistent(!disk.textureTable.token.isNull(), disk.textureCount)
        && db::validation::PointerCountConsistent(!disk.constantTable.token.isNull(), disk.constantCount)
        && db::validation::PointerCountConsistent(!disk.stateBitsTable.token.isNull(), disk.stateBitsCount)
        && (!disk.textureCount || db::validation::MaterialTextureTableDiskBytes(disk.textureCount, &textureBytes));
}
} // namespace

// The record at the temp block's position, then its name, technique set and
// tables with block 4 pushed.
bool LoadMaterial(Material *out)
{
    disk32::MaterialDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (!TablesValid(disk))
        return Drop("Invalid fast-file material tables");
    CopyMaterialScalars(disk, out);
    CopyMaterialInfoScalars(disk.info, &out->info);
    std::memcpy(&out->info.drawSurf, &disk.info.drawSurf, sizeof(out->info.drawSurf));
    out->techniqueSet = nullptr;
    out->textureTable = nullptr;
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.info.name, &out->info.name))
        return false;
    LoadMaterialTechniqueSetPtr(disk.techniqueSet.token, &out->techniqueSet);
    if (!disk.textureTable.token.isNull())
        return Drop("Fast-file material texture tables are not converted yet");
    if (!LoadTable(disk.constantTable.token, disk.constantCount, kConstantBytes, 16, &out->constantTable)
        || !LoadTable(disk.stateBitsTable.token, disk.stateBitsCount, kStateBitsBytes, 4, &out->stateBitsTable)
        || !DB_ValidateMaterialSemantics(out))
    {
        return false;
    }
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadMaterialPtrDisk32(bool atStreamStart, Material **slot)
{
    db::disk32_load::LoadMaterialHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

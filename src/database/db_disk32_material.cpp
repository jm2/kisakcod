#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/18-material.schema
#include <database/db_material_validation.h>
#include <database/db_validation.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

// Material, a wave-3 parse family (docs/design/FASTFILE_LOADER.md). The body
// mirrors Load_Material as the headless server runs it: the record streams
// into the temp block and its table rules run, then with block 4 pushed its
// name, its technique set (through TechniqueSet's pointer step), its texture
// table, its constants and its state bits, and DB_ValidateMaterialSemantics
// checks the result. The texture table and each water are completed objects
// converted into native storage; images load through Image's pointer step,
// and a water's samples stay in block 4. Constants and state bits keep their
// layout: -1 streams them into block 4 (constants 16-aligned) and any other
// token names bytes already there; a present but empty table only aligns or
// checks its token, then becomes null.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using namespace db::material_validation;

constexpr std::uint32_t kConstantBytes = 32;
constexpr std::uint32_t kStateBitsBytes = 8;

// A completed object at `token`: null, an earlier object's native twin, or
// (-1) a record streamed 4-aligned here that `convert` turns into native
// storage.
template <typename Native, typename Convert>
bool LoadCompleted(disk32::PointerToken token, DBAliasKind kind, std::uint32_t bytes, Native **out, Convert convert)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        std::uintptr_t native = 0;
        const db::relocation::Status status = DB_ResolveCompletedObjectNative(token, kind, bytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
        *out = reinterpret_cast<Native *>(native);
        return true;
    }
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!record)
        return false;
    const DBAliasHandle completed = DB_RegisterPointerSlot(record, kind);
    Native *object = nullptr;
    if (!completed || !convert(record, &object) || !DB_CompleteObject(completed, kind, record, bytes, bytes, object))
        return false;
    *out = object;
    return true;
}

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

// IMG_CATEGORY_WATER and TS_WATER_MAP, from gfx_d3d/r_image.h, which no
// headless TU includes.
constexpr std::uint8_t kWaterCategory = 5;
constexpr std::uint8_t kWaterSemantic = 11;

// DB_ValidateHeadlessWaterContract: the image a water samples.
bool WaterImageValid(const water_t &water)
{
    const GfxImage *const image = water.image;
    return image && image->mapType == MAPTYPE_2D && image->semantic == kWaterSemantic
        && image->category == kWaterCategory && image->depth == 1 && image->width == water.M
        && image->height == water.N;
}

// The amplitudes and frequencies, 4-aligned in block 4, and their checks.
bool LoadWaterSamples(water_t *water, std::int32_t sampleCount)
{
    const auto count = static_cast<std::uint32_t>(sampleCount);
    std::uint8_t *const amplitudes = DB_AllocStreamPos(3);
    if (!StreamBytes(amplitudes, static_cast<std::int32_t>(count * sizeof(complex_s))))
        return false;
    std::uint8_t *const frequencies = DB_AllocStreamPos(3);
    if (!StreamBytes(frequencies, static_cast<std::int32_t>(count * sizeof(float))))
        return false;
    water->H0 = reinterpret_cast<complex_s *>(amplitudes);
    water->wTerm = reinterpret_cast<float *>(frequencies);
    if (!db::validation::FiniteComplexArray(water->H0, count)
        || !db::validation::FiniteNonnegativeFloatArray(water->wTerm, count))
    {
        return Drop("Invalid fast-file material water frequency data");
    }
    return true;
}

// Load_water_t, as headless: the header rule (which tests only that the three
// pointers are present), the samples, then the image and its contract.
bool ConvertWater(std::uint8_t *record, water_t **out)
{
    disk32::water_tDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    water_t *const water = AllocNative<water_t>(1);
    if (!water)
        return false;
    Copywater_tScalars(disk, water);
    water->writable.floatTime = kWaterInitialTime;
    const auto present = [&](disk32::Ptr32<void> field) { return field.token.isNull() ? nullptr : record; };
    water->H0 = reinterpret_cast<complex_s *>(present(disk.H0));
    water->wTerm = reinterpret_cast<float *>(present(disk.wTerm));
    water->image = reinterpret_cast<GfxImage *>(present(disk.image));
    std::int32_t sampleCount = 0;
    if (!DB_ValidateWaterHeader(water, &sampleCount) || !LoadWaterSamples(water, sampleCount))
        return false;
    water->image = nullptr;
    LoadGfxImagePtr(disk.image.token, &water->image);
    if (!WaterImageValid(*water))
        return Drop("Invalid headless material water image contract");
    *out = water;
    return true;
}

// Load_MaterialTextureDef: the header rule, then a water (a completed object)
// or an image, which must load.
bool LoadTexture(const disk32::MaterialTextureDefDisk32 &disk, MaterialTextureDef *out)
{
    CopyMaterialTextureDefScalars(disk, out);
    out->u.image = nullptr;
    const disk32::PointerToken token = disk.image.token;
    if (!db::validation::MaterialTextureHeaderValid(disk.samplerState, disk.semantic, !token.isNull(),
                                                    disk.nameStart, disk.nameEnd))
    {
        return Drop("Invalid fast-file material texture header");
    }
    if (disk.semantic == kWaterSemantic)
        return LoadCompleted(token, DBAliasKind::MaterialWater, disk32::kMaterialWaterBytes, &out->u.water, ConvertWater);
    LoadGfxImagePtr(token, &out->u.image);
    return out->u.image || Drop("Fast-file material texture has no image");
}

// Load_MaterialTextureDefArray: every retail texture streams, then each
// converts, in strictly increasing name-hash order.
bool ConvertTextures(std::uint8_t *record, std::uint32_t count, MaterialTextureDef **out)
{
    constexpr auto kTextureBytes = sizeof(disk32::MaterialTextureDefDisk32);
    if (!StreamBytes(record, static_cast<std::int32_t>(count * kTextureBytes)))
        return false;
    MaterialTextureDef *const textures = AllocNative<MaterialTextureDef>(static_cast<std::int32_t>(count));
    if (!textures)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        disk32::MaterialTextureDefDisk32 texture{};
        std::memcpy(&texture, record + index * kTextureBytes, sizeof(texture));
        if (!LoadTexture(texture, &textures[index]))
            return false;
        if (index && textures[index - 1].nameHash >= textures[index].nameHash)
            return Drop("Unordered fast-file material texture table");
    }
    *out = textures;
    return true;
}

// The texture table: a completed object, or a present but empty table that
// only aligns or checks its token, then becomes null.
bool LoadTextureTable(disk32::PointerToken token, std::uint32_t count, MaterialTextureDef **out)
{
    std::uint32_t bytes = 0;
    if (!count)
        return LoadTable(token, 0, 1, 4, out);
    db::validation::MaterialTextureTableDiskBytes(count, &bytes); // checked by TablesValid
    return LoadCompleted(token, DBAliasKind::MaterialTextureTable, bytes, out,
                         [count](std::uint8_t *record, MaterialTextureDef **textures) {
                             return ConvertTextures(record, count, textures);
                         });
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
    if (!LoadTextureTable(disk.textureTable.token, disk.textureCount, &out->textureTable)
        || !LoadTable(disk.constantTable.token, disk.constantCount, kConstantBytes, 16, &out->constantTable)
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

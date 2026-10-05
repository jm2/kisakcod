#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/23-gfxworld.schema
#include <database/db_validation.h>

#include <cstdint>
#include <cstring>
#include <limits>

// GfxWorld, a wave-4 family (docs/design/FASTFILE_LOADER.md): the renderer's
// world, which a headless server only parses past. The body mirrors
// Load_GfxWorld: the 732-byte record streams into the temp block and the
// counts its runtime trusts are checked, then with block 4 pushed its names,
// its indices and sky surfaces (which keep their layout in block 4) and its
// sky image through Image's step. The parts after the sky do not load yet,
// so a world that names one fails closed.
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
bool LookupArraysValid(const Disk &disk, Extents *extents)
{
    const bool probes = disk.reflectionProbeCount != 0;
    const auto probeCount = static_cast<std::int32_t>(disk.reflectionProbeCount);
    std::int32_t textureBytes = 0;
    return db::validation::GfxReflectionProbeCountValid(disk.reflectionProbeCount) && disk.cullGroupCount >= 0
        && probes == !disk.reflectionProbes.token.isNull() && probes == !disk.reflectionProbeTextures.token.isNull()
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

// The parts after the sky, which do not load yet.
bool NotYet(disk32::PointerToken token)
{
    return token.isNull() || Drop("Fast-file world lights, probes and cells have no 64-bit loader yet");
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
    CopyGfxWorldScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
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
    if (!NotYet(disk.sunLight.token) || !NotYet(disk.reflectionProbes.token))
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

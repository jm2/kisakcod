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
// its indices and sky surfaces (which keep their layout in block 4), its sky
// image through Image's step, its sun light (a completed object), its
// reflection probes and their runtime textures, and its DPVS planes. The
// cells and later parts do not load yet, so a world that names one fails
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

// The parts after the DPVS planes, which do not load yet.
bool NotYet(disk32::PointerToken token)
{
    return token.isNull() || Drop("Fast-file world cells and later parts have no 64-bit loader yet");
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
    if (!LoadSky(disk, out) || !LoadLights(disk, out) || !NotYet(disk.cells.token))
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

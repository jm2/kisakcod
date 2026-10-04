#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/17-techniqueset.schema
#include <database/db_material_validation.h>
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// TechniqueSet, a wave-3 parse family (docs/design/FASTFILE_LOADER.md). The
// body mirrors Load_MaterialTechniqueSet, Load_MaterialTechnique and
// Load_MaterialPass: the set streams into the temp block, then with block 4
// pushed its name and each technique, a completed object streamed 4-aligned
// in block 4 and converted into native storage with its passes. Offset tokens
// naming a technique, and the passes' vertex declarations, shaders and
// arguments, are not converted yet and fail closed; so does every technique.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using namespace db::material_validation;

constexpr std::uint32_t kTechniqueCount = 34;
constexpr auto kPassBytes = sizeof(disk32::MaterialPassDisk32);
constexpr auto kTechniqueHeaderBytes = offsetof(disk32::MaterialTechniqueDisk32, passArray);

// A completed object at `token`: null, or (-1) a record streamed here that
// `convert` turns into native storage, reporting its extent in `bytes`.
template <typename Native, typename Convert>
bool LoadCompleted(disk32::PointerToken token, DBAliasKind kind, std::uint32_t metadata, Native **out, Convert convert)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    if (!token.isInline())
        return Drop("Fast-file technique objects named by offset have no 64-bit loader yet");
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!record)
        return false;
    const DBAliasHandle completed = DB_RegisterPointerSlot(record, kind);
    Native *object = nullptr;
    std::uint32_t bytes = 0;
    if (!completed || !convert(record, &object, &bytes)
        || !DB_CompleteObject(completed, kind, record, metadata, bytes, object))
    {
        return false;
    }
    *out = object;
    return true;
}

// Load_MaterialPass on one retail pass: its header rule, then its objects.
bool LoadPass(const disk32::MaterialPassDisk32 &disk, MaterialPass *out)
{
    CopyMaterialPassScalars(disk, out);
    if (disk.vertexDecl.token.isNull() || disk.vertexShader.token.isNull() || disk.pixelShader.token.isNull()
        || !db::validation::MaterialPassLayoutValid(out->perPrimArgCount, out->perObjArgCount, out->stableArgCount,
                                                    !disk.args.token.isNull(), out->customSamplerFlags))
    {
        return Drop("Invalid fast-file material pass header");
    }
    return Drop("Fast-file material vertex declarations are not converted yet");
}

// Load_MaterialPassArray: the passes already streamed after the header.
bool LoadPasses(const std::uint8_t *passes, MaterialTechnique *technique)
{
    for (std::uint32_t index = 0; index < technique->passCount; ++index)
    {
        disk32::MaterialPassDisk32 pass{};
        std::memcpy(&pass, passes + index * kPassBytes, sizeof(pass));
        if (!LoadPass(pass, &technique->passArray[index]))
            return false;
    }
    return true;
}

// Load_MaterialTechnique's header rule: a name, 1 to 4 passes, known flags.
bool TechniqueHeaderValid(const disk32::MaterialTechniqueDisk32 &disk, std::uint32_t *bytes)
{
    return !disk.name.token.isNull() && db::validation::MaterialTechniqueDiskBytes(disk.passCount, bytes)
        && !(disk.flags & ~UINT16_C(0x803F));
}

// Load_MaterialTechnique: the header and its rules, the passes that follow
// it, then the name.
bool ConvertTechnique(std::uint8_t *record, MaterialTechnique **out, std::uint32_t *bytes)
{
    disk32::MaterialTechniqueDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(kTechniqueHeaderBytes)))
        return false;
    std::memcpy(&disk, record, kTechniqueHeaderBytes); // Flawfinder: ignore -- the header only; the passes follow.
    if (!TechniqueHeaderValid(disk, bytes))
        return Drop("Invalid fast-file material technique header");
    if (!StreamBytes(record + kTechniqueHeaderBytes, static_cast<std::int32_t>(*bytes - kTechniqueHeaderBytes)))
        return false;
    const std::size_t nativeBytes = offsetof(MaterialTechnique, passArray) + disk.passCount * sizeof(MaterialPass);
    auto *const technique =
        reinterpret_cast<MaterialTechnique *>(DB_AllocZoneNative(nativeBytes, alignof(MaterialTechnique)));
    if (!technique)
        return Drop("Fast-file native storage is exhausted");
    technique->flags = static_cast<std::uint16_t>(disk.flags & 0x3F);
    technique->passCount = disk.passCount;
    if (!LoadPasses(record + kTechniqueHeaderBytes, technique) || !LoadXString(disk.name, &technique->name))
        return false;
    if (!technique->name || !*technique->name)
        return Drop("Fast-file material technique has no name");
    *out = technique;
    return true;
}
} // namespace

// The set at the temp block's position and Load_MaterialTechniqueSet's header
// rule, then its name and its techniques with block 4 pushed.
bool LoadMaterialTechniqueSet(MaterialTechniqueSet *out)
{
    disk32::MaterialTechniqueSetDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (disk.name.token.isNull() || !db::validation::CountInRange(disk.worldVertFormat, 0, 11))
        return Drop("Invalid fast-file material technique set header");
    CopyMaterialTechniqueSetScalars(disk, out);
    out->hasBeenUploaded = false;
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    for (std::uint32_t index = 0; index < kTechniqueCount; ++index)
    {
        if (!LoadCompleted(disk.techniques[index].token, DBAliasKind::MaterialTechnique,
                           disk32::kMaterialTechniqueSchema, &out->techniques[index], ConvertTechnique))
        {
            return false;
        }
    }
    DB_PopStreamPos();
    if (!out->name || !*out->name)
        return Drop("Fast-file material technique set has no name");
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadMaterialTechniqueSetPtrDisk32(bool atStreamStart, MaterialTechniqueSet **slot)
{
    db::disk32_load::LoadMaterialTechniqueSetHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/17-techniqueset.schema
#include <database/db_disk32_renderer_hooks.h>
#include <database/db_material_validation.h>
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// TechniqueSet, a wave-3 parse family (docs/design/FASTFILE_LOADER.md). The
// body mirrors Load_MaterialTechniqueSet, Load_MaterialTechnique,
// Load_MaterialPass, Load_MaterialVertexDeclaration and the shader loads as
// the x86 loader runs them: the set streams into the temp block, then with
// block 4 pushed its name and each technique. A technique, a vertex
// declaration and a shader are completed objects streamed 4-aligned in block 4
// and converted into native storage. A client then builds the declaration and
// creates the shaders as db_load.cpp does (db_disk32_renderer_hooks.h); a
// headless server keeps their handles null. A pass's arguments
// convert into native storage, a literal's floats staying in block 4. An
// offset token names an earlier completed object and resolves to its native
// twin, as DB_ConvertOffsetToAlias does on x86.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using namespace db::material_validation;

constexpr std::uint32_t kTechniqueCount = 34;
constexpr auto kPassBytes = sizeof(disk32::MaterialPassDisk32);
constexpr auto kTechniqueHeaderBytes = offsetof(disk32::MaterialTechniqueDisk32, passArray);

// A completed object at `token`: null, an earlier object's native twin, or
// (-1) a record streamed here that `convert` turns into native storage,
// reporting its extent in `bytes`.
template <typename Native, typename Convert>
bool LoadCompleted(disk32::PointerToken token, DBAliasKind kind, std::uint32_t metadata, Native **out, Convert convert)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        std::uintptr_t native = 0;
        const db::relocation::Status status = DB_ResolveCompletedObjectNative(token, kind, metadata, &native);
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
    std::uint32_t bytes = 0;
    if (!completed || !convert(record, &object, &bytes)
        || !DB_CompleteObject(completed, kind, record, metadata, bytes, object))
    {
        return false;
    }
    *out = object;
    return true;
}

// Load_MaterialVertexDeclaration: the routing table converts, then a client
// builds the runtime handles; a headless server leaves them null.
bool ConvertVertexDecl(std::uint8_t *record, MaterialVertexDeclaration **out, std::uint32_t *bytes)
{
    disk32::MaterialVertexDeclarationDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    MaterialVertexDeclaration *const decl = AllocNative<MaterialVertexDeclaration>(1);
    if (!decl)
        return false;
    *decl = {};
    decl->streamCount = disk.streamCount;
    std::memcpy(decl->routing.data, disk.routing.data, sizeof(decl->routing.data));
    if (!DB_ValidateMaterialVertexDeclaration(decl))
        return false;
    decl->isLoaded = true;
    for (std::uint32_t index = 0; index < decl->streamCount; ++index)
        decl->hasOptionalSource = decl->hasOptionalSource || decl->routing.data[index].source >= 5;
    BuildVertexDecl(decl);
    *out = decl;
    *bytes = sizeof(disk);
    return true;
}

// Load_GfxVertexShaderLoadDef or its pixel twin: the load definition's rule,
// then the program DWORDs 4-aligned in block 4 and their bytecode check.
template <typename Disk, typename LoadDef>
bool LoadProgram(const Disk &disk, LoadDef *out, db::validation::D3D9ShaderStage stage, const char *description)
{
    out->programSize = disk.programSize;
    out->loadForRenderer = disk.loadForRenderer;
    // The rule checks only whether a program token is present.
    if (!DB_ValidateMaterialShaderLoadDef(disk.program.token.isNull() ? nullptr : &disk, disk.programSize,
                                          disk.loadForRenderer, description))
    {
        return false;
    }
    std::uint8_t *const program = DB_AllocStreamPos(3);
    if (!StreamBytes(program, static_cast<std::int32_t>(disk.programSize * sizeof(std::uint32_t))))
        return false;
    out->program = program;
    return DB_ValidateMaterialShaderProgram(program, disk.programSize, stage, disk.loadForRenderer, description);
}

// A shader's name rules: a token before it loads, and text after.
bool LoadShaderName(disk32::Ptr32<const char> token, const char **out, bool vertex)
{
    if (token.token.isNull())
        return Drop(vertex ? "Fast-file vertex shader has no name" : "Fast-file pixel shader has no name");
    if (!LoadXString(token, out))
        return false;
    if (!*out || !**out)
        return Drop("Fast-file shader has no completed name");
    return true;
}

// Load_MaterialVertexShader or Load_MaterialPixelShader: the name rules, then
// the program, then a client creates the runtime handle; a headless server
// leaves it null.
template <typename Native, typename Disk>
bool ConvertShader(std::uint8_t *record, Native **out, std::uint32_t *bytes)
{
    constexpr bool kVertex = std::is_same_v<Native, MaterialVertexShader>;
    const char *const description = kVertex ? "vertex shader" : "pixel shader";
    Disk disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    Native *const shader = AllocNative<Native>(1);
    if (!shader)
        return false;
    *shader = {};
    if (!LoadShaderName(disk.name, &shader->name, kVertex))
        return false;
    if (!LoadProgram(disk, &shader->prog.loadDef,
                     kVertex ? db::validation::D3D9ShaderStage::Vertex : db::validation::D3D9ShaderStage::Pixel,
                     description))
    {
        return false;
    }
    if (!CreateShader(shader))
        return false;
    *out = shader;
    *bytes = sizeof(disk);
    return true;
}

// Load_MaterialArgumentDef: a literal's four floats, inline or by offset into
// block 4 (they keep their layout); any other value fills the union's first
// four bytes, which every non-pointer member shares.
bool LoadArgument(const disk32::MaterialShaderArgumentDisk32 &disk, MaterialShaderArgument *out)
{
    CopyMaterialShaderArgumentScalars(disk, out);
    out->u.literalConst = nullptr;
    const disk32::PointerToken token = disk.literalConst.token;
    if (out->type != 1 && out->type != 7)
    {
        out->u.nameHash = token.value;
        return true;
    }
    if (token.isNull())
        return Drop("Fast-file literal shader constant has no value");
    std::uintptr_t address = 0;
    if (token.isInline())
    {
        std::uint8_t *const floats = DB_AllocStreamPos(3);
        if (!StreamBytes(floats, 4 * static_cast<std::int32_t>(sizeof(float))))
            return false;
        address = reinterpret_cast<std::uintptr_t>(floats);
    }
    else if (const db::relocation::Status status = DB_ResolveOffsetBytes(
                 token, 4 * sizeof(float), alignof(float), db::relocation::BlockBit(kVirtualBlock), &address);
             status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    out->u.literalConst = reinterpret_cast<const float *>(address);
    return true;
}

// Load_MaterialShaderArgumentArray: every retail argument streams 4-aligned in
// block 4, then each converts; DB_ValidateMaterialPassArguments checks them.
bool LoadArguments(MaterialPass *pass)
{
    const auto count = static_cast<std::uint32_t>(pass->perPrimArgCount) + pass->perObjArgCount + pass->stableArgCount;
    constexpr auto kArgumentBytes = sizeof(disk32::MaterialShaderArgumentDisk32);
    std::uint8_t *const arguments = DB_AllocStreamPos(3);
    if (!StreamBytes(arguments, static_cast<std::int32_t>(count * kArgumentBytes)))
        return false;
    pass->args = AllocNative<MaterialShaderArgument>(static_cast<std::int32_t>(count));
    if (!pass->args)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        disk32::MaterialShaderArgumentDisk32 argument{};
        std::memcpy(&argument, arguments + index * kArgumentBytes, sizeof(argument));
        if (!LoadArgument(argument, &pass->args[index]))
            return false;
    }
    return DB_ValidateMaterialPassArguments(pass, count);
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
    out->args = nullptr;
    if (!LoadCompleted(disk.vertexDecl.token, DBAliasKind::MaterialVertexDeclaration,
                       disk32::kMaterialVertexDeclarationBytes, &out->vertexDecl, ConvertVertexDecl)
        || !LoadCompleted(disk.vertexShader.token, DBAliasKind::MaterialVertexShader, disk32::kMaterialVertexShaderBytes,
                          &out->vertexShader, ConvertShader<MaterialVertexShader, disk32::MaterialVertexShaderDisk32>)
        || !LoadCompleted(disk.pixelShader.token, DBAliasKind::MaterialPixelShader, disk32::kMaterialPixelShaderBytes,
                          &out->pixelShader, ConvertShader<MaterialPixelShader, disk32::MaterialPixelShaderDisk32>))
    {
        return false;
    }
    if (out->vertexShader->prog.loadDef.loadForRenderer != out->pixelShader->prog.loadDef.loadForRenderer)
        return Drop("Fast-file material pass mixes renderer shader variants");
    return LoadArguments(out);
}

std::uint32_t Renderer(const MaterialTechnique *technique)
{
    return technique->passArray[0].pixelShader->prog.loadDef.loadForRenderer;
}

// Load_MaterialPassArray: the passes already streamed after the header, each
// on its first pass's renderer variant.
bool LoadPasses(const std::uint8_t *passes, MaterialTechnique *technique)
{
    for (std::uint32_t index = 0; index < technique->passCount; ++index)
    {
        disk32::MaterialPassDisk32 pass{};
        std::memcpy(&pass, passes + index * kPassBytes, sizeof(pass));
        if (!LoadPass(pass, &technique->passArray[index]))
            return false;
        if (Renderer(technique) != technique->passArray[index].pixelShader->prog.loadDef.loadForRenderer)
            return Drop("Fast-file material technique mixes renderer shader variants");
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
// The 34 technique tokens, every technique on the first one's renderer variant.
bool LoadTechniques(const disk32::MaterialTechniqueSetDisk32 &disk, MaterialTechniqueSet *out)
{
    const MaterialTechnique *first = nullptr;
    for (std::uint32_t index = 0; index < kTechniqueCount; ++index)
    {
        MaterialTechnique *&technique = out->techniques[index];
        if (!LoadCompleted(disk.techniques[index].token, DBAliasKind::MaterialTechnique,
                           disk32::kMaterialTechniqueSchema, &technique, ConvertTechnique))
        {
            return false;
        }
        first = first ? first : technique;
        if (technique && Renderer(technique) != Renderer(first))
            return Drop("Fast-file material technique set mixes renderer variants");
    }
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
    if (!LoadTechniques(disk, out))
        return false;
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

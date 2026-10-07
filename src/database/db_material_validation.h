#pragma once

// The Material and TechniqueSet load validators, shared by the 32-bit loader
// (db_load.cpp) and the 64-bit family loaders; moved verbatim from db_load.cpp.
// An including TU must already have the gfx_d3d material and water types:
// db_load.cpp includes them, and a 64-bit family TU gets them through the
// generated disk32 mirrors. (DB_ValidateHeadlessWaterContract stays in
// db_load.cpp: it needs gfx_d3d/r_image.h's enums, which no mirror brings.) This header includes no client header, so
// headless TUs stay off the client-media include tripwire.

#include <database/db_stream.h>
#include <database/db_validation.h>
#include <qcommon/com_error.h>

#include <cstdint>

namespace db::material_validation
{
inline constexpr db::relocation::BlockMask kDirectBlock4 = db::relocation::BlockBit(4);

inline bool DB_ValidateMaterialShaderLoadDef(
    const void *program,
    uint32_t programDwordCount,
    uint32_t loadForRenderer,
    const char *description)
{
    if (!db::validation::MaterialShaderLoadDefValid(
            program != nullptr,
            programDwordCount,
            loadForRenderer))
    {
        Com_Error(ERR_DROP, "Invalid fast-file %s load definition", description);
        return false;
    }
    return true;
}

inline bool DB_ValidateMaterialShaderProgram(
    const void *program,
    uint32_t programDwordCount,
    db::validation::D3D9ShaderStage stage,
    uint32_t loadForRenderer,
    const char *description)
{
    uint32_t programBytes = 0;
    if (!db::validation::CheckedSpanBytes(
            programDwordCount,
            static_cast<uint32_t>(sizeof(uint32_t)),
            &programBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file %s program size", description);
        return false;
    }

    const db::relocation::Status materialized = DB_ValidateStreamAddress(
        program,
        programBytes,
        alignof(uint32_t),
        kDirectBlock4);
    if (materialized != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file %s program span: %s",
            description,
            db::relocation::StatusName(materialized));
        return false;
    }
    if (!db::validation::D3D9ShaderBytecodeValid(
            static_cast<const uint32_t *>(program),
            programDwordCount,
            stage,
            loadForRenderer))
    {
        Com_Error(ERR_DROP, "Invalid fast-file %s bytecode", description);
        return false;
    }
    return true;
}

inline bool DB_ValidateWaterHeader(const water_t *water, int32_t *sampleCount)
{
    if (sampleCount)
        *sampleCount = 0;
    if (!water || !sampleCount
        || !db::validation::WaterGridValid(water->M, water->N)
        || !db::validation::WaterParametersValid(
            water->Lx,
            water->Lz,
            water->gravity,
            water->windvel,
            water->winddir[0],
            water->winddir[1],
            water->amplitude)
        || !db::validation::FiniteFloatArray(water->codeConstant, 4)
        || !water->H0
        || !water->wTerm
        || !water->image)
    {
        Com_Error(ERR_DROP, "Invalid fast-file material water header");
        return false;
    }

    *sampleCount = water->M * water->N;
    return true;
}

inline bool DB_ValidateMaterialNamedInputs(
    const Material *material,
    const MaterialTechniqueSet *techniqueSet)
{
    if (!material || !techniqueSet)
        return false;

    for (uint32_t techniqueIndex = 0; techniqueIndex < 34; ++techniqueIndex)
    {
        const MaterialTechnique *technique =
            techniqueSet->techniques[techniqueIndex];
        if (!technique)
            continue;
        if (!db::validation::CountInRange(technique->passCount, 1, 4))
        {
            Com_Error(ERR_DROP, "Invalid material technique pass count");
            return false;
        }
        for (uint32_t passIndex = 0;
             passIndex < technique->passCount;
             ++passIndex)
        {
            const MaterialPass &pass = technique->passArray[passIndex];
            const uint32_t argumentCount =
                static_cast<uint32_t>(pass.perPrimArgCount)
                + pass.perObjArgCount
                + pass.stableArgCount;
            if (!db::validation::MaterialPassLayoutValid(
                    pass.perPrimArgCount,
                    pass.perObjArgCount,
                    pass.stableArgCount,
                    pass.args != nullptr,
                    pass.customSamplerFlags)
                || argumentCount > 64)
            {
                Com_Error(ERR_DROP, "Invalid material pass argument span");
                return false;
            }
            for (uint32_t argumentIndex = 0;
                 argumentIndex < argumentCount;
                 ++argumentIndex)
            {
                const MaterialShaderArgument &argument =
                    pass.args[argumentIndex];
                if (argument.type == 2)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->textureTable,
                            material->textureCount,
                            argument.u.nameHash))
                    {
                        Com_Error(
                            ERR_DROP,
                            "Material technique requires a missing named texture");
                        return false;
                    }
                }
                else if (argument.type == 0 || argument.type == 6)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->constantTable,
                            material->constantCount,
                            argument.u.nameHash))
                    {
                        Com_Error(
                            ERR_DROP,
                            "Material technique requires a missing named constant");
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

inline bool DB_ValidateMaterialSemantics(const Material *material)
{
    // A stub carries no technique set; whatever it does carry is checked.
    const bool stub = material && db::validation::IsStubAssetName(material->info.name);
    if (!material
        || !material->info.name
        || !*material->info.name
        || material->info.sortKey >= 64
        || (!stub && !material->techniqueSet)
        || (material->techniqueSet && !material->techniqueSet->remappedTechniqueSet)
        || !db::validation::StrictlyIncreasingNameHashes(
            material->textureTable,
            material->textureCount)
        || !db::validation::StrictlyIncreasingNameHashes(
            material->constantTable,
            material->constantCount)
        || material->stateBitsCount > 136)
    {
        Com_Error(ERR_DROP, "Invalid fast-file material semantics");
        return false;
    }

    for (uint32_t constantIndex = 0;
         constantIndex < material->constantCount;
         ++constantIndex)
    {
        if (!db::validation::FiniteFloatArray(
                material->constantTable[constantIndex].literal,
                4))
        {
            Com_Error(ERR_DROP, "Non-finite fast-file material constant");
            return false;
        }
    }
    for (uint32_t stateIndex = 0;
         stateIndex < material->stateBitsCount;
         ++stateIndex)
    {
        if (!db::validation::MaterialStateBitsDecodeSafe(
                material->stateBitsTable[stateIndex].loadBits[0]))
        {
            Com_Error(ERR_DROP, "Unsafe fast-file material render state");
            return false;
        }
    }

    if (!material->techniqueSet)
        return true; // a stub
    const MaterialTechniqueSet *original = material->techniqueSet;
    const MaterialTechniqueSet *candidate = original->remappedTechniqueSet;
    if (candidate->worldVertFormat != original->worldVertFormat)
    {
        Com_Error(ERR_DROP, "Material technique remap changes vertex format");
        return false;
    }
    for (uint32_t techniqueIndex = 0; techniqueIndex < 34; ++techniqueIndex)
    {
        const MaterialTechnique *originalTechnique =
            original->techniques[techniqueIndex];
        const MaterialTechnique *candidateTechnique =
            candidate->techniques[techniqueIndex];
        const uint32_t originalPassCount = originalTechnique
            ? originalTechnique->passCount
            : 0;
        const uint32_t candidatePassCount = candidateTechnique
            ? candidateTechnique->passCount
            : 0;
        if (!db::validation::MaterialTechniqueStateSpanValid(
                originalTechnique != nullptr,
                originalPassCount,
                material->stateBitsEntry[techniqueIndex],
                material->stateBitsCount)
            || !db::validation::MaterialRemapSlotValid(
                originalTechnique != nullptr,
                originalPassCount,
                candidateTechnique != nullptr,
                candidatePassCount))
        {
            Com_Error(ERR_DROP, "Invalid fast-file material technique state mapping");
            return false;
        }
    }

    if (!DB_ValidateMaterialNamedInputs(material, original)
        || (candidate != original
            && !DB_ValidateMaterialNamedInputs(material, candidate)))
    {
        return false;
    }
    return true;
}

inline bool DB_ValidateMaterialVertexDeclaration(const MaterialVertexDeclaration *declaration)
{
    if (!declaration
        || !db::validation::CountInRange(declaration->streamCount, 1, 12))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material vertex declaration count");
        return false;
    }

    uint16_t destinationMask = 0;
    for (uint32_t index = 0; index < declaration->streamCount; ++index)
    {
        const MaterialStreamRouting &routing = declaration->routing.data[index];
        if (!db::validation::MaterialVertexRoutingValid(routing.source, routing.dest)
            || (destinationMask & (UINT16_C(1) << routing.dest))
            || (index && !db::validation::MaterialVertexRoutingFollows(
                declaration->routing.data[index - 1].source,
                declaration->routing.data[index - 1].dest,
                routing.source,
                routing.dest)))
        {
            Com_Error(ERR_DROP, "Invalid fast-file material vertex declaration routing");
            return false;
        }
        destinationMask = static_cast<uint16_t>(
            destinationMask | (UINT16_C(1) << routing.dest));
    }
    return true;
}

inline bool DB_ValidateMaterialPassArguments(const MaterialPass *pass, uint32_t argumentCount)
{
    const uint32_t perPrimitiveEnd = pass->perPrimArgCount;
    const uint32_t perObjectEnd = perPrimitiveEnd + pass->perObjArgCount;
    uint32_t vertexRegisterMask = 0;
    uint16_t pixelSamplerMask = 0;
    if (pass->customSamplerFlags & 1)
        pixelSamplerMask |= UINT16_C(1) << 1;
    if (pass->customSamplerFlags & 2)
        pixelSamplerMask |= UINT16_C(1) << 2;
    if (pass->customSamplerFlags & 4)
        pixelSamplerMask |= UINT16_C(1) << 3;
    uint32_t pixelConstantMask[8] = {};

    for (uint32_t index = 0; index < argumentCount; ++index)
    {
        const MaterialShaderArgument &argument = pass->args[index];
        db::validation::MaterialArgumentSegment segment;
        uint32_t segmentStart = 0;
        if (index < perPrimitiveEnd)
        {
            segment = db::validation::MaterialArgumentSegment::PerPrimitive;
        }
        else if (index < perObjectEnd)
        {
            segment = db::validation::MaterialArgumentSegment::PerObject;
            segmentStart = perPrimitiveEnd;
        }
        else
        {
            segment = db::validation::MaterialArgumentSegment::Stable;
            segmentStart = perObjectEnd;
        }

        uint32_t sourceIndex = 0;
        if (argument.type == 3 || argument.type == 5)
            sourceIndex = argument.u.codeConst.index;
        else if (argument.type == 4)
            sourceIndex = static_cast<uint32_t>(argument.u.codeSampler);

        if (!db::validation::MaterialArgumentTypeAllowedInSegment(argument.type, segment)
            || !db::validation::MaterialArgumentShapeValid(
                argument.type,
                argument.dest,
                sourceIndex,
                argument.u.codeConst.firstRow,
                argument.u.codeConst.rowCount)
            || (argument.type == 3
                && !db::validation::MaterialCodeConstantAllowedInSegment(
                    sourceIndex,
                    segment))
            || (argument.type == 4
                && !db::validation::MaterialCodeSamplerAllowedInSegment(
                    sourceIndex,
                    segment))
            || ((argument.type == 1 || argument.type == 7)
                && (!argument.u.literalConst
                    || !db::validation::FiniteFloatArray(
                        argument.u.literalConst,
                        4))))
        {
            Com_Error(ERR_DROP, "Invalid fast-file material shader argument");
            return false;
        }

        if (index > segmentStart)
        {
            const MaterialShaderArgument &previous = pass->args[index - 1];
            if (argument.type < previous.type
                || (argument.type == previous.type
                    && ((argument.type == 0 || argument.type == 2 || argument.type == 6)
                        ? argument.u.nameHash < previous.u.nameHash
                        : argument.dest < previous.dest)))
            {
                Com_Error(ERR_DROP, "Unordered fast-file material shader arguments");
                return false;
            }
        }

        if (argument.type == 0 || argument.type == 1 || argument.type == 3)
        {
            const uint32_t registerCount = argument.type == 3
                ? argument.u.codeConst.rowCount
                : 1;
            for (uint32_t row = 0; row < registerCount; ++row)
            {
                const uint32_t registerBit = UINT32_C(1) << (argument.dest + row);
                if (vertexRegisterMask & registerBit)
                {
                    Com_Error(ERR_DROP, "Overlapping fast-file material vertex constants");
                    return false;
                }
                vertexRegisterMask |= registerBit;
            }
        }

        if (argument.type == 2 || argument.type == 4)
        {
            const uint16_t samplerBit = static_cast<uint16_t>(
                UINT16_C(1) << argument.dest);
            if (pixelSamplerMask & samplerBit)
            {
                Com_Error(ERR_DROP, "Overlapping fast-file material pixel samplers");
                return false;
            }
            pixelSamplerMask |= samplerBit;
        }

        if (argument.type == 5 || argument.type == 6 || argument.type == 7)
        {
            const uint32_t word = argument.dest >> 5;
            const uint32_t constantBit = UINT32_C(1) << (argument.dest & 31);
            if (pixelConstantMask[word] & constantBit)
            {
                Com_Error(ERR_DROP, "Overlapping fast-file material pixel constants");
                return false;
            }
            pixelConstantMask[word] |= constantBit;
        }
    }
    return true;
}
} // namespace db::material_validation

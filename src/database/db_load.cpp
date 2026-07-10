#include "database.h"
#include "db_validation.h"

#include <xanim/xanim.h>
#include <xanim/xmodel.h>

#include <sound/snd_local.h>

#include <gfx_d3d/fxprimitives.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_gfx.h>
#include <xanim/dobj.h>
#include <gfx_d3d/r_buffers.h>

#include <DynEntity/DynEntity_client.h>
#include <gfx_d3d/r_water.h>
#include <gfx_d3d/r_image.h>
#include <universal/com_sndalias.h>
#include <gfx_d3d/r_sky.h>
#include <gfx_d3d/r_primarylights.h>
#include <game/g_bsp.h>

#include <cstdlib>
#include <cstring>

namespace
{
constexpr db::relocation::BlockMask kDirectBlock1 = db::relocation::BlockBit(1);
constexpr db::relocation::BlockMask kDirectBlock4 = db::relocation::BlockBit(4);
constexpr db::relocation::BlockMask kDirectBlock7 = db::relocation::BlockBit(7);
constexpr db::relocation::BlockMask kDirectBlock8 = db::relocation::BlockBit(8);

uint32_t DB_CheckedDirectSpanBytes(
    int64_t count,
    uint32_t stride,
    const char *description)
{
    uint32_t result = 0;
    if (count < 0
        || !db::validation::CheckedSpanBytes(
            static_cast<uint64_t>(count),
            stride,
            &result)
        || result > INT32_MAX)
    {
        Com_Error(ERR_DROP, "Invalid fast-file direct span for %s", description);
        return 0;
    }
    return result;
}

bool DB_ValidatePointerCount(
    const void *pointer,
    int64_t count,
    const char *description)
{
    if (!db::validation::PointerCountConsistent(pointer != nullptr, count))
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer/count for %s", description);
        return false;
    }
    return true;
}

template <typename T>
bool DB_ResolveCompletedPointer(
    T **field,
    DBAliasKind kind,
    uint32_t metadata,
    const char *description)
{
    if (!field)
    {
        Com_Error(ERR_DROP, "Missing fast-file completed-object field for %s", description);
        return false;
    }

    uint32_t tokenValue = 0;
    memcpy(&tokenValue, field, sizeof(tokenValue));
    uintptr_t pointer = 0;
    const db::relocation::Status status = DB_ResolveInsertedPointer(
        {tokenValue},
        kind,
        metadata,
        &pointer);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file completed-object reference for %s: %s",
            description,
            db::relocation::StatusName(status));
        return false;
    }
    *field = reinterpret_cast<T *>(pointer);
    return true;
}

template <typename T>
bool DB_ResolveDirectPointer(
    T **field,
    uint64_t requiredBytes,
    size_t alignment,
    db::relocation::BlockMask allowedBlocks,
    const char *description)
{
    if (!field)
    {
        Com_Error(ERR_DROP, "Missing fast-file direct-pointer field for %s", description);
        return false;
    }

    uint32_t tokenValue = 0;
    memcpy(&tokenValue, field, sizeof(tokenValue));
    uintptr_t pointer = 0;
    const db::relocation::Status status = DB_ResolveOffsetBytes(
        {tokenValue},
        requiredBytes ? requiredBytes : 1,
        alignment,
        allowedBlocks,
        &pointer);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file direct reference for %s: %s",
            description,
            db::relocation::StatusName(status));
        return false;
    }
    *field = reinterpret_cast<T *>(pointer);
    return true;
}

bool DB_ValidateMaterializedSpan(
    const void *pointer,
    uint32_t bytes,
    size_t alignment,
    db::relocation::BlockMask allowedBlocks,
    const char *description)
{
    if (!bytes)
        return true;
    const db::relocation::Status status = DB_ValidateStreamAddress(
        pointer,
        bytes,
        alignment,
        allowedBlocks);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid completed fast-file span for %s: %s",
            description,
            db::relocation::StatusName(status));
        return false;
    }
    return true;
}

bool DB_ValidateMaterializedBlock4Span(
    const void *pointer,
    uint32_t bytes,
    size_t alignment,
    const char *description)
{
    return DB_ValidateMaterializedSpan(
        pointer,
        bytes,
        alignment,
        kDirectBlock4,
        description);
}

bool DB_GetClipBrushAdjacencyBytes(
    const cbrush_t *brush,
    uint32_t *adjacencyBytes)
{
    return brush
        && db::validation::ClipMapBrushAdjacencyExtentValid(
            *brush,
            adjacencyBytes);
}

bool DB_ValidateClipMapBoxBrush(const cbrush_t *brush)
{
    return brush && db::validation::ClipMapBoxBrushValid(*brush);
}

bool DB_ValidateXModelPiecesHeader(
    const XModelPieces *pieces,
    int32_t *pieceBytes)
{
    if (!pieces
        || !pieceBytes
        || !db::validation::XModelPiecesLayoutValid(
            pieces->name != nullptr,
            pieces->pieces != nullptr,
            pieces->numpieces,
            pieceBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file model-pieces header");
        return false;
    }
    return true;
}

bool DB_ValidateXModelPieces(const XModelPieces *pieces, uint32_t pieceBytes)
{
    if (!pieces || !pieces->name || !*pieces->name)
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file model-pieces identity");
        return false;
    }
    if (!pieceBytes)
        return true;
    if (!pieces->pieces)
    {
        Com_Error(ERR_DROP, "Missing completed fast-file model-pieces array");
        return false;
    }

    const db::relocation::Status materialized = DB_ValidateStreamAddress(
        pieces->pieces,
        pieceBytes,
        4,
        kDirectBlock4);
    if (materialized != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file model-pieces array: %s",
            db::relocation::StatusName(materialized));
        return false;
    }

    for (int32_t index = 0; index < pieces->numpieces; ++index)
    {
        const XModelPiece &piece = pieces->pieces[index];
        if (!db::validation::XModelPieceRuntimeValid(
                piece.model != nullptr,
                piece.offset))
        {
            Com_Error(ERR_DROP, "Invalid completed fast-file model piece");
            return false;
        }
    }
    return true;
}

bool DB_GetXSurfaceCollisionTreeExtents(
    const XSurfaceCollisionTree *tree,
    uint32_t *nodeBytes,
    uint32_t *leafBytes)
{
    if (nodeBytes)
        *nodeBytes = 0;
    if (leafBytes)
        *leafBytes = 0;
    if (!tree
        || !nodeBytes
        || !leafBytes
        || !tree->nodes
        || !tree->leafs
        || !tree->nodeCount
        || !tree->leafCount
        || tree->nodeCount > db::validation::kMaxXSurfaceCollisionEntries
        || tree->leafCount > db::validation::kMaxXSurfaceCollisionEntries
        || !db::validation::CheckedSpanBytes(
            tree->nodeCount,
            disk32::kXSurfaceCollisionNodeBytes,
            nodeBytes)
        || !db::validation::CheckedSpanBytes(
            tree->leafCount,
            disk32::kXSurfaceCollisionLeafBytes,
            leafBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file surface collision-tree layout");
        return false;
    }
    return true;
}

bool DB_GetXSurfaceLayout(
    const XSurface *surface,
    uint8_t *deformed,
    uint32_t *rigidListBytes)
{
    if (deformed)
        *deformed = 0;
    if (rigidListBytes)
        *rigidListBytes = 0;
    if (!surface || !deformed || !rigidListBytes)
    {
        Com_Error(ERR_DROP, "Invalid fast-file surface layout output");
        return false;
    }

    std::memcpy(deformed, &surface->deformed, sizeof(*deformed));
    constexpr uint32_t maximumRigidLists = 128;
    const bool rigidLayoutValid = *deformed
        ? surface->vertListCount == 0 && !surface->vertList
        : surface->vertListCount >= 1
            && surface->vertListCount <= maximumRigidLists
            && surface->vertList;
    if (*deformed > 1
        || !surface->vertCount
        || !surface->triCount
        || !surface->verts0
        || !surface->triIndices
        || !rigidLayoutValid
        || !db::validation::CheckedSpanBytes(
            surface->vertListCount,
            disk32::kXRigidVertListBytes,
            rigidListBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file surface pointer/count layout");
        return false;
    }
    return true;
}

bool DB_ValidateXSurfaceCollisionTreeGraph(
    const XSurfaceCollisionTree *tree,
    uint32_t nodeBytes,
    uint32_t leafBytes)
{
    if (!tree)
        return false;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(tree->trans[axis])
            || std::isnan(tree->scale[axis])
            || tree->scale[axis] <= 0.0f)
        {
            Com_Error(ERR_DROP, "Invalid fast-file surface collision transform");
            return false;
        }
    }

    const db::relocation::Status nodeSpan = DB_ValidateStreamAddress(
        tree->nodes,
        nodeBytes,
        16,
        kDirectBlock4);
    const db::relocation::Status leafSpan = DB_ValidateStreamAddress(
        tree->leafs,
        leafBytes,
        2,
        kDirectBlock4);
    if (nodeSpan != db::relocation::Status::Ok
        || leafSpan != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file surface collision child span: %s / %s",
            db::relocation::StatusName(nodeSpan),
            db::relocation::StatusName(leafSpan));
        return false;
    }

    const db::validation::XSurfaceCollisionTopologyStatus topology =
        db::validation::ValidateXSurfaceCollisionTopology(
            tree->nodes,
            tree->nodeCount,
            tree->leafs,
            tree->leafCount,
            0,
            UINT16_MAX,
            UINT16_MAX);
    if (topology != db::validation::XSurfaceCollisionTopologyStatus::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file surface collision topology: %s",
            db::validation::XSurfaceCollisionTopologyStatusName(topology));
        return false;
    }
    return true;
}

bool DB_ValidateLoadedXSurface(
    const XSurface *surface,
    uint8_t deformed,
    uint32_t modelBoneCount)
{
    if (!surface)
        return false;

    uint32_t vertexBytes = 0;
    uint32_t indexBytes = 0;
    uint32_t rigidListBytes = 0;
    if (!db::validation::CheckedSpanBytes(
            surface->vertCount,
            32,
            &vertexBytes)
        || !db::validation::CheckedSpanBytes(
            surface->triCount,
            6,
            &indexBytes)
        || !db::validation::CheckedSpanBytes(
            surface->vertListCount,
            disk32::kXRigidVertListBytes,
            &rigidListBytes))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file surface extent");
        return false;
    }

    const db::relocation::Status vertexSpan = DB_ValidateStreamAddress(
        surface->verts0,
        vertexBytes,
        16,
        kDirectBlock7);
    const db::relocation::Status indexSpan = DB_ValidateStreamAddress(
        surface->triIndices,
        indexBytes,
        16,
        kDirectBlock8);
    if (vertexSpan != db::relocation::Status::Ok
        || indexSpan != db::relocation::Status::Ok
        || !db::validation::XSurfaceTriangleIndicesValid(
            surface->triIndices,
            surface->triCount,
            surface->vertCount))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file surface geometry");
        return false;
    }
    if (deformed)
        return true;

    const db::relocation::Status rigidSpan = DB_ValidateStreamAddress(
        surface->vertList,
        rigidListBytes,
        4,
        kDirectBlock4);
    if (rigidSpan != db::relocation::Status::Ok
        || !db::validation::XSurfaceRigidPartitionValid(
            surface->vertList,
            surface->vertListCount,
            surface->vertCount,
            surface->triCount,
            surface->triIndices))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file rigid surface partition");
        return false;
    }

    if (!modelBoneCount)
    {
        Com_Error(ERR_DROP, "Rigid fast-file surface has no model bones");
        return false;
    }
    for (uint32_t index = 0; index < surface->vertListCount; ++index)
    {
        const XRigidVertList &rigid = surface->vertList[index];
        if ((rigid.boneOffset & 63u) != 0
            || (rigid.boneOffset >> 6) >= modelBoneCount)
        {
            Com_Error(ERR_DROP, "Invalid fast-file rigid-surface bone offset");
            return false;
        }

        const db::relocation::Status treeHeader = DB_ValidateStreamAddress(
            rigid.collisionTree,
            disk32::kXSurfaceCollisionTreeBytes,
            4,
            kDirectBlock4);
        uint32_t nodeBytes = 0;
        uint32_t leafBytes = 0;
        if (treeHeader != db::relocation::Status::Ok
            || !DB_GetXSurfaceCollisionTreeExtents(
                rigid.collisionTree,
                &nodeBytes,
                &leafBytes))
        {
            Com_Error(ERR_DROP, "Invalid completed surface collision-tree header");
            return false;
        }
        const db::relocation::Status nodeSpan = DB_ValidateStreamAddress(
            rigid.collisionTree->nodes,
            nodeBytes,
            16,
            kDirectBlock4);
        const db::relocation::Status leafSpan = DB_ValidateStreamAddress(
            rigid.collisionTree->leafs,
            leafBytes,
            2,
            kDirectBlock4);
        if (nodeSpan != db::relocation::Status::Ok
            || leafSpan != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid completed surface collision-tree children");
            return false;
        }

        const db::validation::XSurfaceCollisionTopologyStatus topology =
            db::validation::ValidateXSurfaceCollisionTopology(
                rigid.collisionTree->nodes,
                rigid.collisionTree->nodeCount,
                rigid.collisionTree->leafs,
                rigid.collisionTree->leafCount,
                rigid.triOffset,
                rigid.triCount,
                surface->triCount);
        if (topology != db::validation::XSurfaceCollisionTopologyStatus::Ok)
        {
            Com_Error(
                ERR_DROP,
                "Invalid rigid surface collision relationship: %s",
                db::validation::XSurfaceCollisionTopologyStatusName(topology));
            return false;
        }
    }
    return true;
}

bool DB_ValidateSpeakerMap(const SpeakerMap *speakerMap)
{
    if (!speakerMap || !speakerMap->name)
    {
        Com_Error(ERR_DROP, "Invalid fast-file speaker-map identity");
        return false;
    }

    uint8_t isDefault = 0;
    std::memcpy(&isDefault, &speakerMap->isDefault, sizeof(isDefault));
    if (isDefault > 1)
    {
        Com_Error(ERR_DROP, "Invalid fast-file speaker-map default flag");
        return false;
    }
    if (!isDefault && !*speakerMap->name)
    {
        Com_Error(ERR_DROP, "Invalid fast-file speaker-map name");
        return false;
    }

    for (uint32_t source = 0; source < 2; ++source)
    {
        for (uint32_t output = 0; output < 2; ++output)
        {
            const MSSChannelMap &channelMap =
                speakerMap->channelMaps[source][output];
            const uint32_t expectedSpeakers =
                db::validation::SpeakerMapExpectedSpeakerCount(output);
            if (channelMap.speakerCount
                != static_cast<int32_t>(expectedSpeakers))
            {
                Com_Error(ERR_DROP, "Invalid fast-file speaker-map channel count");
                return false;
            }
            for (uint32_t speaker = 0;
                 speaker < expectedSpeakers;
                 ++speaker)
            {
                const MSSSpeakerLevels &levels = channelMap.speakers[speaker];
                if (!db::validation::SpeakerMapEntryValid(
                        speaker,
                        levels.speaker,
                        levels.numLevels,
                        source + 1,
                        levels.levels[0],
                        levels.levels[1]))
                {
                    Com_Error(ERR_DROP, "Invalid fast-file speaker-map levels");
                    return false;
                }
            }
        }
    }
    return true;
}

bool DB_ValidateSoundAlias(const snd_alias_t *alias)
{
    if (!alias
        || !alias->aliasName
        || !*alias->aliasName
        || !alias->soundFile
        || !alias->volumeFalloffCurve
        || !alias->speakerMap
        || static_cast<uint32_t>(alias->soundFile->type)
            != (static_cast<uint32_t>(alias->flags) >> 6 & 3u))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file sound alias");
        return false;
    }
    return true;
}

bool DB_ValidateSunLight(const GfxLight *light)
{
    if (!light
        || light->type != GFX_LIGHT_TYPE_DIR
        || light->canUseShadowMap > 1
        || !db::validation::FiniteFloatArray(light->color, 12))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file sun light");
        return false;
    }
    return true;
}

bool DB_ValidateMaterialShaderLoadDef(
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

bool DB_ValidateMaterialShaderProgram(
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

bool DB_ValidateWaterHeader(const water_t *water, int32_t *sampleCount)
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

bool DB_ValidateMaterialNamedInputs(
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

bool DB_ValidateMaterialSemantics(const Material *material)
{
    if (!material
        || !material->info.name
        || !*material->info.name
        || material->info.sortKey >= 64
        || !material->techniqueSet
        || !material->techniqueSet->remappedTechniqueSet
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

bool DB_ValidateWeaponAccuracyGraph(
    const WeaponDef *weapon,
    uint32_t graphIndex,
    uint32_t *byteCount)
{
    if (!weapon || !byteCount || graphIndex >= WEAP_ACCURACY_COUNT)
    {
        Com_Error(ERR_DROP, "Invalid fast-file weapon accuracy graph index");
        return false;
    }

    const int32_t count = weapon->accuracyGraphKnotCount[graphIndex];
    const int32_t originalCount = weapon->originalAccuracyGraphKnotCount[graphIndex];
    // Runtime interpolation needs at least two knots, while the editable
    // accuracy-graph backup slots contain exactly sixteen knots per graph.
    if (!db::validation::OptionalMirroredCountInRange(count, originalCount, 2, 16)
        || !db::validation::PointerCountConsistent(
            weapon->accuracyGraphKnots[graphIndex] != nullptr,
            count)
        || !db::validation::PointerCountConsistent(
            weapon->originalAccuracyGraphKnots[graphIndex] != nullptr,
            originalCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file weapon accuracy graph %u", graphIndex);
        return false;
    }

    *byteCount = DB_CheckedDirectSpanBytes(count, 8, "weapon accuracy graph knots");
    return true;
}

bool DB_ValidateWeaponAccuracyGraphKnots(const WeaponDef *weapon, uint32_t graphIndex)
{
    const int32_t count = weapon->accuracyGraphKnotCount[graphIndex];
    if (!count)
        return true;
    if (!db::validation::NormalizedGraphKnots(
            weapon->accuracyGraphKnots[graphIndex],
            static_cast<uint32_t>(count))
        || !db::validation::NormalizedGraphKnots(
            weapon->originalAccuracyGraphKnots[graphIndex],
            static_cast<uint32_t>(count)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file weapon accuracy graph knots %u", graphIndex);
        return false;
    }
    return true;
}

bool DB_ValidateMaterialVertexDeclaration(const MaterialVertexDeclaration *declaration)
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

bool DB_ValidateMaterialPassArguments(const MaterialPass *pass, uint32_t argumentCount)
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

int32_t DB_CheckedCountSum(int64_t left, int64_t right, const char *description)
{
    int32_t result = 0;
    if (!db::validation::CheckedCountSum(left, right, &result))
    {
        Com_Error(ERR_DROP, "Invalid fast-file derived count for %s", description);
        return 0;
    }
    return result;
}

int32_t DB_CheckedCountProduct(int64_t left, int64_t right, const char *description)
{
    int32_t result = 0;
    if (!db::validation::CheckedCountProduct(left, right, &result))
    {
        Com_Error(ERR_DROP, "Invalid fast-file derived count for %s", description);
        return 0;
    }
    return result;
}

int32_t DB_CheckedCountDifference(int64_t total, int64_t removed, const char *description)
{
    int32_t result = 0;
    if (!db::validation::CheckedCountDifference(total, removed, &result))
    {
        Com_Error(ERR_DROP, "Invalid fast-file derived count for %s", description);
        return 0;
    }
    return result;
}

int32_t DB_CheckedCountCeilDiv(int64_t value, int64_t divisor, const char *description)
{
    int32_t result = 0;
    if (!db::validation::CheckedCountCeilDiv(value, divisor, &result))
    {
        Com_Error(ERR_DROP, "Invalid fast-file derived count for %s", description);
        return 0;
    }
    return result;
}

bool DB_ValidateWorldAabbCell(
    const GfxWorld *world,
    const GfxCell *cell)
{
    if (!world || !cell
        || !db::validation::WorldAabbTreePresenceValid(
            cell->aabbTree != nullptr,
            cell->aabbTreeCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world AABB pointer/count");
        return false;
    }

    std::uint8_t *nodeDepths = cell->aabbTreeCount
        ? static_cast<std::uint8_t *>(
            std::malloc(static_cast<std::size_t>(cell->aabbTreeCount)))
        : nullptr;
    if (cell->aabbTreeCount && !nodeDepths)
    {
        Com_Error(ERR_DROP, "Could not allocate world AABB validation state");
        return false;
    }
    const db::validation::WorldAabbTopologyStatus status =
        db::validation::ValidateWorldAabbTopology(
            cell->aabbTree,
            cell->aabbTreeCount,
            world->dpvs.staticSurfaceCount,
            world->dpvs.staticSurfaceCountNoDecal,
            nodeDepths,
            static_cast<std::uint64_t>(cell->aabbTreeCount));
    std::free(nodeDepths);
    if (status != db::validation::WorldAabbTopologyStatus::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file world AABB topology: %s",
            db::validation::WorldAabbTopologyStatusName(status));
        return false;
    }
    return true;
}

bool DB_ValidateWorldAabbTrees(const GfxWorld *world)
{
    if (!world
        || !db::validation::PointerCountConsistent(
            world->cells != nullptr,
            world->dpvsPlanes.cellCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world cell pointer/count");
        return false;
    }
    if (world->surfaceCount < 0
        || !db::validation::WorldAabbSurfacePartitionsValid(
            world->dpvs.staticSurfaceCount,
            world->dpvs.staticSurfaceCountNoDecal,
            static_cast<std::uint32_t>(world->surfaceCount))
        || world->modelCount <= 0
        || !world->models
        || world->models[0].startSurfIndex != 0
        || world->models[0].surfaceCount
            != world->dpvs.staticSurfaceCount
        || world->models[0].surfaceCountNoDecal
            != world->dpvs.staticSurfaceCountNoDecal
        || world->dpvs.smodelCount
            > db::validation::kMaxWorldAabbStaticModels)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world static-surface counts");
        return false;
    }

    std::int32_t totalNodeCount = 0;
    std::int32_t maximumCellNodeCount = 0;
    for (std::int32_t cellIndex = 0;
        cellIndex < world->dpvsPlanes.cellCount;
        ++cellIndex)
    {
        const GfxCell &cell = world->cells[cellIndex];
        if (!db::validation::WorldAabbTreePresenceValid(
                cell.aabbTree != nullptr,
                cell.aabbTreeCount))
        {
            Com_Error(
                ERR_DROP,
                "Invalid fast-file world AABB pointer/count in cell %d",
                cellIndex);
            return false;
        }

        std::int32_t nextTotal = 0;
        if (!db::validation::CheckedCountSum(
                totalNodeCount,
                cell.aabbTreeCount,
                &nextTotal))
        {
            Com_Error(ERR_DROP, "Invalid fast-file aggregate world AABB count");
            return false;
        }
        totalNodeCount = nextTotal;
        if (cell.aabbTreeCount > maximumCellNodeCount)
            maximumCellNodeCount = cell.aabbTreeCount;
    }

    std::uint8_t *nodeDepths = maximumCellNodeCount
        ? static_cast<std::uint8_t *>(
            std::malloc(static_cast<std::size_t>(maximumCellNodeCount)))
        : nullptr;
    if (maximumCellNodeCount && !nodeDepths)
    {
        Com_Error(ERR_DROP, "Could not allocate world AABB validation state");
        return false;
    }
    const std::uint64_t sortedSurfaceCount =
        static_cast<std::uint64_t>(world->dpvs.staticSurfaceCount)
        + world->dpvs.staticSurfaceCountNoDecal;
    std::uint8_t *surfaceCoverage = sortedSurfaceCount
        ? static_cast<std::uint8_t *>(
            std::calloc(static_cast<std::size_t>(sortedSurfaceCount), 1))
        : nullptr;
    if (sortedSurfaceCount && !surfaceCoverage)
    {
        std::free(nodeDepths);
        Com_Error(ERR_DROP, "Could not allocate world AABB surface coverage");
        return false;
    }

    for (std::int32_t cellIndex = 0;
        cellIndex < world->dpvsPlanes.cellCount;
        ++cellIndex)
    {
        const GfxCell &cell = world->cells[cellIndex];
        const db::validation::WorldAabbTopologyStatus status =
            db::validation::ValidateWorldAabbTopology(
                cell.aabbTree,
                cell.aabbTreeCount,
                world->dpvs.staticSurfaceCount,
                world->dpvs.staticSurfaceCountNoDecal,
                nodeDepths,
                static_cast<std::uint64_t>(maximumCellNodeCount));
        if (status != db::validation::WorldAabbTopologyStatus::Ok)
        {
            std::free(nodeDepths);
            std::free(surfaceCoverage);
            Com_Error(
                ERR_DROP,
                "Invalid fast-file world AABB topology in cell %d: %s",
                cellIndex,
                db::validation::WorldAabbTopologyStatusName(status));
            return false;
        }
        if (cell.aabbTreeCount)
        {
            const GfxAabbTree &root = cell.aabbTree[0];
            if (!db::validation::MarkUniqueCoverageSpan(
                    surfaceCoverage,
                    sortedSurfaceCount,
                    root.startSurfIndex,
                    root.surfaceCount)
                || !db::validation::MarkUniqueCoverageSpan(
                    surfaceCoverage,
                    sortedSurfaceCount,
                    root.startSurfIndexNoDecal,
                    root.surfaceCountNoDecal))
            {
                std::free(nodeDepths);
                std::free(surfaceCoverage);
                Com_Error(ERR_DROP, "Overlapping fast-file world AABB root surfaces");
                return false;
            }
        }
    }
    std::free(nodeDepths);
    if (!db::validation::CoverageComplete(
            surfaceCoverage,
            sortedSurfaceCount))
    {
        std::free(surfaceCoverage);
        Com_Error(ERR_DROP, "Fast-file world AABB roots leave uncovered surfaces");
        return false;
    }
    std::free(surfaceCoverage);
    return true;
}
}

struct DynEntityServer // sizeof=0x24
{
    GfxPlacement pose;
    uint16_t flags;
    // padding byte
    // padding byte
    int32_t health;
};

void *varint;
void *varuint;
GfxVertex *varGfxVertex;
uint64_t *varuint64_t           ;
int32_t *varexpressionEntryType     ;
float *varfloat               ;
ComWorld **varComWorldPtr     ;
enum weapInventoryType_t *varweapInventoryType_t     ;
RawFile **varRawFilePtr     ;
GfxLightDef **varGfxLightDefPtr     ;
GfxLightmapArray *varGfxLightmapArray     ;
itemDef_s **varitemDef_ptr     ;
GameWorldSp **varGameWorldSpPtr     ;
XAnimDeltaPartQuat *varXAnimDeltaPartQuat     ;
clipMap_t *varclipMap_t     ;
MaterialPixelShaderProgram *varMaterialPixelShaderProgram     ;
GfxWorldStreamInfo *varGfxWorldStreamInfo     ;
char const * varConstChar           ;
uint16_t *varr_index16_t         ;
MenuList *varMenuList     ;
listBoxDef_s *varlistBoxDef_t     ;
Operand *varOperand     ;
DObjAnimMat *varDObjAnimMat     ;
uint32_t *varXAUDIOSAMPLERATE     ;
mnode_t *varmnode_t     ;
union FxElemDefVisuals *varFxElemDefVisuals     ;
XModelCollSurf_s *varXModelCollSurf     ;
XModelCollTri_s *varXModelCollTri;
DynEntityServer *varDynEntityServer     ;
MaterialStreamRouting *varMaterialStreamRouting     ;
GfxScaledPlacement *varGfxScaledPlacement     ;
FxFloatRange *varFxFloatRange     ;
short *varint16_t             ;
GfxWorld *varGfxWorld     ;
GfxPortal *varGfxPortal     ;
CardMemory *varCardMemory     ;
//XAUDIOFXDATPARAM *varXAUDIOFXDATAPARAM     ;
LocalizeEntry *varLocalizeEntry     ;
MenuList **varMenuListPtr     ;
uint32_t *varunsigned            ;
//XAUDIOCHANNELMAPENTRY *varXAUDIOCHANNELMAPENTRY     ;
MaterialTechnique *varMaterialTechnique     ;
enum MapType *varMapType     ;
sunflare_t *varsunflare_t     ;
PhysPreset *varPhysPreset     ;
//D3DCubeTexture *varIDirect3DCubeTexture9     ;
//uint8_t *varXQuat2           ;
__int16 (*varXQuat2)[2];
//uint16_t (*)[3] varedgeCount_t      ;
Material **varMaterialHandle     ;
//XAUDIOREVERBSETTINGS *varXAUDIOREVERBSETTINGS     ;
pathnode_t *varpathnode_t     ;
unsigned char *varbyte16              ;
StreamFileName *varStreamFileName     ;
XAnimPartTrans *varXAnimPartTrans     ;
enum weapOverlayReticle_t *varweapOverlayReticle_t     ;
uint16_t *varushort              ;
float *varraw_float           ;
unsigned char *varbyte4096            ;
uint16_t *varDynEntityId         ;
clipMap_t **varclipMap_ptr     ;
GfxLightRegionHull *varGfxLightRegionHull     ;
unsigned char *varbyte128             ;
XModel **varXModelPtr     ;
enum XAssetType *varXAssetType     ;
enum weapType_t *varweapType_t     ;
MaterialPass *varMaterialPass     ;
GfxCell *varGfxCell     ;
enum weapPositionAnimNum_t *varweapPositionAnimNum_t     ;
pathlink_s *varpathlink_t     ;
FxElemMarkVisuals *varFxElemMarkVisuals     ;
unsigned char *varXAUDIOSAMPLETYPE     ;
union XAnimPartTransData *varXAnimPartTransData     ;
Font_s *varFont        ;
SndCurve *varSndCurve     ;
editFieldDef_s *vareditFieldDef_t     ;
XSurfaceCollisionNode *varXSurfaceCollisionNode     ;
GfxSceneDynBrush *varGfxSceneDynBrush     ;
pathnode_constant_t *varpathnode_constant_t     ;
cmodel_t *varcmodel_t     ;
unsigned char *varFxElemType          ;
XBoneInfo *varXBoneInfo     ;
FxImpactTable **varFxImpactTablePtr     ;
float *varXAUDIOVOLUME        ;
//XaReverbSettings *varXaReverbSettings     ;
uint8_t *varvec2_           ;
float (*varvec2_t)[2];
FxElemAtlas *varFxElemAtlas     ;
MaterialVertexStreamRouting *varMaterialVertexStreamRouting     ;
CollisionPartition *varCollisionPartition     ;
union XAnimIndices *varXAnimIndices     ;
XAsset *varXAsset      ;
snd_alias_list_t **varsnd_alias_list_ptr     ;
uint32_t *varuint32_t            ;
unsigned char **varGfxImagePixels     ;
enum weapStance_t *varweapStance_t     ;
pathnode_tree_t **varpathnode_tree_ptr     ;
MaterialShaderArgument *varMaterialShaderArgument     ;
WeaponDef *varWeaponDef     ;
enum expDataType *varoperandDataType     ;
//int32_t (*)[4] varXPartBits        ;
ComPrimaryLight *varComPrimaryLight     ;
MaterialTextureDef *varMaterialTextureDef     ;
BOOL * varbool               ;
uint16_t *varUnsignedShort       ;
union MaterialArgumentDef *varMaterialArgumentDef     ;
Glyph *varGlyph        ;
//StreamFileNamePacked *varStreamFileNamePacked     ;
XModelLodInfo *varXModelLodInfo     ;
enum ammoCounterClipType_t *varammoCounterClipType_t     ;
uint16_t *varLeafBrush           ;
//XAUDIOCHANNELMAP *varXAUDIOCHANNELMAP     ;
enum nodeType *varnodeType     ;
columnInfo_s *varcolumnInfo_t     ;
enum snd_alias_type_t *varsnd_alias_type_t     ;
enum activeReticleType_t *varactiveReticleType_t     ;
GfxLightGridEntry *varGfxLightGridEntry     ;
ItemKeyHandler *varItemKeyHandler     ;
union XAUDIOFXPARAM *varXAUDIOFXPARAM     ;
union StreamFileInfo *varStreamFileInfo     ;
GfxPackedVertex *varGfxPackedVertex     ;
cLeaf_t *varcLeaf_t     ;
union FxEffectDefRef *varFxEffectDefRef     ;
unsigned char *varbyteShader          ;
enum WeapAccuracyType *varWeapAccuracyType     ;
unsigned char *varbyte                ;
FxTrailVertex *varFxTrailVertex     ;
//XAUDIOXMAFORMAT *varXAUDIOXMAFORMAT     ;
char const **varTempString        ;
StringTable **varStringTablePtr     ;
//float (*)[4] varraw_vec4_t       ;
//uint8_t *varUShortVec        ;
uint16_t (*varUShortVec)[3];
statement_s *varstatement     ;
//D3DVolumeTexture *varIDirect3DVolumeTexture9     ;
GfxLightGridColors *varGfxLightGridColors     ;
enum operationEnum *varOperator     ;
cLeafBrushNodeLeaf_t *varcLeafBrushNodeLeaf_t     ;
multiDef_s **varmultiDef_ptr     ;
XRigidVertList *varXRigidVertList     ;
DpvsPlane *varDpvsPlane     ;
//short (*)[3] varAxialMaterialNum     ;
XModelPiece *varXModelPiece     ;
XModelPieces **varXModelPiecesPtr     ;
union XAssetHeader *varXAssetHeader     ;
CollisionAabbTree *varCollisionAabbTree     ;
cplane_s *varcplane_t     ;
union operandInternalDataUnion *varoperandInternalDataUnion     ;
short *varXQuat[4]            ;
expressionEntry *varexpressionEntry     ;
XAssetList *varXAssetList     ;
enum weapClass_t *varweapClass_t     ;
enum MaterialWorldVertexFormat *varMaterialWorldVertexFormat     ;
MaterialPixelShader **varMaterialPixelShaderPtr     ;
uint8_t * varvec4_t           ;
char *varchar                ;
FxEffectDef const **varFxEffectDefHandle     ;
uint16_t *varXBlendInfo          ;
GfxImageLoadDef *varGfxImageLoadDef     ;
GfxLightRegion *varGfxLightRegion     ;
GfxPackedPlacement *varGfxPackedPlacement     ;
SoundFile *varSoundFile     ;
DynEntityColl *varDynEntityColl     ;
unsigned char *varuint8_t             ;
GfxShadowGeometry *varGfxShadowGeometry     ;
union SoundFileRef *varSoundFileRef     ;
XModelPieces *varXModelPieces     ;
//uint8_t *varvec3_t           ;
float (*varvec3_t)[3];
Font_s **varFontHandle     ;
GfxImage *varGfxImage     ;
//union MaterialTextureDefInfo *varMaterialTextureDefInfo     ;
water_t **varMaterialTextureDefInfo; // KISAKTODO: this is really the above union
MaterialInfo *varMaterialInfo     ;
union FxSpawnDef *varFxSpawnDef     ;
union FxElemVisuals *varFxElemVisuals     ;
enum weapAnimFiles_t *varweapAnimFiles_t     ;
SunLightParseParams *varSunLightParseParams     ;
FxEffectDef *varFxEffectDef     ;
enum GfxLightType *varGfxLightType     ;
XAnimParts **varXAnimPartsPtr     ;
PathData *varPathData     ;
float *varvec_t               ;
GfxBrushModel *varGfxBrushModel     ;
itemDef_s *varitemDef_t     ;
XAnimPartTransFrames *varXAnimPartTransFrames     ;
//short (*)[3] varShort3           ;
XSurfaceCollisionLeaf *varXSurfaceCollisionLeaf     ;
//D3DBaseTexture *varIDirect3DBaseTexture9     ;
XAnimDeltaPartQuatDataFrames *varXAnimDeltaPartQuatDataFrames     ;
union GfxTexture *varGfxRawTexture     ;
GfxPlacement *varGfxPlacement     ;
GfxLightImage *varGfxLightImage     ;
XModel *varXModel      ;
ComWorld *varComWorld     ;
int32_t *varqboolean            ;
rectDef_s *varrectDef_t     ;
char *varint8_t              ;
GfxAabbTree *varGfxAabbTree     ;
FxElemVec3Range *varFxElemVec3Range     ;
PhysMass *varPhysMass     ;
SndDriverGlobals *varSndDriverGlobals     ;
menuDef_t **varmenuDef_ptr     ;
water_t *varwater_t     ;
GfxWorldVertex *varGfxWorldVertex     ;
GfxLightGrid *varGfxLightGrid     ;
MaterialTechniqueSet *varMaterialTechniqueSet     ;
enum OffhandClass *varOffhandClass     ;
FxIntRange *varFxIntRange     ;
uint32_t *varraw_uint            ;
void *varDWORD               ;
GameWorldSp *varGameWorldSp     ;
XSurfaceVertexInfo *varXSurfaceVertexInfo     ;
enum DynEntityType *varDynEntityType     ;
union GfxColor *varGfxColor     ;
MaterialTechnique **varMaterialTechniquePtr     ;
union GfxTexture *varGfxTexture     ;
GfxWorldDpvsPlanes *varGfxWorldDpvsPlanes     ;
MaterialPixelShader *varMaterialPixelShader     ;
Picmip *varPicmip      ;
int32_t *varint32_t             ;
Material *varMaterial     ;
//XModelHighMipBounds *varXModelHighMipBounds     ;
snd_alias_list_t **varsnd_alias_list_name     ;
DynEntityDef *varDynEntityDef     ;
union XAnimDeltaPartQuatData *varXAnimDeltaPartQuatData     ;
srfTriangles_t *varsrfTriangles_t     ;
XAnimNotifyInfo *varXAnimNotifyInfo     ;
union itemDefData_t *varitemDefData_t     ;
//D3DTexture *varIDirect3DTexture9     ;
GfxLight *varGfxLight     ;
FxImpactEntry *varFxImpactEntry     ;
FxElemVisualState *varFxElemVisualState     ;
windowDef_t *varwindowDef_t     ;
XSurfaceCollisionTree *varXSurfaceCollisionTree     ;
union CollisionAabbTreeIndex *varCollisionAabbTreeIndex     ;
XSurfaceCollisionAabb *varXSurfaceCollisionAabb     ;
XSurface *varXSurface     ;
//union PackedTexCoords *varPackedTexCoords     ;
enum weaponIconRatioType_t *varweaponIconRatioType_t     ;
GfxVertexShaderLoadDef *varGfxVertexShaderLoadDef     ;
enum WeapOverlayInteface_t *varWeapOverlayInteface_t     ;
editFieldDef_s **vareditFieldDef_ptr     ;
MaterialMemory *varMaterialMemory     ;
//XaIwXmaDataInfo *varXaIwXmaDataInfo     ;
FxElemDef *varFxElemDef     ;
union cLeafBrushNodeData_t *varcLeafBrushNodeData_t     ;
PhysGeomList *varPhysGeomList     ;
//GfxStreamingAabbTree *varGfxStreamingAabbTree     ;
LoadedSound **varLoadedSoundPtr     ;
uint32_t *varraw_uint128         ;
StringTable *varStringTable     ;
union GfxDrawSurf *varGfxDrawSurf     ;
DynEntityClient *varDynEntityClient     ;
dmaterial_t *vardmaterial_t     ;
RawFile *varRawFile     ;
SndCurve **varSndCurvePtr     ;
ItemKeyHandler *varItemKeyHandlerNext     ;
MaterialTechniqueSet **varMaterialTechniqueSetPtr     ;
GfxWorldVertexData *varGfxWorldVertexData     ;
MaterialVertexDeclaration *varMaterialVertexDeclaration     ;
enum guidedMissileType_t *varguidedMissileType_t     ;
GfxPixelShaderLoadDef *varGfxPixelShaderLoadDef     ;
union PackedLightingCoords *varPackedLightingCoords     ;
MaterialVertexShader **varMaterialVertexShaderPtr     ;
union XAnimDynamicIndices *varXAnimDynamicIndicesTrans     ;
uint16_t *varStaticModelIndex     ;
GfxStaticModelDrawInst *varGfxStaticModelDrawInst     ;
enum PenetrateType *varPenetrateType     ;
int32_t marker_db_load           ;
GfxLightDef *varGfxLightDef     ;
//union MaterialVertexShaderProgram *varMaterialVertexShaderProgram     ;
SndDriverGlobals **varSndDriverGlobalsPtr     ;
cStaticModel_s *varcStaticModel_t     ;
menuDef_t *varmenuDef_t     ;
expressionEntry **varexpressionEntry_ptr     ;
unsigned char *varbyte4               ;
uint32_t *varraw_DWORD           ;
pathnode_tree_t *varpathnode_tree_t     ;
char const ***varXStringPtr      ;
union pathnode_tree_info_t *varpathnode_tree_info_t     ;
cLeafBrushNode_s *varcLeafBrushNode_t     ;
complex_s *varcomplex_t     ;
WeaponDef **varWeaponDefPtr     ;
LoadedSound *varLoadedSound     ;
//XaSeekTable *varXaSeekTable     ;
//XAUDIOSOURCEFORMAT *varXAUDIOSOURCEFORMAT     ;
unsigned char *varGfxImageCategory     ;
unsigned char *varXAUDIOXMASTREAMCOUNT     ;
GfxSceneDynModel *varGfxSceneDynModel     ;
FxSpawnDefOneShot *varFxSpawnDefOneShot     ;
ScriptStringList *varScriptStringList     ;
union XAnimDynamicIndices *varXAnimDynamicIndicesDeltaQuat     ;
GfxLightRegionAxis *varGfxLightRegionAxis     ;
unsigned char *varraw_byte            ;
void *varvoid                ;
cNode_t *varcNode_t     ;
GfxSurface *varGfxSurface     ;
multiDef_s *varmultiDef_t     ;
union GfxTexture *varGfxTextureLoad;
GameWorldMp **varGameWorldMpPtr     ;
enum WeapStickinessType *varWeapStickinessType     ;
GfxWorld **varGfxWorldPtr     ;
enum weapProjExposion_t *varweapProjExposion_t     ;
snd_alias_t *varsnd_alias_t     ;
unsigned char *varraw_byte16          ;
SpeakerMap *varSpeakerMap     ;
//D3DIndexBuffer *varGfxIndexBuffer     ;
unsigned char *varGfxSamplerState     ;
uint16_t *varraw_ushort          ;
MaterialArgumentCodeConst *varMaterialArgumentCodeConst     ;
union XAnimDynamicFrames *varXAnimDynamicFrames     ;
pathnode_tree_nodes_t *varpathnode_tree_nodes_t     ;
StreamedSound *varStreamedSound     ;
XModelStreamInfo *varXModelStreamInfo     ;
FxElemVelStateInFrame *varFxElemVelStateInFrame     ;
unsigned char *varcbrushedge_t        ;
pathbasenode_t *varpathbasenode_t     ;
GfxStateBits *varGfxStateBits     ;
union PackedUnitVec *varPackedUnitVec     ;
GfxPosTexVertex *varGfxPosTexVertex     ;
uint16_t *varr_index_t           ;
BrushWrapper *varBrushWrapper     ;
GfxPackedVertex *varGfxPackedVertex0     ;
int32_t *varFxElemDefFlags      ;
FxTrailDef *varFxTrailDef     ;
GfxReflectionProbe *varGfxReflectionProbe     ;
GfxStaticModelInst *varGfxStaticModelInst     ;
union entryInternalData *varentryInternalData     ;
GameWorldMp *varGameWorldMp     ;
MaterialVertexShader *varMaterialVertexShader     ;
cbrushside_t *varcbrushside_t     ;
char const **varXString           ;
unsigned char *varBYTE                ;
GfxWorldDpvsDynamic *varGfxWorldDpvsDynamic     ;
FxSpawnDefLooping *varFxSpawnDefLooping     ;
MaterialConstantDef *varMaterialConstantDef     ;
StreamFileNameRaw *varStreamFileNameRaw     ;
GfxCullGroup *varGfxCullGroup     ;
PhysPreset **varPhysPresetPtr     ;
rectDef_s *varUiRectangle     ;
DynEntityPose *varDynEntityPose     ;
MapEnts **varMapEntsPtr     ;
enum ImpactType *varImpactType     ;
FxImpactTable *varFxImpactTable     ;
cbrush_t *varcbrush_t     ;
//D3DVertexBuffer *varGfxVertexBuffer     ;
GfxWorldVertexLayerData *varGfxWorldVertexLayerData     ;
MapEnts *varMapEnts     ;
unsigned char *varXAUDIOCHANNEL       ;
char *varchar2048            ;
//XAUDIOPACKET_ALIGNED *varXAUDIOPACKET_ALIGNED     ;
uint16_t *varScriptString        ;
windowDef_t *varWindow     ;
CollisionBorder *varCollisionBorder     ;
FxElemVelStateSample *varFxElemVelStateSample     ;
XAnimDeltaPart *varXAnimDeltaPart     ;
GfxWorldVertex *varGfxWorldVertex0     ;
//float (*)[3] varshared_vec3_t     ;
listBoxDef_s **varlistBoxDef_ptr     ;
PhysGeomInfo *varPhysGeomInfo     ;
//unsigned char (*)[3] varByteVec          ;
uint16_t *varuint16_t            ;
enum weapFireType_t *varweapFireType_t     ;
enum weaponAltModel_t *varweaponAltModel_t     ;
FxElemVisStateSample *varFxElemVisStateSample     ;
GfxWorldDpvsStatic *varGfxWorldDpvsStatic     ;
XAnimParts *varXAnimParts     ;
short *varshort               ;
GfxImage **varGfxImagePtr     ;
snd_alias_list_t *varsnd_alias_list_t     ;
cLeafBrushNodeChildren_t *varcLeafBrushNodeChildren_t     ;
LocalizeEntry **varLocalizeEntryPtr     ;
uint8_t (*varByteVec)[3];
//MssSound *varMssSound;
MssSoundCOD4 *varMssSound;
IDirect3DVertexBuffer9 **varGfxVertexBuffer;
uint8_t *varXZoneHandle;
MaterialVertexShaderProgram *varMaterialVertexShaderProgram;

void __cdecl Load_byte(bool atStreamStart)
{
    Load_Stream(atStreamStart, varbyte, 1);
}

void __cdecl Load_byteArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varbyte, count);
}

void __cdecl Load_charArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varchar, count);
}

void __cdecl Load_int(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varint, 4);
}

void __cdecl Load_intArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varint, count, 4);
}

void __cdecl Load_uintArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (unsigned char*)varuint, count, 4);
}

void __cdecl Load_uint(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varuint, 4);
}

void __cdecl Load_float(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varfloat, 4);
}

void __cdecl Load_floatArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varfloat, count, 4);
}

void __cdecl Load_raw_uintArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varraw_uint, count, 4);
}

uint8_t *__cdecl AllocLoad_raw_uint128()
{
    return DB_AllocStreamPos(127);
}

void __cdecl Load_raw_uint128Array(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varraw_uint128, count, 4);
}

void __cdecl Load_raw_byteArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varraw_byte, count);
}

void __cdecl Load_raw_byte16Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varraw_byte16, count);
}

void __cdecl Load_vec2_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varvec2_t, count, 8);
}

void __cdecl Load_vec3_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varvec3_t, 12);
}

void __cdecl Load_vec3_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varvec3_t, count, 12);
}

void __cdecl Load_shortArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varshort, count, 2);
}

void __cdecl Load_ushortArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varushort, count, 2);
}

void __cdecl Load_XQuat2(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXQuat2, 4);
}

void __cdecl Load_XQuat2Array(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varXQuat2, count, 4);
}

uint8_t *__cdecl AllocLoad_XBlendInfo()
{
    return DB_AllocStreamPos(1);
}

void __cdecl Load_UnsignedShortArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varUnsignedShort, count, 2);
}

void __cdecl Load_ScriptString(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varScriptString, 2);
    Load_ScriptStringCustom(varScriptString);
}

void __cdecl Load_ScriptStringArray(bool atStreamStart, int32_t count)
{
    uint16_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varScriptString, count, 2);
    var = varScriptString;
    for (i = 0; i < count; ++i)
    {
        varScriptString = var;
        Load_ScriptString(0);
        ++var;
    }
}

uint8_t *__cdecl AllocLoad_raw_byte()
{
    return DB_AllocStreamPos(0);
}

void __cdecl Load_ConstCharArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varConstChar, count);
}

void __cdecl Load_TempString(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varTempString, 4);
    if (*varTempString)
    {
        if (*varTempString == (const char *)-1)
        {
            *varTempString = (const char *)AllocLoad_raw_byte();
            varConstChar = *varTempString;
            Load_TempStringCustom((char **)varTempString);
        }
        else
        {
            DB_ConvertOffsetToTempString(
                (uint32_t*)varTempString,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_TempStringArray(bool atStreamStart, int32_t count)
{
    const char **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varTempString, count, 4);
    var = varTempString;
    for (i = 0; i < count; ++i)
    {
        varTempString = var;
        Load_TempString(0);
        ++var;
    }
}

void __cdecl Load_XString(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXString, 4);
    if (*varXString)
    {
        if (*varXString == (const char *)-1)
        {
            *varXString = (const char *)AllocLoad_raw_byte();
            varConstChar = *varXString;
            Load_XStringCustom((char **)varXString);
        }
        else
        {
            DB_ConvertOffsetToCString(
                (uint32_t*)varXString,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_XStringArray(bool atStreamStart, int32_t count)
{
    const char **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varXString, count, 4);
    var = varXString;
    for (i = 0; i < count; ++i)
    {
        varXString = var;
        Load_XString(0);
        ++var;
    }
}

void __cdecl Load_XStringPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXStringPtr, 4);
    if (*varXStringPtr)
    {
        if (*varXStringPtr == (const char **)-1)
        {
            *varXStringPtr = (const char **)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varXStringPtr,
                DBAliasKind::XStringPointerSlot);
            if (!completed)
                return;
            varXString = *varXStringPtr;
            Load_XString(1);
            if (!**varXStringPtr || !***varXStringPtr)
            {
                Com_Error(ERR_DROP, "Fast-file string holder has no value");
                return;
            }
            DB_SetInsertedPointer(
                completed,
                DBAliasKind::XStringPointerSlot,
                *varXStringPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varXStringPtr,
                DBAliasKind::XStringPointerSlot);
        }
    }
}

void __cdecl Load_ScriptStringList(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varScriptStringList, 8);
    DB_PushStreamPos(4);
    if (varScriptStringList->strings)
    {
        varScriptStringList->strings = (const char **)AllocLoad_FxElemVisStateSample();
        varTempString = varScriptStringList->strings;
        Load_TempStringArray(1, varScriptStringList->count);
    }
    DB_PopStreamPos();
}

void __cdecl Load_complex_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varcomplex_t, count, 8);
}

void __cdecl Load_dmaterial_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)vardmaterial_t, count, 72);
}

void __cdecl Mark_ScriptString()
{
    Mark_ScriptStringCustom(varScriptString);
}

void __cdecl Mark_ScriptStringArray(int32_t count)
{
    uint16_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varScriptString;
    for (i = 0; i < count; ++i)
    {
        varScriptString = var;
        Mark_ScriptString();
        ++var;
    }
}

void __cdecl Load_XAnimIndices()
{
    if (varXAnimParts->numframes >= 0x100u)
    {
        if (varXAnimIndices->_2)
        {
            varXAnimIndices->_2 = (uint16_t*)AllocLoad_XBlendInfo();
            varushort = varXAnimIndices->_2;
            Load_ushortArray(1, varXAnimParts->indexCount);
        }
    }
    else if (varXAnimIndices->_1)
    {
        varXAnimIndices->_1 = AllocLoad_raw_byte();
        varbyte = varXAnimIndices->_1;
        Load_byteArray(1, varXAnimParts->indexCount);
    }
}

void __cdecl Load_XAnimDynamicIndicesDeltaQuat(bool atStreamStart)
{
    const int32_t indexCount = DB_CheckedCountSum(
        varXAnimDeltaPartQuat->size,
        1,
        "animation delta quaternion indices");
    if (varXAnimParts->numframes >= 0x100u)
    {
        iassert(atStreamStart);
        Load_Stream(1, (byte*)varXAnimDynamicIndicesDeltaQuat->_2, 0);
        iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(varXAnimDynamicIndicesDeltaQuat->_2));
        varUnsignedShort = (uint16_t *)varXAnimDynamicIndicesDeltaQuat;
        Load_UnsignedShortArray(1, indexCount);
    }
    else
    {
        iassert(atStreamStart);
        Load_Stream(1, varXAnimDynamicIndicesDeltaQuat->_1, 0);
        iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(varXAnimDynamicIndicesDeltaQuat->_1));
        varbyte = (uint8_t *)varXAnimDynamicIndicesDeltaQuat;
        Load_byteArray(1, indexCount);
    }
}

void __cdecl Load_XAnimDeltaPartQuatDataFrames(bool atStreamStart)
{
    iassert(atStreamStart);
    Load_Stream(1, (uint8_t *)varXAnimDeltaPartQuatDataFrames, 4);
    iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(&varXAnimDeltaPartQuatDataFrames->indices));
    varXAnimDynamicIndicesDeltaQuat = &varXAnimDeltaPartQuatDataFrames->indices;
    Load_XAnimDynamicIndicesDeltaQuat(1);
    if (varXAnimDeltaPartQuatDataFrames->frames)
    {
        varXAnimDeltaPartQuatDataFrames->frames = (__int16 (*)[2])AllocLoad_FxElemVisStateSample();
        varXQuat2 = varXAnimDeltaPartQuatDataFrames->frames;
        if (varXAnimDeltaPartQuat->size)
            Load_XQuat2Array(
                1,
                DB_CheckedCountSum(
                    varXAnimDeltaPartQuat->size,
                    1,
                    "animation delta quaternion frames"));
        else
            Load_XQuat2Array(1, 0);
    }
}

void __cdecl Load_XAnimDeltaPartQuatData(bool atStreamStart)
{
    if (varXAnimDeltaPartQuat->size)
    {
        varXAnimDeltaPartQuatDataFrames = &varXAnimDeltaPartQuatData->frames;
        Load_XAnimDeltaPartQuatDataFrames(atStreamStart);
    }
    else if (atStreamStart)
    {
        varXQuat2 = (__int16 (*)[2])varXAnimDeltaPartQuatData;
        Load_XQuat2(atStreamStart);
    }
}

void __cdecl Load_XAnimDeltaPartQuat(bool atStreamStart)
{
    iassert(atStreamStart);
    Load_Stream(1, (uint8_t *)varXAnimDeltaPartQuat, 4);
    iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(&varXAnimDeltaPartQuat->u));
    varXAnimDeltaPartQuatData = &varXAnimDeltaPartQuat->u;
    Load_XAnimDeltaPartQuatData(1);
}

void __cdecl Load_XAnimDeltaPart(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAnimDeltaPart, 8);
    if (varXAnimDeltaPart->trans)
    {
        varXAnimDeltaPart->trans = (XAnimPartTrans *)AllocLoad_FxElemVisStateSample();
        varXAnimPartTrans = varXAnimDeltaPart->trans;
        Load_XAnimPartTrans(1);
    }
    if (varXAnimDeltaPart->quat)
    {
        varXAnimDeltaPart->quat = (XAnimDeltaPartQuat *)AllocLoad_FxElemVisStateSample();
        varXAnimDeltaPartQuat = varXAnimDeltaPart->quat;
        Load_XAnimDeltaPartQuat(1);
    }
}

void __cdecl Load_XAnimDynamicIndicesTrans(bool atStreamStart)
{
    const int32_t indexCount = DB_CheckedCountSum(
        varXAnimPartTrans->size,
        1,
        "animation translation indices");
    if (varXAnimParts->numframes >= 0x100u)
    {
        if (!atStreamStart)
            MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1550, 0, "%s", "atStreamStart");
        Load_Stream(1, varXAnimDynamicIndicesTrans->_1, 0);
        if (DB_GetStreamPos() != (uint8_t *)varXAnimDynamicIndicesTrans)
            MyAssertHandler(
                "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
                1552,
                0,
                "%s",
                "DB_GetStreamPos() == reinterpret_cast< byte * >( varXAnimDynamicIndicesTrans->_2 )");
        varUnsignedShort = (uint16_t *)varXAnimDynamicIndicesTrans;
        Load_UnsignedShortArray(1, indexCount);
    }
    else
    {
        if (!atStreamStart)
            MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1542, 0, "%s", "atStreamStart");
        Load_Stream(1, varXAnimDynamicIndicesTrans->_1, 0);
        if (DB_GetStreamPos() != (uint8_t *)varXAnimDynamicIndicesTrans)
            MyAssertHandler(
                "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
                1544,
                0,
                "%s",
                "DB_GetStreamPos() == reinterpret_cast< byte * >( varXAnimDynamicIndicesTrans->_1 )");
        varbyte = (uint8_t *)varXAnimDynamicIndicesTrans;
        Load_byteArray(1, indexCount);
    }
}

void __cdecl Load_ByteVecArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varByteVec, count, 3);
}

void __cdecl Load_UShortVecArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varUShortVec, count, 6);
}

void __cdecl Load_XAnimDynamicFrames()
{
    const int32_t frameCount = varXAnimPartTrans->size
        ? DB_CheckedCountSum(varXAnimPartTrans->size, 1, "animation translation frames")
        : 0;
    if (varXAnimPartTrans->smallTrans)
    {
        if (varXAnimDynamicFrames->_1)
        {
            varXAnimDynamicFrames->_1 = (uint8_t (*)[3])AllocLoad_raw_byte();
            varByteVec = varXAnimDynamicFrames->_1;
            if (varXAnimPartTrans->size)
                Load_ByteVecArray(1, frameCount);
            else
                Load_ByteVecArray(1, 0);
        }
    }
    else if (varXAnimDynamicFrames->_1)
    {
        varXAnimDynamicFrames->_2 = (uint16_t(*)[3])AllocLoad_FxElemVisStateSample();
        varUShortVec = varXAnimDynamicFrames->_2;
        if (varXAnimPartTrans->size)
            Load_UShortVecArray(1, frameCount);
        else
            Load_UShortVecArray(1, 0);
    }
}

void __cdecl Load_XAnimPartTransFrames(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1784, 0, "%s", "atStreamStart");
    Load_Stream(1, (uint8_t *)varXAnimPartTransFrames, 28);
    if (DB_GetStreamPos() != (uint8_t *)&varXAnimPartTransFrames->indices)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
            1786,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( &varXAnimPartTransFrames->indices )");
    varXAnimDynamicIndicesTrans = &varXAnimPartTransFrames->indices;
    Load_XAnimDynamicIndicesTrans(1);
    varXAnimDynamicFrames = &varXAnimPartTransFrames->frames;
    Load_XAnimDynamicFrames();
}

void __cdecl Load_XAnimPartTransData(bool atStreamStart)
{
    if (varXAnimPartTrans->size)
    {
        varXAnimPartTransFrames = &varXAnimPartTransData->frames;
        Load_XAnimPartTransFrames(atStreamStart);
    }
    else if (atStreamStart)
    {
        varvec3_t = (float (*)[3])varXAnimPartTransData;
        Load_vec3_t(atStreamStart);
    }
}

void __cdecl Load_XAnimPartTrans(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1923, 0, "%s", "atStreamStart");
    Load_Stream(1, (uint8_t *)varXAnimPartTrans, 4);
    if (DB_GetStreamPos() != (uint8_t *)&varXAnimPartTrans->u)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
            1925,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( &varXAnimPartTrans->u )");
    varXAnimPartTransData = &varXAnimPartTrans->u;
    Load_XAnimPartTransData(1);
}

void __cdecl Load_XAnimNotifyInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAnimNotifyInfo, 8);
    varScriptString = &varXAnimNotifyInfo->name;
    Load_ScriptString(0);
}

void __cdecl Load_XAnimNotifyInfoArray(bool atStreamStart, int32_t count)
{
    XAnimNotifyInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varXAnimNotifyInfo, count, 8);
    var = varXAnimNotifyInfo;
    for (i = 0; i < count; ++i)
    {
        varXAnimNotifyInfo = var;
        Load_XAnimNotifyInfo(0);
        ++var;
    }
}

void __cdecl Load_XAnimParts(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAnimParts, 88);
    DB_PushStreamPos(4);
    varXString = &varXAnimParts->name;
    Load_XString(0);
    if (varXAnimParts->names)
    {
        varXAnimParts->names = (uint16_t *)AllocLoad_XBlendInfo();
        varScriptString = varXAnimParts->names;
        Load_ScriptStringArray(1, varXAnimParts->boneCount[9]);
    }
    if (varXAnimParts->notify)
    {
        varXAnimParts->notify = (XAnimNotifyInfo *)AllocLoad_FxElemVisStateSample();
        varXAnimNotifyInfo = varXAnimParts->notify;
        Load_XAnimNotifyInfoArray(1, varXAnimParts->notifyCount);
    }
    if (varXAnimParts->deltaPart)
    {
        varXAnimParts->deltaPart = (XAnimDeltaPart *)AllocLoad_FxElemVisStateSample();
        varXAnimDeltaPart = varXAnimParts->deltaPart;
        Load_XAnimDeltaPart(1);
    }
    if (varXAnimParts->dataByte)
    {
        varXAnimParts->dataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->dataByte;
        Load_byteArray(1, varXAnimParts->dataByteCount);
    }
    if (varXAnimParts->dataShort)
    {
        varXAnimParts->dataShort = (__int16 *)AllocLoad_XBlendInfo();
        varshort = varXAnimParts->dataShort;
        Load_shortArray(1, varXAnimParts->dataShortCount);
    }
    if (varXAnimParts->dataInt)
    {
        varXAnimParts->dataInt = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varXAnimParts->dataInt;
        Load_intArray(1, varXAnimParts->dataIntCount);
    }
    if (varXAnimParts->randomDataShort)
    {
        varXAnimParts->randomDataShort = (__int16 *)AllocLoad_XBlendInfo();
        varshort = varXAnimParts->randomDataShort;
        Load_shortArray(1, varXAnimParts->randomDataShortCount);
    }
    if (varXAnimParts->randomDataByte)
    {
        varXAnimParts->randomDataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->randomDataByte;
        Load_byteArray(1, varXAnimParts->randomDataByteCount);
    }
    if (varXAnimParts->randomDataInt)
    {
        varXAnimParts->randomDataInt = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varXAnimParts->randomDataInt;
        Load_intArray(1, varXAnimParts->randomDataIntCount);
    }
    varXAnimIndices = &varXAnimParts->indices;
    Load_XAnimIndices();
    DB_PopStreamPos();
}

void __cdecl Load_XAnimPartsPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varXAnimPartsPtr, 4);
    DB_PushStreamPos(0);
    if (*varXAnimPartsPtr)
    {
        value = (uint32_t)*varXAnimPartsPtr;
        if (value == -1 || value == -2)
        {
            *varXAnimPartsPtr = (XAnimParts *)AllocLoad_FxElemVisStateSample();
            varXAnimParts = *varXAnimPartsPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::XAnimParts);
            else
                inserted = {};
            Load_XAnimParts(1);
            Load_XAnimPartsAsset((XAssetHeader *)varXAnimPartsPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::XAnimParts,
                    *varXAnimPartsPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varXAnimPartsPtr,
                DBAliasKind::XAnimParts);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_XAnimNotifyInfo()
{
    varScriptString = &varXAnimNotifyInfo->name;
    Mark_ScriptString();
}

void __cdecl Mark_XAnimNotifyInfoArray(int32_t count)
{
    XAnimNotifyInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXAnimNotifyInfo;
    for (i = 0; i < count; ++i)
    {
        varXAnimNotifyInfo = var;
        Mark_XAnimNotifyInfo();
        ++var;
    }
}

void __cdecl Mark_XAnimParts()
{
    if (varXAnimParts->names)
    {
        varScriptString = varXAnimParts->names;
        Mark_ScriptStringArray(varXAnimParts->boneCount[9]);
    }
    if (varXAnimParts->notify)
    {
        varXAnimNotifyInfo = varXAnimParts->notify;
        Mark_XAnimNotifyInfoArray(varXAnimParts->notifyCount);
    }
}

void __cdecl Mark_XAnimPartsPtr()
{
    if (*varXAnimPartsPtr)
    {
        varXAnimParts = *varXAnimPartsPtr;
        Mark_XAnimPartsAsset(varXAnimParts);
        Mark_XAnimParts();
    }
}

void __cdecl Load_XBoneInfoArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varXBoneInfo, count, 40);
}

void __cdecl Load_DObjAnimMatArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varDObjAnimMat, count, 32);
}

void __cdecl Load_StreamFileNameRaw(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varStreamFileNameRaw, 8);
    varXString = &varStreamFileNameRaw->dir;
    Load_XString(0);
    varXString = &varStreamFileNameRaw->name;
    Load_XString(0);
}

void __cdecl Load_StreamFileInfo(bool atStreamStart)
{
    varStreamFileNameRaw = &varStreamFileInfo->raw;
    Load_StreamFileNameRaw(atStreamStart);
}

void __cdecl Load_StreamFileName(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varStreamFileName, 8);
    varStreamFileInfo = &varStreamFileName->info;
    Load_StreamFileInfo(0);
}

void __cdecl Load_SetSoundData(uint8_t **data, MssSoundCOD4 *mssSound)
{
    SND_SetData(mssSound, *data);
}

void __cdecl Load_MssSound(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]
    uint8_t *sharedData = nullptr;
    uint32_t sharedDataSize = 0;

    Load_Stream(atStreamStart, (unsigned char*)varMssSound, 40);
    DB_PushStreamPos(0);
    if (varMssSound->data)
    {
        value = (uint32_t)varMssSound->data;
        if (value < 0xFFFFFFFE)
        {
            // Alias raw zone bytes, not another LoadedSound's MSS allocation;
            // every LoadedSound owns and frees its processed playback buffer.
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varMssSound->data,
                DBAliasKind::SoundData,
                varMssSound->info.data_len);
            Load_SetSoundData(&varMssSound->data, varMssSound);
        }
        else
        {
            varMssSound->data = AllocLoad_raw_byte();
            sharedData = varMssSound->data;
            sharedDataSize = varMssSound->info.data_len;
            varbyte = varMssSound->data;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::SoundData);
            else
                inserted = {};
            Load_byteArray(1, varMssSound->info.data_len);
            Load_SetSoundData(&varMssSound->data, varMssSound);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::SoundData,
                    sharedData,
                    sharedDataSize);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_LoadedSound(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varLoadedSound, 44);
    DB_PushStreamPos(4);
    varXString = &varLoadedSound->name;
    Load_XString(0);
    varMssSound = &varLoadedSound->sound;
    Load_MssSound(0);
    DB_PopStreamPos();
}

void __cdecl Load_LoadedSoundPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varLoadedSoundPtr, 4);
    DB_PushStreamPos(0);
    if (*varLoadedSoundPtr)
    {
        value = (uint32_t)*varLoadedSoundPtr;
        if (value == -1 || value == -2)
        {
            *varLoadedSoundPtr = (LoadedSound *)AllocLoad_FxElemVisStateSample();
            varLoadedSound = *varLoadedSoundPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::LoadedSound);
            else
                inserted = {};
            Load_LoadedSound(1);
            Load_LoadedSoundAsset((XAssetHeader *)varLoadedSoundPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::LoadedSound,
                    *varLoadedSoundPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varLoadedSoundPtr,
                DBAliasKind::LoadedSound);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_StreamedSound(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varStreamedSound, 8);
    varStreamFileName = &varStreamedSound->filename;
    Load_StreamFileName(0);
}

void __cdecl Load_SoundFileRef(bool atStreamStart)
{
    if (varSoundFile->type == 1)
    {
        varLoadedSoundPtr = &varSoundFileRef->loadSnd;
        Load_LoadedSoundPtr(atStreamStart);
    }
    else
    {
        varStreamedSound = (StreamedSound *)varSoundFileRef;
        Load_StreamedSound(atStreamStart);
    }
}

bool __cdecl Load_SoundFile(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        &varSoundFile->type,
        disk32::kSoundFileBytes);
    if (!db::validation::SoundFileHeaderValid(
            varSoundFile->type,
            varSoundFile->exists))
    {
        Com_Error(ERR_DROP, "Invalid fast-file sound-file header");
        return false;
    }
    varSoundFileRef = &varSoundFile->u;
    Load_SoundFileRef(0);
    return true;
}

bool __cdecl Load_SndCurve(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varSndCurve, 72);
    DB_PushStreamPos(4);
    varXString = &varSndCurve->filename;
    Load_XString(0);
    if (!varSndCurve->filename
        || !db::validation::CountInRange(varSndCurve->knotCount, 2, 8)
        || !db::validation::NormalizedGraphKnots(
            varSndCurve->knots,
            static_cast<uint32_t>(varSndCurve->knotCount)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file sound falloff curve");
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_SndCurvePtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varSndCurvePtr, 4);
    DB_PushStreamPos(0);
    if (*varSndCurvePtr)
    {
        value = (uint32_t)*varSndCurvePtr;
        if (value == -1 || value == -2)
        {
            *varSndCurvePtr = (SndCurve *)AllocLoad_FxElemVisStateSample();
            varSndCurve = *varSndCurvePtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::SndCurve);
            else
                inserted = {};
            if (!Load_SndCurve(1))
            {
                DB_PopStreamPos();
                return;
            }
            Load_SndCurveAsset((XAssetHeader *)varSndCurvePtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::SndCurve,
                    *varSndCurvePtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varSndCurvePtr,
                DBAliasKind::SndCurve);
        }
    }
    DB_PopStreamPos();
}

bool __cdecl Load_SpeakerMap(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varSpeakerMap,
        disk32::kSpeakerMapBytes);
    varXString = &varSpeakerMap->name;
    Load_XString(0);
    if (!DB_ValidateSpeakerMap(varSpeakerMap))
        return false;
    return true;
}

bool __cdecl Load_snd_alias_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varsnd_alias_t,
        disk32::kSndAliasBytes);
    varXString = &varsnd_alias_t->aliasName;
    Load_XString(0);
    varXString = &varsnd_alias_t->subtitle;
    Load_XString(0);
    varXString = &varsnd_alias_t->secondaryAliasName;
    Load_XString(0);
    varXString = &varsnd_alias_t->chainAliasName;
    Load_XString(0);
    if (varsnd_alias_t->soundFile)
    {
        if (varsnd_alias_t->soundFile == (SoundFile *)-1)
        {
            varsnd_alias_t->soundFile = (SoundFile *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile);
            if (!completed)
                return false;
            varSoundFile = varsnd_alias_t->soundFile;
            if (!Load_SoundFile(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::SoundFile,
                    varsnd_alias_t->soundFile,
                    disk32::kSoundFileBytes,
                    disk32::kSoundFileBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile,
                disk32::kSoundFileBytes);
        }
    }
    varSndCurvePtr = &varsnd_alias_t->volumeFalloffCurve;
    Load_SndCurvePtr(0);
    if (varsnd_alias_t->speakerMap)
    {
        if (varsnd_alias_t->speakerMap == (SpeakerMap *)-1)
        {
            varsnd_alias_t->speakerMap = (SpeakerMap *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap);
            if (!completed)
                return false;
            varSpeakerMap = varsnd_alias_t->speakerMap;
            if (!Load_SpeakerMap(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::SpeakerMap,
                    varsnd_alias_t->speakerMap,
                    disk32::kSpeakerMapBytes,
                    disk32::kSpeakerMapBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap,
                disk32::kSpeakerMapBytes);
        }
    }
    if (!DB_ValidateSoundAlias(varsnd_alias_t))
        return false;
    return true;
}

bool __cdecl Load_snd_alias_tArray(bool atStreamStart, int32_t count)
{
    snd_alias_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varsnd_alias_t,
        count,
        disk32::kSndAliasBytes);
    var = varsnd_alias_t;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_t = var;
        if (!Load_snd_alias_t(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_snd_alias_list_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_list_t, 12);
    const uint32_t aliasByteCount = DB_CheckedDirectSpanBytes(
        varsnd_alias_list_t->count,
        disk32::kSndAliasBytes,
        "sound-alias entries");
    if (!DB_ValidatePointerCount(
            varsnd_alias_list_t->head,
            varsnd_alias_list_t->count,
            "sound-alias entries"))
    {
        return;
    }
    if (varsnd_alias_list_t->count == 0 && varsnd_alias_list_t->head)
    {
        Com_Error(ERR_DROP, "Invalid present-empty fast-file sound-alias list");
        return;
    }
    DB_PushStreamPos(4);
    varXString = &varsnd_alias_list_t->aliasName;
    Load_XString(0);
    if (!varsnd_alias_list_t->aliasName || !*varsnd_alias_list_t->aliasName)
    {
        Com_Error(ERR_DROP, "Fast-file sound-alias list has no name");
        DB_PopStreamPos();
        return;
    }
    if (varsnd_alias_list_t->head)
    {
        if (varsnd_alias_list_t->head == (snd_alias_t *)-1)
        {
            varsnd_alias_list_t->head = (snd_alias_t *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray);
            if (!completed)
            {
                DB_PopStreamPos();
                return;
            }
            varsnd_alias_t = varsnd_alias_list_t->head;
            if (!Load_snd_alias_tArray(1, varsnd_alias_list_t->count))
            {
                DB_PopStreamPos();
                return;
            }
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::SndAliasArray,
                    varsnd_alias_list_t->head,
                    aliasByteCount,
                    aliasByteCount))
            {
                DB_PopStreamPos();
                return;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray,
                aliasByteCount);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_snd_alias_list_ptr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (unsigned char*)varsnd_alias_list_ptr, 4);
    DB_PushStreamPos(0);
    if (*varsnd_alias_list_ptr)
    {
        value = (uint32_t)*varsnd_alias_list_ptr;
        if (value == -1 || value == -2)
        {
            *varsnd_alias_list_ptr = (snd_alias_list_t*)AllocLoad_FxElemVisStateSample();
            varsnd_alias_list_t = *varsnd_alias_list_ptr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::SndAliasList);
            else
                inserted = {};
            Load_snd_alias_list_t(1);
            Load_snd_alias_list_Asset((XAssetHeader*)varsnd_alias_list_ptr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::SndAliasList,
                    *varsnd_alias_list_ptr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varsnd_alias_list_ptr,
                DBAliasKind::SndAliasList);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_SndAliasCustom(snd_alias_list_t **var)
{
    if (*var)
    {
        varXStringPtr = (const char ***)var;
        Load_XStringPtr(0);
        if (!*varXStringPtr || !**varXStringPtr || !***varXStringPtr)
        {
            Com_Error(ERR_DROP, "Fast-file sound alias reference has no name");
            return;
        }
        *(XAssetHeader *)var = DB_FindXAssetHeader(ASSET_TYPE_SOUND, **varXStringPtr);
    }
}

void __cdecl Load_snd_alias_list_name(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_list_name, 4);
    Load_SndAliasCustom(varsnd_alias_list_name);
}

void __cdecl Load_snd_alias_list_nameArray(bool atStreamStart, int32_t count)
{
    snd_alias_list_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varsnd_alias_list_name, count, 4);
    var = varsnd_alias_list_name;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_list_name = var;
        Load_snd_alias_list_name(0);
        ++var;
    }
}

void __cdecl Mark_LoadedSoundPtr()
{
    if (*varLoadedSoundPtr)
    {
        varLoadedSound = *varLoadedSoundPtr;
        Mark_LoadedSoundAsset(varLoadedSound);
    }
}

void __cdecl Mark_SoundFileRef()
{
    if (varSoundFile->type == 1)
    {
        varLoadedSoundPtr = &varSoundFileRef->loadSnd;
        Mark_LoadedSoundPtr();
    }
}

void __cdecl Mark_SoundFile()
{
    varSoundFileRef = &varSoundFile->u;
    Mark_SoundFileRef();
}

void __cdecl Mark_SndCurvePtr()
{
    if (*varSndCurvePtr)
    {
        varSndCurve = *varSndCurvePtr;
        Mark_SndCurveAsset(varSndCurve);
    }
}

void __cdecl Mark_snd_alias_t()
{
    if (varsnd_alias_t->soundFile)
    {
        varSoundFile = varsnd_alias_t->soundFile;
        Mark_SoundFile();
    }
    varSndCurvePtr = &varsnd_alias_t->volumeFalloffCurve;
    Mark_SndCurvePtr();
}

void __cdecl Mark_snd_alias_tArray(int32_t count)
{
    snd_alias_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varsnd_alias_t;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_t = var;
        Mark_snd_alias_t();
        ++var;
    }
}

void __cdecl Mark_snd_alias_list_t()
{
    if (varsnd_alias_list_t->head)
    {
        varsnd_alias_t = varsnd_alias_list_t->head;
        Mark_snd_alias_tArray(varsnd_alias_list_t->count);
    }
}

void __cdecl Mark_snd_alias_list_ptr()
{
    if (*varsnd_alias_list_ptr)
    {
        varsnd_alias_list_t = *varsnd_alias_list_ptr;
        Mark_snd_alias_list_Asset(varsnd_alias_list_t);
        Mark_snd_alias_list_t();
    }
}

void __cdecl Mark_snd_alias_list_name()
{
    Mark_SndAliasCustom(varsnd_alias_list_name);
}

void __cdecl Mark_snd_alias_list_nameArray(int32_t count)
{
    snd_alias_list_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varsnd_alias_list_name;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_list_name = var;
        Mark_snd_alias_list_name();
        ++var;
    }
}

void __cdecl Load_MaterialInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialInfo, 24);
    varXString = &varMaterialInfo->name;
    Load_XString(0);
}

void __cdecl Load_GfxWorldVertex0Array(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxWorldVertex0, count, 44);
}

void __cdecl Load_GfxPackedVertex0Array(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxPackedVertex0, count, 32);
}

void __cdecl Load_GfxBrushModelArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxBrushModel, count, 56);
}

void __cdecl Load_XSurfaceCollisionLeafArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varXSurfaceCollisionLeaf,
        count,
        disk32::kXSurfaceCollisionLeafBytes);
}

cbrush_t *__cdecl AllocLoad_GfxPackedVertex0()
{
    return (cbrush_t *)DB_AllocStreamPos(15);
}

void __cdecl Load_XSurfaceCollisionNodeArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varXSurfaceCollisionNode,
        count,
        disk32::kXSurfaceCollisionNodeBytes);
}

bool __cdecl Load_XSurfaceCollisionTree(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varXSurfaceCollisionTree,
        disk32::kXSurfaceCollisionTreeBytes);
    uint32_t nodeBytes = 0;
    uint32_t leafBytes = 0;
    if (!DB_GetXSurfaceCollisionTreeExtents(
            varXSurfaceCollisionTree,
            &nodeBytes,
            &leafBytes))
    {
        return false;
    }

    varXSurfaceCollisionTree->nodes =
        (XSurfaceCollisionNode *)AllocLoad_GfxPackedVertex0();
    if (!varXSurfaceCollisionTree->nodes
        || !DB_IsStreamRangeValid(
            varXSurfaceCollisionTree->nodes,
            nodeBytes))
    {
        Com_Error(ERR_DROP, "Fast-file surface collision nodes exceed block 4");
        return false;
    }
    varXSurfaceCollisionNode = varXSurfaceCollisionTree->nodes;
    Load_XSurfaceCollisionNodeArray(
        1,
        static_cast<int32_t>(varXSurfaceCollisionTree->nodeCount));

    varXSurfaceCollisionTree->leafs =
        (XSurfaceCollisionLeaf *)AllocLoad_XBlendInfo();
    if (!varXSurfaceCollisionTree->leafs
        || !DB_IsStreamRangeValid(
            varXSurfaceCollisionTree->leafs,
            leafBytes))
    {
        Com_Error(ERR_DROP, "Fast-file surface collision leaves exceed block 4");
        return false;
    }
    varXSurfaceCollisionLeaf = varXSurfaceCollisionTree->leafs;
    Load_XSurfaceCollisionLeafArray(
        1,
        static_cast<int32_t>(varXSurfaceCollisionTree->leafCount));

    return DB_ValidateXSurfaceCollisionTreeGraph(
        varXSurfaceCollisionTree,
        nodeBytes,
        leafBytes);
}

bool __cdecl Load_XRigidVertList(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varXRigidVertList,
        disk32::kXRigidVertListBytes);
    if (varXRigidVertList->collisionTree)
    {
        if (varXRigidVertList->collisionTree == (XSurfaceCollisionTree *)-1)
        {
            varXRigidVertList->collisionTree = (XSurfaceCollisionTree *)AllocLoad_FxElemVisStateSample();
            if (!varXRigidVertList->collisionTree)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file surface collision tree");
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree);
            if (!completed)
                return false;
            varXSurfaceCollisionTree = varXRigidVertList->collisionTree;
            if (!Load_XSurfaceCollisionTree(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::XSurfaceCollisionTree,
                    varXRigidVertList->collisionTree,
                    disk32::kXSurfaceCollisionTreeBytes,
                    disk32::kXSurfaceCollisionTreeBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree,
                disk32::kXSurfaceCollisionTreeBytes);
        }
    }
    return true;
}

bool __cdecl Load_XRigidVertListArray(bool atStreamStart, int32_t count)
{
    XRigidVertList *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    int32_t rigidListBytes = 0;
    if (!db::validation::CheckedArrayBytes(
            count,
            disk32::kXRigidVertListBytes,
            &rigidListBytes)
        || !DB_IsStreamRangeValid(
            varXRigidVertList,
            static_cast<uint32_t>(rigidListBytes)))
    {
        Com_Error(ERR_DROP, "Fast-file rigid-vertex lists exceed block 4");
        return false;
    }
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varXRigidVertList,
        count,
        disk32::kXRigidVertListBytes);
    var = varXRigidVertList;
    for (i = 0; i < count; ++i)
    {
        varXRigidVertList = var;
        if (!Load_XRigidVertList(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_GfxVertexBuffer(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxVertexBuffer, 4);
}

void __cdecl Load_XBlendInfoArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varXBlendInfo, count, 2);
}

void __cdecl Load_XSurfaceVertexInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXSurfaceVertexInfo, 12);
    const int32_t blend7 = DB_CheckedCountProduct(
        7, varXSurfaceVertexInfo->vertCount[3], "surface blend weights");
    const int32_t blend5 = DB_CheckedCountProduct(
        5, varXSurfaceVertexInfo->vertCount[2], "surface blend weights");
    const int32_t blend3 = DB_CheckedCountProduct(
        3, varXSurfaceVertexInfo->vertCount[1], "surface blend weights");
    const int32_t blendHigh = DB_CheckedCountSum(
        blend7, blend5, "surface blend weights");
    const int32_t blendNonRigid = DB_CheckedCountSum(
        blendHigh, blend3, "surface blend weights");
    const int32_t blendCount = DB_CheckedCountSum(
        blendNonRigid,
        varXSurfaceVertexInfo->vertCount[0],
        "surface blend weights");
    const uint32_t blendByteCount = DB_CheckedDirectSpanBytes(
        blendCount,
        2,
        "surface blend weights");
    if (varXSurfaceVertexInfo->vertsBlend)
    {
        if (varXSurfaceVertexInfo->vertsBlend == (uint16_t *)-1)
        {
            varXSurfaceVertexInfo->vertsBlend = (uint16_t *)AllocLoad_XBlendInfo();
            varXBlendInfo = varXSurfaceVertexInfo->vertsBlend;
            Load_XBlendInfoArray(1, blendCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXSurfaceVertexInfo->vertsBlend,
                blendByteCount,
                2,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_r_index_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varr_index_t, count, 2);
}

void __cdecl Load_r_index16_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varr_index16_t, count, 2);
}

void __cdecl Load_XZoneHandle(bool atStreamStart)
{
    Load_Stream(atStreamStart, varXZoneHandle, 1);
    varbyte = varXZoneHandle;
    Load_byte(0);
    Load_GetCurrentZoneHandle(varXZoneHandle);
}

bool __cdecl Load_XSurface(bool atStreamStart)
{
    Load_Stream(atStreamStart, (unsigned char*)varXSurface, 56);
    uint8_t deformed = 0;
    uint32_t rigidListBytes = 0;
    if (!DB_GetXSurfaceLayout(
            varXSurface,
            &deformed,
            &rigidListBytes))
    {
        return false;
    }
    const uint32_t vertexByteCount = DB_CheckedDirectSpanBytes(
        varXSurface->vertCount,
        32,
        "surface vertices");
    const int32_t indexCount = DB_CheckedCountProduct(
        3,
        varXSurface->triCount,
        "surface triangle indices");
    const uint32_t indexByteCount = DB_CheckedDirectSpanBytes(
        indexCount,
        2,
        "surface triangle index bytes");
    varXZoneHandle = &varXSurface->zoneHandle;
    Load_XZoneHandle(0);
    varXSurfaceVertexInfo = &varXSurface->vertInfo;
    Load_XSurfaceVertexInfo(0);
    DB_PushStreamPos(7);
    if (varXSurface->verts0)
    {
        if (varXSurface->verts0 == (GfxPackedVertex *)-1)
        {
            varXSurface->verts0 = (GfxPackedVertex *)AllocLoad_GfxPackedVertex0();
            varGfxPackedVertex0 = varXSurface->verts0;
            Load_GfxPackedVertex0Array(1, varXSurface->vertCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXSurface->verts0,
                vertexByteCount,
                16,
                kDirectBlock7);
        }
    }
    DB_PopStreamPos();
    DBAliasHandle completedRigidLists;
    if (varXSurface->vertList)
    {
        if (varXSurface->vertList == (XRigidVertList *)-1)
        {
            varXSurface->vertList = (XRigidVertList *)AllocLoad_FxElemVisStateSample();
            if (!varXSurface->vertList)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file rigid-vertex lists");
                return false;
            }
            completedRigidLists = DB_RegisterPointerSlot(
                varXSurface->vertList,
                DBAliasKind::XRigidVertListArray);
            if (!completedRigidLists)
                return false;
            varXRigidVertList = varXSurface->vertList;
            if (!Load_XRigidVertListArray(
                    1,
                    static_cast<int32_t>(varXSurface->vertListCount)))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varXSurface->vertList,
                DBAliasKind::XRigidVertListArray,
                rigidListBytes);
        }
    }
    DB_PushStreamPos(8);
    if (varXSurface->triIndices)
    {
        if (varXSurface->triIndices == (uint16_t *)-1)
        {
            varXSurface->triIndices = (uint16_t *)AllocLoad_GfxPackedVertex0();
            varr_index16_t = varXSurface->triIndices;
            Load_r_index16_tArray(1, indexCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXSurface->triIndices,
                indexByteCount,
                16,
                kDirectBlock8);
        }
    }
    DB_PopStreamPos();
    if (!DB_ValidateLoadedXSurface(
            varXSurface,
            deformed,
            varXModel ? varXModel->numBones : 0))
        return false;
    if (completedRigidLists
        && !DB_CompleteObject(
            completedRigidLists,
            DBAliasKind::XRigidVertListArray,
            varXSurface->vertList,
            rigidListBytes,
            rigidListBytes))
    {
        return false;
    }
    return true;
}

bool __cdecl Load_XSurfaceArray(bool atStreamStart, int32_t count)
{
    XSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, &varXSurface->tileMode, count, 56);
    var = varXSurface;
    for (i = 0; i < count; ++i)
    {
        varXSurface = var;
        if (!Load_XSurface(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_GfxTextureLoad(bool atStreamStart)
{
    DBAliasHandle inserted;
    IDirect3DBaseTexture9 *value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (unsigned char*)varGfxTextureLoad, 4);
    DB_PushStreamPos(0);
    if (varGfxTextureLoad->basemap)
    {
        // LWSS: union abuse here
        value = varGfxTextureLoad->basemap;
        if (value == (IDirect3DBaseTexture9 *)-1 || value == (IDirect3DBaseTexture9*)-2)
        {
            varGfxTextureLoad->basemap = (IDirect3DBaseTexture9*)AllocLoad_FxElemVisStateSample();
            varGfxImageLoadDef = varGfxTextureLoad->loadDef;
            if (value == (IDirect3DBaseTexture9*)-2)
                inserted = DB_InsertPointer(DBAliasKind::GfxTexture);
            else
                inserted = {};
            Load_GfxImageLoadDef(1);
            Load_Texture(varGfxTextureLoad, varGfxImage);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GfxTexture,
                    varGfxTextureLoad->basemap,
                    static_cast<uint32_t>(varGfxImage->mapType));
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varGfxTextureLoad,
                DBAliasKind::GfxTexture,
                static_cast<uint32_t>(varGfxImage->mapType));
            if (varGfxTextureLoad->basemap)
            {
                // Image_Release drops one COM reference for every GfxImage.
                varGfxTextureLoad->basemap->AddRef();
            }
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxRawTextureArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxRawTexture, count, 4);
}

void __cdecl Load_GfxImageLoadDef(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../gfx_d3d/r_image_load_db.h", 2614, 0, "%s", "atStreamStart");
    iassert(OFFSET_TO_GfxImageLoadDef_DATA == 16);
    Load_Stream(1, (unsigned char*)varGfxImageLoadDef, 16);
    if (DB_GetStreamPos() != varGfxImageLoadDef->data)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../gfx_d3d/r_image_load_db.h",
            2616,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( varGfxImageLoadDef->data )");
    varbyte = &varGfxImageLoadDef->data[0];
    Load_byteArray(1, varGfxImageLoadDef->resourceSize);
}

void __cdecl Load_GfxImage(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxImage, 36);
    DB_PushStreamPos(4);
    varXString = &varGfxImage->name;
    Load_XString(0);
    varGfxTextureLoad = &varGfxImage->texture;
    Load_GfxTextureLoad(0);
    DB_PopStreamPos();
}

void __cdecl Load_GfxImagePtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxImagePtr, 4);
    DB_PushStreamPos(0);
    if (*varGfxImagePtr)
    {
        value = (uint32_t)*varGfxImagePtr;
        if (value == -1 || value == -2)
        {
            *varGfxImagePtr = (GfxImage *)AllocLoad_FxElemVisStateSample();
            varGfxImage = *varGfxImagePtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::GfxImage);
            else
                inserted = {};
            Load_GfxImage(1);
            Load_GfxImageAsset((XAssetHeader *)varGfxImagePtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GfxImage,
                    *varGfxImagePtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varGfxImagePtr,
                DBAliasKind::GfxImage);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_GfxImagePtr()
{
    if (*varGfxImagePtr)
    {
        varGfxImage = *varGfxImagePtr;
        Mark_GfxImageAsset(varGfxImage);
    }
}

bool __cdecl Load_water_t(bool atStreamStart)
{
    if (!atStreamStart)
    {
        Com_Error(ERR_DROP, "Invalid fast-file material water stream start");
        return false;
    }
    Load_Stream(
        1,
        (uint8_t *)varwater_t,
        disk32::kMaterialWaterBytes);
    varwater_t->writable.floatTime = kWaterInitialTime;
    int32_t sampleCount = 0;
    if (!DB_ValidateWaterHeader(varwater_t, &sampleCount))
        return false;

    varwater_t->H0 = (complex_s *)AllocLoad_FxElemVisStateSample();
    if (!varwater_t->H0)
    {
        Com_Error(ERR_DROP, "Could not allocate material water amplitudes");
        return false;
    }
    varcomplex_t = varwater_t->H0;
    Load_complex_tArray(1, sampleCount);

    varwater_t->wTerm = (float *)AllocLoad_FxElemVisStateSample();
    if (!varwater_t->wTerm)
    {
        Com_Error(ERR_DROP, "Could not allocate material water frequencies");
        return false;
    }
    varfloat = varwater_t->wTerm;
    Load_floatArray(1, sampleCount);

    uint32_t amplitudeBytes = 0;
    uint32_t frequencyBytes = 0;
    if (!db::validation::CheckedSpanBytes(
            static_cast<uint32_t>(sampleCount),
            static_cast<uint32_t>(sizeof(complex_s)),
            &amplitudeBytes)
        || !db::validation::CheckedSpanBytes(
            static_cast<uint32_t>(sampleCount),
            static_cast<uint32_t>(sizeof(float)),
            &frequencyBytes)
        || DB_ValidateStreamAddress(
            varwater_t->H0,
            amplitudeBytes,
            alignof(complex_s),
            kDirectBlock4) != db::relocation::Status::Ok
        || DB_ValidateStreamAddress(
            varwater_t->wTerm,
            frequencyBytes,
            alignof(float),
            kDirectBlock4) != db::relocation::Status::Ok
        || !db::validation::FiniteComplexArray(
            varwater_t->H0,
            static_cast<uint32_t>(sampleCount))
        || !db::validation::FiniteNonnegativeFloatArray(
            varwater_t->wTerm,
            static_cast<uint32_t>(sampleCount)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material water frequency data");
        return false;
    }
    varGfxImagePtr = &varwater_t->image;
    Load_GfxImagePtr(0);
    if (!varwater_t->image)
    {
        Com_Error(ERR_DROP, "Fast-file material water has no completed image");
        return false;
    }
    return true;
}

void __cdecl Mark_water_t()
{
    varGfxImagePtr = &varwater_t->image;
    Mark_GfxImagePtr();
}

void __cdecl Load_DWORDArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varDWORD, count, 4);
}

bool __cdecl Load_GfxVertexShaderLoadDef(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxVertexShaderLoadDef,
        disk32::kMaterialShaderLoadDefBytes);
    if (!DB_ValidateMaterialShaderLoadDef(
            varGfxVertexShaderLoadDef->program,
            varGfxVertexShaderLoadDef->programSize,
            varGfxVertexShaderLoadDef->loadForRenderer,
            "vertex shader"))
    {
        return false;
    }

    varGfxVertexShaderLoadDef->program = (uint32_t *)AllocLoad_FxElemVisStateSample();
    if (!varGfxVertexShaderLoadDef->program)
        return false;
    varDWORD = static_cast<uint32_t *>(varGfxVertexShaderLoadDef->program);
    Load_DWORDArray(1, varGfxVertexShaderLoadDef->programSize);
    return DB_ValidateMaterialShaderProgram(
        varGfxVertexShaderLoadDef->program,
        varGfxVertexShaderLoadDef->programSize,
        db::validation::D3D9ShaderStage::Vertex,
        varGfxVertexShaderLoadDef->loadForRenderer,
        "vertex shader");
}

bool __cdecl Load_GfxPixelShaderLoadDef(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxPixelShaderLoadDef,
        disk32::kMaterialShaderLoadDefBytes);
    if (!DB_ValidateMaterialShaderLoadDef(
            varGfxPixelShaderLoadDef->program,
            varGfxPixelShaderLoadDef->programSize,
            varGfxPixelShaderLoadDef->loadForRenderer,
            "pixel shader"))
    {
        return false;
    }

    varGfxPixelShaderLoadDef->program = (uint32_t *)AllocLoad_FxElemVisStateSample();
    if (!varGfxPixelShaderLoadDef->program)
        return false;
    varDWORD = static_cast<uint32_t *>(varGfxPixelShaderLoadDef->program);
    Load_DWORDArray(1, varGfxPixelShaderLoadDef->programSize);
    return DB_ValidateMaterialShaderProgram(
        varGfxPixelShaderLoadDef->program,
        varGfxPixelShaderLoadDef->programSize,
        db::validation::D3D9ShaderStage::Pixel,
        varGfxPixelShaderLoadDef->loadForRenderer,
        "pixel shader");
}

bool __cdecl Load_MaterialVertexShaderProgram(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialVertexShaderProgram,
        disk32::kMaterialShaderProgramBytes);
    varGfxVertexShaderLoadDef = &varMaterialVertexShaderProgram->loadDef;
    if (!Load_GfxVertexShaderLoadDef(0))
        return false;
    return Load_CreateMaterialVertexShader(
        &varMaterialVertexShaderProgram->loadDef,
        varMaterialVertexShader);
}

bool __cdecl Load_MaterialPixelShaderProgram(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialPixelShaderProgram,
        disk32::kMaterialShaderProgramBytes);
    varGfxPixelShaderLoadDef = &varMaterialPixelShaderProgram->loadDef;
    if (!Load_GfxPixelShaderLoadDef(0))
        return false;
    return Load_CreateMaterialPixelShader(
        &varMaterialPixelShaderProgram->loadDef,
        varMaterialPixelShader);
}

bool __cdecl Load_MaterialVertexShader(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialVertexShader,
        disk32::kMaterialVertexShaderBytes);
    varMaterialVertexShader->prog.vs = nullptr;
    if (!varMaterialVertexShader->name)
    {
        Com_Error(ERR_DROP, "Fast-file vertex shader has no name");
        return false;
    }
    varXString = &varMaterialVertexShader->name;
    Load_XString(0);
    if (!varMaterialVertexShader->name || !*varMaterialVertexShader->name)
    {
        Com_Error(ERR_DROP, "Fast-file vertex shader has no completed name");
        return false;
    }
    varMaterialVertexShaderProgram = &varMaterialVertexShader->prog;
    return Load_MaterialVertexShaderProgram(0);
}

bool __cdecl Load_MaterialVertexShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialVertexShaderPtr, 4);
    if (*varMaterialVertexShaderPtr)
    {
        if (*varMaterialVertexShaderPtr == (MaterialVertexShader *)-1)
        {
            *varMaterialVertexShaderPtr = (MaterialVertexShader *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader);
            if (!completed)
                return false;
            varMaterialVertexShader = *varMaterialVertexShaderPtr;
            if (!Load_MaterialVertexShader(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexShader,
                    *varMaterialVertexShaderPtr,
                    disk32::kMaterialVertexShaderBytes,
                    disk32::kMaterialVertexShaderBytes))
            {
                if ((*varMaterialVertexShaderPtr)->prog.vs)
                {
                    (*varMaterialVertexShaderPtr)->prog.vs->Release();
                    (*varMaterialVertexShaderPtr)->prog.vs = nullptr;
                }
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader,
                disk32::kMaterialVertexShaderBytes);
        }
    }
    return true;
}

bool __cdecl Load_MaterialPixelShader(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialPixelShader,
        disk32::kMaterialPixelShaderBytes);
    varMaterialPixelShader->prog.ps = nullptr;
    if (!varMaterialPixelShader->name)
    {
        Com_Error(ERR_DROP, "Fast-file pixel shader has no name");
        return false;
    }
    varXString = &varMaterialPixelShader->name;
    Load_XString(0);
    if (!varMaterialPixelShader->name || !*varMaterialPixelShader->name)
    {
        Com_Error(ERR_DROP, "Fast-file pixel shader has no completed name");
        return false;
    }
    varMaterialPixelShaderProgram = &varMaterialPixelShader->prog;
    return Load_MaterialPixelShaderProgram(0);
}

bool __cdecl Load_MaterialPixelShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialPixelShaderPtr, 4);
    if (*varMaterialPixelShaderPtr)
    {
        if (*varMaterialPixelShaderPtr == (MaterialPixelShader *)-1)
        {
            *varMaterialPixelShaderPtr = (MaterialPixelShader *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader);
            if (!completed)
                return false;
            varMaterialPixelShader = *varMaterialPixelShaderPtr;
            if (!Load_MaterialPixelShader(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialPixelShader,
                    *varMaterialPixelShaderPtr,
                    disk32::kMaterialPixelShaderBytes,
                    disk32::kMaterialPixelShaderBytes))
            {
                if ((*varMaterialPixelShaderPtr)->prog.ps)
                {
                    (*varMaterialPixelShaderPtr)->prog.ps->Release();
                    (*varMaterialPixelShaderPtr)->prog.ps = nullptr;
                }
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader,
                disk32::kMaterialPixelShaderBytes);
        }
    }
    return true;
}

bool __cdecl Load_MaterialVertexDeclaration(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        &varMaterialVertexDeclaration->streamCount,
        disk32::kMaterialVertexDeclarationBytes);
    if (!DB_ValidateMaterialVertexDeclaration(varMaterialVertexDeclaration))
        return false;
    varMaterialVertexDeclaration->hasOptionalSource = false;
    varMaterialVertexDeclaration->isLoaded = false;
    for (uint32_t index = 0; index < varMaterialVertexDeclaration->streamCount; ++index)
    {
        if (varMaterialVertexDeclaration->routing.data[index].source >= 5)
        {
            varMaterialVertexDeclaration->hasOptionalSource = true;
            break;
        }
    }
    return true;
}

void __cdecl Load_MaterialArgumentCodeConst(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialArgumentCodeConst, 4);
}

void __cdecl Load_MaterialArgumentDef(bool atStreamStart)
{
    switch (varMaterialShaderArgument->type)
    {
    case 1u:
    case 7u:
        if (!varMaterialArgumentDef->literalConst)
        {
            Com_Error(ERR_DROP, "Fast-file literal shader constant has no value");
            return;
        }
        if (varMaterialArgumentDef->literalConst == (const float *)-1)
        {
            varfloat = (float *)AllocLoad_FxElemVisStateSample();
            varMaterialArgumentDef->literalConst = varfloat;
            Load_floatArray(1, 4);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varMaterialArgumentDef->literalConst,
                16,
                4,
                kDirectBlock4);
        }
        break;
    case 3u:
    case 5u:
        if (atStreamStart)
        {
            varMaterialArgumentCodeConst = (MaterialArgumentCodeConst *)varMaterialArgumentDef;
            Load_MaterialArgumentCodeConst(atStreamStart);
        }
        break;
    case 4u:
        if (atStreamStart)
        {
            varuint = varMaterialArgumentDef;
            Load_uint(atStreamStart);
        }
        break;
    default:
        if (atStreamStart)
        {
            varuint = varMaterialArgumentDef;
            Load_uint(atStreamStart);
        }
        break;
    }
}

void __cdecl Load_MaterialShaderArgument(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialShaderArgument, 8);
    varMaterialArgumentDef = &varMaterialShaderArgument->u;
    Load_MaterialArgumentDef(0);
}

void __cdecl Load_MaterialShaderArgumentArray(bool atStreamStart, int32_t count)
{
    MaterialShaderArgument *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varMaterialShaderArgument, count, 8);
    var = varMaterialShaderArgument;
    for (i = 0; i < count; ++i)
    {
        varMaterialShaderArgument = var;
        Load_MaterialShaderArgument(0);
        ++var;
    }
}

void __cdecl Load_GfxStateBitsArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxStateBits, count, 8);
}

bool __cdecl Load_MaterialPass(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (unsigned char*)varMaterialPass,
        disk32::kMaterialPassBytes);
    const uint32_t argumentCount = static_cast<uint32_t>(varMaterialPass->perPrimArgCount)
        + varMaterialPass->perObjArgCount
        + varMaterialPass->stableArgCount;
    if (!varMaterialPass->vertexDecl
        || !varMaterialPass->vertexShader
        || !varMaterialPass->pixelShader
        || !db::validation::MaterialPassLayoutValid(
            varMaterialPass->perPrimArgCount,
            varMaterialPass->perObjArgCount,
            varMaterialPass->stableArgCount,
            varMaterialPass->args != nullptr,
            varMaterialPass->customSamplerFlags))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material pass header");
        return false;
    }
    if (varMaterialPass->vertexDecl)
    {
        if (varMaterialPass->vertexDecl == (MaterialVertexDeclaration*)-1)
        {
            varMaterialPass->vertexDecl = (MaterialVertexDeclaration*)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration);
            if (!completed)
                return false;
            varMaterialVertexDeclaration = varMaterialPass->vertexDecl;
            if (!Load_MaterialVertexDeclaration(1))
                return false;
            Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexDeclaration,
                    varMaterialPass->vertexDecl,
                    disk32::kMaterialVertexDeclarationBytes,
                    disk32::kMaterialVertexDeclarationBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration,
                disk32::kMaterialVertexDeclarationBytes);
        }
    }
    varMaterialVertexShaderPtr = &varMaterialPass->vertexShader;
    if (!Load_MaterialVertexShaderPtr(0))
        return false;
    varMaterialPixelShaderPtr = &varMaterialPass->pixelShader;
    if (!Load_MaterialPixelShaderPtr(0))
        return false;
    if (varMaterialPass->vertexShader->prog.loadDef.loadForRenderer
        != varMaterialPass->pixelShader->prog.loadDef.loadForRenderer)
    {
        Com_Error(ERR_DROP, "Fast-file material pass mixes renderer shader variants");
        return false;
    }
    if (varMaterialPass->args)
    {
        varMaterialPass->args = (MaterialShaderArgument*)AllocLoad_FxElemVisStateSample();
        varMaterialShaderArgument = varMaterialPass->args;
        Load_MaterialShaderArgumentArray(1, static_cast<int32_t>(argumentCount));
        if (!DB_ValidateMaterialPassArguments(varMaterialPass, argumentCount))
            return false;
    }
    return true;
}

bool __cdecl Load_MaterialPassArray(bool atStreamStart, int32_t count)
{
    MaterialPass *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varMaterialPass,
        count,
        disk32::kMaterialPassBytes);
    var = (MaterialPass *)varMaterialPass;
    for (i = 0; i < count; ++i)
    {
        varMaterialPass = (MaterialPass*)&var->vertexDecl;
        if (!Load_MaterialPass(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_MaterialTechnique(bool atStreamStart)
{
    if (!atStreamStart)
    {
        Com_Error(ERR_DROP, "Invalid fast-file material technique stream start");
        return false;
    }
    Load_Stream(
        1,
        (uint8_t *)varMaterialTechnique,
        disk32::kMaterialTechniqueHeaderBytes); // 0x2668
    if (!varMaterialTechnique->name
        || !db::validation::CountInRange(varMaterialTechnique->passCount, 1, 4)
        || (varMaterialTechnique->flags & ~UINT16_C(0x803F))
        || DB_GetStreamPos() != (uint8_t *)varMaterialTechnique->passArray)
    {
        Com_Error(ERR_DROP, "Invalid fast-file material technique header");
        return false;
    }
    varMaterialTechnique->flags &= UINT16_C(0x3F);
    varMaterialPass = (MaterialPass*)&varMaterialTechnique->passArray[0].vertexDecl;
    if (!Load_MaterialPassArray(1, varMaterialTechnique->passCount)) // 0x2990
        return false;
    const uint16_t loadForRenderer =
        varMaterialTechnique->passArray[0].pixelShader->prog.loadDef.loadForRenderer;
    for (uint32_t passIndex = 1;
         passIndex < varMaterialTechnique->passCount;
         ++passIndex)
    {
        const MaterialPass &pass = varMaterialTechnique->passArray[passIndex];
        if (pass.pixelShader->prog.loadDef.loadForRenderer != loadForRenderer)
        {
            Com_Error(
                ERR_DROP,
                "Fast-file material technique mixes renderer shader variants");
            return false;
        }
    }
    varXString = &varMaterialTechnique->name;
    Load_XString(0); // 0x29A1
    if (!varMaterialTechnique->name || !*varMaterialTechnique->name)
    {
        Com_Error(ERR_DROP, "Fast-file material technique has no name");
        return false;
    }
    return true;
}

bool __cdecl Load_MaterialTextureDefInfo(bool atStreamStart)
{
    if (varMaterialTextureDef->semantic == 11)
    {
        if (!varMaterialTextureDefInfo || !*varMaterialTextureDefInfo)
        {
            Com_Error(ERR_DROP, "Fast-file water texture has no definition");
            return false;
        }
        if (*varMaterialTextureDefInfo == (water_t*)-1)
        {
            *varMaterialTextureDefInfo =
                (water_t*)AllocLoad_FxElemVisStateSample();
            if (!*varMaterialTextureDefInfo)
                return false;
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater);
            if (!completed)
                return false;
            varwater_t = *varMaterialTextureDefInfo;
            if (!Load_water_t(1))
                return false;
            if (!Load_PicmipWater(varMaterialTextureDefInfo))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialWater,
                    *varMaterialTextureDefInfo,
                    disk32::kMaterialWaterBytes,
                    disk32::kMaterialWaterBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater,
                disk32::kMaterialWaterBytes);
        }
    }
    else
    {
        varGfxImagePtr = (GfxImage **)varMaterialTextureDefInfo;
        Load_GfxImagePtr(atStreamStart);
        if (!*varGfxImagePtr)
        {
            Com_Error(ERR_DROP, "Fast-file material texture has no image");
            return false;
        }
    }
    return true;
}

bool __cdecl Load_MaterialTextureDef(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialTextureDef,
        disk32::kMaterialTextureDefBytes);
    if (!db::validation::MaterialTextureHeaderValid(
            varMaterialTextureDef->samplerState,
            varMaterialTextureDef->semantic,
            varMaterialTextureDef->u.image != nullptr,
            static_cast<uint8_t>(varMaterialTextureDef->nameStart),
            static_cast<uint8_t>(varMaterialTextureDef->nameEnd)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material texture header");
        return false;
    }
    varMaterialTextureDefInfo = (water_t**)&varMaterialTextureDef->u;
    return Load_MaterialTextureDefInfo(0);
}

bool __cdecl Load_MaterialTextureDefArray(bool atStreamStart, int32_t count)
{
    MaterialTextureDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varMaterialTextureDef,
        count,
        disk32::kMaterialTextureDefBytes);
    var = varMaterialTextureDef;
    for (i = 0; i < count; ++i)
    {
        varMaterialTextureDef = var;
        if (!Load_MaterialTextureDef(0))
            return false;
        if (i && var[-1].nameHash >= var->nameHash)
        {
            Com_Error(ERR_DROP, "Unordered fast-file material texture table");
            return false;
        }
        ++var;
    }
    return true;
}

void __cdecl Load_MaterialConstantDefArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varMaterialConstantDef, count, 32);
}

bool __cdecl Load_MaterialTechniquePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniquePtr, 4);
    if (*varMaterialTechniquePtr)
    {
        if (*varMaterialTechniquePtr == (MaterialTechnique *)-1)
        {
            *varMaterialTechniquePtr = (MaterialTechnique *)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique);
            if (!completed)
                return false;
            varMaterialTechnique = *varMaterialTechniquePtr;
            if (!Load_MaterialTechnique(1))
                return false;
            uint32_t techniqueBytes = 0;
            if (!db::validation::MaterialTechniqueDiskBytes(
                    varMaterialTechnique->passCount,
                    &techniqueBytes)
                || !DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTechnique,
                    *varMaterialTechniquePtr,
                    disk32::kMaterialTechniqueSchema,
                    techniqueBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique,
                disk32::kMaterialTechniqueSchema);
        }
    }
    return true;
}

bool __cdecl Load_MaterialTechniquePtrArray(bool atStreamStart, int32_t count)
{
    MaterialTechnique **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varMaterialTechniquePtr, count, 4);
    var = varMaterialTechniquePtr;
    for (i = 0; i < count; ++i)
    {
        varMaterialTechniquePtr = var;
        if (!Load_MaterialTechniquePtr(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_MaterialTechniqueSet(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniqueSet, 148);
    if (!varMaterialTechniqueSet->name
        || !db::validation::CountInRange(
            varMaterialTechniqueSet->worldVertFormat,
            0,
            11))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material technique set header");
        return false;
    }
    varMaterialTechniqueSet->hasBeenUploaded = false;
    varMaterialTechniqueSet->remappedTechniqueSet = nullptr;
    DB_PushStreamPos(4);
    varXString = &varMaterialTechniqueSet->name;
    Load_XString(0);
    varMaterialTechniquePtr = varMaterialTechniqueSet->techniques;
    if (!Load_MaterialTechniquePtrArray(0, 34))
    {
        DB_PopStreamPos();
        return false;
    }
    int32_t loadForRenderer = -1;
    for (uint32_t techniqueIndex = 0; techniqueIndex < 34; ++techniqueIndex)
    {
        const MaterialTechnique *technique =
            varMaterialTechniqueSet->techniques[techniqueIndex];
        if (!technique)
            continue;
        const uint16_t techniqueRenderer =
            technique->passArray[0].pixelShader->prog.loadDef.loadForRenderer;
        if (loadForRenderer < 0)
        {
            loadForRenderer = techniqueRenderer;
        }
        else if (static_cast<uint32_t>(loadForRenderer) != techniqueRenderer)
        {
            Com_Error(
                ERR_DROP,
                "Fast-file material technique set mixes renderer variants");
            DB_PopStreamPos();
            return false;
        }
    }
    DB_PopStreamPos();
    if (!varMaterialTechniqueSet->name || !*varMaterialTechniqueSet->name)
    {
        Com_Error(ERR_DROP, "Fast-file material technique set has no name");
        return false;
    }
    return true;
}

bool __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniqueSetPtr, 4);
    DB_PushStreamPos(0);
    if (*varMaterialTechniqueSetPtr)
    {
        value = (uint32_t)*varMaterialTechniqueSetPtr;
        if (value == -1 || value == -2)
        {
            *varMaterialTechniqueSetPtr = (MaterialTechniqueSet *)AllocLoad_FxElemVisStateSample();
            varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::MaterialTechniqueSet);
            else
                inserted = {};
            if (!Load_MaterialTechniqueSet(1))
            {
                DB_PopStreamPos();
                return false;
            }
            Load_MaterialTechniqueSetAsset((XAssetHeader *)varMaterialTechniqueSetPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::MaterialTechniqueSet,
                    *varMaterialTechniqueSetPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varMaterialTechniqueSetPtr,
                DBAliasKind::MaterialTechniqueSet);
        }
    }
    DB_PopStreamPos();
    return true;
}

bool __cdecl Load_Material(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterial, 80);
    if (!DB_ValidatePointerCount(
            varMaterial->textureTable,
            varMaterial->textureCount,
            "material textures")
        || !DB_ValidatePointerCount(
            varMaterial->constantTable,
            varMaterial->constantCount,
            "material constants")
        || !DB_ValidatePointerCount(
            varMaterial->stateBitsTable,
            varMaterial->stateBitsCount,
            "material state bits"))
    {
        return false;
    }
    const uint32_t constantByteCount = DB_CheckedDirectSpanBytes(
        varMaterial->constantCount,
        32,
        "material constants");
    const uint32_t stateBitsByteCount = DB_CheckedDirectSpanBytes(
        varMaterial->stateBitsCount,
        8,
        "material state bits");
    uint32_t textureByteCount = 0;
    if (varMaterial->textureCount
        && !db::validation::MaterialTextureTableDiskBytes(
            varMaterial->textureCount,
            &textureByteCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file material texture-table extent");
        return false;
    }
    DB_PushStreamPos(4);
    varMaterialInfo = &varMaterial->info;
    Load_MaterialInfo(0);
    varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
    if (!Load_MaterialTechniqueSetPtr(0))
    {
        DB_PopStreamPos();
        return false;
    }
    if (varMaterial->textureTable)
    {
        if (!varMaterial->textureCount)
        {
            // Present-empty spans are legal in disk32, but have no completed
            // object start or runtime consumer. Preserve inline alignment or
            // validate a direct token, then canonicalize the span to null.
            uint32_t textureToken = 0;
            memcpy(
                &textureToken,
                &varMaterial->textureTable,
                sizeof(textureToken));
            if (textureToken == disk32::kInline)
            {
                if (!AllocLoad_FxElemVisStateSample())
                {
                    Com_Error(ERR_DROP, "Could not align empty material texture table");
                    DB_PopStreamPos();
                    return false;
                }
            }
            else
            {
                DB_ConvertOffsetToPointer(
                    (uint32_t*)&varMaterial->textureTable,
                    0,
                    4,
                    kDirectBlock4);
            }
            varMaterial->textureTable = nullptr;
        }
        else if (varMaterial->textureTable == (MaterialTextureDef *)-1)
        {
            varMaterial->textureTable = (MaterialTextureDef *)AllocLoad_FxElemVisStateSample();
            if (!varMaterial->textureTable)
            {
                Com_Error(ERR_DROP, "Could not allocate material texture table");
                DB_PopStreamPos();
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable);
            if (!completed)
            {
                DB_PopStreamPos();
                return false;
            }
            varMaterialTextureDef = varMaterial->textureTable;
            if (!Load_MaterialTextureDefArray(1, varMaterial->textureCount))
            {
                DB_PopStreamPos();
                return false;
            }
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTextureTable,
                    varMaterial->textureTable,
                    textureByteCount,
                    textureByteCount))
            {
                DB_PopStreamPos();
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable,
                textureByteCount);
        }
    }
    if (varMaterial->constantTable)
    {
        if (!varMaterial->constantCount)
        {
            uint32_t constantToken = 0;
            memcpy(
                &constantToken,
                &varMaterial->constantTable,
                sizeof(constantToken));
            if (constantToken == disk32::kInline)
            {
                if (!AllocLoad_GfxPackedVertex0())
                {
                    Com_Error(ERR_DROP, "Could not align empty material constant table");
                    DB_PopStreamPos();
                    return false;
                }
            }
            else
            {
                DB_ConvertOffsetToPointer(
                    (uint32_t*)&varMaterial->constantTable,
                    0,
                    16,
                    kDirectBlock4);
            }
            varMaterial->constantTable = nullptr;
        }
        else if (varMaterial->constantTable == (MaterialConstantDef *)-1)
        {
            varMaterial->constantTable = (MaterialConstantDef *)AllocLoad_GfxPackedVertex0();
            varMaterialConstantDef = varMaterial->constantTable;
            Load_MaterialConstantDefArray(1, varMaterial->constantCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varMaterial->constantTable,
                constantByteCount,
                16,
                kDirectBlock4);
        }
    }
    if (varMaterial->stateBitsTable)
    {
        if (!varMaterial->stateBitsCount)
        {
            uint32_t stateToken = 0;
            memcpy(
                &stateToken,
                &varMaterial->stateBitsTable,
                sizeof(stateToken));
            if (stateToken == disk32::kInline)
            {
                if (!AllocLoad_FxElemVisStateSample())
                {
                    Com_Error(ERR_DROP, "Could not align empty material state table");
                    DB_PopStreamPos();
                    return false;
                }
            }
            else
            {
                DB_ConvertOffsetToPointer(
                    (uint32_t*)&varMaterial->stateBitsTable,
                    0,
                    4,
                    kDirectBlock4);
            }
            varMaterial->stateBitsTable = nullptr;
        }
        else if (varMaterial->stateBitsTable == (GfxStateBits *)-1)
        {
            varMaterial->stateBitsTable = (GfxStateBits *)AllocLoad_FxElemVisStateSample();
            varGfxStateBits = varMaterial->stateBitsTable;
            Load_GfxStateBitsArray(1, varMaterial->stateBitsCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount,
                4,
                kDirectBlock4);
        }
    }
    if (!DB_ValidateMaterialSemantics(varMaterial))
    {
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_MaterialHandle(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varMaterialHandle, 4);
    DB_PushStreamPos(0);
    if (*varMaterialHandle)
    {
        value = (uint32_t)*varMaterialHandle;
        if (value == -1 || value == -2)
        {
            *varMaterialHandle = (Material *)AllocLoad_FxElemVisStateSample();
            varMaterial = *varMaterialHandle;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::Material);
            else
                inserted = {};
            if (!Load_Material(1))
            {
                DB_PopStreamPos();
                return;
            }
            Load_MaterialAsset((XAssetHeader *)varMaterialHandle);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::Material,
                    *varMaterialHandle);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varMaterialHandle,
                DBAliasKind::Material);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_MaterialHandleArray(bool atStreamStart, int32_t count)
{
    Material **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varMaterialHandle, count, 4);
    var = varMaterialHandle;
    for (i = 0; i < count; ++i)
    {
        varMaterialHandle = var;
        Load_MaterialHandle(0);
        ++var;
    }
}

void __cdecl Mark_MaterialTextureDefInfo()
{
    if (varMaterialTextureDef->semantic == 11)
    {
        if (varMaterialTextureDefInfo && *varMaterialTextureDefInfo)
        {
            varwater_t = *(water_t**)varMaterialTextureDefInfo;
            Mark_water_t();
        }
    }
    else
    {
        varGfxImagePtr = (GfxImage **)varMaterialTextureDefInfo;
        Mark_GfxImagePtr();
    }
}

void __cdecl Mark_MaterialTextureDef()
{
    varMaterialTextureDefInfo = (water_t**)&varMaterialTextureDef->u;
    Mark_MaterialTextureDefInfo();
}

void __cdecl Mark_MaterialTextureDefArray(int32_t count)
{
    MaterialTextureDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialTextureDef;
    for (i = 0; i < count; ++i)
    {
        varMaterialTextureDef = var;
        Mark_MaterialTextureDef();
        ++var;
    }
}

void __cdecl Mark_MaterialTechniqueSetPtr()
{
    if (*varMaterialTechniqueSetPtr)
    {
        varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
        Mark_MaterialTechniqueSetAsset(varMaterialTechniqueSet);
    }
}

void __cdecl Mark_Material()
{
    varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
    Mark_MaterialTechniqueSetPtr();
    if (varMaterial->textureTable)
    {
        varMaterialTextureDef = varMaterial->textureTable;
        Mark_MaterialTextureDefArray(varMaterial->textureCount);
    }
}

void __cdecl Mark_MaterialHandle()
{
    if (*varMaterialHandle)
    {
        varMaterial = *varMaterialHandle;
        Mark_MaterialAsset(varMaterial);
        Mark_Material();
    }
}

void __cdecl Mark_MaterialHandleArray(int32_t count)
{
    Material **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialHandle;
    for (i = 0; i < count; ++i)
    {
        varMaterialHandle = var;
        Mark_MaterialHandle();
        ++var;
    }
}

void __cdecl Load_GfxLightImage(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightImage, 8);
    varGfxImagePtr = &varGfxLightImage->image;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxLightDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightDef, 16);
    DB_PushStreamPos(4);
    varXString = &varGfxLightDef->name;
    Load_XString(0);
    varGfxLightImage = &varGfxLightDef->attenuation;
    Load_GfxLightImage(0);
    DB_PopStreamPos();
}

void __cdecl Load_GfxLightDefPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxLightDefPtr, 4);
    DB_PushStreamPos(0);
    if (*varGfxLightDefPtr)
    {
        value = (uint32_t)*varGfxLightDefPtr;
        if (value == -1 || value == -2)
        {
            *varGfxLightDefPtr = (GfxLightDef *)AllocLoad_FxElemVisStateSample();
            varGfxLightDef = *varGfxLightDefPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::GfxLightDef);
            else
                inserted = {};
            Load_GfxLightDef(1);
            Load_LightDefAsset((XAssetHeader *)varGfxLightDefPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GfxLightDef,
                    *varGfxLightDefPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varGfxLightDefPtr,
                DBAliasKind::GfxLightDef);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxLight(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        &varGfxLight->type,
        disk32::kGfxLightBytes);
    varGfxLightDefPtr = &varGfxLight->def;
    Load_GfxLightDefPtr(0);
}

void __cdecl Mark_GfxLightImage()
{
    varGfxImagePtr = &varGfxLightImage->image;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxLightDef()
{
    varGfxLightImage = &varGfxLightDef->attenuation;
    Mark_GfxLightImage();
}

void __cdecl Mark_GfxLightDefPtr()
{
    if (*varGfxLightDefPtr)
    {
        varGfxLightDef = *varGfxLightDefPtr;
        Mark_LightDefAsset(varGfxLightDef);
        Mark_GfxLightDef();
    }
}

void __cdecl Mark_GfxLight()
{
    varGfxLightDefPtr = &varGfxLight->def;
    Mark_GfxLightDefPtr();
}

void __cdecl Load_GfxSurface(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxSurface, 48);
    varMaterialHandle = &varGfxSurface->material;
    Load_MaterialHandle(0);
}

void __cdecl Load_GfxSurfaceArray(bool atStreamStart, int32_t count)
{
    GfxSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxSurface, count, 48);
    var = varGfxSurface;
    for (i = 0; i < count; ++i)
    {
        varGfxSurface = var;
        Load_GfxSurface(0);
        ++var;
    }
}

void __cdecl Load_GfxLightmapArray(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightmapArray, 8);
    varGfxImagePtr = &varGfxLightmapArray->primary;
    Load_GfxImagePtr(0);
    varGfxImagePtr = &varGfxLightmapArray->secondary;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxLightmapArrayArray(bool atStreamStart, int32_t count)
{
    GfxLightmapArray *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightmapArray, count, 8);
    var = varGfxLightmapArray;
    for (i = 0; i < count; ++i)
    {
        varGfxLightmapArray = var;
        Load_GfxLightmapArray(0);
        ++var;
    }
}

void __cdecl Mark_GfxSurface()
{
    varMaterialHandle = &varGfxSurface->material;
    Mark_MaterialHandle();
}

void __cdecl Mark_GfxSurfaceArray(int32_t count)
{
    GfxSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxSurface;
    for (i = 0; i < count; ++i)
    {
        varGfxSurface = var;
        Mark_GfxSurface();
        ++var;
    }
}

void __cdecl Mark_GfxLightmapArray()
{
    varGfxImagePtr = &varGfxLightmapArray->primary;
    Mark_GfxImagePtr();
    varGfxImagePtr = &varGfxLightmapArray->secondary;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxLightmapArrayArray(int32_t count)
{
    GfxLightmapArray *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxLightmapArray;
    for (i = 0; i < count; ++i)
    {
        varGfxLightmapArray = var;
        Mark_GfxLightmapArray();
        ++var;
    }
}

void __cdecl Load_PhysPreset(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varPhysPreset, 44);
    DB_PushStreamPos(4);
    varXString = &varPhysPreset->name;
    Load_XString(0);
    varXString = &varPhysPreset->sndAliasPrefix;
    Load_XString(0);
    DB_PopStreamPos();
}

void __cdecl Load_PhysPresetPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varPhysPresetPtr, 4);
    DB_PushStreamPos(0);
    if (*varPhysPresetPtr)
    {
        value = (uint32_t)*varPhysPresetPtr;
        if (value == -1 || value == -2)
        {
            *varPhysPresetPtr = (PhysPreset *)AllocLoad_FxElemVisStateSample();
            varPhysPreset = *varPhysPresetPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::PhysPreset);
            else
                inserted = {};
            Load_PhysPreset(1);
            Load_PhysPresetAsset((XAssetHeader *)varPhysPresetPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::PhysPreset,
                    *varPhysPresetPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varPhysPresetPtr,
                DBAliasKind::PhysPreset);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_PhysPresetPtr()
{
    if (*varPhysPresetPtr)
    {
        varPhysPreset = *varPhysPresetPtr;
        Mark_PhysPresetAsset(varPhysPreset);
    }
}

void __cdecl Load_cplane_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varcplane_t,
        disk32::kCPlaneBytes);
}

void __cdecl Load_cplane_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varcplane_t,
        count,
        disk32::kCPlaneBytes);
}

bool __cdecl Load_cbrushside_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varcbrushside_t,
        disk32::kCBrushSideBytes);
    if (!varcbrushside_t->plane
        || varcbrushside_t->plane == (cplane_s *)-1
        || varcbrushside_t->plane == (cplane_s *)-2)
    {
        Com_Error(ERR_DROP, "Invalid fast-file clipmap brush-side plane token");
        return false;
    }
    return DB_ResolveDirectPointer(
        &varcbrushside_t->plane,
        disk32::kCPlaneBytes,
        4,
        kDirectBlock4,
        "clipmap brush-side plane");
}

XAsset *__cdecl AllocLoad_FxElemVisStateSample()
{
    return (XAsset *)DB_AllocStreamPos(3);
}

bool __cdecl Load_cbrushside_tArray(bool atStreamStart, int32_t count)
{
    cbrushside_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varcbrushside_t,
        count,
        disk32::kCBrushSideBytes);
    var = varcbrushside_t;
    for (i = 0; i < count; ++i)
    {
        varcbrushside_t = var;
        if (!Load_cbrushside_t(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_cbrushedge_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, varcbrushedge_t, 1);
}

void __cdecl Load_cbrushedge_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varcbrushedge_t, count);
}

void __cdecl Load_XModelCollTriArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (unsigned char*)varXModelCollTri, count, 48);
}

void __cdecl Load_XModelCollSurf(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModelCollSurf, 44);
    if (varXModelCollSurf->collTris)
    {
        varXModelCollSurf->collTris = (XModelCollTri_s *)AllocLoad_FxElemVisStateSample();
        varXModelCollTri = varXModelCollSurf->collTris;
        Load_XModelCollTriArray(1, varXModelCollSurf->numCollTris);
    }
}

void __cdecl Load_XModelCollSurfArray(bool atStreamStart, int32_t count)
{
    XModelCollSurf_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varXModelCollSurf, count, 44);
    var = varXModelCollSurf;
    for (i = 0; i < count; ++i)
    {
        varXModelCollSurf = var;
        Load_XModelCollSurf(0);
        ++var;
    }
}

bool __cdecl Load_BrushWrapper(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varBrushWrapper,
        disk32::kBrushWrapperBytes);

    int32_t sideBytes = 0;
    int32_t planeBytes = 0;
    int32_t adjacencyBytes = 0;
    if (!db::validation::BrushWrapperLayoutValid(
            varBrushWrapper->sides != nullptr,
            varBrushWrapper->planes != nullptr,
            varBrushWrapper->numsides,
            varBrushWrapper->baseAdjacentSide != nullptr,
            varBrushWrapper->totalEdgeCount,
            &sideBytes,
            &planeBytes,
            &adjacencyBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file physics brush header");
        return false;
    }

    disk32::PointerToken sidePlaneTokens[
        db::validation::kMaxBrushNonaxialSides] = {};
    if (varBrushWrapper->sides)
    {
        varBrushWrapper->sides =
            (cbrushside_t *)AllocLoad_FxElemVisStateSample();
        if (!varBrushWrapper->sides
            || !DB_IsStreamRangeValid(
                varBrushWrapper->sides,
                static_cast<uint32_t>(sideBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file physics brush sides exceed block 4");
            return false;
        }
        varcbrushside_t = varBrushWrapper->sides;
        Load_StreamArray(
            1,
            (uint8_t *)varcbrushside_t,
            static_cast<int32_t>(varBrushWrapper->numsides),
            disk32::kCBrushSideBytes);

        for (uint32_t sideIndex = 0;
            sideIndex < varBrushWrapper->numsides;
            ++sideIndex)
        {
            cbrushside_t &side = varBrushWrapper->sides[sideIndex];
            memcpy(
                &sidePlaneTokens[sideIndex].value,
                &side.plane,
                sizeof(sidePlaneTokens[sideIndex].value));
            const disk32::PointerToken token = sidePlaneTokens[sideIndex];
            if (token.isNull() || token.isSharedInline())
            {
                Com_Error(ERR_DROP, "Invalid fast-file physics brush-side plane token");
                return false;
            }
            if (token.isInline())
            {
                side.plane = (cplane_s *)AllocLoad_FxElemVisStateSample();
                if (!side.plane
                    || !DB_IsStreamRangeValid(
                        side.plane,
                        disk32::kCPlaneBytes))
                {
                    Com_Error(ERR_DROP, "Fast-file inline brush-side plane exceeds block 4");
                    return false;
                }
                varcplane_t = side.plane;
                Load_cplane_t(1);
            }
            else
            {
                // Preserve the disk token separately so native pointer values
                // cannot be confused with unresolved 32-bit offsets on x86.
                side.plane = nullptr;
            }
        }
    }
    if (varBrushWrapper->baseAdjacentSide)
    {
        varBrushWrapper->baseAdjacentSide = AllocLoad_raw_byte();
        if (!varBrushWrapper->baseAdjacentSide
            || !DB_IsStreamRangeValid(
                varBrushWrapper->baseAdjacentSide,
                static_cast<uint32_t>(adjacencyBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file physics brush adjacency exceeds block 4");
            return false;
        }
        varcbrushedge_t = varBrushWrapper->baseAdjacentSide;
        Load_cbrushedge_tArray(1, varBrushWrapper->totalEdgeCount);
    }
    if (varBrushWrapper->planes)
    {
        if (varBrushWrapper->planes == (cplane_s *)-1)
        {
            varBrushWrapper->planes =
                (cplane_s *)AllocLoad_FxElemVisStateSample();
            if (!varBrushWrapper->planes
                || !DB_IsStreamRangeValid(
                    varBrushWrapper->planes,
                    static_cast<uint32_t>(planeBytes)))
            {
                Com_Error(ERR_DROP, "Fast-file physics brush planes exceed block 4");
                return false;
            }
            varcplane_t = varBrushWrapper->planes;
            Load_cplane_tArray(
                1,
                static_cast<int32_t>(varBrushWrapper->numsides));
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varBrushWrapper->planes,
                static_cast<uint32_t>(planeBytes),
                4,
                kDirectBlock4);
        }
    }

    for (uint32_t sideIndex = 0;
        sideIndex < varBrushWrapper->numsides;
        ++sideIndex)
    {
        const disk32::PointerToken token = sidePlaneTokens[sideIndex];
        if (!token.isOffset())
            continue;
        uintptr_t pointer = 0;
        const db::relocation::Status status = DB_ResolveOffsetBytes(
            token,
            disk32::kCPlaneBytes,
            4,
            kDirectBlock4,
            &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(
                ERR_DROP,
                "Invalid deferred fast-file brush-side plane: %s",
                db::relocation::StatusName(status));
            return false;
        }
        varBrushWrapper->sides[sideIndex].plane =
            reinterpret_cast<cplane_s *>(pointer);
    }

    if (!DB_ValidateMaterializedBlock4Span(
            varBrushWrapper->sides,
            static_cast<uint32_t>(sideBytes),
            4,
            "brush sides")
        || !DB_ValidateMaterializedBlock4Span(
            varBrushWrapper->baseAdjacentSide,
            static_cast<uint32_t>(adjacencyBytes),
            1,
            "brush adjacency")
        || !DB_ValidateMaterializedBlock4Span(
            varBrushWrapper->planes,
            static_cast<uint32_t>(planeBytes),
            4,
            "brush planes")
        || !db::validation::BrushWrapperRuntimeValid(*varBrushWrapper))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file physics brush graph");
        return false;
    }
    return true;
}

bool __cdecl Load_PhysGeomInfo(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varPhysGeomInfo,
        disk32::kPhysGeomInfoBytes);
    if (varPhysGeomInfo->brush)
    {
        if (varPhysGeomInfo->brush == (BrushWrapper *)-1)
        {
            varPhysGeomInfo->brush =
                (BrushWrapper *)AllocLoad_FxElemVisStateSample();
            if (!varPhysGeomInfo->brush)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file physics brush");
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper);
            if (!completed)
                return false;
            varBrushWrapper = varPhysGeomInfo->brush;
            if (!Load_BrushWrapper(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::BrushWrapper,
                    varPhysGeomInfo->brush,
                    disk32::kBrushWrapperBytes,
                    disk32::kBrushWrapperBytes))
            {
                return false;
            }
        }
        else if (!DB_ResolveCompletedPointer(
                &varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper,
                disk32::kBrushWrapperBytes,
                "physics brush"))
        {
            return false;
        }
    }
    if (!db::validation::PhysGeomInfoRuntimeValid(*varPhysGeomInfo))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file physics geometry");
        return false;
    }
    return true;
}

bool __cdecl Load_PhysGeomInfoArray(bool atStreamStart, int32_t count)
{
    PhysGeomInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varPhysGeomInfo,
        count,
        disk32::kPhysGeomInfoBytes);
    var = varPhysGeomInfo;
    for (i = 0; i < count; ++i)
    {
        varPhysGeomInfo = var;
        if (!Load_PhysGeomInfo(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_PhysGeomList(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varPhysGeomList,
        disk32::kPhysGeomListBytes);
    int32_t geomBytes = 0;
    if (!db::validation::PhysGeomListLayoutValid(
            varPhysGeomList->geoms != nullptr,
            varPhysGeomList->count,
            &geomBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file physics geometry-list header");
        return false;
    }
    if (varPhysGeomList->geoms)
    {
        varPhysGeomList->geoms =
            (PhysGeomInfo *)AllocLoad_FxElemVisStateSample();
        if (!varPhysGeomList->geoms
            || !DB_IsStreamRangeValid(
                varPhysGeomList->geoms,
                static_cast<uint32_t>(geomBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file physics geometry list exceeds block 4");
            return false;
        }
        varPhysGeomInfo = varPhysGeomList->geoms;
        if (!Load_PhysGeomInfoArray(
                1,
                static_cast<int32_t>(varPhysGeomList->count)))
        {
            return false;
        }
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varPhysGeomList->geoms,
            static_cast<uint32_t>(geomBytes),
            4,
            "physics geometry list")
        || !db::validation::PhysGeomListRuntimeValid(*varPhysGeomList))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file physics geometry list");
        return false;
    }
    return true;
}

bool __cdecl Load_XModel(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModel, 220);
    const int32_t nonRootBoneCount = DB_CheckedCountDifference(
        varXModel->numBones,
        varXModel->numRootBones,
        "model non-root bones");
    const int32_t nonRootTransformCount = DB_CheckedCountProduct(
        4,
        nonRootBoneCount,
        "model non-root transforms");
    const uint32_t parentListByteCount = DB_CheckedDirectSpanBytes(
        nonRootBoneCount,
        1,
        "model parent list");
    const uint32_t quatByteCount = DB_CheckedDirectSpanBytes(
        nonRootTransformCount,
        2,
        "model quaternions");
    const uint32_t translationByteCount = DB_CheckedDirectSpanBytes(
        nonRootTransformCount,
        4,
        "model translations");
    const uint32_t classificationByteCount = DB_CheckedDirectSpanBytes(
        varXModel->numBones,
        1,
        "model part classifications");
    const uint32_t baseMatByteCount = DB_CheckedDirectSpanBytes(
        varXModel->numBones,
        32,
        "model base matrices");
    const uint32_t boneNameByteCount = DB_CheckedDirectSpanBytes(
        varXModel->numBones,
        2,
        "model bone names");
    DB_PushStreamPos(4);
    varXString = &varXModel->name;
    Load_XString(0);
    if (varXModel->boneNames)
    {
        if (varXModel->boneNames == (uint16_t *)-1)
        {
            varXModel->boneNames = (uint16_t *)AllocLoad_XBlendInfo();
            varScriptString = varXModel->boneNames;
            Load_ScriptStringArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->boneNames,
                boneNameByteCount,
                2,
                kDirectBlock4);
        }
    }
    if (varXModel->parentList)
    {
        if (varXModel->parentList == (uint8_t *)-1)
        {
            varXModel->parentList = AllocLoad_raw_byte();
            varbyte = varXModel->parentList;
            Load_byteArray(1, nonRootBoneCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->parentList,
                parentListByteCount,
                1,
                kDirectBlock4);
        }
    }
    if (varXModel->quats)
    {
        if (varXModel->quats == (__int16 *)-1)
        {
            varXModel->quats = (__int16 *)AllocLoad_XBlendInfo();
            varshort = varXModel->quats;
            Load_shortArray(1, nonRootTransformCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->quats,
                quatByteCount,
                2,
                kDirectBlock4);
        }
    }
    if (varXModel->trans)
    {
        if (varXModel->trans == (float *)-1)
        {
            varXModel->trans = (float *)AllocLoad_FxElemVisStateSample();
            varfloat = varXModel->trans;
            Load_floatArray(1, nonRootTransformCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->trans,
                translationByteCount,
                4,
                kDirectBlock4);
        }
    }
    if (varXModel->partClassification)
    {
        if (varXModel->partClassification == (uint8_t *)-1)
        {
            varXModel->partClassification = AllocLoad_raw_byte();
            varbyte = varXModel->partClassification;
            Load_byteArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->partClassification,
                classificationByteCount,
                1,
                kDirectBlock4);
        }
    }
    if (varXModel->baseMat)
    {
        if (varXModel->baseMat == (DObjAnimMat *)-1)
        {
            varXModel->baseMat = (DObjAnimMat *)AllocLoad_FxElemVisStateSample();
            varDObjAnimMat = varXModel->baseMat;
            Load_DObjAnimMatArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varXModel->baseMat,
                baseMatByteCount,
                4,
                kDirectBlock4);
        }
    }
    if (varXModel->surfs)
    {
        varXModel->surfs = (XSurface *)AllocLoad_FxElemVisStateSample();
        varXSurface = varXModel->surfs;
        if (!Load_XSurfaceArray(1, varXModel->numsurfs))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    if (varXModel->materialHandles)
    {
        varXModel->materialHandles = (Material **)AllocLoad_FxElemVisStateSample();
        varMaterialHandle = varXModel->materialHandles;
        Load_MaterialHandleArray(1, varXModel->numsurfs);
    }
    if (varXModel->collSurfs)
    {
        varXModel->collSurfs = (XModelCollSurf_s *)AllocLoad_FxElemVisStateSample();
        varXModelCollSurf = varXModel->collSurfs;
        Load_XModelCollSurfArray(1, varXModel->numCollSurfs);
    }
    if (varXModel->boneInfo)
    {
        varXModel->boneInfo = (XBoneInfo *)AllocLoad_FxElemVisStateSample();
        varXBoneInfo = varXModel->boneInfo;
        Load_XBoneInfoArray(1, varXModel->numBones);
    }
    varPhysPresetPtr = &varXModel->physPreset;
    Load_PhysPresetPtr(0);
    if (varXModel->physGeoms)
    {
        if (varXModel->physGeoms == (PhysGeomList *)-1)
        {
            varXModel->physGeoms = (PhysGeomList *)AllocLoad_FxElemVisStateSample();
            if (!varXModel->physGeoms)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file physics geometry list");
                DB_PopStreamPos();
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varXModel->physGeoms,
                DBAliasKind::PhysGeomList);
            if (!completed)
            {
                varXModel->physGeoms = nullptr;
                DB_PopStreamPos();
                return false;
            }
            varPhysGeomList = varXModel->physGeoms;
            if (!Load_PhysGeomList(1)
                || !DB_CompleteObject(
                    completed,
                    DBAliasKind::PhysGeomList,
                    varXModel->physGeoms,
                    disk32::kPhysGeomListBytes,
                    disk32::kPhysGeomListBytes))
            {
                varXModel->physGeoms = nullptr;
                DB_PopStreamPos();
                return false;
            }
        }
        else if (!DB_ResolveCompletedPointer(
                &varXModel->physGeoms,
                DBAliasKind::PhysGeomList,
                disk32::kPhysGeomListBytes,
                "model physics geometry list"))
        {
            varXModel->physGeoms = nullptr;
            DB_PopStreamPos();
            return false;
        }
    }
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_XModelPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varXModelPtr, 4);
    DB_PushStreamPos(0);
    if (*varXModelPtr)
    {
        value = (uint32_t)*varXModelPtr;
        if (value == -1 || value == -2)
        {
            *varXModelPtr = (XModel *)AllocLoad_FxElemVisStateSample();
            varXModel = *varXModelPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::XModel);
            else
                inserted = {};
            if (!Load_XModel(1))
            {
                *varXModelPtr = nullptr;
                DB_PopStreamPos();
                return;
            }
            Load_XModelAsset((XAssetHeader *)varXModelPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::XModel,
                    *varXModelPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varXModelPtr,
                DBAliasKind::XModel);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_XModelPtrArray(bool atStreamStart, int32_t count)
{
    XModel **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varXModelPtr, count, 4);
    var = varXModelPtr;
    for (i = 0; i < count; ++i)
    {
        varXModelPtr = var;
        Load_XModelPtr(0);
        ++var;
    }
}

void __cdecl Load_XModelPiece(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varXModelPiece,
        disk32::kXModelPieceBytes);
    varXModelPtr = &varXModelPiece->model;
    Load_XModelPtr(0);
}

bool __cdecl Load_XModelPieceArray(bool atStreamStart, int32_t count)
{
    XModelPiece *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    int32_t pieceBytes = 0;
    if (!db::validation::CheckedArrayBytes(
            count,
            disk32::kXModelPieceBytes,
            &pieceBytes)
        || !DB_IsStreamRangeValid(
            varXModelPiece,
            static_cast<uint32_t>(pieceBytes)))
    {
        Com_Error(ERR_DROP, "Fast-file model-pieces array exceeds its stream block");
        return false;
    }

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varXModelPiece,
        count,
        disk32::kXModelPieceBytes);
    var = varXModelPiece;
    for (i = 0; i < count; ++i)
    {
        varXModelPiece = var;
        Load_XModelPiece(0);
        ++var;
    }
    return true;
}

bool __cdecl Load_XModelPieces(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varXModelPieces,
        disk32::kXModelPiecesBytes);
    int32_t pieceBytes = 0;
    if (!DB_ValidateXModelPiecesHeader(varXModelPieces, &pieceBytes))
        return false;
    const bool hasPieceArray = varXModelPieces->pieces != nullptr;
    varXString = &varXModelPieces->name;
    Load_XString(0);
    if (hasPieceArray)
    {
        varXModelPieces->pieces = (XModelPiece *)AllocLoad_FxElemVisStateSample();
        if (!varXModelPieces->pieces)
        {
            Com_Error(ERR_DROP, "Cannot allocate fast-file model pieces");
            return false;
        }
        varXModelPiece = varXModelPieces->pieces;
        if (!Load_XModelPieceArray(1, varXModelPieces->numpieces))
            return false;
    }
    return DB_ValidateXModelPieces(
        varXModelPieces,
        static_cast<uint32_t>(pieceBytes));
}

bool __cdecl Load_XModelPiecesPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModelPiecesPtr, 4);
    if (*varXModelPiecesPtr)
    {
        if (*varXModelPiecesPtr == (XModelPieces *)-1)
        {
            *varXModelPiecesPtr = (XModelPieces *)AllocLoad_FxElemVisStateSample();
            if (!*varXModelPiecesPtr)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file model-pieces header");
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                *varXModelPiecesPtr,
                DBAliasKind::XModelPieces);
            if (!completed)
                return false;
            varXModelPieces = *varXModelPiecesPtr;
            if (!Load_XModelPieces(1))
                return false;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::XModelPieces,
                    *varXModelPiecesPtr,
                    disk32::kXModelPiecesBytes,
                    disk32::kXModelPiecesBytes))
            {
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varXModelPiecesPtr,
                DBAliasKind::XModelPieces,
                disk32::kXModelPiecesBytes);
        }
    }
    return true;
}

void __cdecl Mark_XModel()
{
    if (varXModel->boneNames)
    {
        varScriptString = varXModel->boneNames;
        Mark_ScriptStringArray(varXModel->numBones);
    }
    if (varXModel->materialHandles)
    {
        varMaterialHandle = varXModel->materialHandles;
        Mark_MaterialHandleArray(varXModel->numsurfs);
    }
    varPhysPresetPtr = &varXModel->physPreset;
    Mark_PhysPresetPtr();
}

void __cdecl Mark_XModelPtr()
{
    if (*varXModelPtr)
    {
        varXModel = *varXModelPtr;
        Mark_XModelAsset(varXModel);
        Mark_XModel();
    }
}

void __cdecl Mark_XModelPtrArray(int32_t count)
{
    XModel **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXModelPtr;
    for (i = 0; i < count; ++i)
    {
        varXModelPtr = var;
        Mark_XModelPtr();
        ++var;
    }
}

void __cdecl Mark_XModelPiece()
{
    varXModelPtr = &varXModelPiece->model;
    Mark_XModelPtr();
}

void __cdecl Mark_XModelPieceArray(int32_t count)
{
    XModelPiece *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXModelPiece;
    for (i = 0; i < count; ++i)
    {
        varXModelPiece = var;
        Mark_XModelPiece();
        ++var;
    }
}

void __cdecl Mark_XModelPieces()
{
    if (varXModelPieces->pieces)
    {
        varXModelPiece = varXModelPieces->pieces;
        Mark_XModelPieceArray(varXModelPieces->numpieces);
    }
}

void __cdecl Mark_XModelPiecesPtr()
{
    if (*varXModelPiecesPtr)
    {
        varXModelPieces = *varXModelPiecesPtr;
        Mark_XModelPieces();
    }
}

void __cdecl Load_pathlink_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varpathlink_t, count, 12);
}

bool __cdecl Load_pathnode_constant_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varpathnode_constant_t,
        disk32::kPathNodeConstantBytes);
    const int32_t linkCount = varpathnode_constant_t->totalLinkCount;
    int32_t linkBytes = 0;
    if (!varPathData
        || varPathData->nodeCount > db::validation::kMaxPathNodes
        || linkCount > static_cast<int32_t>(varPathData->nodeCount)
        || (varpathnode_constant_t->Links != nullptr)
            != (linkCount != 0)
        || !db::validation::CheckedArrayBytes(
            linkCount,
            disk32::kPathLinkBytes,
            &linkBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-node links");
        return false;
    }
    varScriptString = &varpathnode_constant_t->targetname;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->script_linkName;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->script_noteworthy;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->target;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->animscript;
    Load_ScriptString(0);
    if (varpathnode_constant_t->Links)
    {
        varpathnode_constant_t->Links = (pathlink_s *)AllocLoad_FxElemVisStateSample();
        if (!varpathnode_constant_t->Links
            || !DB_IsStreamRangeValid(
                varpathnode_constant_t->Links,
                static_cast<uint32_t>(linkBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path-node links exceed block 4");
            return false;
        }
        varpathlink_t = varpathnode_constant_t->Links;
        Load_pathlink_tArray(1, linkCount);
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varpathnode_constant_t->Links,
            static_cast<uint32_t>(linkBytes),
            4,
            "path-node links")
        || !db::validation::PathLinksRuntimeValid(
            varpathnode_constant_t->Links,
            static_cast<uint32_t>(linkCount),
            varPathData->nodeCount))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file path-node links");
        return false;
    }
    return true;
}

bool __cdecl Load_pathnode_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varpathnode_t,
        disk32::kPathNodeBytes);
    std::memset(
        &varpathnode_t->dynamic.pOwner,
        0,
        sizeof(varpathnode_t->dynamic.pOwner));
    std::memset(
        &varpathnode_t->transient,
        0,
        sizeof(varpathnode_t->transient));
    if (!db::validation::PathNodeTypeValid(
            static_cast<int32_t>(varpathnode_t->constant.type)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-node type");
        return false;
    }
    varpathnode_constant_t = &varpathnode_t->constant;
    return Load_pathnode_constant_t(0);
}

bool __cdecl Load_pathnode_tArray(bool atStreamStart, int32_t count)
{
    pathnode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varpathnode_t,
        count,
        disk32::kPathNodeBytes);
    var = varpathnode_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_t = var;
        if (!Load_pathnode_t(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_pathbasenode_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varpathbasenode_t,
        count,
        disk32::kPathBaseNodeBytes);
}

bool __cdecl Load_pathnode_tree_nodes_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varpathnode_tree_nodes_t,
        disk32::kPathTreeLeafInfoBytes);
    const int32_t leafNodeCount = varpathnode_tree_nodes_t->nodeCount;
    int32_t leafNodeBytes = 0;
    if (!varPathData
        || varPathData->nodeCount > db::validation::kMaxPathNodes
        || !db::validation::CountInRange(
            leafNodeCount,
            1,
            varPathData->nodeCount)
        || !varpathnode_tree_nodes_t->nodes
        || !db::validation::CheckedArrayBytes(
            leafNodeCount,
            disk32::kPathNodeIndexBytes,
            &leafNodeBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-tree leaf");
        return false;
    }
    if (varpathnode_tree_nodes_t->nodes)
    {
        varpathnode_tree_nodes_t->nodes = (uint16_t *)AllocLoad_XBlendInfo();
        if (!varpathnode_tree_nodes_t->nodes
            || !DB_IsStreamRangeValid(
                varpathnode_tree_nodes_t->nodes,
                static_cast<uint32_t>(leafNodeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path-tree leaf exceeds block 4");
            return false;
        }
        varushort = varpathnode_tree_nodes_t->nodes;
        Load_ushortArray(1, leafNodeCount);
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varpathnode_tree_nodes_t->nodes,
            static_cast<uint32_t>(leafNodeBytes),
            2,
            "path-tree leaf nodes")
        || !db::validation::AllU16Below(
            varpathnode_tree_nodes_t->nodes,
            static_cast<uint32_t>(leafNodeCount),
            varPathData->nodeCount))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file path-tree leaf");
        return false;
    }
    return true;
}

bool __cdecl Load_pathnode_tree_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_ptr, 4);
    if (!varPathData || !varPathData->nodeTree
        || !db::validation::CountInRange(
            varPathData->nodeTreeCount,
            1,
            db::validation::kMaxPathTreeNodes)
        || !*varpathnode_tree_ptr
        || *varpathnode_tree_ptr == (pathnode_tree_t *)-1
        || *varpathnode_tree_ptr == (pathnode_tree_t *)-2)
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-tree child token");
        return false;
    }
    if (!DB_ResolveDirectPointer(
            varpathnode_tree_ptr,
            disk32::kPathTreeBytes,
            4,
            kDirectBlock4,
            "path-tree child"))
    {
        return false;
    }
    uint64_t childIndex = 0;
    if (!db::validation::SerializedArrayElementIndex(
            varPathData->nodeTree,
            static_cast<uint32_t>(varPathData->nodeTreeCount),
            disk32::kPathTreeBytes,
            *varpathnode_tree_ptr,
            &childIndex))
    {
        Com_Error(ERR_DROP, "Fast-file path-tree child is not an owned node");
        return false;
    }
    return true;
}

bool __cdecl Load_pathnode_tree_ptrArray(bool atStreamStart, int32_t count)
{
    pathnode_tree_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varpathnode_tree_ptr, count, 4);
    var = varpathnode_tree_ptr;
    for (i = 0; i < count; ++i)
    {
        varpathnode_tree_ptr = var;
        if (!Load_pathnode_tree_ptr(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_pathnode_tree_info_t(bool atStreamStart)
{
    if (varpathnode_tree_t->axis < 0)
    {
        varpathnode_tree_nodes_t = (pathnode_tree_nodes_t *)varpathnode_tree_info_t;
        return Load_pathnode_tree_nodes_t(atStreamStart);
    }
    if (varpathnode_tree_t->axis > 2
        || !std::isfinite(varpathnode_tree_t->dist))
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-tree split");
        return false;
    }
    varpathnode_tree_ptr = (pathnode_tree_t **)varpathnode_tree_info_t;
    return Load_pathnode_tree_ptrArray(atStreamStart, 2);
}

bool __cdecl Load_pathnode_tree_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varpathnode_tree_t,
        disk32::kPathTreeBytes);
    varpathnode_tree_info_t = &varpathnode_tree_t->u;
    return Load_pathnode_tree_info_t(0);
}

bool __cdecl Load_pathnode_tree_tArray(bool atStreamStart, int32_t count)
{
    pathnode_tree_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varpathnode_tree_t,
        count,
        disk32::kPathTreeBytes);
    var = varpathnode_tree_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_tree_t = var;
        if (!Load_pathnode_tree_t(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Mark_pathnode_constant_t()
{
    varScriptString = &varpathnode_constant_t->targetname;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->script_linkName;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->script_noteworthy;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->target;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->animscript;
    Mark_ScriptString();
}

void __cdecl Mark_pathnode_t()
{
    varpathnode_constant_t = &varpathnode_t->constant;
    Mark_pathnode_constant_t();
}

void __cdecl Mark_pathnode_tArray(int32_t count)
{
    pathnode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varpathnode_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_t = var;
        Mark_pathnode_t();
        ++var;
    }
}

bool __cdecl Load_PathData(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varPathData,
        disk32::kPathDataBytes);
    db::validation::PathDataLayoutExtents extents = {};
    if (!db::validation::PathDataLayoutValid(*varPathData, &extents))
    {
        Com_Error(ERR_DROP, "Invalid fast-file path-data layout");
        return false;
    }
    if (varPathData->nodes)
    {
        varPathData->nodes = (pathnode_t *)AllocLoad_FxElemVisStateSample();
        if (!varPathData->nodes
            || !DB_IsStreamRangeValid(
                varPathData->nodes,
                static_cast<uint32_t>(extents.nodeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path nodes exceed block 4");
            return false;
        }
        varpathnode_t = varPathData->nodes;
        if (!Load_pathnode_tArray(
                1,
                static_cast<int32_t>(varPathData->nodeCount)))
        {
            return false;
        }
    }
    DB_PushStreamPos(1);
    if (varPathData->basenodes)
    {
        varPathData->basenodes = (pathbasenode_t *)AllocLoad_GfxPackedVertex0();
        if (!varPathData->basenodes
            || !DB_IsStreamRangeValid(
                varPathData->basenodes,
                static_cast<uint32_t>(extents.baseNodeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path base nodes exceed block 1");
            DB_PopStreamPos();
            return false;
        }
        varpathbasenode_t = varPathData->basenodes;
        Load_pathbasenode_tArray(
            1,
            static_cast<int32_t>(varPathData->nodeCount));
    }
    DB_PopStreamPos();
    if (varPathData->chainNodeForNode)
    {
        varPathData->chainNodeForNode = (uint16_t *)AllocLoad_XBlendInfo();
        if (!varPathData->chainNodeForNode
            || !DB_IsStreamRangeValid(
                varPathData->chainNodeForNode,
                static_cast<uint32_t>(extents.chainMapBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path chain map exceeds block 4");
            return false;
        }
        varUnsignedShort = varPathData->chainNodeForNode;
        Load_UnsignedShortArray(
            1,
            static_cast<int32_t>(varPathData->nodeCount));
    }
    if (varPathData->nodeForChainNode)
    {
        varPathData->nodeForChainNode = (uint16_t *)AllocLoad_XBlendInfo();
        if (!varPathData->nodeForChainNode
            || !DB_IsStreamRangeValid(
                varPathData->nodeForChainNode,
                static_cast<uint32_t>(extents.chainMapBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file inverse path chain map exceeds block 4");
            return false;
        }
        varUnsignedShort = varPathData->nodeForChainNode;
        Load_UnsignedShortArray(
            1,
            static_cast<int32_t>(varPathData->nodeCount));
    }
    if (varPathData->pathVis)
    {
        varPathData->pathVis = AllocLoad_raw_byte();
        if (!varPathData->pathVis
            || !DB_IsStreamRangeValid(
                varPathData->pathVis,
                static_cast<uint32_t>(extents.visibilityBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path visibility exceeds block 4");
            return false;
        }
        varbyte = varPathData->pathVis;
        Load_byteArray(1, extents.visibilityBytes);
    }
    if (varPathData->nodeTree)
    {
        varPathData->nodeTree = (pathnode_tree_t *)AllocLoad_FxElemVisStateSample();
        if (!varPathData->nodeTree
            || !DB_IsStreamRangeValid(
                varPathData->nodeTree,
                static_cast<uint32_t>(extents.treeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file path tree exceeds block 4");
            return false;
        }
        varpathnode_tree_t = varPathData->nodeTree;
        if (!Load_pathnode_tree_tArray(1, varPathData->nodeTreeCount))
            return false;
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varPathData->nodes,
            static_cast<uint32_t>(extents.nodeBytes),
            4,
            "path nodes")
        || !DB_ValidateMaterializedSpan(
            varPathData->basenodes,
            static_cast<uint32_t>(extents.baseNodeBytes),
            16,
            kDirectBlock1,
            "path base nodes")
        || !DB_ValidateMaterializedBlock4Span(
            varPathData->chainNodeForNode,
            static_cast<uint32_t>(extents.chainMapBytes),
            2,
            "path chain map")
        || !DB_ValidateMaterializedBlock4Span(
            varPathData->nodeForChainNode,
            static_cast<uint32_t>(extents.chainMapBytes),
            2,
            "inverse path chain map")
        || !DB_ValidateMaterializedBlock4Span(
            varPathData->pathVis,
            static_cast<uint32_t>(extents.visibilityBytes),
            1,
            "path visibility")
        || !DB_ValidateMaterializedBlock4Span(
            varPathData->nodeTree,
            static_cast<uint32_t>(extents.treeBytes),
            4,
            "path tree")
        || !db::validation::PathNodesRuntimeValid(
            varPathData->nodes,
            varPathData->nodeCount)
        || !db::validation::PathChainMapsRuntimeValid(
            varPathData->chainNodeForNode,
            varPathData->nodeForChainNode,
            varPathData->nodeCount,
            varPathData->chainNodeCount)
        || !db::validation::PathTreeGraphValid(
            varPathData->nodeTree,
            static_cast<uint32_t>(varPathData->nodeTreeCount),
            varPathData->nodeCount,
            disk32::kPathTreeBytes))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file path graph");
        return false;
    }
    return true;
}

bool __cdecl Load_GameWorldSp(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGameWorldSp,
        disk32::kGameWorldSpBytes);
    DB_PushStreamPos(4);
    varXString = &varGameWorldSp->name;
    Load_XString(0);
    varPathData = &varGameWorldSp->path;
    if (!Load_PathData(0))
    {
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_GameWorldMp(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGameWorldMp, 4);
    DB_PushStreamPos(4);
    varXString = &varGameWorldMp->name;
    Load_XString(0);
    DB_PopStreamPos();
}

void __cdecl Load_GameWorldSpPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGameWorldSpPtr, 4);
    DB_PushStreamPos(0);
    if (*varGameWorldSpPtr)
    {
        value = (uint32_t)*varGameWorldSpPtr;
        if (value == -1 || value == -2)
        {
            *varGameWorldSpPtr = (GameWorldSp *)AllocLoad_FxElemVisStateSample();
            if (!*varGameWorldSpPtr
                || !DB_IsStreamRangeValid(
                    *varGameWorldSpPtr,
                    disk32::kGameWorldSpBytes))
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file SP world header");
                *varGameWorldSpPtr = nullptr;
                DB_PopStreamPos();
                return;
            }
            varGameWorldSp = *varGameWorldSpPtr;
            if (value == -2)
            {
                inserted = DB_InsertPointer(DBAliasKind::GameWorldSp);
                if (!inserted)
                {
                    *varGameWorldSpPtr = nullptr;
                    DB_PopStreamPos();
                    return;
                }
            }
            else
                inserted = {};
            if (!Load_GameWorldSp(1))
            {
                *varGameWorldSpPtr = nullptr;
                DB_PopStreamPos();
                return;
            }
            Load_GameWorldSpAsset((XAssetHeader *)varGameWorldSpPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GameWorldSp,
                    *varGameWorldSpPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varGameWorldSpPtr,
                DBAliasKind::GameWorldSp);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_GameWorldMpPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGameWorldMpPtr, 4);
    DB_PushStreamPos(0);
    if (*varGameWorldMpPtr)
    {
        value = (uint32_t)*varGameWorldMpPtr;
        if (value == -1 || value == -2)
        {
            *varGameWorldMpPtr = (GameWorldMp *)AllocLoad_FxElemVisStateSample();
            varGameWorldMp = *varGameWorldMpPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::GameWorldMp);
            else
                inserted = {};
            Load_GameWorldMp(1);
            Load_GameWorldMpAsset((XAssetHeader *)varGameWorldMpPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GameWorldMp,
                    *varGameWorldMpPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varGameWorldMpPtr,
                DBAliasKind::GameWorldMp);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_PathData()
{
    if (varPathData->nodes)
    {
        varpathnode_t = varPathData->nodes;
        Mark_pathnode_tArray(varPathData->nodeCount);
    }
}

void __cdecl Mark_GameWorldSp()
{
    varPathData = &varGameWorldSp->path;
    Mark_PathData();
}

void __cdecl Mark_GameWorldSpPtr()
{
    if (*varGameWorldSpPtr)
    {
        varGameWorldSp = *varGameWorldSpPtr;
        Mark_GameWorldSpAsset(varGameWorldSp);
        Mark_GameWorldSp();
    }
}

void __cdecl Mark_GameWorldMpPtr()
{
    if (*varGameWorldMpPtr)
    {
        varGameWorldMp = *varGameWorldMpPtr;
        Mark_GameWorldMpAsset(varGameWorldMp);
    }
}

void __cdecl Load_FxEffectDefHandle(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varFxEffectDefHandle, 4);
    DB_PushStreamPos(0);
    if (*varFxEffectDefHandle)
    {
        value = (uint32_t)*varFxEffectDefHandle;
        if (value == -1 || value == -2)
        {
            *varFxEffectDefHandle = (const FxEffectDef *)AllocLoad_FxElemVisStateSample();
            varFxEffectDef = (FxEffectDef *)*varFxEffectDefHandle;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::FxEffectDef);
            else
                inserted = {};
            Load_FxEffectDef(1);
            Load_FxEffectDefAsset((XAssetHeader *)varFxEffectDefHandle);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::FxEffectDef,
                    *varFxEffectDefHandle);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varFxEffectDefHandle,
                DBAliasKind::FxEffectDef);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_FxEffectDefHandleArray(bool atStreamStart, int32_t count)
{
    const FxEffectDef **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varFxEffectDefHandle, count, 4);
    var = varFxEffectDefHandle;
    for (i = 0; i < count; ++i)
    {
        varFxEffectDefHandle = var;
        Load_FxEffectDefHandle(0);
        ++var;
    }
}

void __cdecl Load_FxEffectDefRef(bool atStreamStart)
{
    varXString = (const char **)varFxEffectDefRef;
    Load_XString(atStreamStart);
    Load_FxEffectDefFromName((const char **)varFxEffectDefRef);
}

void __cdecl Load_FxElemMarkVisuals(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxElemMarkVisuals, 8);
    varMaterialHandle = (Material **)varFxElemMarkVisuals;
    Load_MaterialHandleArray(0, 2);
}

void __cdecl Load_FxElemMarkVisualsArray(bool atStreamStart, int32_t count)
{
    FxElemMarkVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varFxElemMarkVisuals, count, 8);
    var = varFxElemMarkVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemMarkVisuals = var;
        Load_FxElemMarkVisuals(0);
        ++var;
    }
}

void __cdecl Load_FxElemVisuals(bool atStreamStart)
{
    switch (varFxElemDef->elemType)
    {
    case 5u:
        varXModelPtr = (XModel **)varFxElemVisuals;
        Load_XModelPtr(atStreamStart);
        break;
    case 0xAu:
        varFxEffectDefRef = (FxEffectDefRef *)varFxElemVisuals;
        Load_FxEffectDefRef(atStreamStart);
        break;
    case 8u:
        varXString = (const char **)varFxElemVisuals;
        Load_XString(atStreamStart);
        break;
    default:
        if (varFxElemDef->elemType != 6 && varFxElemDef->elemType != 7)
        {
            varMaterialHandle = (Material **)varFxElemVisuals;
            Load_MaterialHandle(atStreamStart);
        }
        break;
    }
}

void __cdecl Load_FxElemVisualsArray(bool atStreamStart, int32_t count)
{
    FxElemVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varFxElemVisuals, count, 4);
    var = varFxElemVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemVisuals = var;
        Load_FxElemVisuals(0);
        ++var;
    }
}

void __cdecl Load_FxElemVisStateSampleArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, varFxElemVisStateSample->base.color, count, 48);
}

void __cdecl Load_FxElemVelStateSampleArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varFxElemVelStateSample, count, 96);
}

void __cdecl Load_FxElemDefVisuals(bool atStreamStart)
{
    if (varFxElemDef->elemType == 9)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemDefVisuals->markArray = (FxElemMarkVisuals *)AllocLoad_FxElemVisStateSample();
            varFxElemMarkVisuals = varFxElemDefVisuals->markArray;
            Load_FxElemMarkVisualsArray(1, varFxElemDef->visualCount);
        }
    }
    else if (varFxElemDef->visualCount > 1u)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemDefVisuals->markArray = (FxElemMarkVisuals *)AllocLoad_FxElemVisStateSample();
            varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals->markArray;
            Load_FxElemVisualsArray(1, varFxElemDef->visualCount);
        }
    }
    else
    {
        varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals;
        Load_FxElemVisuals(atStreamStart);
    }
}

void __cdecl Load_FxTrailVertexArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varFxTrailVertex, count, 20);
}

void __cdecl Load_FxTrailDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxTrailDef, 28);
    if (varFxTrailDef->verts)
    {
        varFxTrailDef->verts = (FxTrailVertex *)AllocLoad_FxElemVisStateSample();
        varFxTrailVertex = varFxTrailDef->verts;
        Load_FxTrailVertexArray(1, varFxTrailDef->vertCount);
    }
    if (varFxTrailDef->inds)
    {
        varFxTrailDef->inds = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varFxTrailDef->inds;
        Load_ushortArray(1, varFxTrailDef->indCount);
    }
}

void __cdecl Load_FxElemDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxElemDef, 252);
    if (varFxElemDef->velSamples)
    {
        varFxElemDef->velSamples = (FxElemVelStateSample *)AllocLoad_FxElemVisStateSample();
        varFxElemVelStateSample = varFxElemDef->velSamples;
        Load_FxElemVelStateSampleArray(
            1,
            DB_CheckedCountSum(varFxElemDef->velIntervalCount, 1, "effect velocity samples"));
    }
    if (varFxElemDef->visSamples)
    {
        varFxElemDef->visSamples = (FxElemVisStateSample *)AllocLoad_FxElemVisStateSample();
        varFxElemVisStateSample = varFxElemDef->visSamples;
        Load_FxElemVisStateSampleArray(
            1,
            DB_CheckedCountSum(varFxElemDef->visStateIntervalCount, 1, "effect visual samples"));
    }
    varFxElemDefVisuals = &varFxElemDef->visuals;
    Load_FxElemDefVisuals(0);
    varFxEffectDefRef = &varFxElemDef->effectOnImpact;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectOnDeath;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectEmitted;
    Load_FxEffectDefRef(0);
    if (varFxElemDef->trailDef)
    {
        varFxElemDef->trailDef = (FxTrailDef *)AllocLoad_FxElemVisStateSample();
        varFxTrailDef = varFxElemDef->trailDef;
        Load_FxTrailDef(1);
    }
}

void __cdecl Load_FxElemDefArray(bool atStreamStart, int32_t count)
{
    FxElemDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varFxElemDef, count, 252);
    var = varFxElemDef;
    for (i = 0; i < count; ++i)
    {
        varFxElemDef = var;
        Load_FxElemDef(0);
        ++var;
    }
}

void __cdecl Load_FxEffectDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxEffectDef, 32);
    DB_PushStreamPos(4);
    varXString = &varFxEffectDef->name;
    Load_XString(0);
    if (varFxEffectDef->elemDefs)
    {
        varFxEffectDef->elemDefs = (const FxElemDef *)AllocLoad_FxElemVisStateSample();
        varFxElemDef = (FxElemDef*)varFxEffectDef->elemDefs;
        const int32_t emittedAndOneShot = DB_CheckedCountSum(
            varFxEffectDef->elemDefCountEmission,
            varFxEffectDef->elemDefCountOneShot,
            "effect element definitions");
        const int32_t elementCount = DB_CheckedCountSum(
            emittedAndOneShot,
            varFxEffectDef->elemDefCountLooping,
            "effect element definitions");
        Load_FxElemDefArray(1, elementCount);
    }
    DB_PopStreamPos();
}

void __cdecl Mark_FxEffectDefHandle()
{
    if (*varFxEffectDefHandle)
    {
        varFxEffectDef = (FxEffectDef *)*varFxEffectDefHandle;
        Mark_FxEffectDefAsset(varFxEffectDef);
        Mark_FxEffectDef();
    }
}

void __cdecl Mark_FxEffectDefHandleArray(int32_t count)
{
    const FxEffectDef **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxEffectDefHandle;
    for (i = 0; i < count; ++i)
    {
        varFxEffectDefHandle = var;
        Mark_FxEffectDefHandle();
        ++var;
    }
}

void __cdecl Mark_FxElemMarkVisuals()
{
    varMaterialHandle = (Material **)varFxElemMarkVisuals;
    Mark_MaterialHandleArray(2);
}

void __cdecl Mark_FxElemMarkVisualsArray(int32_t count)
{
    FxElemMarkVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemMarkVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemMarkVisuals = var;
        Mark_FxElemMarkVisuals();
        ++var;
    }
}

void __cdecl Mark_FxElemVisuals()
{
    if (varFxElemDef->elemType == 5)
    {
        varXModelPtr = (XModel **)varFxElemVisuals;
        Mark_XModelPtr();
    }
    else if (varFxElemDef->elemType != 10
        && varFxElemDef->elemType != 8
        && varFxElemDef->elemType != 6
        && varFxElemDef->elemType != 7)
    {
        varMaterialHandle = (Material **)varFxElemVisuals;
        Mark_MaterialHandle();
    }
}

void __cdecl Mark_FxElemVisualsArray(int32_t count)
{
    FxElemVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemVisuals = var;
        Mark_FxElemVisuals();
        ++var;
    }
}

void __cdecl Mark_FxElemDefVisuals()
{
    if (varFxElemDef->elemType == 9)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemMarkVisuals = varFxElemDefVisuals->markArray;
            Mark_FxElemMarkVisualsArray(varFxElemDef->visualCount);
        }
    }
    else if (varFxElemDef->visualCount > 1u)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals->markArray;
            Mark_FxElemVisualsArray(varFxElemDef->visualCount);
        }
    }
    else
    {
        varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals;
        Mark_FxElemVisuals();
    }
}

void __cdecl Mark_FxElemDef()
{
    varFxElemDefVisuals = &varFxElemDef->visuals;
    Mark_FxElemDefVisuals();
}

void __cdecl Mark_FxElemDefArray(int32_t count)
{
    FxElemDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemDef;
    for (i = 0; i < count; ++i)
    {
        varFxElemDef = var;
        Mark_FxElemDef();
        ++var;
    }
}

void __cdecl Mark_FxEffectDef()
{
    if (varFxEffectDef->elemDefs)
    {
        varFxElemDef = (FxElemDef*)varFxEffectDef->elemDefs;
        const int32_t emittedAndOneShot = DB_CheckedCountSum(
            varFxEffectDef->elemDefCountEmission,
            varFxEffectDef->elemDefCountOneShot,
            "marked effect element definitions");
        const int32_t elementCount = DB_CheckedCountSum(
            emittedAndOneShot,
            varFxEffectDef->elemDefCountLooping,
            "marked effect element definitions");
        Mark_FxElemDefArray(elementCount);
    }
}

void __cdecl Load_DynEntityDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityDef, 96);
    varXModelPtr = &varDynEntityDef->xModel;
    Load_XModelPtr(0);
    varFxEffectDefHandle = &varDynEntityDef->destroyFx;
    Load_FxEffectDefHandle(0);
    varXModelPiecesPtr = &varDynEntityDef->destroyPieces;
    if (!Load_XModelPiecesPtr(0))
        return;
    varPhysPresetPtr = &varDynEntityDef->physPreset;
    Load_PhysPresetPtr(0);
}

void __cdecl Load_DynEntityDefArray(bool atStreamStart, int32_t count)
{
    DynEntityDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varDynEntityDef, count, 96);
    var = varDynEntityDef;
    for (i = 0; i < count; ++i)
    {
        varDynEntityDef = var;
        Load_DynEntityDef(0);
        ++var;
    }
}

void __cdecl Load_DynEntityCollArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varDynEntityColl, count, 20);
}

void __cdecl Load_DynEntityPoseArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varDynEntityPose, count, 32);
}

void __cdecl Load_DynEntityClientArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varDynEntityClient, count, 12);
}

void __cdecl Mark_DynEntityDef()
{
    varXModelPtr = &varDynEntityDef->xModel;
    Mark_XModelPtr();
    varFxEffectDefHandle = &varDynEntityDef->destroyFx;
    Mark_FxEffectDefHandle();
    varXModelPiecesPtr = &varDynEntityDef->destroyPieces;
    Mark_XModelPiecesPtr();
    varPhysPresetPtr = &varDynEntityDef->physPreset;
    Mark_PhysPresetPtr();
}

void __cdecl Mark_DynEntityDefArray(int32_t count)
{
    DynEntityDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varDynEntityDef;
    for (i = 0; i < count; ++i)
    {
        varDynEntityDef = var;
        Mark_DynEntityDef();
        ++var;
    }
}

void __cdecl Load_MapEnts(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMapEnts, 12);
    DB_PushStreamPos(4);
    varXString = &varMapEnts->name;
    Load_XString(0);
    if (varMapEnts->entityString)
    {
        varMapEnts->entityString = (char *)AllocLoad_raw_byte();
        varchar = varMapEnts->entityString;
        Load_charArray(1, varMapEnts->numEntityChars);
    }
    DB_PopStreamPos();
}

void __cdecl Load_MapEntsPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varMapEntsPtr, 4);
    DB_PushStreamPos(0);
    if (*varMapEntsPtr)
    {
        value = (uint32_t)*varMapEntsPtr;
        if (value == -1 || value == -2)
        {
            *varMapEntsPtr = (MapEnts *)AllocLoad_FxElemVisStateSample();
            varMapEnts = *varMapEntsPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::MapEnts);
            else
                inserted = {};
            Load_MapEnts(1);
            Load_MapEntsAsset((XAssetHeader *)varMapEntsPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::MapEnts,
                    *varMapEntsPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varMapEntsPtr,
                DBAliasKind::MapEnts);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_MapEntsPtr()
{
    if (*varMapEntsPtr)
    {
        varMapEnts = *varMapEntsPtr;
        Mark_MapEntsAsset(varMapEnts);
    }
}

void __cdecl Load_cStaticModel_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcStaticModel_t, 80);
    varXModelPtr = &varcStaticModel_t->xmodel;
    Load_XModelPtr(0);
}

void __cdecl Load_cStaticModel_tArray(bool atStreamStart, int32_t count)
{
    cStaticModel_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varcStaticModel_t, count, 80);
    var = varcStaticModel_t;
    for (i = 0; i < count; ++i)
    {
        varcStaticModel_t = var;
        Load_cStaticModel_t(0);
        ++var;
    }
}

void __cdecl Load_cNode_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcNode_t, 8);
    if (varcNode_t->plane)
    {
        if (varcNode_t->plane == (cplane_s *)-1)
        {
            varcNode_t->plane = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varcNode_t->plane;
            Load_cplane_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varcNode_t->plane,
                20,
                4,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_cNode_tArray(bool atStreamStart, int32_t count)
{
    cNode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varcNode_t, count, 8);
    var = varcNode_t;
    for (i = 0; i < count; ++i)
    {
        varcNode_t = var;
        Load_cNode_t(0);
        ++var;
    }
}

void __cdecl Load_cLeaf_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varcLeaf_t, count, 44);
}

void __cdecl Load_cLeafBrushNodeLeaf_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcLeafBrushNodeLeaf_t, 4);
    const uint32_t brushIndexByteCount = DB_CheckedDirectSpanBytes(
        varcLeafBrushNode_t->leafBrushCount,
        2,
        "leaf-brush node indices");
    if (varcLeafBrushNodeLeaf_t->brushes)
    {
        if (varcLeafBrushNodeLeaf_t->brushes == (uint16_t *)-1)
        {
            varcLeafBrushNodeLeaf_t->brushes = (uint16_t *)AllocLoad_XBlendInfo();
            varLeafBrush = varcLeafBrushNodeLeaf_t->brushes;
            Load_LeafBrushArray(1, varcLeafBrushNode_t->leafBrushCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varcLeafBrushNodeLeaf_t->brushes,
                brushIndexByteCount,
                2,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_cLeafBrushNodeChildren_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcLeafBrushNodeChildren_t, 12);
}

void __cdecl Load_cLeafBrushNodeData_t(bool atStreamStart)
{
    if (varcLeafBrushNode_t->leafBrushCount <= 0)
    {
        if (atStreamStart)
        {
            varcLeafBrushNodeChildren_t = (cLeafBrushNodeChildren_t *)varcLeafBrushNodeData_t;
            Load_cLeafBrushNodeChildren_t(atStreamStart);
        }
    }
    else
    {
        varcLeafBrushNodeLeaf_t = &varcLeafBrushNodeData_t->leaf;
        Load_cLeafBrushNodeLeaf_t(atStreamStart);
    }
}

void __cdecl Load_cLeafBrushNode_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varcLeafBrushNode_t->axis, 20);
    varcLeafBrushNodeData_t = &varcLeafBrushNode_t->data;
    Load_cLeafBrushNodeData_t(0);
}

void __cdecl Load_cLeafBrushNode_tArray(bool atStreamStart, int32_t count)
{
    cLeafBrushNode_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, &varcLeafBrushNode_t->axis, count, 20);
    var = varcLeafBrushNode_t;
    for (i = 0; i < count; ++i)
    {
        varcLeafBrushNode_t = var;
        Load_cLeafBrushNode_t(0);
        ++var;
    }
}

void __cdecl Load_CollisionBorder(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varCollisionBorder, 28);
}

void __cdecl Load_CollisionBorderArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varCollisionBorder, count, 28);
}

void __cdecl Load_CollisionPartition(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varCollisionPartition->triCount, 12);
    const uint32_t borderByteCount = DB_CheckedDirectSpanBytes(
        varCollisionPartition->borderCount,
        28,
        "collision partition borders");
    if (varCollisionPartition->borders)
    {
        if (varCollisionPartition->borders == (CollisionBorder *)-1)
        {
            varCollisionPartition->borders = (CollisionBorder *)AllocLoad_FxElemVisStateSample();
            varCollisionBorder = varCollisionPartition->borders;
            Load_CollisionBorderArray(1, varCollisionPartition->borderCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varCollisionPartition->borders,
                borderByteCount,
                4,
                kDirectBlock4);
        }
    }
}

void __cdecl Load_CollisionPartitionArray(bool atStreamStart, int32_t count)
{
    CollisionPartition *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, &varCollisionPartition->triCount, count, 12);
    var = varCollisionPartition;
    for (i = 0; i < count; ++i)
    {
        varCollisionPartition = var;
        Load_CollisionPartition(0);
        ++var;
    }
}

void __cdecl Load_CollisionAabbTreeArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varCollisionAabbTree, count, 32);
}

void __cdecl Load_cmodel_tArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varcmodel_t, count, 72);
}

bool __cdecl Load_cbrush_t(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varcbrush_t,
        disk32::kCBrushBytes);
    if (varcbrush_t->numsides
            > db::validation::kMaxClipMapBrushNonaxialSides
        || (varcbrush_t->numsides != 0) != (varcbrush_t->sides != nullptr))
    {
        Com_Error(ERR_DROP, "Invalid fast-file clipmap brush side count");
        return false;
    }
    if (varcbrush_t->sides)
    {
        const uint32_t sideBytes = DB_CheckedDirectSpanBytes(
            varcbrush_t->numsides,
            disk32::kCBrushSideBytes,
            "clipmap brush sides");
        if (varcbrush_t->sides == (cbrushside_t *)-1
            || varcbrush_t->sides == (cbrushside_t *)-2)
        {
            Com_Error(ERR_DROP, "Invalid inline fast-file clipmap brush sides");
            return false;
        }
        if (!DB_ResolveDirectPointer(
                &varcbrush_t->sides,
                sideBytes,
                4,
                kDirectBlock4,
                "clipmap brush sides"))
        {
            return false;
        }
    }

    uint32_t adjacencyBytes = 0;
    if (!db::validation::ClipMapBrushAdjacencyPrefixExtent(
            *varcbrush_t,
            &adjacencyBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file clipmap brush adjacency layout");
        return false;
    }
    if ((adjacencyBytes != 0) && !varcbrush_t->baseAdjacentSide)
    {
        Com_Error(ERR_DROP, "Missing fast-file clipmap brush adjacency");
        return false;
    }
    if (varcbrush_t->baseAdjacentSide)
    {
        if (varcbrush_t->baseAdjacentSide == (uint8_t *)-1
            || varcbrush_t->baseAdjacentSide == (uint8_t *)-2)
        {
            Com_Error(ERR_DROP, "Invalid inline fast-file clipmap brush adjacency");
            return false;
        }
        if (!DB_ResolveDirectPointer(
                &varcbrush_t->baseAdjacentSide,
                adjacencyBytes,
                1,
                kDirectBlock4,
                "clipmap brush adjacency"))
        {
            return false;
        }
    }
    uint32_t validatedAdjacencyBytes = 0;
    if (!DB_GetClipBrushAdjacencyBytes(
            varcbrush_t,
            &validatedAdjacencyBytes)
        || validatedAdjacencyBytes != adjacencyBytes)
    {
        Com_Error(ERR_DROP, "Invalid fast-file clipmap brush adjacency data");
        return false;
    }
    return true;
}

bool __cdecl Load_cbrush_tArray(bool atStreamStart, int32_t count)
{
    cbrush_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varcbrush_t,
        count,
        disk32::kCBrushBytes);
    var = varcbrush_t;
    for (i = 0; i < count; ++i)
    {
        varcbrush_t = var;
        if (!Load_cbrush_t(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_LeafBrushArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varLeafBrush, count, 2);
}

bool __cdecl Load_clipMap_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varclipMap_t, 284);
    db::validation::ClipMapBrushLayoutExtents brushExtents = {};
    if (!db::validation::ClipMapBrushLayoutValid(
            *varclipMap_t,
            &brushExtents))
    {
        Com_Error(ERR_DROP, "Invalid fast-file clipmap brush layout");
        return false;
    }
    // These dimensions remain runtime invariants even when an optional payload is absent.
    const int32_t triangleIndexCount = DB_CheckedCountProduct(
        3,
        varclipMap_t->triCount,
        "clipmap triangle indices");
    const int32_t walkableWordCount = DB_CheckedCountCeilDiv(
        triangleIndexCount,
        32,
        "clipmap walkable-edge words");
    const int32_t walkableByteCount = DB_CheckedCountProduct(
        4,
        walkableWordCount,
        "clipmap walkable-edge bytes");
    const int32_t visibilityByteCount = DB_CheckedCountProduct(
        varclipMap_t->numClusters,
        varclipMap_t->clusterBytes,
        "clipmap visibility");
    const uint32_t planeByteCount =
        static_cast<uint32_t>(brushExtents.planeBytes);
    DB_PushStreamPos(4);
    varXString = &varclipMap_t->name;
    Load_XString(0);
    if (varclipMap_t->planes)
    {
        if (varclipMap_t->planes == (cplane_s *)-1)
        {
            varclipMap_t->planes = (cplane_s *)AllocLoad_FxElemVisStateSample();
            if (!varclipMap_t->planes
                || !DB_IsStreamRangeValid(
                    varclipMap_t->planes,
                    planeByteCount))
            {
                Com_Error(ERR_DROP, "Fast-file clipmap planes exceed block 4");
                DB_PopStreamPos();
                return false;
            }
            varcplane_t = varclipMap_t->planes;
            Load_cplane_tArray(1, varclipMap_t->planeCount);
        }
        else if (!DB_ResolveDirectPointer(
                &varclipMap_t->planes,
                planeByteCount,
                4,
                kDirectBlock4,
                "clipmap planes"))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    if (varclipMap_t->staticModelList)
    {
        varclipMap_t->staticModelList = (cStaticModel_s *)AllocLoad_FxElemVisStateSample();
        varcStaticModel_t = varclipMap_t->staticModelList;
        Load_cStaticModel_tArray(1, varclipMap_t->numStaticModels);
    }
    if (varclipMap_t->materials)
    {
        varclipMap_t->materials = (dmaterial_t *)AllocLoad_FxElemVisStateSample();
        if (!varclipMap_t->materials
            || !DB_IsStreamRangeValid(
                varclipMap_t->materials,
                static_cast<uint32_t>(brushExtents.materialBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file clipmap materials exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        vardmaterial_t = varclipMap_t->materials;
        Load_dmaterial_tArray(
            1,
            static_cast<int32_t>(varclipMap_t->numMaterials));
    }
    if (varclipMap_t->brushsides)
    {
        varclipMap_t->brushsides = (cbrushside_t *)AllocLoad_FxElemVisStateSample();
        if (!varclipMap_t->brushsides
            || !DB_IsStreamRangeValid(
                varclipMap_t->brushsides,
                static_cast<uint32_t>(brushExtents.brushSideBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file clipmap brush sides exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varcbrushside_t = varclipMap_t->brushsides;
        if (!Load_cbrushside_tArray(
                1,
                static_cast<int32_t>(varclipMap_t->numBrushSides)))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    if (varclipMap_t->brushEdges)
    {
        varclipMap_t->brushEdges = AllocLoad_raw_byte();
        if (!varclipMap_t->brushEdges
            || !DB_IsStreamRangeValid(
                varclipMap_t->brushEdges,
                static_cast<uint32_t>(brushExtents.brushEdgeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file clipmap brush edges exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varcbrushedge_t = varclipMap_t->brushEdges;
        Load_cbrushedge_tArray(
            1,
            static_cast<int32_t>(varclipMap_t->numBrushEdges));
    }
    if (varclipMap_t->nodes)
    {
        varclipMap_t->nodes = (cNode_t *)AllocLoad_FxElemVisStateSample();
        varcNode_t = varclipMap_t->nodes;
        Load_cNode_tArray(1, varclipMap_t->numNodes);
    }
    if (varclipMap_t->leafs)
    {
        varclipMap_t->leafs = (cLeaf_t *)AllocLoad_FxElemVisStateSample();
        varcLeaf_t = varclipMap_t->leafs;
        Load_cLeaf_tArray(1, varclipMap_t->numLeafs);
    }
    if (varclipMap_t->leafbrushes)
    {
        varclipMap_t->leafbrushes = (uint16_t *)AllocLoad_XBlendInfo();
        varLeafBrush = varclipMap_t->leafbrushes;
        Load_LeafBrushArray(1, varclipMap_t->numLeafBrushes);
    }
    if (varclipMap_t->leafbrushNodes)
    {
        varclipMap_t->leafbrushNodes = (cLeafBrushNode_s *)AllocLoad_FxElemVisStateSample();
        varcLeafBrushNode_t = varclipMap_t->leafbrushNodes;
        Load_cLeafBrushNode_tArray(1, varclipMap_t->leafbrushNodesCount);
    }
    if (varclipMap_t->leafsurfaces)
    {
        varclipMap_t->leafsurfaces = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varuint = varclipMap_t->leafsurfaces;
        Load_uintArray(1, varclipMap_t->numLeafSurfaces);
    }
    if (varclipMap_t->verts)
    {
        varclipMap_t->verts = (float (*)[3])AllocLoad_FxElemVisStateSample();
        varvec3_t = varclipMap_t->verts;
        Load_vec3_tArray(1, varclipMap_t->vertCount);
    }
    if (varclipMap_t->triIndices)
    {
        varclipMap_t->triIndices = (uint16_t *)AllocLoad_XBlendInfo();
        varUnsignedShort = varclipMap_t->triIndices;
        Load_UnsignedShortArray(1, triangleIndexCount);
    }
    if (varclipMap_t->triEdgeIsWalkable)
    {
        varclipMap_t->triEdgeIsWalkable = AllocLoad_raw_byte();
        varbyte = varclipMap_t->triEdgeIsWalkable;
        Load_byteArray(1, walkableByteCount);
    }
    if (varclipMap_t->borders)
    {
        varclipMap_t->borders = (CollisionBorder *)AllocLoad_FxElemVisStateSample();
        varCollisionBorder = varclipMap_t->borders;
        Load_CollisionBorderArray(1, varclipMap_t->borderCount);
    }
    if (varclipMap_t->partitions)
    {
        varclipMap_t->partitions = (CollisionPartition *)AllocLoad_FxElemVisStateSample();
        varCollisionPartition = varclipMap_t->partitions;
        Load_CollisionPartitionArray(1, varclipMap_t->partitionCount);
    }
    if (varclipMap_t->aabbTrees)
    {
        varclipMap_t->aabbTrees = (CollisionAabbTree *)AllocLoad_FxElemVisStateSample();
        varCollisionAabbTree = varclipMap_t->aabbTrees;
        Load_CollisionAabbTreeArray(1, varclipMap_t->aabbTreeCount);
    }
    if (varclipMap_t->cmodels)
    {
        varclipMap_t->cmodels = (cmodel_t *)AllocLoad_FxElemVisStateSample();
        varcmodel_t = varclipMap_t->cmodels;
        Load_cmodel_tArray(1, varclipMap_t->numSubModels);
    }
    if (varclipMap_t->brushes)
    {
        varclipMap_t->brushes = AllocLoad_GfxPackedVertex0();
        if (!varclipMap_t->brushes
            || !DB_IsStreamRangeValid(
                varclipMap_t->brushes,
                static_cast<uint32_t>(brushExtents.brushBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file clipmap brushes exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varcbrush_t = varclipMap_t->brushes;
        if (!Load_cbrush_tArray(1, varclipMap_t->numBrushes))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    if (varclipMap_t->visibility)
    {
        varclipMap_t->visibility = AllocLoad_raw_byte();
        varbyte = varclipMap_t->visibility;
        Load_byteArray(1, visibilityByteCount);
    }
    varMapEntsPtr = &varclipMap_t->mapEnts;
    Load_MapEntsPtr(0);
    DBAliasHandle completedBoxBrush;
    if (varclipMap_t->box_brush)
    {
        if (varclipMap_t->box_brush == (cbrush_t *)-1)
        {
            varclipMap_t->box_brush = AllocLoad_GfxPackedVertex0();
            if (!varclipMap_t->box_brush
                || !DB_IsStreamRangeValid(
                    varclipMap_t->box_brush,
                    disk32::kCBrushBytes))
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file clipmap box brush");
                DB_PopStreamPos();
                return false;
            }
            completedBoxBrush = DB_RegisterPointerSlot(
                varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush);
            if (!completedBoxBrush)
            {
                DB_PopStreamPos();
                return false;
            }
            varcbrush_t = varclipMap_t->box_brush;
            if (!Load_cbrush_t(1))
            {
                DB_PopStreamPos();
                return false;
            }
        }
        else if (!DB_ResolveCompletedPointer(
                &varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush,
                disk32::kCBrushBytes,
                "clipmap box brush"))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    uint64_t ordinaryBoxIndex = 0;
    if (!DB_ValidateMaterializedBlock4Span(
            varclipMap_t->planes,
            static_cast<uint32_t>(brushExtents.planeBytes),
            4,
            "clipmap planes")
        || !DB_ValidateMaterializedBlock4Span(
            varclipMap_t->materials,
            static_cast<uint32_t>(brushExtents.materialBytes),
            4,
            "clipmap materials")
        || !DB_ValidateMaterializedBlock4Span(
            varclipMap_t->brushsides,
            static_cast<uint32_t>(brushExtents.brushSideBytes),
            4,
            "clipmap brush sides")
        || !DB_ValidateMaterializedBlock4Span(
            varclipMap_t->brushEdges,
            static_cast<uint32_t>(brushExtents.brushEdgeBytes),
            1,
            "clipmap brush edges")
        || !DB_ValidateMaterializedBlock4Span(
            varclipMap_t->brushes,
            static_cast<uint32_t>(brushExtents.brushBytes),
            16,
            "clipmap brushes")
        || !db::validation::ClipMapBrushGraphValid(*varclipMap_t)
        || !DB_ValidateClipMapBoxBrush(varclipMap_t->box_brush)
        || db::validation::ExactArrayElementIndex(
            varclipMap_t->brushes,
            varclipMap_t->numBrushes,
            varclipMap_t->box_brush,
            &ordinaryBoxIndex))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file clipmap brush graph");
        DB_PopStreamPos();
        return false;
    }
    if (completedBoxBrush
        && !DB_CompleteObject(
            completedBoxBrush,
            DBAliasKind::ClipMapBoxBrush,
            varclipMap_t->box_brush,
            disk32::kCBrushBytes,
            disk32::kCBrushBytes))
    {
        varclipMap_t->box_brush = nullptr;
        DB_PopStreamPos();
        return false;
    }
    if (varclipMap_t->dynEntDefList[0])
    {
        varclipMap_t->dynEntDefList[0] = (DynEntityDef *)AllocLoad_FxElemVisStateSample();
        varDynEntityDef = varclipMap_t->dynEntDefList[0];
        Load_DynEntityDefArray(1, varclipMap_t->dynEntCount[0]);
    }
    if (varclipMap_t->dynEntDefList[1])
    {
        varclipMap_t->dynEntDefList[1] = (DynEntityDef *)AllocLoad_FxElemVisStateSample();
        varDynEntityDef = varclipMap_t->dynEntDefList[1];
        Load_DynEntityDefArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntPoseList[0])
    {
        varclipMap_t->dynEntPoseList[0] = (DynEntityPose *)AllocLoad_FxElemVisStateSample();
        varDynEntityPose = varclipMap_t->dynEntPoseList[0];
        Load_DynEntityPoseArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntPoseList[1])
    {
        varclipMap_t->dynEntPoseList[1] = (DynEntityPose *)AllocLoad_FxElemVisStateSample();
        varDynEntityPose = varclipMap_t->dynEntPoseList[1];
        Load_DynEntityPoseArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntClientList[0])
    {
        varclipMap_t->dynEntClientList[0] = (DynEntityClient *)AllocLoad_FxElemVisStateSample();
        varDynEntityClient = varclipMap_t->dynEntClientList[0];
        Load_DynEntityClientArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntClientList[1])
    {
        varclipMap_t->dynEntClientList[1] = (DynEntityClient *)AllocLoad_FxElemVisStateSample();
        varDynEntityClient = varclipMap_t->dynEntClientList[1];
        Load_DynEntityClientArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntCollList[0])
    {
        varclipMap_t->dynEntCollList[0] = (DynEntityColl *)AllocLoad_FxElemVisStateSample();
        varDynEntityColl = varclipMap_t->dynEntCollList[0];
        Load_DynEntityCollArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntCollList[1])
    {
        varclipMap_t->dynEntCollList[1] = (DynEntityColl *)AllocLoad_FxElemVisStateSample();
        varDynEntityColl = varclipMap_t->dynEntCollList[1];
        Load_DynEntityCollArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_clipMap_ptr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varclipMap_ptr, 4);
    DB_PushStreamPos(0);
    if (*varclipMap_ptr)
    {
        value = (uint32_t)*varclipMap_ptr;
        if (value == -1 || value == -2)
        {
            *varclipMap_ptr = (clipMap_t *)AllocLoad_FxElemVisStateSample();
            if (!*varclipMap_ptr)
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file clipmap header");
                DB_PopStreamPos();
                return;
            }
            varclipMap_t = *varclipMap_ptr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::ClipMap);
            else
                inserted = {};
            if (!Load_clipMap_t(1))
            {
                *varclipMap_ptr = nullptr;
                DB_PopStreamPos();
                return;
            }
            Load_ClipMapAsset((XAssetHeader *)varclipMap_ptr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::ClipMap,
                    *varclipMap_ptr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varclipMap_ptr,
                DBAliasKind::ClipMap);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_cStaticModel_t()
{
    varXModelPtr = &varcStaticModel_t->xmodel;
    Mark_XModelPtr();
}

void __cdecl Mark_cStaticModel_tArray(int32_t count)
{
    cStaticModel_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varcStaticModel_t;
    for (i = 0; i < count; ++i)
    {
        varcStaticModel_t = var;
        Mark_cStaticModel_t();
        ++var;
    }
}

void __cdecl Mark_clipMap_t()
{
    if (varclipMap_t->staticModelList)
    {
        varcStaticModel_t = varclipMap_t->staticModelList;
        Mark_cStaticModel_tArray(varclipMap_t->numStaticModels);
    }
    varMapEntsPtr = &varclipMap_t->mapEnts;
    Mark_MapEntsPtr();
    if (varclipMap_t->dynEntDefList[0])
    {
        varDynEntityDef = varclipMap_t->dynEntDefList[0];
        Mark_DynEntityDefArray(varclipMap_t->dynEntCount[0]);
    }
    if (varclipMap_t->dynEntDefList[1])
    {
        varDynEntityDef = varclipMap_t->dynEntDefList[1];
        Mark_DynEntityDefArray(varclipMap_t->dynEntCount[1]);
    }
}

void __cdecl Mark_clipMap_ptr()
{
    if (*varclipMap_ptr)
    {
        varclipMap_t = *varclipMap_ptr;
        Mark_ClipMapAsset(varclipMap_t);
        Mark_clipMap_t();
    }
}

void __cdecl Load_ComPrimaryLight(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varComPrimaryLight->type, 68);
    varXString = &varComPrimaryLight->defName;
    Load_XString(0);
}

void __cdecl Load_ComPrimaryLightArray(bool atStreamStart, int32_t count)
{
    ComPrimaryLight *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, &varComPrimaryLight->type, count, 68);
    var = varComPrimaryLight;
    for (i = 0; i < count; ++i)
    {
        varComPrimaryLight = var;
        Load_ComPrimaryLight(0);
        ++var;
    }
}

void __cdecl Load_ComWorld(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varComWorld, 16);
    DB_PushStreamPos(4);
    varXString = &varComWorld->name;
    Load_XString(0);
    if (varComWorld->primaryLights)
    {
        varComWorld->primaryLights = (ComPrimaryLight *)AllocLoad_FxElemVisStateSample();
        varComPrimaryLight = varComWorld->primaryLights;
        Load_ComPrimaryLightArray(1, varComWorld->primaryLightCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_ComWorldPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varComWorldPtr, 4);
    DB_PushStreamPos(0);
    if (*varComWorldPtr)
    {
        value = (uint32_t)*varComWorldPtr;
        if (value == -1 || value == -2)
        {
            *varComWorldPtr = (ComWorld *)AllocLoad_FxElemVisStateSample();
            varComWorld = *varComWorldPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::ComWorld);
            else
                inserted = {};
            Load_ComWorld(1);
            Load_ComWorldAsset((XAssetHeader *)varComWorldPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::ComWorld,
                    *varComWorldPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varComWorldPtr,
                DBAliasKind::ComWorld);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_ComWorldPtr()
{
    if (*varComWorldPtr)
    {
        varComWorld = *varComWorldPtr;
        Mark_ComWorldAsset(varComWorld);
    }
}

void __cdecl Load_operandInternalDataUnion(bool atStreamStart)
{
    if (varOperand->dataType)
    {
        if (varOperand->dataType == VAL_FLOAT)
        {
            if (atStreamStart)
            {
                varfloat = &varoperandInternalDataUnion->floatVal;
                Load_float(atStreamStart);
            }
        }
        else if (varOperand->dataType == VAL_STRING)
        {
            varXString = (const char **)varoperandInternalDataUnion;
            Load_XString(atStreamStart);
        }
    }
    else if (atStreamStart)
    {
        varint = varoperandInternalDataUnion;
        Load_int(atStreamStart);
    }
}

void __cdecl Load_Operand(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varOperand, 8);
    varoperandInternalDataUnion = &varOperand->internals;
    Load_operandInternalDataUnion(0);
}

void __cdecl Load_Operator(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varOperator, 4);
}

void __cdecl Load_entryInternalData(bool atStreamStart)
{
    if (varexpressionEntry->type)
    {
        varOperand = (Operand *)varentryInternalData;
        Load_Operand(atStreamStart);
    }
    else if (atStreamStart)
    {
        varOperator = &varentryInternalData->op;
        Load_Operator(atStreamStart);
    }
}

void __cdecl Load_expressionEntry(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varexpressionEntry, 12);
    varentryInternalData = &varexpressionEntry->data;
    Load_entryInternalData(0);
}

void __cdecl Load_expressionEntry_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varexpressionEntry_ptr, 4);
    if (*varexpressionEntry_ptr)
    {
        *varexpressionEntry_ptr = (expressionEntry *)AllocLoad_FxElemVisStateSample();
        varexpressionEntry = *varexpressionEntry_ptr;
        Load_expressionEntry(1);
    }
}

void __cdecl Load_expressionEntry_ptrArray(bool atStreamStart, int32_t count)
{
    expressionEntry **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varexpressionEntry_ptr, count, 4);
    var = varexpressionEntry_ptr;
    for (i = 0; i < count; ++i)
    {
        varexpressionEntry_ptr = var;
        Load_expressionEntry_ptr(0);
        ++var;
    }
}

void __cdecl Load_statement(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varstatement, 8);
    if (varstatement->entries)
    {
        varstatement->entries = (expressionEntry **)AllocLoad_FxElemVisStateSample();
        varexpressionEntry_ptr = varstatement->entries;
        Load_expressionEntry_ptrArray(1, varstatement->numEntries);
    }
}

void __cdecl Load_listBoxDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varlistBoxDef_t, 340);
    varXString = &varlistBoxDef_t->doubleClick;
    Load_XString(0);
    varMaterialHandle = &varlistBoxDef_t->selectIcon;
    Load_MaterialHandle(0);
}

void __cdecl Load_listBoxDef_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varlistBoxDef_ptr, 4);
    if (*varlistBoxDef_ptr)
    {
        *varlistBoxDef_ptr = (listBoxDef_s *)AllocLoad_FxElemVisStateSample();
        varlistBoxDef_t = *varlistBoxDef_ptr;
        Load_listBoxDef_t(1);
    }
}

void __cdecl Load_editFieldDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)vareditFieldDef_t, 32);
}

void __cdecl Load_editFieldDef_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)vareditFieldDef_ptr, 4);
    if (*vareditFieldDef_ptr)
    {
        *vareditFieldDef_ptr = (editFieldDef_s *)AllocLoad_FxElemVisStateSample();
        vareditFieldDef_t = *vareditFieldDef_ptr;
        Load_editFieldDef_t(1);
    }
}

void __cdecl Load_multiDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varmultiDef_t, 392);
    varXString = (const char **)varmultiDef_t;
    Load_XStringArray(0, 32);
    varXString = varmultiDef_t->dvarStr;
    Load_XStringArray(0, 32);
}

void __cdecl Load_multiDef_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varmultiDef_ptr, 4);
    if (*varmultiDef_ptr)
    {
        *varmultiDef_ptr = (multiDef_s *)AllocLoad_FxElemVisStateSample();
        varmultiDef_t = *varmultiDef_ptr;
        Load_multiDef_t(1);
    }
}

void __cdecl Load_windowDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varwindowDef_t, 156);
    varXString = &varwindowDef_t->name;
    Load_XString(0);
    varXString = &varwindowDef_t->group;
    Load_XString(0);
    varMaterialHandle = &varwindowDef_t->background;
    Load_MaterialHandle(0);
}

void __cdecl Load_Window(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varWindow, 156);
    varwindowDef_t = varWindow;
    Load_windowDef_t(0);
}

void __cdecl Load_ItemKeyHandler(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varItemKeyHandler, 12);
    varXString = &varItemKeyHandler->action;
    Load_XString(0);
    if (varItemKeyHandler->next)
    {
        varItemKeyHandler->next = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
        varItemKeyHandlerNext = varItemKeyHandler->next;
        Load_ItemKeyHandlerNext(1);
    }
}

void __cdecl Load_ItemKeyHandlerNext(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varItemKeyHandlerNext, 12);
    varItemKeyHandler = varItemKeyHandlerNext;
    Load_ItemKeyHandler(0);
}

void __cdecl Load_itemDefData_t(bool atStreamStart)
{
    switch (varitemDef_t->type)
    {
    case 6:
        varlistBoxDef_ptr = &varitemDefData_t->listBox;
        Load_listBoxDef_ptr(atStreamStart);
        break;
    case 4:
    case 9:
    case 0x10:
    case 0x12:
    case 0xB:
    case 0xE:
    case 0xA:
    case 0:
    case 0x11:
        vareditFieldDef_ptr = (editFieldDef_s **)varitemDefData_t;
        Load_editFieldDef_ptr(atStreamStart);
        break;
    case 0xC:
        varmultiDef_ptr = (multiDef_s **)varitemDefData_t;
        Load_multiDef_ptr(atStreamStart);
        break;
    case 0xD:
        varXString = (const char **)varitemDefData_t;
        Load_XString(atStreamStart);
        break;
    }
}

void __cdecl Load_itemDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varitemDef_t, 372);
    varWindow = &varitemDef_t->window;
    Load_Window(0);
    varXString = &varitemDef_t->text;
    Load_XString(0);
    varXString = &varitemDef_t->mouseEnterText;
    Load_XString(0);
    varXString = &varitemDef_t->mouseExitText;
    Load_XString(0);
    varXString = &varitemDef_t->mouseEnter;
    Load_XString(0);
    varXString = &varitemDef_t->mouseExit;
    Load_XString(0);
    varXString = &varitemDef_t->action;
    Load_XString(0);
    varXString = &varitemDef_t->onAccept;
    Load_XString(0);
    varXString = &varitemDef_t->onFocus;
    Load_XString(0);
    varXString = &varitemDef_t->leaveFocus;
    Load_XString(0);
    varXString = &varitemDef_t->dvar;
    Load_XString(0);
    varXString = &varitemDef_t->dvarTest;
    Load_XString(0);
    if (varitemDef_t->onKey)
    {
        varitemDef_t->onKey = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
        varItemKeyHandler = varitemDef_t->onKey;
        Load_ItemKeyHandler(1);
    }
    varXString = &varitemDef_t->enableDvar;
    Load_XString(0);
    varsnd_alias_list_ptr = &varitemDef_t->focusSound;
    Load_snd_alias_list_ptr(0);
    varitemDefData_t = &varitemDef_t->typeData;
    Load_itemDefData_t(0);
    varstatement = &varitemDef_t->visibleExp;
    Load_statement(0);
    varstatement = &varitemDef_t->textExp;
    Load_statement(0);
    varstatement = &varitemDef_t->materialExp;
    Load_statement(0);
    varstatement = &varitemDef_t->rectXExp;
    Load_statement(0);
    varstatement = &varitemDef_t->rectYExp;
    Load_statement(0);
    varstatement = &varitemDef_t->rectWExp;
    Load_statement(0);
    varstatement = &varitemDef_t->rectHExp;
    Load_statement(0);
    varstatement = &varitemDef_t->forecolorAExp;
    Load_statement(0);
}

void __cdecl Load_itemDef_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varitemDef_ptr, 4);
    if (*varitemDef_ptr)
    {
        *varitemDef_ptr = (itemDef_s *)AllocLoad_FxElemVisStateSample();
        varitemDef_t = *varitemDef_ptr;
        Load_itemDef_t(1);
    }
}

void __cdecl Load_itemDef_ptrArray(bool atStreamStart, int32_t count)
{
    itemDef_s **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varitemDef_ptr, count, 4);
    var = varitemDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varitemDef_ptr = var;
        Load_itemDef_ptr(0);
        ++var;
    }
}

void __cdecl Load_menuDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varmenuDef_t, 284);
    DB_PushStreamPos(4);
    varWindow = &varmenuDef_t->window;
    Load_Window(0);
    varXString = &varmenuDef_t->font;
    Load_XString(0);
    varXString = &varmenuDef_t->onOpen;
    Load_XString(0);
    varXString = &varmenuDef_t->onClose;
    Load_XString(0);
    varXString = &varmenuDef_t->onESC;
    Load_XString(0);
    if (varmenuDef_t->onKey)
    {
        varmenuDef_t->onKey = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
        varItemKeyHandler = varmenuDef_t->onKey;
        Load_ItemKeyHandler(1);
    }
    varstatement = &varmenuDef_t->visibleExp;
    Load_statement(0);
    varXString = &varmenuDef_t->allowedBinding;
    Load_XString(0);
    varXString = &varmenuDef_t->soundName;
    Load_XString(0);
    varstatement = &varmenuDef_t->rectXExp;
    Load_statement(0);
    varstatement = &varmenuDef_t->rectYExp;
    Load_statement(0);
    if (varmenuDef_t->items)
    {
        varmenuDef_t->items = (itemDef_s **)AllocLoad_FxElemVisStateSample();
        varitemDef_ptr = varmenuDef_t->items;
        Load_itemDef_ptrArray(1, varmenuDef_t->itemCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_menuDef_ptr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varmenuDef_ptr, 4);
    DB_PushStreamPos(0);
    if (*varmenuDef_ptr)
    {
        value = (uint32_t)*varmenuDef_ptr;
        if (value == -1 || value == -2)
        {
            *varmenuDef_ptr = (menuDef_t *)AllocLoad_FxElemVisStateSample();
            varmenuDef_t = *varmenuDef_ptr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::MenuDef);
            else
                inserted = {};
            Load_menuDef_t(1);
            Load_MenuAsset((XAssetHeader *)varmenuDef_ptr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::MenuDef,
                    *varmenuDef_ptr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varmenuDef_ptr,
                DBAliasKind::MenuDef);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_menuDef_ptrArray(bool atStreamStart, int32_t count)
{
    menuDef_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varmenuDef_ptr, count, 4);
    var = varmenuDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varmenuDef_ptr = var;
        Load_menuDef_ptr(0);
        ++var;
    }
}

void __cdecl Load_MenuList(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMenuList, 12);
    DB_PushStreamPos(4);
    varXString = &varMenuList->name;
    Load_XString(0);
    if (varMenuList->menus)
    {
        varMenuList->menus = (menuDef_t **)AllocLoad_FxElemVisStateSample();
        varmenuDef_ptr = varMenuList->menus;
        Load_menuDef_ptrArray(1, varMenuList->menuCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_MenuListPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varMenuListPtr, 4);
    DB_PushStreamPos(0);
    if (*varMenuListPtr)
    {
        value = (uint32_t)*varMenuListPtr;
        if (value == -1 || value == -2)
        {
            *varMenuListPtr = (MenuList *)AllocLoad_FxElemVisStateSample();
            varMenuList = *varMenuListPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::MenuList);
            else
                inserted = {};
            Load_MenuList(1);
            Load_MenuListAsset((XAssetHeader *)varMenuListPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::MenuList,
                    *varMenuListPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varMenuListPtr,
                DBAliasKind::MenuList);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_listBoxDef_t()
{
    varMaterialHandle = &varlistBoxDef_t->selectIcon;
    Mark_MaterialHandle();
}

void __cdecl Mark_listBoxDef_ptr()
{
    if (*varlistBoxDef_ptr)
    {
        varlistBoxDef_t = *varlistBoxDef_ptr;
        Mark_listBoxDef_t();
    }
}

void __cdecl Mark_windowDef_t()
{
    varMaterialHandle = &varwindowDef_t->background;
    Mark_MaterialHandle();
}

void __cdecl Mark_Window()
{
    varwindowDef_t = varWindow;
    Mark_windowDef_t();
}

void __cdecl Mark_itemDefData_t()
{
    if (varitemDef_t->type == 6)
    {
        varlistBoxDef_ptr = &varitemDefData_t->listBox;
        Mark_listBoxDef_ptr();
    }
}

void __cdecl Mark_itemDef_t()
{
    varWindow = &varitemDef_t->window;
    Mark_Window();
    varsnd_alias_list_ptr = &varitemDef_t->focusSound;
    Mark_snd_alias_list_ptr();
    varitemDefData_t = &varitemDef_t->typeData;
    Mark_itemDefData_t();
}

void __cdecl Mark_itemDef_ptr()
{
    if (*varitemDef_ptr)
    {
        varitemDef_t = *varitemDef_ptr;
        Mark_itemDef_t();
    }
}

void __cdecl Mark_itemDef_ptrArray(int32_t count)
{
    itemDef_s **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varitemDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varitemDef_ptr = var;
        Mark_itemDef_ptr();
        ++var;
    }
}

void __cdecl Mark_menuDef_t()
{
    varWindow = &varmenuDef_t->window;
    Mark_Window();
    if (varmenuDef_t->items)
    {
        varitemDef_ptr = varmenuDef_t->items;
        Mark_itemDef_ptrArray(varmenuDef_t->itemCount);
    }
}

void __cdecl Mark_menuDef_ptr()
{
    if (*varmenuDef_ptr)
    {
        varmenuDef_t = *varmenuDef_ptr;
        Mark_MenuAsset(varmenuDef_t);
        Mark_menuDef_t();
    }
}

void __cdecl Mark_menuDef_ptrArray(int32_t count)
{
    menuDef_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varmenuDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varmenuDef_ptr = var;
        Mark_menuDef_ptr();
        ++var;
    }
}

void __cdecl Mark_MenuList()
{
    if (varMenuList->menus)
    {
        varmenuDef_ptr = varMenuList->menus;
        Mark_menuDef_ptrArray(varMenuList->menuCount);
    }
}

void __cdecl Mark_MenuListPtr()
{
    if (*varMenuListPtr)
    {
        varMenuList = *varMenuListPtr;
        Mark_MenuListAsset(varMenuList);
        Mark_MenuList();
    }
}

void __cdecl Load_LocalizeEntry(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varLocalizeEntry, 8);
    DB_PushStreamPos(4);
    varXString = &varLocalizeEntry->value;
    Load_XString(0);
    varXString = &varLocalizeEntry->name;
    Load_XString(0);
    DB_PopStreamPos();
}

void __cdecl Load_LocalizeEntryPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varLocalizeEntryPtr, 4);
    DB_PushStreamPos(0);
    if (*varLocalizeEntryPtr)
    {
        value = (uint32_t)*varLocalizeEntryPtr;
        if (value == -1 || value == -2)
        {
            *varLocalizeEntryPtr = (LocalizeEntry *)AllocLoad_FxElemVisStateSample();
            varLocalizeEntry = *varLocalizeEntryPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::LocalizeEntry);
            else
                inserted = {};
            Load_LocalizeEntry(1);
            Load_LocalizeEntryAsset((XAssetHeader *)varLocalizeEntryPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::LocalizeEntry,
                    *varLocalizeEntryPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varLocalizeEntryPtr,
                DBAliasKind::LocalizeEntry);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_LocalizeEntryPtr()
{
    if (*varLocalizeEntryPtr)
    {
        varLocalizeEntry = *varLocalizeEntryPtr;
        Mark_LocalizeEntryAsset(varLocalizeEntry);
    }
}

void __cdecl Load_FxImpactEntry(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxImpactEntry, 132);
    varFxEffectDefHandle = (const FxEffectDef **)varFxImpactEntry;
    Load_FxEffectDefHandleArray(0, 29);
    varFxEffectDefHandle = varFxImpactEntry->flesh;
    Load_FxEffectDefHandleArray(0, 4);
}

void __cdecl Load_FxImpactEntryArray(bool atStreamStart, int32_t count)
{
    FxImpactEntry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varFxImpactEntry, count, 132);
    var = varFxImpactEntry;
    for (i = 0; i < count; ++i)
    {
        varFxImpactEntry = var;
        Load_FxImpactEntry(0);
        ++var;
    }
}

void __cdecl Load_FxImpactTable(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxImpactTable, 8);
    DB_PushStreamPos(4);
    varXString = &varFxImpactTable->name;
    Load_XString(0);
    if (varFxImpactTable->table)
    {
        varFxImpactTable->table = (FxImpactEntry *)AllocLoad_FxElemVisStateSample();
        varFxImpactEntry = varFxImpactTable->table;
        Load_FxImpactEntryArray(1, 12);
    }
    DB_PopStreamPos();
}

void __cdecl Load_FxImpactTablePtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varFxImpactTablePtr, 4);
    DB_PushStreamPos(0);
    if (*varFxImpactTablePtr)
    {
        value = (uint32_t)*varFxImpactTablePtr;
        if (value == -1 || value == -2)
        {
            *varFxImpactTablePtr = (FxImpactTable *)AllocLoad_FxElemVisStateSample();
            varFxImpactTable = *varFxImpactTablePtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::FxImpactTable);
            else
                inserted = {};
            Load_FxImpactTable(1);
            Load_FxImpactTableAsset((XAssetHeader *)varFxImpactTablePtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::FxImpactTable,
                    *varFxImpactTablePtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varFxImpactTablePtr,
                DBAliasKind::FxImpactTable);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_FxImpactEntry()
{
    varFxEffectDefHandle = (const FxEffectDef **)varFxImpactEntry;
    Mark_FxEffectDefHandleArray(29);
    varFxEffectDefHandle = varFxImpactEntry->flesh;
    Mark_FxEffectDefHandleArray(4);
}

void __cdecl Mark_FxImpactEntryArray(int32_t count)
{
    FxImpactEntry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxImpactEntry;
    for (i = 0; i < count; ++i)
    {
        varFxImpactEntry = var;
        Mark_FxImpactEntry();
        ++var;
    }
}

void __cdecl Mark_FxImpactTable()
{
    if (varFxImpactTable->table)
    {
        varFxImpactEntry = varFxImpactTable->table;
        Mark_FxImpactEntryArray(12);
    }
}

void __cdecl Mark_FxImpactTablePtr()
{
    if (*varFxImpactTablePtr)
    {
        varFxImpactTable = *varFxImpactTablePtr;
        Mark_FxImpactTableAsset(varFxImpactTable);
        Mark_FxImpactTable();
    }
}

void __cdecl Load_WeaponDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varWeaponDef, 2168);
    uint32_t accuracyGraphByteCount[WEAP_ACCURACY_COUNT] = {};
    for (uint32_t graphIndex = 0; graphIndex < WEAP_ACCURACY_COUNT; ++graphIndex)
    {
        if (!DB_ValidateWeaponAccuracyGraph(
                varWeaponDef,
                graphIndex,
                &accuracyGraphByteCount[graphIndex]))
        {
            return;
        }
    }
    DB_PushStreamPos(4);
    varXString = &varWeaponDef->szInternalName;
    Load_XString(0);
    varXString = &varWeaponDef->szDisplayName;
    Load_XString(0);
    varXString = &varWeaponDef->szOverlayName;
    Load_XString(0);
    varXModelPtr = varWeaponDef->gunXModel;
    Load_XModelPtrArray(0, 16);
    varXModelPtr = &varWeaponDef->handXModel;
    Load_XModelPtr(0);
    varXString = varWeaponDef->szXAnims;
    Load_XStringArray(0, 33);
    varXString = &varWeaponDef->szModeName;
    Load_XString(0);
    varScriptString = varWeaponDef->hideTags;
    Load_ScriptStringArray(0, 8);
    varScriptString = varWeaponDef->notetrackSoundMapKeys;
    Load_ScriptStringArray(0, 16);
    varScriptString = varWeaponDef->notetrackSoundMapValues;
    Load_ScriptStringArray(0, 16);
    varFxEffectDefHandle = &varWeaponDef->viewFlashEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldFlashEffect;
    Load_FxEffectDefHandle(0);
    varsnd_alias_list_name = &varWeaponDef->pickupSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pickupSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->projectileSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pullbackSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pullbackSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLoopSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLoopSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireStopSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireStopSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLastSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLastSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->emptyFireSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->emptyFireSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeHitSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeMissSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->rechamberSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->rechamberSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadStartSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadStartSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEndSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEndSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->detonateSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->detonateSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->altSwitchSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->altSwitchSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->raiseSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->raiseSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->putawaySound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->putawaySoundPlayer;
    Load_snd_alias_list_name(0);
    if (varWeaponDef->bounceSound)
    {
        if (varWeaponDef->bounceSound == (snd_alias_list_t **)-1)
        {
            varWeaponDef->bounceSound = (snd_alias_list_t **)AllocLoad_FxElemVisStateSample();
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable);
            if (!completed)
            {
                DB_PopStreamPos();
                return;
            }
            varsnd_alias_list_name = varWeaponDef->bounceSound;
            Load_snd_alias_list_nameArray(
                1,
                disk32::kWeaponBounceSoundCount);
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::WeaponBounceSoundTable,
                    varWeaponDef->bounceSound,
                    disk32::kWeaponBounceSoundTableBytes,
                    disk32::kWeaponBounceSoundTableBytes))
            {
                DB_PopStreamPos();
                return;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable,
                disk32::kWeaponBounceSoundTableBytes);
        }
    }
    varFxEffectDefHandle = &varWeaponDef->viewShellEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldShellEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->viewLastShotEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldLastShotEjectEffect;
    Load_FxEffectDefHandle(0);
    varMaterialHandle = &varWeaponDef->reticleCenter;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->reticleSide;
    Load_MaterialHandle(0);
    varXModelPtr = varWeaponDef->worldModel;
    Load_XModelPtrArray(0, 16);
    varXModelPtr = &varWeaponDef->worldClipModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->rocketModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->knifeModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->worldKnifeModel;
    Load_XModelPtr(0);
    varMaterialHandle = &varWeaponDef->hudIcon;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->ammoCounterIcon;
    Load_MaterialHandle(0);
    varXString = &varWeaponDef->szAmmoName;
    Load_XString(0);
    varXString = &varWeaponDef->szClipName;
    Load_XString(0);
    varXString = &varWeaponDef->szSharedAmmoCapName;
    Load_XString(0);
    varMaterialHandle = &varWeaponDef->overlayMaterial;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->overlayMaterialLowRes;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->killIcon;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->dpadIcon;
    Load_MaterialHandle(0);
    varXString = &varWeaponDef->szAltWeaponName;
    Load_XString(0);
    varXModelPtr = &varWeaponDef->projectileModel;
    Load_XModelPtr(0);
    varFxEffectDefHandle = &varWeaponDef->projExplosionEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->projDudEffect;
    Load_FxEffectDefHandle(0);
    varsnd_alias_list_name = &varWeaponDef->projExplosionSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->projDudSound;
    Load_snd_alias_list_name(0);
    varFxEffectDefHandle = &varWeaponDef->projTrailEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->projIgnitionEffect;
    Load_FxEffectDefHandle(0);
    varsnd_alias_list_name = &varWeaponDef->projIgnitionSound;
    Load_snd_alias_list_name(0);
    varXString = varWeaponDef->accuracyGraphName;
    Load_XString(0);
    if (varWeaponDef->accuracyGraphKnots[0])
    {
        if (varWeaponDef->accuracyGraphKnots[0] == (float (*)[2]) - 1)
        {
            varWeaponDef->accuracyGraphKnots[0] = (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->accuracyGraphKnots[0];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[0]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->accuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4);
        }
    }
    if (varWeaponDef->originalAccuracyGraphKnots[0])
    {
        if (varWeaponDef->originalAccuracyGraphKnots[0] == (float (*)[2]) - 1)
        {
            varWeaponDef->originalAccuracyGraphKnots[0] = (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->originalAccuracyGraphKnots[0];
            Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[0]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4);
        }
    }
    if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 0))
        return;
    varXString = &varWeaponDef->accuracyGraphName[1];
    Load_XString(0);
    if (varWeaponDef->accuracyGraphKnots[1])
    {
        if (varWeaponDef->accuracyGraphKnots[1] == (float (*)[2]) - 1)
        {
            varWeaponDef->accuracyGraphKnots[1] = (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->accuracyGraphKnots[1];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[1]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->accuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4);
        }
    }
    if (varWeaponDef->originalAccuracyGraphKnots[1])
    {
        if (varWeaponDef->originalAccuracyGraphKnots[1] == (float (*)[2]) - 1)
        {
            varWeaponDef->originalAccuracyGraphKnots[1] = (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->originalAccuracyGraphKnots[1];
            Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[1]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4);
        }
    }
    if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 1))
        return;
    varXString = &varWeaponDef->szUseHintString;
    Load_XString(0);
    varXString = &varWeaponDef->dropHintString;
    Load_XString(0);
    varXString = &varWeaponDef->szScript;
    Load_XString(0);
    varXString = &varWeaponDef->fireRumble;
    Load_XString(0);
    varXString = &varWeaponDef->meleeImpactRumble;
    Load_XString(0);
    DB_PopStreamPos();
}

void __cdecl Load_WeaponDefPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varWeaponDefPtr, 4);
    DB_PushStreamPos(0);
    if (*varWeaponDefPtr)
    {
        value = (uint32_t)*varWeaponDefPtr;
        if (value == -1 || value == -2)
        {
            *varWeaponDefPtr = (WeaponDef *)AllocLoad_FxElemVisStateSample();
            varWeaponDef = *varWeaponDefPtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::WeaponDef);
            else
                inserted = {};
            Load_WeaponDef(1);
            Load_WeaponDefAsset((XAssetHeader *)varWeaponDefPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::WeaponDef,
                    *varWeaponDefPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varWeaponDefPtr,
                DBAliasKind::WeaponDef);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_WeaponDef()
{
    varXModelPtr = varWeaponDef->gunXModel;
    Mark_XModelPtrArray(16);
    varXModelPtr = &varWeaponDef->handXModel;
    Mark_XModelPtr();
    varScriptString = varWeaponDef->hideTags;
    Mark_ScriptStringArray(8);
    varScriptString = varWeaponDef->notetrackSoundMapKeys;
    Mark_ScriptStringArray(16);
    varScriptString = varWeaponDef->notetrackSoundMapValues;
    Mark_ScriptStringArray(16);
    varFxEffectDefHandle = &varWeaponDef->viewFlashEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldFlashEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->pickupSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pickupSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->projectileSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pullbackSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pullbackSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLoopSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLoopSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireStopSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireStopSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLastSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLastSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->emptyFireSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->emptyFireSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeHitSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeMissSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->rechamberSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->rechamberSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadStartSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadStartSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEndSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEndSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->detonateSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->detonateSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->altSwitchSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->altSwitchSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->raiseSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->raiseSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->putawaySound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->putawaySoundPlayer;
    Mark_snd_alias_list_name();
    if (varWeaponDef->bounceSound)
    {
        varsnd_alias_list_name = varWeaponDef->bounceSound;
        Mark_snd_alias_list_nameArray(29);
    }
    varFxEffectDefHandle = &varWeaponDef->viewShellEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldShellEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->viewLastShotEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldLastShotEjectEffect;
    Mark_FxEffectDefHandle();
    varMaterialHandle = &varWeaponDef->reticleCenter;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->reticleSide;
    Mark_MaterialHandle();
    varXModelPtr = varWeaponDef->worldModel;
    Mark_XModelPtrArray(16);
    varXModelPtr = &varWeaponDef->worldClipModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->rocketModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->knifeModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->worldKnifeModel;
    Mark_XModelPtr();
    varMaterialHandle = &varWeaponDef->hudIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->ammoCounterIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->overlayMaterial;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->overlayMaterialLowRes;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->killIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->dpadIcon;
    Mark_MaterialHandle();
    varXModelPtr = &varWeaponDef->projectileModel;
    Mark_XModelPtr();
    varFxEffectDefHandle = &varWeaponDef->projExplosionEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->projDudEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->projExplosionSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->projDudSound;
    Mark_snd_alias_list_name();
    varFxEffectDefHandle = &varWeaponDef->projTrailEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->projIgnitionEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->projIgnitionSound;
    Mark_snd_alias_list_name();
}

void __cdecl Mark_WeaponDefPtr()
{
    if (*varWeaponDefPtr)
    {
        varWeaponDef = *varWeaponDefPtr;
        Mark_WeaponDefAsset(varWeaponDef);
        Mark_WeaponDef();
    }
}

void __cdecl Load_RawFile(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varRawFile, 12);
    DB_PushStreamPos(4);
    varXString = &varRawFile->name;
    Load_XString(0);
    if (varRawFile->buffer)
    {
        varRawFile->buffer = (const char *)AllocLoad_raw_byte();
        varConstChar = (const char*)varRawFile->buffer;
        const int32_t bufferSize = DB_CheckedCountSum(
            varRawFile->len,
            1,
            "raw-file buffer");
        Load_ConstCharArray(1, bufferSize);
    }
    DB_PopStreamPos();
}

void __cdecl Load_RawFilePtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varRawFilePtr, 4);
    DB_PushStreamPos(0);
    if (*varRawFilePtr)
    {
        value = (uint32_t)*varRawFilePtr;
        if (value == -1 || value == -2)
        {
            *varRawFilePtr = (RawFile *)AllocLoad_FxElemVisStateSample();
            varRawFile = *varRawFilePtr;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::RawFile);
            else
                inserted = {};
            Load_RawFile(1);
            Load_RawFileAsset((XAssetHeader *)varRawFilePtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::RawFile,
                    *varRawFilePtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varRawFilePtr,
                DBAliasKind::RawFile);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_RawFilePtr()
{
    if (*varRawFilePtr)
    {
        varRawFile = *varRawFilePtr;
        Mark_RawFileAsset(varRawFile);
    }
}

bool __cdecl Load_StringTable(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varStringTable,
        disk32::kStringTableBytes);
    const int32_t valueCount = DB_CheckedCountProduct(
        varStringTable->rowCount,
        varStringTable->columnCount,
        "string-table values");
    if (!DB_ValidatePointerCount(
            varStringTable->values,
            valueCount,
            "string-table values"))
    {
        return false;
    }
    if (valueCount == 0 && varStringTable->values)
    {
        Com_Error(ERR_DROP, "Invalid present-empty fast-file string table");
        return false;
    }
    varXString = &varStringTable->name;
    Load_XString(0);
    if (!varStringTable->name || !*varStringTable->name)
    {
        Com_Error(ERR_DROP, "Fast-file string table has no name");
        return false;
    }
    if (varStringTable->values)
    {
        varStringTable->values = (const char **)AllocLoad_FxElemVisStateSample();
        varXString = varStringTable->values;
        Load_XStringArray(1, valueCount);
    }
    return true;
}

void __cdecl Load_StringTablePtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varStringTablePtr, 4);
    if (*varStringTablePtr)
    {
        if (*varStringTablePtr == (StringTable *)-1)
        {
            *varStringTablePtr = (StringTable *)AllocLoad_FxElemVisStateSample();
            StringTable * const serializedStringTable = *varStringTablePtr;
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                serializedStringTable,
                DBAliasKind::StringTable);
            if (!completed)
                return;
            varStringTable = serializedStringTable;
            if (!Load_StringTable(1))
                return;
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::StringTable,
                    serializedStringTable,
                    disk32::kStringTableBytes,
                    disk32::kStringTableBytes))
            {
                return;
            }
            Load_StringTableAsset((XAssetHeader *)varStringTablePtr);
            if (!*varStringTablePtr)
            {
                Com_Error(ERR_DROP, "Fast-file string table was not registered");
                return;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)varStringTablePtr,
                DBAliasKind::StringTable,
                disk32::kStringTableBytes);
        }
    }
}

void __cdecl Mark_StringTablePtr()
{
    if (*varStringTablePtr)
    {
        varStringTable = *varStringTablePtr;
        Mark_StringTableAsset(varStringTable);
    }
}

void __cdecl Load_GfxStaticModelDrawInst(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxStaticModelDrawInst, 76);
    varXModelPtr = &varGfxStaticModelDrawInst->model;
    Load_XModelPtr(0);
}

void __cdecl Load_GfxStaticModelDrawInstArray(bool atStreamStart, int32_t count)
{
    GfxStaticModelDrawInst *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxStaticModelDrawInst, count, 76);
    var = varGfxStaticModelDrawInst;
    for (i = 0; i < count; ++i)
    {
        varGfxStaticModelDrawInst = var;
        Load_GfxStaticModelDrawInst(0);
        ++var;
    }
}

void __cdecl Load_GfxStaticModelInstArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxStaticModelInst, count, 28);
}

void __cdecl Mark_GfxStaticModelDrawInst()
{
    varXModelPtr = &varGfxStaticModelDrawInst->model;
    Mark_XModelPtr();
}

void __cdecl Mark_GfxStaticModelDrawInstArray(int32_t count)
{
    GfxStaticModelDrawInst *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxStaticModelDrawInst;
    for (i = 0; i < count; ++i)
    {
        varGfxStaticModelDrawInst = var;
        Mark_GfxStaticModelDrawInst();
        ++var;
    }
}

void __cdecl Load_sunflare_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varsunflare_t, 96);
    varMaterialHandle = &varsunflare_t->spriteMaterial;
    Load_MaterialHandle(0);
    varMaterialHandle = &varsunflare_t->flareMaterial;
    Load_MaterialHandle(0);
}

void __cdecl Mark_sunflare_t()
{
    varMaterialHandle = &varsunflare_t->spriteMaterial;
    Mark_MaterialHandle();
    varMaterialHandle = &varsunflare_t->flareMaterial;
    Mark_MaterialHandle();
}

void __cdecl Load_GfxReflectionProbe(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxReflectionProbe,
        disk32::kGfxReflectionProbeBytes);
    varGfxImagePtr = &varGfxReflectionProbe->reflectionImage;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxReflectionProbeArray(bool atStreamStart, int32_t count)
{
    GfxReflectionProbe *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxReflectionProbe,
        count,
        disk32::kGfxReflectionProbeBytes);
    var = varGfxReflectionProbe;
    for (i = 0; i < count; ++i)
    {
        varGfxReflectionProbe = var;
        Load_GfxReflectionProbe(0);
        ++var;
    }
}

void __cdecl Mark_GfxReflectionProbe()
{
    varGfxImagePtr = &varGfxReflectionProbe->reflectionImage;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxReflectionProbeArray(int32_t count)
{
    GfxReflectionProbe *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxReflectionProbe;
    for (i = 0; i < count; ++i)
    {
        varGfxReflectionProbe = var;
        Mark_GfxReflectionProbe();
        ++var;
    }
}

void __cdecl Load_StaticModelIndexArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varStaticModelIndex, count, 2);
}

bool __cdecl Load_GfxAabbTree(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxAabbTree,
        disk32::kGfxAabbTreeBytes);
    if (!DB_ValidatePointerCount(
            varGfxAabbTree->smodelIndexes,
            varGfxAabbTree->smodelIndexCount,
            "world AABB static-model indices"))
    {
        return false;
    }
    const uint32_t smodelIndexByteCount = DB_CheckedDirectSpanBytes(
        varGfxAabbTree->smodelIndexCount,
        2,
        "world AABB static-model indices");
    if (varGfxAabbTree->smodelIndexes)
    {
        if (varGfxAabbTree->smodelIndexes == (uint16_t *)-1)
        {
            varGfxAabbTree->smodelIndexes = (uint16_t *)AllocLoad_XBlendInfo();
            if (!varGfxAabbTree->smodelIndexes
                || !DB_IsStreamRangeValid(
                    varGfxAabbTree->smodelIndexes,
                    smodelIndexByteCount))
            {
                Com_Error(
                    ERR_DROP,
                    "Fast-file world AABB static-model indices exceed block 4");
                return false;
            }
            varStaticModelIndex = varGfxAabbTree->smodelIndexes;
            Load_StaticModelIndexArray(1, varGfxAabbTree->smodelIndexCount);
        }
        else if (!DB_ResolveDirectPointer(
                &varGfxAabbTree->smodelIndexes,
                smodelIndexByteCount,
                2,
                kDirectBlock4,
                "world AABB static-model indices"))
        {
            return false;
        }
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varGfxAabbTree->smodelIndexes,
            smodelIndexByteCount,
            2,
            "world AABB static-model indices")
        || !db::validation::AllU16Below(
            varGfxAabbTree->smodelIndexes,
            varGfxAabbTree->smodelIndexCount,
            varGfxWorld->dpvs.smodelCount))
    {
        Com_Error(ERR_DROP, "Fast-file world AABB has an invalid static-model index");
        return false;
    }
    return true;
}

bool __cdecl Load_GfxAabbTreeArray(bool atStreamStart, int32_t count)
{
    GfxAabbTree *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxAabbTree,
        count,
        disk32::kGfxAabbTreeBytes);
    var = varGfxAabbTree;
    for (i = 0; i < count; ++i)
    {
        varGfxAabbTree = var;
        if (!Load_GfxAabbTree(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_GfxCell(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxCell,
        disk32::kGfxCellBytes);
    db::validation::GfxCellLayoutExtents extents = {};
    if (!db::validation::GfxCellLayoutValid(*varGfxCell, &extents))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world cell layout");
        return false;
    }
    if (varGfxCell->aabbTree)
    {
        varGfxCell->aabbTree = (GfxAabbTree *)AllocLoad_FxElemVisStateSample();
        if (!varGfxCell->aabbTree
            || !DB_IsStreamRangeValid(
                varGfxCell->aabbTree,
                static_cast<uint32_t>(extents.aabbTreeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world cell AABB trees exceed block 4");
            return false;
        }
        varGfxAabbTree = varGfxCell->aabbTree;
        if (!Load_GfxAabbTreeArray(1, varGfxCell->aabbTreeCount))
            return false;
    }
    if (!DB_ValidateWorldAabbCell(varGfxWorld, varGfxCell))
        return false;
    if (varGfxCell->portals)
    {
        varGfxCell->portals = (GfxPortal *)AllocLoad_FxElemVisStateSample();
        if (!varGfxCell->portals
            || !DB_IsStreamRangeValid(
                varGfxCell->portals,
                static_cast<uint32_t>(extents.portalBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world cell portals exceed block 4");
            return false;
        }
        varGfxPortal = varGfxCell->portals;
        if (!Load_GfxPortalArray(1, varGfxCell->portalCount))
            return false;
    }
    if (varGfxCell->cullGroups)
    {
        varGfxCell->cullGroups = (int32_t *)AllocLoad_FxElemVisStateSample();
        if (!varGfxCell->cullGroups
            || !DB_IsStreamRangeValid(
                varGfxCell->cullGroups,
                static_cast<uint32_t>(extents.cullGroupBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world cell cull groups exceed block 4");
            return false;
        }
        varint = varGfxCell->cullGroups;
        Load_intArray(1, varGfxCell->cullGroupCount);
    }
    if (varGfxCell->reflectionProbes)
    {
        varGfxCell->reflectionProbes = AllocLoad_raw_byte();
        if (!varGfxCell->reflectionProbes
            || !DB_IsStreamRangeValid(
                varGfxCell->reflectionProbes,
                static_cast<uint32_t>(extents.reflectionProbeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world cell reflection probes exceed block 4");
            return false;
        }
        varbyte = varGfxCell->reflectionProbes;
        Load_byteArray(1, varGfxCell->reflectionProbeCount);
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varGfxCell->aabbTree,
            static_cast<uint32_t>(extents.aabbTreeBytes),
            4,
            "world cell AABB trees")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxCell->portals,
            static_cast<uint32_t>(extents.portalBytes),
            4,
            "world cell portals")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxCell->cullGroups,
            static_cast<uint32_t>(extents.cullGroupBytes),
            4,
            "world cell cull groups")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxCell->reflectionProbes,
            static_cast<uint32_t>(extents.reflectionProbeBytes),
            1,
            "world cell reflection probes"))
    {
        return false;
    }
    return true;
}

bool __cdecl Load_GfxCellArray(bool atStreamStart, int32_t count)
{
    GfxCell *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxCell,
        count,
        disk32::kGfxCellBytes);
    var = varGfxCell;
    for (i = 0; i < count; ++i)
    {
        varGfxCell = var;
        if (!Load_GfxCell(0))
            return false;
        ++var;
    }
    return true;
}

bool __cdecl Load_GfxPortal(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxPortal,
        disk32::kGfxPortalBytes);
    varGfxPortal->writable = {};
    if (!varGfxPortal->cell
        || varGfxPortal->cell == (GfxCell *)-1
        || varGfxPortal->cell == (GfxCell *)-2)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world portal cell token");
        return false;
    }
    if (!DB_ResolveDirectPointer(
            &varGfxPortal->cell,
            disk32::kGfxCellBytes,
            4,
            kDirectBlock4,
            "world portal cell"))
    {
        return false;
    }
    uint64_t cellIndex = 0;
    if (!varGfxWorld
        || !db::validation::SerializedArrayElementIndex(
            varGfxWorld->cells,
            varGfxWorld->dpvsPlanes.cellCount,
            disk32::kGfxCellBytes,
            varGfxPortal->cell,
            &cellIndex))
    {
        Com_Error(ERR_DROP, "Fast-file world portal target is not a cell");
        return false;
    }
    if (!varGfxPortal->vertices
        || varGfxPortal->vertexCount
            < db::validation::kMinGfxPortalVertices
        || varGfxPortal->vertexCount
            > db::validation::kMaxGfxPortalVertices)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world portal vertex layout");
        return false;
    }
    const uint32_t vertexBytes = DB_CheckedDirectSpanBytes(
        varGfxPortal->vertexCount,
        disk32::kVec3Bytes,
        "world portal vertices");
    if (varGfxPortal->vertices)
    {
        varGfxPortal->vertices = (float (*)[3])AllocLoad_FxElemVisStateSample();
        if (!varGfxPortal->vertices
            || !DB_IsStreamRangeValid(
                varGfxPortal->vertices,
                vertexBytes))
        {
            Com_Error(ERR_DROP, "Fast-file world portal vertices exceed block 4");
            return false;
        }
        varvec3_t = varGfxPortal->vertices;
        Load_vec3_tArray(1, varGfxPortal->vertexCount);
    }
    if (!DB_ValidateMaterializedBlock4Span(
            varGfxPortal->vertices,
            vertexBytes,
            4,
            "world portal vertices")
        || !db::validation::GfxPortalRuntimeValid(*varGfxPortal))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file world portal");
        return false;
    }
    return true;
}

bool __cdecl Load_GfxPortalArray(bool atStreamStart, int32_t count)
{
    GfxPortal *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxPortal,
        count,
        disk32::kGfxPortalBytes);
    var = varGfxPortal;
    for (i = 0; i < count; ++i)
    {
        varGfxPortal = var;
        if (!Load_GfxPortal(0))
            return false;
        ++var;
    }
    return true;
}

void __cdecl Load_GfxCullGroupArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxCullGroup,
        count,
        disk32::kGfxCullGroupBytes);
}

void __cdecl Load_GfxLightGridEntryArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightGridEntry, count, 4);
}

void __cdecl Load_GfxLightGridColorsArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightGridColors, count, 168);
}

void __cdecl Load_MaterialMemory(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialMemory, 8);
    varMaterialHandle = &varMaterialMemory->material;
    Load_MaterialHandle(0);
}

void __cdecl Load_MaterialMemoryArray(bool atStreamStart, int32_t count)
{
    MaterialMemory *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varMaterialMemory, count, 8);
    var = varMaterialMemory;
    for (i = 0; i < count; ++i)
    {
        varMaterialMemory = var;
        Load_MaterialMemory(0);
        ++var;
    }
}

void __cdecl Load_GfxWorldVertexData(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldVertexData, 8);
    const int32_t vertexCount = DB_CheckedCountSum(
        varGfxWorld->vertexCount,
        0,
        "world vertices");
    const int32_t vertexDataSize = DB_CheckedCountProduct(
        vertexCount,
        44,
        "world vertex-buffer bytes");
    if (varGfxWorldVertexData->vertices)
    {
        varGfxWorldVertexData->vertices = (GfxWorldVertex *)AllocLoad_FxElemVisStateSample();
        varGfxWorldVertex0 = varGfxWorldVertexData->vertices;
        Load_GfxWorldVertex0Array(1, vertexCount);
    }
    varGfxVertexBuffer = &varGfxWorldVertexData->worldVb;
    Load_GfxVertexBuffer(0);
    Load_VertexBuffer(
        &varGfxWorldVertexData->worldVb,
        (uint8_t *)varGfxWorld->vd.vertices,
        vertexDataSize);
}

void __cdecl Load_GfxWorldVertexLayerData(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldVertexLayerData, 8);
    const int32_t layerDataSize = DB_CheckedCountSum(
        varGfxWorld->vertexLayerDataSize,
        0,
        "world vertex-layer bytes");
    if (varGfxWorldVertexLayerData->data)
    {
        varGfxWorldVertexLayerData->data = AllocLoad_raw_byte();
        varbyte = varGfxWorldVertexLayerData->data;
        Load_byteArray(1, layerDataSize);
    }
    varGfxVertexBuffer = &varGfxWorldVertexLayerData->layerVb;
    Load_GfxVertexBuffer(0);
    Load_VertexBuffer(&varGfxWorldVertexLayerData->layerVb, varGfxWorld->vld.data, layerDataSize);
}

void __cdecl Load_GfxLightGrid(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightGrid, 56);
    if (varGfxLightGrid->rowAxis >= 3 || varGfxLightGrid->colAxis >= 3)
    {
        Com_Error(ERR_DROP, "Invalid fast-file light-grid axes");
        return;
    }
    if (varGfxLightGrid->rowDataStart)
    {
        varGfxLightGrid->rowDataStart = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxLightGrid->rowDataStart;
        const int32_t rowRange = DB_CheckedCountDifference(
            varGfxLightGrid->maxs[varGfxLightGrid->rowAxis],
            varGfxLightGrid->mins[varGfxLightGrid->rowAxis],
            "light-grid row range");
        const int32_t rowCount = DB_CheckedCountSum(
            rowRange,
            1,
            "light-grid rows");
        Load_ushortArray(1, rowCount);
    }
    if (varGfxLightGrid->rawRowData)
    {
        varGfxLightGrid->rawRowData = AllocLoad_raw_byte();
        varbyte = varGfxLightGrid->rawRowData;
        Load_byteArray(1, varGfxLightGrid->rawRowDataSize);
    }
    if (varGfxLightGrid->entries)
    {
        varGfxLightGrid->entries = (GfxLightGridEntry *)AllocLoad_FxElemVisStateSample();
        varGfxLightGridEntry = varGfxLightGrid->entries;
        Load_GfxLightGridEntryArray(1, varGfxLightGrid->entryCount);
    }
    if (varGfxLightGrid->colors)
    {
        varGfxLightGrid->colors = (GfxLightGridColors *)AllocLoad_FxElemVisStateSample();
        varGfxLightGridColors = varGfxLightGrid->colors;
        Load_GfxLightGridColorsArray(1, varGfxLightGrid->colorCount);
    }
}

void __cdecl Load_GfxSceneDynModelArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxSceneDynModel, count, 6);
}

void __cdecl Load_GfxSceneDynBrushArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxSceneDynBrush, count, 4);
}

void __cdecl Load_GfxDrawSurfArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxDrawSurf, count, 8);
}

void __cdecl Load_GfxShadowGeometry(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxShadowGeometry, 12);
    if (varGfxShadowGeometry->sortedSurfIndex)
    {
        varGfxShadowGeometry->sortedSurfIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxShadowGeometry->sortedSurfIndex;
        Load_ushortArray(1, varGfxShadowGeometry->surfaceCount);
    }
    if (varGfxShadowGeometry->smodelIndex)
    {
        varGfxShadowGeometry->smodelIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxShadowGeometry->smodelIndex;
        Load_ushortArray(1, varGfxShadowGeometry->smodelCount);
    }
}

void __cdecl Load_GfxShadowGeometryArray(bool atStreamStart, int32_t count)
{
    GfxShadowGeometry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxShadowGeometry, count, 12);
    var = varGfxShadowGeometry;
    for (i = 0; i < count; ++i)
    {
        varGfxShadowGeometry = var;
        Load_GfxShadowGeometry(0);
        ++var;
    }
}

void __cdecl Load_GfxLightRegionAxisArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightRegionAxis, count, 20);
}

void __cdecl Load_GfxLightRegionHull(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegionHull, 80);
    if (varGfxLightRegionHull->axis)
    {
        varGfxLightRegionHull->axis = (GfxLightRegionAxis *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegionAxis = varGfxLightRegionHull->axis;
        Load_GfxLightRegionAxisArray(1, varGfxLightRegionHull->axisCount);
    }
}

void __cdecl Load_GfxLightRegionHullArray(bool atStreamStart, int32_t count)
{
    GfxLightRegionHull *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightRegionHull, count, 80);
    var = varGfxLightRegionHull;
    for (i = 0; i < count; ++i)
    {
        varGfxLightRegionHull = var;
        Load_GfxLightRegionHull(0);
        ++var;
    }
}

void __cdecl Load_GfxLightRegion(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegion, 8);
    if (varGfxLightRegion->hulls)
    {
        varGfxLightRegion->hulls = (GfxLightRegionHull *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegionHull = varGfxLightRegion->hulls;
        Load_GfxLightRegionHullArray(1, varGfxLightRegion->hullCount);
    }
}

void __cdecl Load_GfxLightRegionArray(bool atStreamStart, int32_t count)
{
    GfxLightRegion *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_StreamArray(atStreamStart, (uint8_t *)varGfxLightRegion, count, 8);
    var = varGfxLightRegion;
    for (i = 0; i < count; ++i)
    {
        varGfxLightRegion = var;
        Load_GfxLightRegion(0);
        ++var;
    }
}

void __cdecl Load_GfxWorldDpvsDynamic(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsDynamic, 48);
    // DPVS dimensions are consumed by runtime visibility indexing, not just archive reads.
    const int32_t cellBitCount0 = DB_CheckedCountProduct(
        varGfxWorld->dpvsPlanes.cellCount,
        varGfxWorldDpvsDynamic->dynEntClientWordCount[0],
        "dynamic entity cell bits");
    const int32_t cellBitCount1 = DB_CheckedCountProduct(
        varGfxWorld->dpvsPlanes.cellCount,
        varGfxWorldDpvsDynamic->dynEntClientWordCount[1],
        "dynamic entity cell bits");
    const int32_t visibilityByteCount0 = DB_CheckedCountProduct(
        32,
        varGfxWorldDpvsDynamic->dynEntClientWordCount[0],
        "dynamic entity visibility");
    const int32_t visibilityByteCount1 = DB_CheckedCountProduct(
        32,
        varGfxWorldDpvsDynamic->dynEntClientWordCount[1],
        "dynamic entity visibility");
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntCellBits[0])
    {
        varGfxWorldDpvsDynamic->dynEntCellBits[0] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsDynamic->dynEntCellBits[0];
        Load_raw_uintArray(1, cellBitCount0);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntCellBits[1])
    {
        varGfxWorldDpvsDynamic->dynEntCellBits[1] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsDynamic->dynEntCellBits[1];
        Load_raw_uintArray(1, cellBitCount1);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][0])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][0] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][0];
        Load_raw_byte16Array(1, visibilityByteCount0);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][0])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][0] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][0];
        Load_raw_byte16Array(1, visibilityByteCount1);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][1])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][1] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][1];
        Load_raw_byte16Array(1, visibilityByteCount0);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][1])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][1] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][1];
        Load_raw_byte16Array(1, visibilityByteCount1);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][2])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][2] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][2];
        Load_raw_byte16Array(1, visibilityByteCount0);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][2])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][2] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][2];
        Load_raw_byte16Array(1, visibilityByteCount1);
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorldDpvsStatic(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsStatic, 104);
    // Validate the full metadata relationship before installing any optional DPVS payload.
    if (varGfxWorld->surfaceCount < 0
        || !db::validation::WorldAabbSurfacePartitionsValid(
            varGfxWorldDpvsStatic->staticSurfaceCount,
            varGfxWorldDpvsStatic->staticSurfaceCountNoDecal,
            static_cast<std::uint32_t>(varGfxWorld->surfaceCount)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world static-surface counts");
        return;
    }
    if (varGfxWorldDpvsStatic->smodelCount
        > db::validation::kMaxWorldAabbStaticModels)
    {
        Com_Error(ERR_DROP, "Invalid fast-file world static-model count");
        return;
    }
    const int32_t lodDataCount = DB_CheckedCountProduct(
        2,
        varGfxWorldDpvsStatic->smodelVisDataCount,
        "static model LOD data");
    const int32_t sortedSurfaceCount = DB_CheckedCountSum(
        varGfxWorldDpvsStatic->staticSurfaceCountNoDecal,
        varGfxWorldDpvsStatic->staticSurfaceCount,
        "sorted world surfaces");
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[0])
    {
        varGfxWorldDpvsStatic->smodelVisData[0] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[0];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[1])
    {
        varGfxWorldDpvsStatic->smodelVisData[1] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[1];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[2])
    {
        varGfxWorldDpvsStatic->smodelVisData[2] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[2];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[0])
    {
        varGfxWorldDpvsStatic->surfaceVisData[0] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[0];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[1])
    {
        varGfxWorldDpvsStatic->surfaceVisData[1] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[1];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[2])
    {
        varGfxWorldDpvsStatic->surfaceVisData[2] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[2];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->lodData)
    {
        varGfxWorldDpvsStatic->lodData = (uint32_t *)AllocLoad_raw_uint128();
        varraw_uint128 = varGfxWorldDpvsStatic->lodData;
        Load_raw_uint128Array(1, lodDataCount);
    }
    DB_PopStreamPos();
    if (varGfxWorldDpvsStatic->sortedSurfIndex)
    {
        varGfxWorldDpvsStatic->sortedSurfIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxWorldDpvsStatic->sortedSurfIndex;
        Load_ushortArray(1, sortedSurfaceCount);
    }
    if (!DB_ValidatePointerCount(
            varGfxWorldDpvsStatic->sortedSurfIndex,
            sortedSurfaceCount,
            "sorted world surfaces"))
    {
        return;
    }
    if (!db::validation::AllU16Below(
            varGfxWorldDpvsStatic->sortedSurfIndex,
            static_cast<std::uint32_t>(sortedSurfaceCount),
            varGfxWorldDpvsStatic->staticSurfaceCount))
    {
        Com_Error(ERR_DROP, "Fast-file world has an invalid sorted surface index");
        return;
    }
    if (varGfxWorldDpvsStatic->smodelInsts)
    {
        varGfxWorldDpvsStatic->smodelInsts = (GfxStaticModelInst *)AllocLoad_FxElemVisStateSample();
        varGfxStaticModelInst = varGfxWorldDpvsStatic->smodelInsts;
        Load_GfxStaticModelInstArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    if (varGfxWorldDpvsStatic->surfaces)
    {
        varGfxWorldDpvsStatic->surfaces = (GfxSurface *)AllocLoad_FxElemVisStateSample();
        varGfxSurface = varGfxWorldDpvsStatic->surfaces;
        Load_GfxSurfaceArray(1, varGfxWorld->surfaceCount);
    }
    if (varGfxWorldDpvsStatic->cullGroups)
    {
        varGfxWorldDpvsStatic->cullGroups = (GfxCullGroup *)AllocLoad_FxElemVisStateSample();
        varGfxCullGroup = varGfxWorldDpvsStatic->cullGroups;
        Load_GfxCullGroupArray(1, varGfxWorld->cullGroupCount);
    }
    if (varGfxWorldDpvsStatic->smodelDrawInsts)
    {
        varGfxWorldDpvsStatic->smodelDrawInsts = (GfxStaticModelDrawInst *)AllocLoad_FxElemVisStateSample();
        varGfxStaticModelDrawInst = varGfxWorldDpvsStatic->smodelDrawInsts;
        Load_GfxStaticModelDrawInstArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceMaterials)
    {
        varGfxWorldDpvsStatic->surfaceMaterials = (GfxDrawSurf *)AllocLoad_FxElemVisStateSample();
        varGfxDrawSurf = varGfxWorldDpvsStatic->surfaceMaterials;
        Load_GfxDrawSurfArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceCastsSunShadow)
    {
        varGfxWorldDpvsStatic->surfaceCastsSunShadow = (uint32_t *)AllocLoad_raw_uint128();
        varraw_uint128 = varGfxWorldDpvsStatic->surfaceCastsSunShadow;
        Load_raw_uint128Array(1, varGfxWorldDpvsStatic->surfaceVisDataCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorldDpvsPlanes(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsPlanes, 16);
    if (!DB_ValidatePointerCount(
            varGfxWorldDpvsPlanes->planes,
            varGfxWorld->planeCount,
            "world planes"))
    {
        return;
    }
    const uint32_t planeByteCount = DB_CheckedDirectSpanBytes(
        varGfxWorld->planeCount,
        20,
        "world planes");
    const int32_t sceneEntityCellBitCount = DB_CheckedCountProduct(
        varGfxWorldDpvsPlanes->cellCount,
        256,
        "scene entity cell bits");
    if (varGfxWorldDpvsPlanes->planes)
    {
        if (varGfxWorldDpvsPlanes->planes == (cplane_s *)-1)
        {
            varGfxWorldDpvsPlanes->planes = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varGfxWorldDpvsPlanes->planes;
            Load_cplane_tArray(1, varGfxWorld->planeCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varGfxWorldDpvsPlanes->planes,
                planeByteCount,
                4,
                kDirectBlock4);
        }
    }
    if (varGfxWorldDpvsPlanes->nodes)
    {
        varGfxWorldDpvsPlanes->nodes = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxWorldDpvsPlanes->nodes;
        Load_ushortArray(1, varGfxWorld->nodeCount);
    }
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsPlanes->sceneEntCellBits)
    {
        varGfxWorldDpvsPlanes->sceneEntCellBits = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsPlanes->sceneEntCellBits;
        Load_raw_uintArray(1, sceneEntityCellBitCount);
    }
    DB_PopStreamPos();
}

bool __cdecl Load_GfxWorld(bool atStreamStart)
{
    Load_Stream(
        atStreamStart,
        (uint8_t *)varGfxWorld,
        disk32::kGfxWorldBytes);
    // World dimensions and primary-light ordering are runtime invariants even if
    // an individual optional visibility buffer is absent from the archive.
    const int32_t cellCount = DB_CheckedCountSum(
        varGfxWorld->dpvsPlanes.cellCount,
        0,
        "world cells");
    int32_t cellBytes = 0;
    if (!db::validation::GfxWorldCellLayoutValid(
            varGfxWorld->cells != nullptr,
            cellCount,
            &cellBytes)
        || !db::validation::GfxWorldCellBitsValid(
            cellCount,
            varGfxWorld->cellBitsCount)
        || varGfxWorld->surfaceCount < 0
        || !db::validation::WorldAabbSurfacePartitionsValid(
            varGfxWorld->dpvs.staticSurfaceCount,
            varGfxWorld->dpvs.staticSurfaceCountNoDecal,
            static_cast<uint32_t>(varGfxWorld->surfaceCount)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world cell array");
        return false;
    }
    int32_t reflectionProbeBytes = 0;
    int32_t reflectionProbeTextureBytes = 0;
    int32_t cullGroupBytes = 0;
    int32_t modelBytes = 0;
    if (!db::validation::GfxReflectionProbeCountValid(
            varGfxWorld->reflectionProbeCount)
        || varGfxWorld->cullGroupCount < 0
        || (varGfxWorld->reflectionProbes != nullptr)
            != (varGfxWorld->reflectionProbeCount != 0)
        || (varGfxWorld->reflectionProbeTextures != nullptr)
            != (varGfxWorld->reflectionProbeCount != 0)
        || (varGfxWorld->dpvs.cullGroups != nullptr)
            != (varGfxWorld->cullGroupCount != 0)
        || varGfxWorld->modelCount <= 0
        || !varGfxWorld->models
        || !db::validation::CheckedArrayBytes(
            static_cast<int32_t>(varGfxWorld->reflectionProbeCount),
            disk32::kGfxReflectionProbeBytes,
            &reflectionProbeBytes)
        || !db::validation::CheckedArrayBytes(
            static_cast<int32_t>(varGfxWorld->reflectionProbeCount),
            disk32::kGfxTextureBytes,
            &reflectionProbeTextureBytes)
        || !db::validation::CheckedArrayBytes(
            varGfxWorld->cullGroupCount,
            disk32::kGfxCullGroupBytes,
            &cullGroupBytes)
        || !db::validation::CheckedArrayBytes(
            varGfxWorld->modelCount,
            disk32::kGfxBrushModelBytes,
            &modelBytes))
    {
        Com_Error(ERR_DROP, "Invalid fast-file world cell lookup arrays");
        return false;
    }
    const int32_t cellWordCount = DB_CheckedCountCeilDiv(
        cellCount,
        32,
        "world cell words");
    const int32_t cellCasterCount = DB_CheckedCountProduct(
        cellCount,
        cellWordCount,
        "world cell-caster bits");
    int32_t sortedSurfaceCount = 0;
    int32_t cellCasterBytes = 0;
    int32_t sortedSurfaceBytes = 0;
    if (!db::validation::CheckedCountSum(
            varGfxWorld->dpvs.staticSurfaceCountNoDecal,
            varGfxWorld->dpvs.staticSurfaceCount,
            &sortedSurfaceCount)
        || !DB_ValidatePointerCount(
            varGfxWorld->cellCasterBits,
            cellCasterCount,
            "world cell-caster bits")
        || !DB_ValidatePointerCount(
            varGfxWorld->dpvs.sortedSurfIndex,
            sortedSurfaceCount,
            "sorted world surfaces")
        || !db::validation::CheckedArrayBytes(
            cellCasterCount,
            sizeof(uint32_t),
            &cellCasterBytes)
        || !db::validation::CheckedArrayBytes(
            sortedSurfaceCount,
            sizeof(uint16_t),
            &sortedSurfaceBytes))
    {
        return false;
    }
    const int32_t primaryLightCount = DB_CheckedCountSum(
        varGfxWorld->primaryLightCount,
        0,
        "world primary lights");
    const int32_t lightsThroughSun = DB_CheckedCountSum(
        varGfxWorld->sunPrimaryLightIndex,
        1,
        "world sun light index");
    const int32_t relevantPrimaryLightCount = DB_CheckedCountDifference(
        primaryLightCount,
        lightsThroughSun,
        "world relevant primary lights");
    const int32_t entityShadowVisCount = DB_CheckedCountProduct(
        relevantPrimaryLightCount,
        4096,
        "entity shadow visibility");
    const int32_t dynamicModelCount = DB_CheckedCountSum(
        varGfxWorld->dpvsDyn.dynEntClientCount[0],
        0,
        "dynamic world models");
    const int32_t dynamicBrushCount = DB_CheckedCountSum(
        varGfxWorld->dpvsDyn.dynEntClientCount[1],
        0,
        "dynamic world brushes");
    const int32_t modelShadowVisCount = DB_CheckedCountProduct(
        dynamicModelCount,
        relevantPrimaryLightCount,
        "dynamic model shadow visibility");
    const int32_t brushShadowVisCount = DB_CheckedCountProduct(
        dynamicBrushCount,
        relevantPrimaryLightCount,
        "dynamic brush shadow visibility");
    DB_PushStreamPos(4);
    varXString = &varGfxWorld->name;
    Load_XString(0);
    varXString = &varGfxWorld->baseName;
    Load_XString(0);
    if (varGfxWorld->indices)
    {
        varGfxWorld->indices = (uint16_t *)AllocLoad_XBlendInfo();
        varr_index_t = varGfxWorld->indices;
        Load_r_index_tArray(1, varGfxWorld->indexCount);
    }
    if (varGfxWorld->skyStartSurfs)
    {
        varGfxWorld->skyStartSurfs = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varGfxWorld->skyStartSurfs;
        Load_intArray(1, varGfxWorld->skySurfCount);
    }
    varGfxImagePtr = &varGfxWorld->skyImage;
    Load_GfxImagePtr(0);
    if (varGfxWorld->sunLight)
    {
        if (varGfxWorld->sunLight == (GfxLight *)-1)
        {
            varGfxWorld->sunLight = (GfxLight *)AllocLoad_FxElemVisStateSample();
            if (!varGfxWorld->sunLight
                || !DB_IsStreamRangeValid(
                    varGfxWorld->sunLight,
                    disk32::kGfxLightBytes))
            {
                Com_Error(ERR_DROP, "Fast-file sun light exceeds block 4");
                DB_PopStreamPos();
                return false;
            }
            const DBAliasHandle completed = DB_RegisterPointerSlot(
                varGfxWorld->sunLight,
                DBAliasKind::GfxLight);
            if (!completed)
            {
                DB_PopStreamPos();
                return false;
            }
            varGfxLight = varGfxWorld->sunLight;
            Load_GfxLight(1);
            if (!DB_ValidateSunLight(varGfxWorld->sunLight))
            {
                DB_PopStreamPos();
                return false;
            }
            if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::GfxLight,
                    varGfxWorld->sunLight,
                    disk32::kGfxLightBytes,
                    disk32::kGfxLightBytes))
            {
                DB_PopStreamPos();
                return false;
            }
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t*)&varGfxWorld->sunLight,
                DBAliasKind::GfxLight,
                disk32::kGfxLightBytes);
        }
    }
    if (varGfxWorld->reflectionProbes)
    {
        varGfxWorld->reflectionProbes = (GfxReflectionProbe *)AllocLoad_FxElemVisStateSample();
        if (!varGfxWorld->reflectionProbes
            || !DB_IsStreamRangeValid(
                varGfxWorld->reflectionProbes,
                static_cast<uint32_t>(reflectionProbeBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world reflection probes exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varGfxReflectionProbe = varGfxWorld->reflectionProbes;
        Load_GfxReflectionProbeArray(
            1,
            static_cast<int32_t>(varGfxWorld->reflectionProbeCount));
    }
    DB_PushStreamPos(1);
    if (varGfxWorld->reflectionProbeTextures)
    {
        varGfxWorld->reflectionProbeTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        if (!varGfxWorld->reflectionProbeTextures
            || !DB_IsStreamRangeValid(
                varGfxWorld->reflectionProbeTextures,
                static_cast<uint32_t>(reflectionProbeTextureBytes)))
        {
            Com_Error(
                ERR_DROP,
                "Fast-file world reflection textures exceed block 1");
            DB_PopStreamPos();
            DB_PopStreamPos();
            return false;
        }
        varGfxRawTexture = varGfxWorld->reflectionProbeTextures;
        Load_GfxRawTextureArray(
            1,
            static_cast<int32_t>(varGfxWorld->reflectionProbeCount));
    }
    DB_PopStreamPos();
    varGfxWorldDpvsPlanes = &varGfxWorld->dpvsPlanes;
    Load_GfxWorldDpvsPlanes(0);
    if (!DB_ValidatePointerCount(varGfxWorld->cells, cellCount, "world cells"))
    {
        DB_PopStreamPos();
        return false;
    }
    if (varGfxWorld->cells)
    {
        varGfxWorld->cells = (GfxCell *)AllocLoad_FxElemVisStateSample();
        if (!varGfxWorld->cells
            || !DB_IsStreamRangeValid(
                varGfxWorld->cells,
                static_cast<uint32_t>(cellBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world cells exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varGfxCell = varGfxWorld->cells;
        if (!Load_GfxCellArray(1, varGfxWorld->dpvsPlanes.cellCount))
        {
            DB_PopStreamPos();
            return false;
        }
    }
    if (varGfxWorld->lightmaps)
    {
        varGfxWorld->lightmaps = (GfxLightmapArray *)AllocLoad_FxElemVisStateSample();
        varGfxLightmapArray = varGfxWorld->lightmaps;
        Load_GfxLightmapArrayArray(1, varGfxWorld->lightmapCount);
    }
    varGfxLightGrid = &varGfxWorld->lightGrid;
    Load_GfxLightGrid(0);
    DB_PushStreamPos(1);
    if (varGfxWorld->lightmapPrimaryTextures)
    {
        varGfxWorld->lightmapPrimaryTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        varGfxRawTexture = varGfxWorld->lightmapPrimaryTextures;
        Load_GfxRawTextureArray(1, varGfxWorld->lightmapCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->lightmapSecondaryTextures)
    {
        varGfxWorld->lightmapSecondaryTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        varGfxRawTexture = varGfxWorld->lightmapSecondaryTextures;
        Load_GfxRawTextureArray(1, varGfxWorld->lightmapCount);
    }
    DB_PopStreamPos();
    if (varGfxWorld->models)
    {
        varGfxWorld->models = (GfxBrushModel *)AllocLoad_FxElemVisStateSample();
        if (!varGfxWorld->models
            || !DB_IsStreamRangeValid(
                varGfxWorld->models,
                static_cast<uint32_t>(modelBytes)))
        {
            Com_Error(ERR_DROP, "Fast-file world models exceed block 4");
            DB_PopStreamPos();
            return false;
        }
        varGfxBrushModel = varGfxWorld->models;
        Load_GfxBrushModelArray(1, varGfxWorld->modelCount);
    }
    if (varGfxWorld->materialMemory)
    {
        varGfxWorld->materialMemory = (MaterialMemory *)AllocLoad_FxElemVisStateSample();
        varMaterialMemory = varGfxWorld->materialMemory;
        Load_MaterialMemoryArray(1, varGfxWorld->materialMemoryCount);
    }
    varGfxWorldVertexData = &varGfxWorld->vd;
    Load_GfxWorldVertexData(0);
    varGfxWorldVertexLayerData = &varGfxWorld->vld;
    Load_GfxWorldVertexLayerData(0);
    varsunflare_t = &varGfxWorld->sun;
    Load_sunflare_t(0);
    varGfxImagePtr = &varGfxWorld->outdoorImage;
    Load_GfxImagePtr(0);
    DB_PushStreamPos(1);
    if (varGfxWorld->cellCasterBits)
    {
        varGfxWorld->cellCasterBits = (uint32_t *)AllocLoad_FxElemVisStateSample();
        if (!varGfxWorld->cellCasterBits
            || !DB_IsStreamRangeValid(
                varGfxWorld->cellCasterBits,
                static_cast<uint32_t>(cellCasterBytes)))
        {
            Com_Error(
                ERR_DROP,
                "Fast-file world cell-caster bits exceed block 1");
            DB_PopStreamPos();
            DB_PopStreamPos();
            return false;
        }
        varraw_uint = varGfxWorld->cellCasterBits;
        Load_raw_uintArray(1, cellCasterCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->sceneDynModel)
    {
        varGfxWorld->sceneDynModel = (GfxSceneDynModel *)AllocLoad_FxElemVisStateSample();
        varGfxSceneDynModel = varGfxWorld->sceneDynModel;
        Load_GfxSceneDynModelArray(1, dynamicModelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->sceneDynBrush)
    {
        varGfxWorld->sceneDynBrush = (GfxSceneDynBrush *)AllocLoad_FxElemVisStateSample();
        varGfxSceneDynBrush = varGfxWorld->sceneDynBrush;
        Load_GfxSceneDynBrushArray(1, dynamicBrushCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightEntityShadowVis)
    {
        varGfxWorld->primaryLightEntityShadowVis = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightEntityShadowVis;
        Load_raw_uintArray(1, entityShadowVisCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightDynEntShadowVis[0])
    {
        varGfxWorld->primaryLightDynEntShadowVis[0] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightDynEntShadowVis[0];
        Load_raw_uintArray(1, modelShadowVisCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightDynEntShadowVis[1])
    {
        varGfxWorld->primaryLightDynEntShadowVis[1] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightDynEntShadowVis[1];
        Load_raw_uintArray(1, brushShadowVisCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->nonSunPrimaryLightForModelDynEnt)
    {
        varGfxWorld->nonSunPrimaryLightForModelDynEnt = AllocLoad_raw_byte();
        varraw_byte = varGfxWorld->nonSunPrimaryLightForModelDynEnt;
        Load_raw_byteArray(1, dynamicModelCount);
    }
    DB_PopStreamPos();
    if (varGfxWorld->shadowGeom)
    {
        varGfxWorld->shadowGeom = (GfxShadowGeometry *)AllocLoad_FxElemVisStateSample();
        varGfxShadowGeometry = varGfxWorld->shadowGeom;
        Load_GfxShadowGeometryArray(1, primaryLightCount);
    }
    if (varGfxWorld->lightRegion)
    {
        varGfxWorld->lightRegion = (GfxLightRegion *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegion = varGfxWorld->lightRegion;
        Load_GfxLightRegionArray(1, primaryLightCount);
    }
    varGfxWorldDpvsStatic = &varGfxWorld->dpvs;
    Load_GfxWorldDpvsStatic(0);
    varGfxWorldDpvsDynamic = &varGfxWorld->dpvsDyn;
    Load_GfxWorldDpvsDynamic(0);
    if (!DB_ValidateMaterializedBlock4Span(
            varGfxWorld->cells,
            static_cast<uint32_t>(cellBytes),
            4,
            "world cells")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxWorld->reflectionProbes,
            static_cast<uint32_t>(reflectionProbeBytes),
            4,
            "world reflection probes")
        || !DB_ValidateMaterializedSpan(
            varGfxWorld->reflectionProbeTextures,
            static_cast<uint32_t>(reflectionProbeTextureBytes),
            4,
            kDirectBlock1,
            "world reflection textures")
        || !DB_ValidateMaterializedSpan(
            varGfxWorld->cellCasterBits,
            static_cast<uint32_t>(cellCasterBytes),
            4,
            kDirectBlock1,
            "world cell-caster bits")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxWorld->dpvs.cullGroups,
            static_cast<uint32_t>(cullGroupBytes),
            4,
            "world cull groups")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxWorld->dpvs.sortedSurfIndex,
            static_cast<uint32_t>(sortedSurfaceBytes),
            2,
            "sorted world surfaces")
        || !DB_ValidateMaterializedBlock4Span(
            varGfxWorld->models,
            static_cast<uint32_t>(modelBytes),
            4,
            "world brush models")
        || !db::validation::GfxWorldCellGraphValid(
            *varGfxWorld,
            disk32::kGfxCellBytes)
        || !DB_ValidateWorldAabbTrees(varGfxWorld))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file world cell graph");
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}

void __cdecl Load_GfxWorldPtr(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldPtr, 4);
    DB_PushStreamPos(0);
    if (*varGfxWorldPtr)
    {
        value = (uint32_t)*varGfxWorldPtr;
        if (value == -1 || value == -2)
        {
            *varGfxWorldPtr = (GfxWorld *)AllocLoad_FxElemVisStateSample();
            if (!*varGfxWorldPtr
                || !DB_IsStreamRangeValid(
                    *varGfxWorldPtr,
                    disk32::kGfxWorldBytes))
            {
                Com_Error(ERR_DROP, "Cannot allocate fast-file world header");
                *varGfxWorldPtr = nullptr;
                DB_PopStreamPos();
                return;
            }
            varGfxWorld = *varGfxWorldPtr;
            if (value == -2)
            {
                inserted = DB_InsertPointer(DBAliasKind::GfxWorld);
                if (!inserted)
                {
                    *varGfxWorldPtr = nullptr;
                    DB_PopStreamPos();
                    return;
                }
            }
            else
                inserted = {};
            if (!Load_GfxWorld(1))
            {
                *varGfxWorldPtr = nullptr;
                DB_PopStreamPos();
                return;
            }
            Load_GfxWorldAsset((XAssetHeader *)varGfxWorldPtr);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::GfxWorld,
                    *varGfxWorldPtr);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varGfxWorldPtr,
                DBAliasKind::GfxWorld);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_MaterialMemory()
{
    varMaterialHandle = &varMaterialMemory->material;
    Mark_MaterialHandle();
}

void __cdecl Mark_MaterialMemoryArray(int32_t count)
{
    MaterialMemory *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialMemory;
    for (i = 0; i < count; ++i)
    {
        varMaterialMemory = var;
        Mark_MaterialMemory();
        ++var;
    }
}

void __cdecl Mark_GfxWorldDpvsStatic()
{
    if (varGfxWorldDpvsStatic->surfaces)
    {
        varGfxSurface = varGfxWorldDpvsStatic->surfaces;
        Mark_GfxSurfaceArray(varGfxWorld->surfaceCount);
    }
    if (varGfxWorldDpvsStatic->smodelDrawInsts)
    {
        varGfxStaticModelDrawInst = varGfxWorldDpvsStatic->smodelDrawInsts;
        Mark_GfxStaticModelDrawInstArray(varGfxWorldDpvsStatic->smodelCount);
    }
}

void __cdecl Mark_GfxWorld()
{
    varGfxImagePtr = &varGfxWorld->skyImage;
    Mark_GfxImagePtr();
    if (varGfxWorld->sunLight)
    {
        varGfxLight = varGfxWorld->sunLight;
        Mark_GfxLight();
    }
    if (varGfxWorld->reflectionProbes)
    {
        varGfxReflectionProbe = varGfxWorld->reflectionProbes;
        Mark_GfxReflectionProbeArray(varGfxWorld->reflectionProbeCount);
    }
    if (varGfxWorld->lightmaps)
    {
        varGfxLightmapArray = varGfxWorld->lightmaps;
        Mark_GfxLightmapArrayArray(varGfxWorld->lightmapCount);
    }
    if (varGfxWorld->materialMemory)
    {
        varMaterialMemory = varGfxWorld->materialMemory;
        Mark_MaterialMemoryArray(varGfxWorld->materialMemoryCount);
    }
    varsunflare_t = &varGfxWorld->sun;
    Mark_sunflare_t();
    varGfxImagePtr = &varGfxWorld->outdoorImage;
    Mark_GfxImagePtr();
    varGfxWorldDpvsStatic = &varGfxWorld->dpvs;
    Mark_GfxWorldDpvsStatic();
}

void __cdecl Mark_GfxWorldPtr()
{
    if (*varGfxWorldPtr)
    {
        varGfxWorld = *varGfxWorldPtr;
        Mark_GfxWorldAsset(varGfxWorld);
        Mark_GfxWorld();
    }
}

void __cdecl Load_GlyphArray(bool atStreamStart, int32_t count)
{
    Load_StreamArray(atStreamStart, (uint8_t *)varGlyph, count, 24);
}

void __cdecl Load_Font(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varFont, 24);
    if (!db::validation::CountInRange(varFont->glyphCount, 96, 65536)
        || !varFont->glyphs)
    {
        Com_Error(ERR_DROP, "Invalid fast-file font glyph table");
        return;
    }
    const uint32_t glyphByteCount = DB_CheckedDirectSpanBytes(
        varFont->glyphCount,
        24,
        "font glyphs");
    DB_PushStreamPos(4);
    varXString = &varFont->fontName;
    Load_XString(0);
    varMaterialHandle = &varFont->material;
    Load_MaterialHandle(0);
    varMaterialHandle = &varFont->glowMaterial;
    Load_MaterialHandle(0);
    if (varFont->glyphs)
    {
        if (varFont->glyphs == (Glyph *)-1)
        {
            varFont->glyphs = (Glyph *)AllocLoad_FxElemVisStateSample();
            varGlyph = varFont->glyphs;
            Load_GlyphArray(1, varFont->glyphCount);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varFont->glyphs,
                glyphByteCount,
                4,
                kDirectBlock4);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_FontHandle(bool atStreamStart)
{
    DBAliasHandle inserted;
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varFontHandle, 4);
    DB_PushStreamPos(0);
    if (*varFontHandle)
    {
        value = (uint32_t)*varFontHandle;
        if (value == -1 || value == -2)
        {
            *varFontHandle = (Font_s *)AllocLoad_FxElemVisStateSample();
            varFont = *varFontHandle;
            if (value == -2)
                inserted = DB_InsertPointer(DBAliasKind::Font);
            else
                inserted = {};
            Load_Font(1);
            Load_FontAsset((XAssetHeader *)varFontHandle);
            if (inserted)
                DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::Font,
                    *varFontHandle);
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varFontHandle,
                DBAliasKind::Font);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_Font()
{
    varMaterialHandle = &varFont->material;
    Mark_MaterialHandle();
    varMaterialHandle = &varFont->glowMaterial;
    Mark_MaterialHandle();
}

void __cdecl Mark_FontHandle()
{
    if (*varFontHandle)
    {
        varFont = *varFontHandle;
        Mark_FontAsset(varFont);
        Mark_Font();
    }
}

void __cdecl Load_XAssetHeader(bool atStreamStart)
{
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)
    {
        Com_Error(ERR_DROP, "Invalid fast-file asset type %d", varXAsset->type);
        return;
    }

    switch (varXAsset->type)
    {
    case ASSET_TYPE_PHYSPRESET:
        varPhysPresetPtr = (PhysPreset **)varXAssetHeader;
        Load_PhysPresetPtr(atStreamStart);
        break;
    case ASSET_TYPE_XANIMPARTS:
        varXAnimPartsPtr = (XAnimParts **)varXAssetHeader;
        Load_XAnimPartsPtr(atStreamStart);
        break;
    case ASSET_TYPE_XMODEL:
        varXModelPtr = (XModel **)varXAssetHeader;
        Load_XModelPtr(atStreamStart);
        break;
    case ASSET_TYPE_MATERIAL:
        varMaterialHandle = (Material **)varXAssetHeader;
        Load_MaterialHandle(atStreamStart);
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        varMaterialTechniqueSetPtr = (MaterialTechniqueSet **)varXAssetHeader;
        Load_MaterialTechniqueSetPtr(atStreamStart);
        break;
    case ASSET_TYPE_IMAGE:
        varGfxImagePtr = (GfxImage **)varXAssetHeader;
        Load_GfxImagePtr(atStreamStart);
        break;
    case ASSET_TYPE_SOUND:
        varsnd_alias_list_ptr = (snd_alias_list_t **)varXAssetHeader;
        Load_snd_alias_list_ptr(atStreamStart);
        break;
    case ASSET_TYPE_SOUND_CURVE:
        varSndCurvePtr = (SndCurve **)varXAssetHeader;
        Load_SndCurvePtr(atStreamStart);
        break;
    case ASSET_TYPE_LOADED_SOUND:
        varLoadedSoundPtr = (LoadedSound **)varXAssetHeader;
        Load_LoadedSoundPtr(atStreamStart);
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        varclipMap_ptr = (clipMap_t **)varXAssetHeader;
        Load_clipMap_ptr(atStreamStart);
        break;
    case ASSET_TYPE_COMWORLD:
        varComWorldPtr = (ComWorld **)varXAssetHeader;
        Load_ComWorldPtr(atStreamStart);
        break;
    case ASSET_TYPE_GAMEWORLD_SP:
        varGameWorldSpPtr = (GameWorldSp **)varXAssetHeader;
        Load_GameWorldSpPtr(atStreamStart);
        break;
    case ASSET_TYPE_GAMEWORLD_MP:
        varGameWorldMpPtr = (GameWorldMp **)varXAssetHeader;
        Load_GameWorldMpPtr(atStreamStart);
        break;
    case ASSET_TYPE_MAP_ENTS:
        varMapEntsPtr = (MapEnts **)varXAssetHeader;
        Load_MapEntsPtr(atStreamStart);
        break;
    case ASSET_TYPE_GFXWORLD:
        varGfxWorldPtr = (GfxWorld **)varXAssetHeader;
        Load_GfxWorldPtr(atStreamStart);
        break;
    case ASSET_TYPE_LIGHT_DEF:
        varGfxLightDefPtr = (GfxLightDef **)varXAssetHeader;
        Load_GfxLightDefPtr(atStreamStart);
        break;
    case ASSET_TYPE_FONT:
        varFontHandle = (Font_s **)varXAssetHeader;
        Load_FontHandle(atStreamStart);
        break;
    case ASSET_TYPE_MENULIST:
        varMenuListPtr = (MenuList **)varXAssetHeader;
        Load_MenuListPtr(atStreamStart);
        break;
    case ASSET_TYPE_MENU:
        varmenuDef_ptr = (menuDef_t **)varXAssetHeader;
        Load_menuDef_ptr(atStreamStart);
        break;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        varLocalizeEntryPtr = (LocalizeEntry **)varXAssetHeader;
        Load_LocalizeEntryPtr(atStreamStart);
        break;
    case ASSET_TYPE_WEAPON:
        varWeaponDefPtr = (WeaponDef **)varXAssetHeader;
        Load_WeaponDefPtr(atStreamStart);
        break;
    case ASSET_TYPE_FX:
        varFxEffectDefHandle = (const FxEffectDef **)varXAssetHeader;
        Load_FxEffectDefHandle(atStreamStart);
        break;
    case ASSET_TYPE_IMPACT_FX:
        varFxImpactTablePtr = (FxImpactTable **)varXAssetHeader;
        Load_FxImpactTablePtr(atStreamStart);
        break;
    case ASSET_TYPE_RAWFILE:
        varRawFilePtr = (RawFile **)varXAssetHeader;
        Load_RawFilePtr(atStreamStart);
        break;
    case ASSET_TYPE_STRINGTABLE:
        varStringTablePtr = (StringTable **)varXAssetHeader;
        Load_StringTablePtr(atStreamStart);
        break;
    default:
        Com_Error(ERR_DROP, "Unsupported fast-file asset type %d", varXAsset->type);
        break;
    }
}

void __cdecl Load_XAsset(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAsset, 8);
    varXAssetHeader = &varXAsset->header;
    Load_XAssetHeader(0);
}

void __cdecl Mark_XAssetHeader()
{
    switch (varXAsset->type)
    {
    case ASSET_TYPE_PHYSPRESET:
        varPhysPresetPtr = (PhysPreset **)varXAssetHeader;
        Mark_PhysPresetPtr();
        break;
    case ASSET_TYPE_XANIMPARTS:
        varXAnimPartsPtr = (XAnimParts **)varXAssetHeader;
        Mark_XAnimPartsPtr();
        break;
    case ASSET_TYPE_XMODEL:
        varXModelPtr = (XModel **)varXAssetHeader;
        Mark_XModelPtr();
        break;
    case ASSET_TYPE_MATERIAL:
        varMaterialHandle = (Material **)varXAssetHeader;
        Mark_MaterialHandle();
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        varMaterialTechniqueSetPtr = (MaterialTechniqueSet **)varXAssetHeader;
        Mark_MaterialTechniqueSetPtr();
        break;
    case ASSET_TYPE_IMAGE:
        varGfxImagePtr = (GfxImage **)varXAssetHeader;
        Mark_GfxImagePtr();
        break;
    case ASSET_TYPE_SOUND:
        varsnd_alias_list_ptr = (snd_alias_list_t **)varXAssetHeader;
        Mark_snd_alias_list_ptr();
        break;
    case ASSET_TYPE_SOUND_CURVE:
        varSndCurvePtr = (SndCurve **)varXAssetHeader;
        Mark_SndCurvePtr();
        break;
    case ASSET_TYPE_LOADED_SOUND:
        varLoadedSoundPtr = (LoadedSound **)varXAssetHeader;
        Mark_LoadedSoundPtr();
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        varclipMap_ptr = (clipMap_t **)varXAssetHeader;
        Mark_clipMap_ptr();
        break;
    case ASSET_TYPE_COMWORLD:
        varComWorldPtr = (ComWorld **)varXAssetHeader;
        Mark_ComWorldPtr();
        break;
    case ASSET_TYPE_GAMEWORLD_SP:
        varGameWorldSpPtr = (GameWorldSp **)varXAssetHeader;
        Mark_GameWorldSpPtr();
        break;
    case ASSET_TYPE_GAMEWORLD_MP:
        varGameWorldMpPtr = (GameWorldMp **)varXAssetHeader;
        Mark_GameWorldMpPtr();
        break;
    case ASSET_TYPE_MAP_ENTS:
        varMapEntsPtr = (MapEnts **)varXAssetHeader;
        Mark_MapEntsPtr();
        break;
    case ASSET_TYPE_GFXWORLD:
        varGfxWorldPtr = (GfxWorld **)varXAssetHeader;
        Mark_GfxWorldPtr();
        break;
    case ASSET_TYPE_LIGHT_DEF:
        varGfxLightDefPtr = (GfxLightDef **)varXAssetHeader;
        Mark_GfxLightDefPtr();
        break;
    case ASSET_TYPE_FONT:
        varFontHandle = (Font_s **)varXAssetHeader;
        Mark_FontHandle();
        break;
    case ASSET_TYPE_MENULIST:
        varMenuListPtr = (MenuList **)varXAssetHeader;
        Mark_MenuListPtr();
        break;
    case ASSET_TYPE_MENU:
        varmenuDef_ptr = (menuDef_t **)varXAssetHeader;
        Mark_menuDef_ptr();
        break;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        varLocalizeEntryPtr = (LocalizeEntry **)varXAssetHeader;
        Mark_LocalizeEntryPtr();
        break;
    case ASSET_TYPE_WEAPON:
        varWeaponDefPtr = (WeaponDef **)varXAssetHeader;
        Mark_WeaponDefPtr();
        break;
    case ASSET_TYPE_FX:
        varFxEffectDefHandle = (const FxEffectDef **)varXAssetHeader;
        Mark_FxEffectDefHandle();
        break;
    case ASSET_TYPE_IMPACT_FX:
        varFxImpactTablePtr = (FxImpactTable **)varXAssetHeader;
        Mark_FxImpactTablePtr();
        break;
    case ASSET_TYPE_RAWFILE:
        varRawFilePtr = (RawFile **)varXAssetHeader;
        Mark_RawFilePtr();
        break;
    case ASSET_TYPE_STRINGTABLE:
        varStringTablePtr = (StringTable **)varXAssetHeader;
        Mark_StringTablePtr();
        break;
    }
}

void __cdecl Mark_XAsset()
{
    varXAssetHeader = &varXAsset->header;
    Mark_XAssetHeader();
}

void __cdecl Mark_SndAliasCustom(snd_alias_list_t **var)
{
    varsnd_alias_list_ptr = var;
    Mark_snd_alias_list_ptr();
}

void __cdecl DB_SaveDObjs()
{
    int32_t handle; // [esp+0h] [ebp-Ch]
    int32_t handlea; // [esp+0h] [ebp-Ch]
    DObj_s *obj; // [esp+4h] [ebp-8h]
    DObj_s *obja; // [esp+4h] [ebp-8h]
    int32_t localClientNum; // [esp+8h] [ebp-4h]

    for (localClientNum = 0; localClientNum < 1; ++localClientNum)
    {
        for (handle = 0; handle < 1152; ++handle)
        {
            obj = Com_GetClientDObj(handle, localClientNum);
            if (obj)
                DObjArchive(obj);
        }
    }
    for (handlea = 0; handlea < 1024; ++handlea)
    {
        obja = Com_GetServerDObj(handlea);
        if (obja)
            DObjArchive(obja);
    }
}

void __cdecl DB_LoadDObjs()
{
    int32_t handle; // [esp+0h] [ebp-Ch]
    int32_t handlea; // [esp+0h] [ebp-Ch]
    DObj_s *obj; // [esp+4h] [ebp-8h]
    DObj_s *obja; // [esp+4h] [ebp-8h]
    int32_t localClientNum; // [esp+8h] [ebp-4h]

    for (localClientNum = 0; localClientNum < 1; ++localClientNum)
    {
        for (handle = 0; handle < 1152; ++handle)
        {
            obj = Com_GetClientDObj(handle, localClientNum);
            if (obj)
                DObjUnarchive(obj);
        }
    }
    for (handlea = 0; handlea < 1024; ++handlea)
    {
        obja = Com_GetServerDObj(handlea);
        if (obja)
            DObjUnarchive(obja);
    }
}

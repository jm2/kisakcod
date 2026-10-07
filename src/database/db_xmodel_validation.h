#pragma once

// The XModel and XSurface load rules that read only the loaded objects, shared
// by the 32-bit loader (db_load.cpp) and the 64-bit XModel loader. Moved from
// db_load.cpp; DB_ValidateLoadedXSurface and DB_ValidateLoadedXModel there
// keep only their checks of in-place disk records (the rigid-vertex lists, the
// collision-tree headers, the surfaces, the material handles and the collision
// surfaces, which a 64-bit load converts into native storage) and then call
// DB_ValidateXSurfaceGraph and DB_ValidateXModelGraph here.

#include <database/db_disk32.h>
#include <database/db_stream.h>
#include <database/db_validation.h>
#include <qcommon/com_error.h>
#include <xanim/xanim.h>
#include <xanim/xmodel.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace db::xmodel_validation
{
inline constexpr db::relocation::BlockMask kDirectBlock4 = db::relocation::BlockBit(4);
inline constexpr db::relocation::BlockMask kDirectBlock7 = db::relocation::BlockBit(7);
inline constexpr db::relocation::BlockMask kDirectBlock8 = db::relocation::BlockBit(8);

inline bool DB_ValidateMaterializedSpan(
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

inline bool DB_ValidateMaterializedBlock4Span(
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

inline bool DB_GetXSurfaceCollisionTreeExtents(
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

inline bool DB_ValidateXSurfaceCollisionTreeGraph(
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

inline bool DB_ValidateXSurfaceGraph(
    const XSurface *surface,
    uint8_t deformed,
    uint32_t modelBoneCount)
{
    if (!surface || deformed > 1u || surface->deformed != (deformed != 0u)
        || !modelBoneCount || modelBoneCount > 128u)
        return false;

    uint32_t vertexBytes = 0;
    uint32_t indexBytes = 0;
    uint32_t rigidListBytes = 0;
    uint32_t blendElementCount = 0;
    uint32_t blendBytes = 0;
    if (!db::validation::XSurfaceTriangleCountValid(surface->triCount)
        || !db::validation::CheckedSpanBytes(
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
            &rigidListBytes)
        || !db::validation::XSurfaceSkinningLayoutValid(
            surface->vertInfo.vertCount,
            surface->vertInfo.vertsBlend != nullptr,
            surface->vertCount,
            deformed != 0u,
            &blendElementCount)
        || !db::validation::CheckedSpanBytes(
            blendElementCount,
            static_cast<uint32_t>(sizeof(uint16_t)),
            &blendBytes))
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
        || !db::validation::XSurfaceVertexPayloadValid(
            surface->verts0,
            surface->vertCount)
        || !db::validation::XSurfaceTriangleIndicesValid(
            surface->triIndices,
            surface->triCount,
            surface->vertCount))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file surface geometry");
        return false;
    }
    uint32_t surfacePartBits[4] = {};
    std::memcpy(
        surfacePartBits,
        surface->partBits,
        sizeof(surfacePartBits));
    if (deformed)
    {
        const db::relocation::Status blendSpan = DB_ValidateStreamAddress(
            surface->vertInfo.vertsBlend,
            blendBytes,
            2,
            kDirectBlock4);
        if (blendSpan != db::relocation::Status::Ok
            || !db::validation::XSurfaceBlendRecordsValid(
                surface->vertInfo.vertsBlend,
                surface->vertInfo.vertCount,
                modelBoneCount,
                surfacePartBits))
        {
            Com_Error(ERR_DROP, "Invalid completed fast-file skin weights");
            return false;
        }
        return true;
    }

    if (!db::validation::XSurfaceRigidPartitionValid(
            surface->vertList,
            surface->vertListCount,
            surface->vertCount,
            surface->triCount,
            surface->triIndices)
        || !db::validation::XSurfaceRigidSkinningValid(
            surface->vertList,
            surface->vertListCount,
            surface->vertCount,
            modelBoneCount,
            surfacePartBits))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file rigid surface partition");
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

        uint32_t nodeBytes = 0;
        uint32_t leafBytes = 0;
        if (!DB_GetXSurfaceCollisionTreeExtents(
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

inline bool DB_ValidateXModelGraph(
    const XModel *const model,
    const uint32_t boneNameBytes,
    const uint32_t parentListBytes,
    const uint32_t quaternionBytes,
    const uint32_t translationBytes,
    const uint32_t classificationBytes,
    const uint32_t baseMatrixBytes)
{
    if (model && db::validation::IsStubAssetName(model->name))
        return true; // a stub holds only its name: no bones, surfaces or LODs
    if (!model || !model->name || !*model->name
        || !db::validation::CountInRange(
            model->numCollSurfs,
            0,
            UINT16_MAX)
        || (model->numCollSurfs != 0) != (model->collSurfs != nullptr)
        || (model->numCollSurfs != 0
            && (model->collLod < 0 || model->collLod >= model->numLods)))
        return false;

    const db::validation::XModelPointerPresence pointers = {
        model->name != nullptr,
        model->boneNames != nullptr,
        model->parentList != nullptr,
        model->quats != nullptr,
        model->trans != nullptr,
        model->partClassification != nullptr,
        model->baseMat != nullptr,
        model->surfs != nullptr,
        model->materialHandles != nullptr,
        model->boneInfo != nullptr,
    };
    if (!db::validation::XModelHeaderLayoutValid(
            model->numBones,
            model->numRootBones,
            model->numsurfs,
            model->numLods,
            model->lodRampType,
            pointers))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file model header");
        return false;
    }

    const uint32_t nonRootBoneCount =
        model->numBones - model->numRootBones;
    uint32_t boneInfoBytes = 0u;
    if (!db::validation::CheckedSpanBytes(
            model->numBones,
            40u,
            &boneInfoBytes)
        || !DB_ValidateMaterializedBlock4Span(
            model->boneNames,
            boneNameBytes,
            2,
            "model bone names")
        || !DB_ValidateMaterializedBlock4Span(
            model->parentList,
            parentListBytes,
            1,
            "model parent list")
        || !DB_ValidateMaterializedBlock4Span(
            model->quats,
            quaternionBytes,
            2,
            "model quaternions")
        || !DB_ValidateMaterializedBlock4Span(
            model->trans,
            translationBytes,
            4,
            "model translations")
        || !DB_ValidateMaterializedBlock4Span(
            model->partClassification,
            classificationBytes,
            1,
            "model part classifications")
        || !DB_ValidateMaterializedBlock4Span(
            model->baseMat,
            baseMatrixBytes,
            4,
            "model base matrices")
        || !DB_ValidateMaterializedBlock4Span(
            model->boneInfo,
            boneInfoBytes,
            4,
            "model bone info")
        || !db::validation::XModelPartClassificationsValid(
            model->partClassification,
            model->numBones)
        || !db::validation::FiniteFloatArray(
            model->trans,
            3u * nonRootBoneCount)
        || !db::validation::XModelBasePoseValid(
            model->baseMat,
            model->numBones)
        || !db::validation::XModelBoneInfoValid(
            model->boneInfo,
            model->numBones)
        || !db::validation::XModelBoundsValid(
            model->mins,
            model->maxs,
            model->radius))
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file model array span");
        return false;
    }

    for (uint32_t child = 0u; child < nonRootBoneCount; ++child)
    {
        const uint32_t boneIndex = model->numRootBones + child;
        const uint32_t parentOffset = model->parentList[child];
        if (!parentOffset || parentOffset > boneIndex)
        {
            Com_Error(ERR_DROP, "Invalid fast-file model parent relationship");
            return false;
        }
    }

    uint32_t expectedSurfaceIndex = 0u;
    for (uint32_t lod = 0u;
         lod < static_cast<uint32_t>(model->numLods);
         ++lod)
    {
        const XModelLodInfo &lodInfo = model->lodInfo[lod];
        uint32_t lodPartBits[4] = {};
        std::memcpy(lodPartBits, lodInfo.partBits, sizeof(lodPartBits));
        if (lodInfo.surfIndex != expectedSurfaceIndex
            || !db::validation::XModelLodLayoutValid(
                lod,
                lodInfo.surfIndex,
                lodInfo.numsurfs,
                model->numsurfs,
                lodInfo.lod,
                lodInfo.dist,
                model->numBones,
                lodPartBits)
            || lodInfo.numsurfs > model->numsurfs - expectedSurfaceIndex)
        {
            Com_Error(ERR_DROP, "Invalid completed fast-file model LOD");
            return false;
        }
        const float previousLodDistance = lod > 0u
            ? model->lodInfo[lod - 1u].dist
            : 0.0f;
        if (!db::validation::XModelLodDistanceFollows(
                previousLodDistance,
                lodInfo.dist))
        {
            Com_Error(ERR_DROP, "Fast-file model LOD distances are not monotonic");
            return false;
        }

        uint32_t lodVertexCount = 0u;
        uint32_t lodTriangleCount = 0u;
        if (!db::validation::XModelLodSurfaceCacheLayoutValid(
                &model->surfs[lodInfo.surfIndex],
                lodInfo.numsurfs,
                &lodVertexCount,
                &lodTriangleCount)
            || !db::validation::XModelStaticCacheLayoutValid(
                lodInfo.smcIndexPlusOne,
                lodInfo.smcAllocBits,
                lodVertexCount,
                lodTriangleCount))
        {
            Com_Error(ERR_DROP, "Invalid completed fast-file model cache layout");
            return false;
        }
        for (uint32_t surface = 0u;
             surface < lodInfo.numsurfs;
             ++surface)
        {
            const XSurface &xsurface =
                model->surfs[lodInfo.surfIndex + surface];
            for (uint32_t word = 0u; word < 4u; ++word)
            {
                if ((static_cast<uint32_t>(xsurface.partBits[word])
                        & ~lodPartBits[word]) != 0u)
                {
                    Com_Error(
                        ERR_DROP,
                        "Model surface bones escape its LOD part bits");
                    return false;
                }
            }
        }
        expectedSurfaceIndex += lodInfo.numsurfs;
    }
    if (expectedSurfaceIndex != model->numsurfs)
    {
        Com_Error(ERR_DROP, "Fast-file model LODs do not cover its surfaces");
        return false;
    }
    for (uint32_t surface = 0u; surface < model->numsurfs; ++surface)
    {
        if (!model->materialHandles[surface])
        {
            Com_Error(ERR_DROP, "Fast-file model surface has no material");
            return false;
        }
    }
    uint32_t aggregateCollisionContents = 0u;
    for (int32_t surface = 0;
         surface < model->numCollSurfs;
         ++surface)
    {
        const XModelCollSurf_s &collision = model->collSurfs[surface];
        uint32_t triangleBytes = 0u;
        if (!db::validation::CountInRange(
                collision.numCollTris,
                1,
                UINT16_MAX)
            || !collision.collTris
            || !db::validation::CheckedSpanBytes(
                static_cast<uint32_t>(collision.numCollTris),
                48u,
                &triangleBytes)
            || !DB_ValidateMaterializedBlock4Span(
                collision.collTris,
                triangleBytes,
                4,
                "model collision triangles")
            || !db::validation::FiniteFloatArray(collision.mins, 3)
            || !db::validation::FiniteFloatArray(collision.maxs, 3)
            || (collision.contents != 0
                && (collision.boneIdx < 0
                    || collision.boneIdx >= model->numBones)))
        {
            Com_Error(ERR_DROP, "Invalid completed model collision graph");
            return false;
        }
        aggregateCollisionContents |= static_cast<uint32_t>(
            collision.contents);
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            if (collision.mins[axis] > collision.maxs[axis])
            {
                Com_Error(ERR_DROP, "Invalid completed model collision bounds");
                return false;
            }
        }
        for (int32_t triangle = 0;
             triangle < collision.numCollTris;
             ++triangle)
        {
            if (!db::validation::FiniteFloatArray(
                    collision.collTris[triangle].plane,
                    12))
            {
                Com_Error(ERR_DROP, "Invalid completed model collision triangle");
                return false;
            }
        }
    }
    if (static_cast<uint32_t>(model->contents)
        != aggregateCollisionContents)
    {
        Com_Error(ERR_DROP, "Fast-file model collision contents are inconsistent");
        return false;
    }
    return true;
}
} // namespace db::xmodel_validation

#include "r_model_skin.h"
#include "r_init.h"
#include "r_dvars.h"
#include <qcommon/com_playerprofile.h>
#include "r_buffers.h"
#include <universal/profile.h>
#include "r_dobj_skin.h"
#include "r_model_surface_stream.h"
#include <database/db_validation.h>

#include <cstring>

namespace model_surface_stream = gfx::model_surface_stream;


//uint32_t const *const g_shortBoneWeightPerm__uint4 820f4950     gfx_d3d : r_model_skin.obj
//struct __vector4 const g_shortBoneWeightPerm 85b981d0     gfx_d3d : r_model_skin.obj

static void __cdecl R_MultiplySkelMat(const DObjSkelMat *mat0, const DObjSkelMat *mat1, DObjSkelMat *out)
{
    out->axis[0][0] = mat0->axis[0][0] * mat1->axis[0][0]
        + mat0->axis[0][1] * mat1->axis[1][0]
        + mat0->axis[0][2] * mat1->axis[2][0];
    out->axis[0][1] = mat0->axis[0][0] * mat1->axis[0][1]
        + mat0->axis[0][1] * mat1->axis[1][1]
        + mat0->axis[0][2] * mat1->axis[2][1];
    out->axis[0][2] = mat0->axis[0][0] * mat1->axis[0][2]
        + mat0->axis[0][1] * mat1->axis[1][2]
        + mat0->axis[0][2] * mat1->axis[2][2];
    out->axis[1][0] = mat0->axis[1][0] * mat1->axis[0][0]
        + mat0->axis[1][1] * mat1->axis[1][0]
        + mat0->axis[1][2] * mat1->axis[2][0];
    out->axis[1][1] = mat0->axis[1][0] * mat1->axis[0][1]
        + mat0->axis[1][1] * mat1->axis[1][1]
        + mat0->axis[1][2] * mat1->axis[2][1];
    out->axis[1][2] = mat0->axis[1][0] * mat1->axis[0][2]
        + mat0->axis[1][1] * mat1->axis[1][2]
        + mat0->axis[1][2] * mat1->axis[2][2];
    out->axis[2][0] = mat0->axis[2][0] * mat1->axis[0][0]
        + mat0->axis[2][1] * mat1->axis[1][0]
        + mat0->axis[2][2] * mat1->axis[2][0];
    out->axis[2][1] = mat0->axis[2][0] * mat1->axis[0][1]
        + mat0->axis[2][1] * mat1->axis[1][1]
        + mat0->axis[2][2] * mat1->axis[2][1];
    out->axis[2][2] = mat0->axis[2][0] * mat1->axis[0][2]
        + mat0->axis[2][1] * mat1->axis[1][2]
        + mat0->axis[2][2] * mat1->axis[2][2];
    out->origin[0] = mat0->origin[0] * mat1->axis[0][0]
        + mat0->origin[1] * mat1->axis[1][0]
        + mat0->origin[2] * mat1->axis[2][0]
        + mat1->origin[0];
    out->origin[1] = mat0->origin[0] * mat1->axis[0][1]
        + mat0->origin[1] * mat1->axis[1][1]
        + mat0->origin[2] * mat1->axis[2][1]
        + mat1->origin[1];
    out->origin[2] = mat0->origin[0] * mat1->axis[0][2]
        + mat0->origin[1] * mat1->axis[1][2]
        + mat0->origin[2] * mat1->axis[2][2]
        + mat1->origin[2];
}

namespace
{
constexpr uint32_t kSkinVertexArenaBytes = 0x480000u;
constexpr uint32_t kSkinRecordAlignment = alignof(GfxModelRigidSurface);
constexpr uint32_t kHiddenSkinRecordBytes = sizeof(GfxModelHiddenSurface);
constexpr uint32_t kSkinnedSkinRecordBytes = sizeof(GfxModelSkinnedSurface);
constexpr uint32_t kRigidSkinRecordBytes = sizeof(GfxModelRigidSurface);

const GfxBackEndData *R_GetSkinStreamOwner(
    const void *const stream,
    const uint32_t streamBytes)
{
    if (!stream || !streamBytes)
        return nullptr;

    const uintptr_t streamAddress = reinterpret_cast<uintptr_t>(stream);
    for (const GfxBackEndData &data : s_backEndData)
    {
        const uintptr_t begin = reinterpret_cast<uintptr_t>(data.surfsBuffer);
        const uintptr_t end = begin + sizeof(data.surfsBuffer);
        const uint32_t publishedBytes = Sys_AtomicLoad(&data.surfPos);
        if (streamAddress >= begin && streamAddress <= end
            && streamBytes <= end - streamAddress
            && publishedBytes <= sizeof(data.surfsBuffer)
            && streamAddress - begin <= publishedBytes
            && streamBytes <= publishedBytes - (streamAddress - begin))
        {
            return &data;
        }
    }
    return nullptr;
}

bool R_SurfaceSkinningMetadataValid(
    const XSurface &surface,
    const uint32_t modelBoneCount)
{
    if (!surface.verts0)
        return false;

    uint32_t blendElementCount = 0u;
    if (!db::validation::XSurfaceSkinningLayoutValid(
            surface.vertInfo.vertCount,
            surface.vertInfo.vertsBlend != nullptr,
            surface.vertCount,
            surface.deformed,
            &blendElementCount))
    {
        return false;
    }

    uint32_t surfacePartBits[4] = {};
    std::memcpy(
        surfacePartBits,
        surface.partBits,
        sizeof(surfacePartBits));
    return surface.deformed
        ? db::validation::XSurfaceBlendRecordsValid(
            surface.vertInfo.vertsBlend,
            surface.vertInfo.vertCount,
            modelBoneCount,
            surfacePartBits)
        : db::validation::XSurfaceRigidSkinningValid(
            surface.vertList,
            surface.vertListCount,
            surface.vertCount,
            modelBoneCount,
            surfacePartBits);
}

bool R_CommandContainsSurfaceBones(
    const SkinXModelCmd &skinCmd,
    const GfxModelSkinnedSurface &surface)
{
    for (uint32_t localBone = 0u;
         localBone < surface.info.boneCount;
         ++localBone)
    {
        const uint32_t localBits = static_cast<uint32_t>(
            surface.xsurf->partBits[localBone >> 5]);
        if ((localBits
                & (UINT32_C(0x80000000) >> (localBone & 31u))) == 0u)
        {
            continue;
        }
        const uint32_t globalBone = surface.info.boneIndex + localBone;
        if ((skinCmd.surfacePartBits[globalBone >> 5]
                & (UINT32_C(0x80000000) >> (globalBone & 31u))) == 0u)
        {
            return false;
        }
    }
    return true;
}

bool R_ValidateSkinXModelStream(
    const SkinXModelCmd *const skinCmd,
    const GfxBackEndData **const ownerOut)
{
    if (!skinCmd || !ownerOut || !skinCmd->modelSurfs || !skinCmd->mat
        || !skinCmd->surfCount || !skinCmd->modelSurfWordCount)
    {
        return false;
    }

    uint32_t streamBytes = 0u;
    if (!model_surface_stream::TryMultiply(
            skinCmd->modelSurfWordCount,
            static_cast<uint32_t>(sizeof(uint32_t)),
            &streamBytes))
    {
        return false;
    }
    const GfxBackEndData *const owner =
        R_GetSkinStreamOwner(skinCmd->modelSurfs, streamBytes);
    if (!owner || owner != frontEndDataOut)
        return false;

    bool outputModeSet = false;
    bool directOutput = false;
    uint32_t priorOutputEnd = 0u;

    model_surface_stream::Cursor cursor(
        skinCmd->modelSurfs,
        streamBytes,
        skinCmd->surfCount);
    for (uint32_t index = 0u; index < skinCmd->surfCount; ++index)
    {
        model_surface_stream::View record;
        if (!cursor.Next(
                kHiddenSkinRecordBytes,
                kSkinnedSkinRecordBytes,
                kRigidSkinRecordBytes,
                &record))
        {
            return false;
        }
        if (record.kind != model_surface_stream::RecordKind::Skinned)
            continue;

        const GfxModelSkinnedSurface *const surface =
            reinterpret_cast<const GfxModelSkinnedSurface *>(record.data);
        if (!surface->xsurf || !surface->info.baseMat
            || !model_surface_stream::IsBoneSpanValid(
                surface->info.boneIndex,
                surface->info.boneCount)
            || !R_SurfaceSkinningMetadataValid(
                *surface->xsurf,
                surface->info.boneCount)
            || !R_CommandContainsSurfaceBones(*skinCmd, *surface))
        {
            return false;
        }

        uint32_t vertexBytes = 0u;
        if (!surface->xsurf->vertCount
            || !model_surface_stream::TryMultiply(
                surface->xsurf->vertCount,
                static_cast<uint32_t>(sizeof(GfxPackedVertex)),
                &vertexBytes))
        {
            return false;
        }

        uint32_t outputOffset = 0u;
        uint32_t publishedOutputBytes = 0u;
        const bool recordDirect = surface->skinnedCachedOffset
            == model_surface_stream::kDirectSkinnedTag;
        if (recordDirect)
        {
            if (!owner->tempSkinBuf || !surface->skinnedVert)
                return false;
            const uintptr_t output =
                reinterpret_cast<uintptr_t>(surface->skinnedVert);
            const uintptr_t tempBegin =
                reinterpret_cast<uintptr_t>(owner->tempSkinBuf);
            publishedOutputBytes = Sys_AtomicLoad(&owner->tempSkinPos);
            if ((output & 15u) != 0u || output < tempBegin
                || output - tempBegin > kSkinVertexArenaBytes
                || vertexBytes > kSkinVertexArenaBytes - (output - tempBegin))
            {
                return false;
            }
            outputOffset = static_cast<uint32_t>(output - tempBegin);
        }
        else
        {
            outputOffset = static_cast<uint32_t>(
                surface->skinnedCachedOffset);
            if (!owner->skinnedCacheVb || !gfxBuf.skinnedCacheLockAddr
                || (outputOffset & 15u) != 0u
                || outputOffset > kSkinVertexArenaBytes
                || vertexBytes > kSkinVertexArenaBytes - outputOffset)
            {
                return false;
            }
            publishedOutputBytes = Sys_AtomicLoad(
                &owner->skinnedCacheVb->used);
        }

        if (publishedOutputBytes > kSkinVertexArenaBytes
            || outputOffset > publishedOutputBytes
            || vertexBytes > publishedOutputBytes - outputOffset
            || (outputModeSet
                && (recordDirect != directOutput
                    || outputOffset != priorOutputEnd)))
        {
            return false;
        }
        outputModeSet = true;
        directOutput = recordDirect;
        priorOutputEnd = outputOffset + vertexBytes;
    }

    if (!cursor.Finished() || !outputModeSet)
        return false;
    *ownerOut = owner;
    return true;
}
} // namespace

void R_SkinXModelCmd(const SkinXModelCmd *skinCmd)
{
    if (dx.deviceLost) return;

    PROF_SCOPED("R_SkinXModel");

    //bool sseEnabled = sys_SSE->current.enabled && r_sse_skinning->current.enabled;
    //bool sseStateUsed = false;

    const GfxBackEndData *streamOwner = nullptr;
    iassert(skinCmd);
    if (!R_ValidateSkinXModelStream(skinCmd, &streamOwner))
    {
        Com_Error(
            ERR_FATAL,
            "Renderer skin worker stream invariant failed");
        return;
    }

    const uint32_t streamBytes =
        static_cast<uint32_t>(skinCmd->modelSurfWordCount)
        * sizeof(uint32_t);
    model_surface_stream::Cursor cursor(
        skinCmd->modelSurfs,
        streamBytes,
        skinCmd->surfCount);

    int boneIndex = -1;
    uint8_t boneCount = 0u;
    const DObjAnimMat *baseMat = nullptr;
    GfxPackedVertex *skinVerticesOut = NULL;

    DObjSkelMat __declspec(align(16)) boneSkelMats[128];
    memset(boneSkelMats, 0, sizeof(boneSkelMats));

    for (uint32_t i = 0; i < skinCmd->surfCount; i++)
    {
        model_surface_stream::View record;
        if (!cursor.Next(
                kHiddenSkinRecordBytes,
                kSkinnedSkinRecordBytes,
                kRigidSkinRecordBytes,
                &record))
        {
            return;
        }
        if (record.kind != model_surface_stream::RecordKind::Skinned)
            continue;

        GfxModelSkinnedSurface *const skinnedSurf =
            reinterpret_cast<GfxModelSkinnedSurface *>(record.data);

        if (boneIndex != skinnedSurf->info.boneIndex
            || boneCount != skinnedSurf->info.boneCount
            || baseMat != skinnedSurf->info.baseMat)
        {
            boneIndex = skinnedSurf->info.boneIndex;
            boneCount = skinnedSurf->info.boneCount;
            baseMat = skinnedSurf->info.baseMat;
            const int totalBones = boneIndex + skinnedSurf->info.boneCount;
            for (uint32_t j = boneIndex; j < totalBones; j++)
            {
                if ((skinCmd->surfacePartBits[j >> 5] & (0x80000000 >> (j & 0x1F))) == 0)
                    continue;

                //if (sseStateUsed)
                //{
                //    sseStateUsed = false;
                //    /*_m_empty();*/
                //}

                DObjSkelMat mat0, mat1;

                ConvertQuatToInverseSkelMat(&baseMat[j - boneIndex], &mat0);
                ConvertQuatToSkelMat(&skinCmd->mat[j], &mat1);
                R_MultiplySkelMat(&mat0, &mat1, &boneSkelMats[j]);

                boneSkelMats[j].axis[0][3] = 0.0f;
                boneSkelMats[j].axis[1][3] = 0.0f;
                boneSkelMats[j].axis[2][3] = 0.0f;
                boneSkelMats[j].origin[3] = 1.0f;
            }
        }

        XSurface *xsurf = skinnedSurf->xsurf;

        iassert(xsurf);

        if (skinnedSurf->skinnedCachedOffset < 0)
        {
            iassert((reinterpret_cast<uintptr_t>(skinnedSurf->skinnedVert) & 15u) == 0u);
            skinVerticesOut = skinnedSurf->skinnedVert;
        }
        else
        {
            iassert(gfxBuf.skinnedCacheLockAddr);
            iassert((reinterpret_cast<uintptr_t>(gfxBuf.skinnedCacheLockAddr) & 15u) == 0u);
            iassert(((skinnedSurf->skinnedCachedOffset & 15) == 0));
            skinVerticesOut = (GfxPackedVertex*)&gfxBuf.skinnedCacheLockAddr[skinnedSurf->skinnedCachedOffset];
        }

        // LWSS: this makes the viewmodels flicker. Decomp is not fully accurate (should just have templated functions)
        //if (sseEnabled)
        //{
        //    if (!sseStateUsed)
        //    {
        //        sseStateUsed = true;
        //        //_m_empty();
        //    }
        //
        //    GfxPackedVertexNormal *skinVertNormalIn = 0, *skinVertNormalOut = 0;
        //    if (gfxBuf.fastSkin)
        //    {
        //        if (skinnedSurf->skinnedCachedOffset >= 0)
        //            skinVertNormalOut = &gfxBuf.skinnedCacheNormalsAddr[skinnedSurf->skinnedCachedOffset >> 5];
        //        if (skinnedSurf->skinnedVert)
        //            skinVertNormalIn = &gfxBuf.oldSkinnedCacheNormalsAddr[(int)skinnedSurf->skinnedVert >> 5];
        //    }
        //    R_SkinXSurfaceSkinnedSse(xsurf, &boneSkelMats[boneIndex], skinVertNormalIn, skinVertNormalOut, skinnedVert);
        //}
        //else
        //{

            R_SkinXSurfaceSkinned(xsurf, &boneSkelMats[boneIndex], skinVerticesOut);

        //}
    }

    iassert(cursor.Finished());
    (void)streamOwner;

    //if (sseStateUsed)
        /*_m_empty();*/
}


void __cdecl R_SkinXSurfaceSkinned(
    const XSurface *xsurf,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *skinVerticesOut)
{
    if (xsurf->deformed)
        R_SkinXSurfaceWeight(xsurf->verts0, &xsurf->vertInfo, boneMatrix, skinVerticesOut);
    else
        R_SkinXSurfaceRigid(xsurf, xsurf->vertCount, boneMatrix, skinVerticesOut);
}

void __cdecl R_SkinXSurfaceWeight(
    const GfxPackedVertex *inVerts,
    const XSurfaceVertexInfo *vertexInfo,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *outVerts)
{
    const uint16_t *vertsBlend; // [esp+30h] [ebp-8h]
    int vertIndex; // [esp+34h] [ebp-4h]

    PROF_SCOPED("SkinXSurfaceWeight");

    vertIndex = 0;
    vertsBlend = vertexInfo->vertsBlend;
    if (vertexInfo->vertCount[0])
    {
        R_SkinXSurfaceWeight0(inVerts, vertsBlend, vertexInfo->vertCount[0], boneMatrix, outVerts);
        vertIndex = vertexInfo->vertCount[0];
        vertsBlend += vertIndex;
    }
    if (vertexInfo->vertCount[1])
    {
        R_SkinXSurfaceWeight1(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[1], boneMatrix, &outVerts[vertIndex]);
        vertIndex += vertexInfo->vertCount[1];
        vertsBlend += 3 * vertexInfo->vertCount[1];
    }
    if (vertexInfo->vertCount[2])
    {
        R_SkinXSurfaceWeight2(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[2], boneMatrix, &outVerts[vertIndex]);
        vertIndex += vertexInfo->vertCount[2];
        vertsBlend += 5 * vertexInfo->vertCount[2];
    }
    if (vertexInfo->vertCount[3])
        R_SkinXSurfaceWeight3(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[3], boneMatrix, &outVerts[vertIndex]);
}


static void __cdecl MatrixTransformVertexAndBasis(
    const float *offset,
    float binormalSign,
    PackedUnitVec normal,
    PackedUnitVec tangent,
    const DObjSkelMat *mat,
    GfxPackedVertex *vert)
{
    float rotated[3]; // [esp+B8h] [ebp-18h]
    float unpacked[3]; // [esp+C4h] [ebp-Ch]

    vert->xyz[0] = *offset * mat->axis[0][0] + offset[1] * mat->axis[1][0] + offset[2] * mat->axis[2][0] + mat->origin[0];
    vert->xyz[1] = *offset * mat->axis[0][1] + offset[1] * mat->axis[1][1] + offset[2] * mat->axis[2][1] + mat->origin[1];
    vert->xyz[2] = *offset * mat->axis[0][2] + offset[1] * mat->axis[1][2] + offset[2] * mat->axis[2][2] + mat->origin[2];
    vert->binormalSign = binormalSign;

    Vec3UnpackUnitVec(normal, unpacked);

    rotated[0] = unpacked[0] * mat->axis[0][0] + unpacked[1] * mat->axis[1][0] + unpacked[2] * mat->axis[2][0];
    rotated[1] = unpacked[0] * mat->axis[0][1] + unpacked[1] * mat->axis[1][1] + unpacked[2] * mat->axis[2][1];
    rotated[2] = unpacked[0] * mat->axis[0][2] + unpacked[1] * mat->axis[1][2] + unpacked[2] * mat->axis[2][2];

    normal.array[0] = (int)(rotated[0] * 127.0f + 127.5f);
    normal.array[1] = (int)(rotated[1] * 127.0f + 127.5f);
    normal.array[2] = (int)(rotated[2] * 127.0f + 127.5f);
    normal.array[3] = 63;
    vert->normal = normal;

    Vec3UnpackUnitVec(tangent, unpacked);

    rotated[0] = unpacked[0] * mat->axis[0][0] + unpacked[1] * mat->axis[1][0] + unpacked[2] * mat->axis[2][0];
    rotated[1] = unpacked[0] * mat->axis[0][1] + unpacked[1] * mat->axis[1][1] + unpacked[2] * mat->axis[2][1];
    rotated[2] = unpacked[0] * mat->axis[0][2] + unpacked[1] * mat->axis[1][2] + unpacked[2] * mat->axis[2][2];

    tangent.array[0] = (int)(rotated[0] * 127.0f + 127.5f);
    tangent.array[1] = (int)(rotated[1] * 127.0f + 127.5f);
    tangent.array[2] = (int)(rotated[2] * 127.0f + 127.5f);
    tangent.array[3] = 63;

    vert->tangent = tangent;
}

void __cdecl R_SkinXSurfaceWeight0(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    int vertIndex; // [esp+1Ch] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        ++vertexBlend;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight1(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    const DObjSkelMat *matrix; // [esp+18h] [ebp-1Ch]
    float offset[3]; // [esp+20h] [ebp-14h] BYREF
    float boneWeight; // [esp+2Ch] [ebp-8h]
    int vertIndex; // [esp+30h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        boneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, offset);
        Vec3Scale(offset, boneWeight, offset);
        boneWeight = 1.0 - boneWeight;
        Vec3Mad(offset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 3;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight2(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    const DObjSkelMat *matrix; // [esp+1Ch] [ebp-2Ch]
    const DObjSkelMat *matrixa; // [esp+1Ch] [ebp-2Ch]
    float offset[3]; // [esp+24h] [ebp-24h] BYREF
    float boneWeight; // [esp+30h] [ebp-18h]
    float totalOffset[3]; // [esp+34h] [ebp-14h] BYREF
    int vertIndex; // [esp+40h] [ebp-8h]
    float totalBoneWeight; // [esp+44h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        totalBoneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, totalOffset);
        Vec3Scale(totalOffset, totalBoneWeight, totalOffset);
        matrixa = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[3]);
        boneWeight = (double)vertexBlend[4] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixa, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        boneWeight = 1.0 - totalBoneWeight;
        Vec3Mad(totalOffset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 5;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight3(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    const DObjSkelMat *matrix; // [esp+20h] [ebp-2Ch]
    const DObjSkelMat *matrixa; // [esp+20h] [ebp-2Ch]
    const DObjSkelMat *matrixb; // [esp+20h] [ebp-2Ch]
    float offset[3]; // [esp+28h] [ebp-24h] BYREF
    float boneWeight; // [esp+34h] [ebp-18h]
    float totalOffset[3]; // [esp+38h] [ebp-14h] BYREF
    int vertIndex; // [esp+44h] [ebp-8h]
    float totalBoneWeight; // [esp+48h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        totalBoneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, totalOffset);
        Vec3Scale(totalOffset, totalBoneWeight, totalOffset);
        matrixa = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[3]);
        boneWeight = (double)vertexBlend[4] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixa, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        matrixb = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[5]);
        boneWeight = (double)vertexBlend[6] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixb, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        boneWeight = 1.0 - totalBoneWeight;
        Vec3Mad(totalOffset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 7;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceRigid(
    const XSurface *surf,
    int totalVertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertices)
{
    uint32_t i; // [esp+44h] [ebp-1Ch]
    int vertCount; // [esp+48h] [ebp-18h]
    int vertIndex; // [esp+4Ch] [ebp-14h]
    GfxPackedVertex *vertex; // [esp+50h] [ebp-10h]
    const XRigidVertList *vertList; // [esp+54h] [ebp-Ch]
    GfxPackedVertex *v; // [esp+58h] [ebp-8h]
    const DObjSkelMat *bone; // [esp+5Ch] [ebp-4h]

    iassert(vertices);
    iassert((reinterpret_cast<uintptr_t>(vertices) & 15u) == 0u);
    iassert((reinterpret_cast<uintptr_t>(boneMatrix) & 15u) == 0u);

    PROF_SCOPED("SkinXSurfaceWeight");

    v = surf->verts0;
    vertex = vertices;
    for (i = 0; i < surf->vertListCount; ++i)
    {
        vertList = &surf->vertList[i];
        vertCount = vertList->vertCount;
        iassert((vertList->boneOffset % 64) == 0); // LWSS ADD
        bone = (const DObjSkelMat *)((char *)boneMatrix + vertList->boneOffset);
        for (vertIndex = 0; vertIndex < vertCount; ++vertIndex)
        {
            MatrixTransformVertexAndBasis(v->xyz, v->binormalSign, v->normal, v->tangent, bone, vertex);
            vertex->color.packed = v->color.packed;
            vertex->texCoord.packed = v->texCoord.packed;
            ++v;
            ++vertex;
        }
    }

    iassert(vertex - vertices == totalVertCount);
}

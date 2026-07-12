#include "r_dobj_skin.h"

#include <xanim/dobj.h>
#include <xanim/xmodel.h>
#include <xanim/dobj_utils.h>
#include <qcommon/sys_time.h>
#include <universal/profile.h>
#include "r_model.h"
#include "r_dvars.h"
#include "r_workercmds.h"
#include "r_buffers.h"
#include "r_model_pose.h"
#include "r_dpvs.h"
#include "r_model_surface_stream.h"
#include "r_reservation_atomic.h"
#include <database/db_validation.h>

#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace model_surface_stream = gfx::model_surface_stream;

static void __cdecl R_FlagXModelAsSkinned(GfxSceneEntity *sceneEnt, uint32_t surfaceCount)
{
    iassert(Sys_AtomicLoad(&sceneEnt->cull.state) == CULL_STATE_SKINNED_PENDING);
    Sys_AtomicStore(&sceneEnt->cull.state, surfaceCount + CULL_STATE_DONE);
}

#ifdef KISAK_MP
static GfxSkinCacheEntry *__cdecl CG_GetSkinCacheEntry(cpose_t *pose)
{
    return &pose->skinCacheEntry;
}
#endif

namespace
{
constexpr uint32_t kSceneSurfaceArenaBytes = 0x20000u;
constexpr uint32_t kSkinVertexArenaBytes = 0x480000u;
constexpr uint32_t kSkinVertexStride = sizeof(GfxPackedVertex);
constexpr uint32_t kModelSurfaceAlignment = alignof(GfxModelRigidSurface);

struct DObjSkinPlan
{
    uint32_t recordBytes = 0u;
    uint32_t surfaceCount = 0u;
    uint32_t skinnedVertexCount = 0u;
};

bool R_IsRigidDObjSurface(
    const XSurface &surface,
    const bool fastFileLoad)
{
    return !surface.deformed && fastFileLoad
        && surface.vertListCount == 1u;
}

bool R_GetShiftedSurfacePartBits(
    const XSurface &surface,
    const uint32_t boneIndex,
    uint32_t (&shifted)[4])
{
    if (boneIndex >= DOBJ_MAX_PARTS)
        return false;

    const uint32_t block = boneIndex >> 5;
    const uint32_t shift = boneIndex & 31u;
    const uint32_t source[7] = {
        0u,
        0u,
        0u,
        static_cast<uint32_t>(surface.partBits[0]),
        static_cast<uint32_t>(surface.partBits[1]),
        static_cast<uint32_t>(surface.partBits[2]),
        static_cast<uint32_t>(surface.partBits[3]),
    };
    if (!shift)
    {
        shifted[0] = source[3u - block];
        shifted[1] = source[4u - block];
        shifted[2] = source[5u - block];
        shifted[3] = source[6u - block];
        return true;
    }

    const uint32_t carryShift = 32u - shift;
    shifted[0] = source[3u - block] >> shift;
    shifted[1] = (source[4u - block] >> shift)
        | (source[3u - block] << carryShift);
    shifted[2] = (source[5u - block] >> shift)
        | (source[4u - block] << carryShift);
    shifted[3] = (source[6u - block] >> shift)
        | (source[5u - block] << carryShift);
    return true;
}

bool R_SurfaceIsHidden(
    const uint32_t (&surfaceBits)[4],
    const uint32_t (&hideBits)[4])
{
    return ((surfaceBits[0] & hideBits[0])
        | (surfaceBits[1] & hideBits[1])
        | (surfaceBits[2] & hideBits[2])
        | (surfaceBits[3] & hideBits[3])) != 0u;
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

bool R_PlanDObjSkinStream(
    const GfxSceneEntity &sceneEnt,
    const DObj_s &obj,
    const uint32_t (&hideBits)[4],
    const bool fastFileLoad,
    DObjSkinPlan *const planOut)
{
    if (!planOut || !obj.models || !obj.numModels
        || obj.numModels > DOBJ_MAX_SUBMODELS)
    {
        return false;
    }

    DObjSkinPlan plan;
    uint32_t boneIndex = 0u;
    for (uint32_t modelIndex = 0u; modelIndex < obj.numModels; ++modelIndex)
    {
        const XModel *const model = DObjGetModel(&obj, modelIndex);
        if (!model || !model->numBones
            || model->numRootBones > model->numBones
            || model->numLods <= 0 || model->numLods > MAX_LODS
            || !model->baseMat || !model->boneInfo
            || !model->materialHandles
            || (model->numBones != model->numRootBones
                && (!model->parentList || !model->quats || !model->trans))
            || !model_surface_stream::IsBoneSpanValid(
                boneIndex,
                model->numBones))
        {
            return false;
        }

        const int lod = sceneEnt.cull.lods[modelIndex];
        if (lod >= 0)
        {
            if (lod >= MAX_LODS || lod >= model->numLods
                || !model->baseMat || !model->surfs)
            {
                return false;
            }
            const XModelLodInfo &lodInfo = model->lodInfo[lod];
            if (lodInfo.surfIndex > model->numsurfs
                || lodInfo.numsurfs > model->numsurfs - lodInfo.surfIndex
                || !lodInfo.numsurfs)
            {
                return false;
            }

            uint32_t nextSurfaceCount = 0u;
            if (!model_surface_stream::TryAdd(
                    plan.surfaceCount,
                    lodInfo.numsurfs,
                    &nextSurfaceCount)
                || nextSurfaceCount
                    > (std::numeric_limits<uint16_t>::max)())
            {
                return false;
            }
            plan.surfaceCount = nextSurfaceCount;

            const XSurface *const surfaces =
                &model->surfs[lodInfo.surfIndex];
            for (uint32_t surfaceIndex = 0u;
                 surfaceIndex < lodInfo.numsurfs;
                 ++surfaceIndex)
            {
                const XSurface &surface = surfaces[surfaceIndex];
                if (!R_SurfaceSkinningMetadataValid(
                        surface,
                        model->numBones))
                    return false;
                for (uint32_t word = 0u; word < 4u; ++word)
                {
                    if ((static_cast<uint32_t>(surface.partBits[word])
                            & ~static_cast<uint32_t>(
                                lodInfo.partBits[word])) != 0u)
                    {
                        return false;
                    }
                }

                uint32_t shiftedBits[4] = {};
                if (!R_GetShiftedSurfacePartBits(
                        surface,
                        boneIndex,
                        shiftedBits))
                {
                    return false;
                }

                uint32_t recordSize = sizeof(GfxModelHiddenSurface);
                if (!R_SurfaceIsHidden(shiftedBits, hideBits))
                {
                    if (R_IsRigidDObjSurface(surface, fastFileLoad))
                    {
                        if (!surface.vertList
                            || (surface.vertList[0].boneOffset & 63u) != 0u
                            || (surface.vertList[0].boneOffset >> 6)
                                >= model->numBones)
                        {
                            return false;
                        }
                        recordSize = sizeof(GfxModelRigidSurface);
                    }
                    else
                    {
                        uint32_t nextVertexCount = 0u;
                        if (!model_surface_stream::TryAdd(
                                plan.skinnedVertexCount,
                                surface.vertCount,
                                &nextVertexCount)
                            || nextVertexCount
                                > kSkinVertexArenaBytes / kSkinVertexStride)
                        {
                            return false;
                        }
                        plan.skinnedVertexCount = nextVertexCount;
                        recordSize = sizeof(GfxModelSkinnedSurface);
                    }
                }

                uint32_t recordOffset = 0u;
                uint32_t nextRecordBytes = 0u;
                if (!model_surface_stream::TryPlanRecord(
                        plan.recordBytes,
                        recordSize,
                        kModelSurfaceAlignment,
                        kSceneSurfaceArenaBytes,
                        &recordOffset,
                        &nextRecordBytes)
                    || recordOffset != plan.recordBytes)
                {
                    return false;
                }
                plan.recordBytes = nextRecordBytes;
            }
        }

        boneIndex += model->numBones;
    }

    if (boneIndex != obj.numBones || !plan.surfaceCount || !plan.recordBytes
        || (plan.recordBytes & (sizeof(uint32_t) - 1u)) != 0u)
    {
        return false;
    }
    *planOut = plan;
    return true;
}

void R_BuildRigidDObjSurface(
    void *const storage,
    XSurface *const surface,
    const GfxModelSurfaceInfo &surfaceInfo,
    const DObjAnimMat *const boneMatrix)
{
    GfxModelRigidSurface *const rigid =
        ::new (storage) GfxModelRigidSurface{};
    rigid->surf.skinnedCachedOffset = model_surface_stream::kRigidTag;
    rigid->surf.xsurf = surface;
    rigid->surf.info = surfaceInfo;
    rigid->placement.scale = 1.0f;

    const uint32_t localBoneIndex = surface->vertList[0].boneOffset >> 6;
    const DObjAnimMat *const baseMat =
        &surfaceInfo.baseMat[localBoneIndex];
    const DObjAnimMat *const animatedMat =
        &boneMatrix[surfaceInfo.boneIndex + localBoneIndex];
    DObjSkelMat inverseBaseMat;
    DObjSkelMat skelMat;
    ConvertQuatToInverseSkelMat(baseMat, &inverseBaseMat);
    ConvertQuatToSkelMat(animatedMat, &skelMat);

    float origin[4];
    QuatMultiplyInverse(baseMat->quat, animatedMat->quat, origin);
    Vec4Normalize(origin);
    rigid->placement.base.quat[0] = origin[0];
    rigid->placement.base.quat[1] = origin[1];
    rigid->placement.base.quat[2] = origin[2];
    rigid->placement.base.quat[3] = origin[3];

    float transformedOrigin[3];
    R_TransformSkelMat(inverseBaseMat.origin, &skelMat, transformedOrigin);
    Vec3Add(
        transformedOrigin,
        scene.def.viewOffset,
        rigid->placement.base.origin);
}
} // namespace

int R_SkinSceneDObjModels(
    GfxSceneEntity *sceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix)
{
    iassert(sceneEnt);
    iassert(obj);
    iassert(boneMatrix);
    if (!sceneEnt || !obj || !boneMatrix || !frontEndDataOut
        || sceneEnt->cull.skinnedSurfs.firstSurf)
    {
        return 0;
    }

    const bool fastFileLoad = IsFastFileLoad();
    const bool useSkinCache = gfxBuf.skinCache;
    const bool useFastSkin = gfxBuf.fastSkin;
    if (fastFileLoad && DObjBad(obj))
        return 0;
    uint32_t hideBits[4] = {};
    DObjGetHidePartBits(obj, hideBits);

    PROF_SCOPED("R_SkinXModel");

    DObjSkinPlan plan;
    if (!R_PlanDObjSkinStream(
            *sceneEnt,
            *obj,
            hideBits,
            fastFileLoad,
            &plan))
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_SURFS_SIZE);
        return 0;
    }

    uint32_t streamOffset = 0u;
    if (!model_surface_stream::TryReserveAligned(
            &frontEndDataOut->surfPos,
            plan.recordBytes,
            sizeof(frontEndDataOut->surfsBuffer),
            kModelSurfaceAlignment,
            &streamOffset))
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_SURFS_SIZE);
        return 0;
    }

    uint32_t vertexBytes = 0u;
    uint32_t vertexOffset = 0u;
    uint32_t oldVertexOffset = 0x80000001u;
#ifdef KISAK_MP
    GfxSkinCacheEntry *pendingCacheEntry = nullptr;
    uint32_t pendingCacheFrame = 0u;
    uint16_t pendingCacheAge = 0u;
    bool pendingCacheCountFits = false;
#endif
    if (plan.skinnedVertexCount)
    {
        if (!model_surface_stream::TryMultiply(
                plan.skinnedVertexCount,
                kSkinVertexStride,
                &vertexBytes))
        {
            return 0;
        }

        if (useSkinCache && !frontEndDataOut->skinnedCacheVb)
            return 0;
        volatile uint32_t *const vertexCursor = useSkinCache
            ? &frontEndDataOut->skinnedCacheVb->used
            : &frontEndDataOut->tempSkinPos;
        if ((useSkinCache
                && (!gfxBuf.skinnedCacheLockAddr
                    || (reinterpret_cast<uintptr_t>(
                        gfxBuf.skinnedCacheLockAddr) & 15u) != 0u))
            || (!useSkinCache
                && (!frontEndDataOut->tempSkinBuf
                    || (reinterpret_cast<uintptr_t>(
                        frontEndDataOut->tempSkinBuf) & 15u) != 0u))
            || !gfx::reservation_atomic::TryReserve(
                vertexCursor,
                vertexBytes,
                kSkinVertexArenaBytes,
                &vertexOffset))
        {
            R_WarnOncePerFrame(
                useSkinCache
                    ? R_WARN_MAX_SKINNED_CACHE_VERTICES
                    : R_WARN_TEMP_SKIN_BUF_SIZE);
            return 0;
        }

        if (!useSkinCache)
        {
            Z_VirtualCommit(
                &frontEndDataOut->tempSkinBuf[vertexOffset],
                static_cast<int>(vertexBytes));
        }
#ifdef KISAK_MP
        else if (useFastSkin && sceneEnt->info.pose)
        {
            pendingCacheEntry = CG_GetSkinCacheEntry(sceneEnt->info.pose);
            pendingCacheFrame = gfxBuf.skinnedCacheNormalsFrameCount;
            pendingCacheCountFits = plan.skinnedVertexCount
                <= (std::numeric_limits<uint16_t>::max)();
            if (pendingCacheCountFits)
            {
                if (pendingCacheEntry->ageCount < 3u
                    && gfxBuf.skinnedCacheNormalsFrameCount
                        - pendingCacheEntry->frameCount == 1u
                    && pendingCacheEntry->numSkinnedVerts
                        == plan.skinnedVertexCount
                    && pendingCacheEntry->skinnedCachedOffset >= 0
                    && static_cast<uint32_t>(
                        pendingCacheEntry->skinnedCachedOffset)
                        <= kSkinVertexArenaBytes - vertexBytes)
                {
                    oldVertexOffset = static_cast<uint32_t>(
                        pendingCacheEntry->skinnedCachedOffset);
                    pendingCacheAge = static_cast<uint16_t>(
                        pendingCacheEntry->ageCount + 1u);
                }
            }
        }
#endif
    }

    SkinXModelCmd skinCmd{};
    uint8_t *const stream = &frontEndDataOut->surfsBuffer[streamOffset];
    uint32_t recordBytes = 0u;
    uint32_t builtSurfaces = 0u;
    uint32_t builtVertices = 0u;
    uint32_t boneIndex = 0u;

    for (uint32_t modelIndex = 0u; modelIndex < obj->numModels; ++modelIndex)
    {
        XModel *const model = DObjGetModel(obj, modelIndex);
        const uint32_t boneCount = model->numBones;
        const int lod = sceneEnt->cull.lods[modelIndex];
        if (lod >= 0)
        {
            const XModelLodInfo &lodInfo = model->lodInfo[lod];
            XSurface *const surfaces = &model->surfs[lodInfo.surfIndex];
            GfxModelSurfaceInfo surfaceInfo{};
            surfaceInfo.baseMat = model->baseMat;
            surfaceInfo.boneIndex = static_cast<uint8_t>(boneIndex);
            surfaceInfo.boneCount = static_cast<uint8_t>(boneCount);
            surfaceInfo.gfxEntIndex = sceneEnt->gfxEntIndex;

            for (uint32_t surfaceIndex = 0u;
                 surfaceIndex < lodInfo.numsurfs;
                 ++surfaceIndex)
            {
                XSurface *const surface = &surfaces[surfaceIndex];
                uint32_t shiftedBits[4] = {};
                if (!R_GetShiftedSurfacePartBits(
                        *surface,
                        boneIndex,
                        shiftedBits))
                {
                    return 0;
                }

                const bool hidden = R_SurfaceIsHidden(shiftedBits, hideBits);
                const bool rigid = !hidden
                    && R_IsRigidDObjSurface(*surface, fastFileLoad);
                const uint32_t recordSize = hidden
                    ? sizeof(GfxModelHiddenSurface)
                    : rigid
                        ? sizeof(GfxModelRigidSurface)
                        : sizeof(GfxModelSkinnedSurface);
                uint32_t recordOffset = 0u;
                uint32_t nextRecordBytes = 0u;
                if (!model_surface_stream::TryPlanRecord(
                        recordBytes,
                        recordSize,
                        kModelSurfaceAlignment,
                        plan.recordBytes,
                        &recordOffset,
                        &nextRecordBytes)
                    || recordOffset != recordBytes)
                {
                    return 0;
                }

                void *const record = stream + recordOffset;
                if (hidden)
                {
                    ::new (record) GfxModelHiddenSurface{
                        model_surface_stream::kHiddenTag};
                }
                else
                {
                    for (uint32_t word = 0u; word < 4u; ++word)
                        skinCmd.surfacePartBits[word] |= shiftedBits[word];

                    if (rigid)
                    {
                        R_BuildRigidDObjSurface(
                            record,
                            surface,
                            surfaceInfo,
                            boneMatrix);
                    }
                    else
                    {
                        uint32_t nextBuiltVertices = 0u;
                        if (builtVertices > plan.skinnedVertexCount
                            || surface->vertCount
                                > plan.skinnedVertexCount - builtVertices
                            || !model_surface_stream::TryAdd(
                                builtVertices,
                                surface->vertCount,
                                &nextBuiltVertices))
                        {
                            return 0;
                        }

                        GfxModelSkinnedSurface *const skinned =
                            ::new (record) GfxModelSkinnedSurface{};
                        skinned->xsurf = surface;
                        skinned->info = surfaceInfo;

                        uint32_t relativeVertexOffset = 0u;
                        if (!model_surface_stream::TryMultiply(
                                builtVertices,
                                kSkinVertexStride,
                                &relativeVertexOffset))
                        {
                            return 0;
                        }
                        uint32_t absoluteVertexOffset = 0u;
                        uint32_t surfaceVertexBytes = 0u;
                        if (!model_surface_stream::TryAdd(
                                vertexOffset,
                                relativeVertexOffset,
                                &absoluteVertexOffset)
                            || !model_surface_stream::TryMultiply(
                                surface->vertCount,
                                kSkinVertexStride,
                                &surfaceVertexBytes)
                            || absoluteVertexOffset > kSkinVertexArenaBytes
                            || surfaceVertexBytes > kSkinVertexArenaBytes
                                - absoluteVertexOffset)
                        {
                            return 0;
                        }
                        if (useSkinCache)
                        {
                            skinned->skinnedCachedOffset =
                                static_cast<int32_t>(
                                    absoluteVertexOffset);
                            if (!model_surface_stream::TryAdd(
                                    oldVertexOffset,
                                    relativeVertexOffset,
                                    &skinned->oldSkinnedCachedOffset))
                            {
                                return 0;
                            }
                        }
                        else
                        {
                            skinned->skinnedCachedOffset =
                                model_surface_stream::kDirectSkinnedTag;
                            skinned->skinnedVert =
                                reinterpret_cast<GfxPackedVertex *>(
                                    &frontEndDataOut->tempSkinBuf[
                                        absoluteVertexOffset]);
                        }
                        builtVertices = nextBuiltVertices;
                    }
                }

                recordBytes = nextRecordBytes;
                ++builtSurfaces;
            }
        }
        boneIndex += boneCount;
    }

    if (recordBytes != plan.recordBytes
        || builtSurfaces != plan.surfaceCount
        || builtVertices != plan.skinnedVertexCount)
    {
        return 0;
    }

    int requiredPartBits[4] = {};
    for (uint32_t word = 0u; word < 4u; ++word)
    {
        requiredPartBits[word] =
            std::bit_cast<int32_t>(skinCmd.surfacePartBits[word]);
    }
    iassert(DObjSkelAreBonesUpToDate(obj, requiredPartBits));
    if (!DObjSkelAreBonesUpToDate(obj, requiredPartBits))
        return 0;

    sceneEnt->cull.skinnedSurfs.wordCount = static_cast<uint16_t>(
        plan.recordBytes / sizeof(uint32_t));
    sceneEnt->cull.skinnedSurfs.surfCount = static_cast<uint16_t>(
        plan.surfaceCount);
    sceneEnt->cull.skinnedSurfs.firstSurf = stream;

    if (r_xdebug->current.integer)
        R_XModelDebug(obj, requiredPartBits);

    if (plan.skinnedVertexCount)
    {
        skinCmd.modelSurfs = stream;
        skinCmd.mat = boneMatrix;
        skinCmd.surfCount = static_cast<uint16_t>(plan.surfaceCount);
        skinCmd.modelSurfWordCount =
            sceneEnt->cull.skinnedSurfs.wordCount;
        R_AddWorkerCmd<WRKCMD_SKIN_XMODEL>(skinCmd);
#ifdef KISAK_MP
        if (pendingCacheEntry)
        {
            pendingCacheEntry->frameCount = pendingCacheFrame;
            pendingCacheEntry->ageCount = pendingCacheAge;
            if (pendingCacheCountFits)
            {
                pendingCacheEntry->numSkinnedVerts = static_cast<uint16_t>(
                    plan.skinnedVertexCount);
                pendingCacheEntry->skinnedCachedOffset =
                    static_cast<int32_t>(vertexOffset);
            }
            else
            {
                pendingCacheEntry->numSkinnedVerts = 0u;
                pendingCacheEntry->skinnedCachedOffset = -1;
            }
        }
#endif
    }

    return static_cast<int>(plan.surfaceCount);
}

void __cdecl R_SkinGfxEntityCmd(GfxSceneEntity **data)
{
    DObjAnimMat *boneMatrix; // [esp+0h] [ebp-14h]
    const DObj_s *obj; // [esp+4h] [ebp-10h] BYREF
    GfxSceneEntity *localSceneEnt; // [esp+8h] [ebp-Ch] BYREF
    GfxSceneEntity *sceneEnt; // [esp+Ch] [ebp-8h]
    GfxSceneEntity **pSceneEnt; // [esp+10h] [ebp-4h]

    iassert( data );
    pSceneEnt = data;
    sceneEnt = *data;
    boneMatrix = R_UpdateSceneEntBounds(sceneEnt, &localSceneEnt, &obj, 0);
    if (boneMatrix)
    {
        iassert( localSceneEnt );
        R_SkinSceneDObj(sceneEnt, localSceneEnt, obj, boneMatrix, 0);
    }
}

void __cdecl R_SkinSceneDObj(
    GfxSceneEntity *sceneEnt,
    GfxSceneEntity *localSceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix,
    int waitForCullState)
{
    volatile uint32_t state; // [esp+0h] [ebp-8h]
    int surfaceCount; // [esp+4h] [ebp-4h]

    iassert(localSceneEnt);
    iassert(boneMatrix);
    iassert(obj);

    if (Sys_AtomicLoad(&localSceneEnt->cull.state) < CULL_STATE_DONE)
    {
        if (Sys_AtomicCompareExchange(
                &sceneEnt->cull.state,
                CULL_STATE_SKINNED_PENDING,
                CULL_STATE_BOUNDED) == CULL_STATE_BOUNDED)
        {
            surfaceCount = R_SkinSceneDObjModels(localSceneEnt, obj, boneMatrix);
            R_FlagXModelAsSkinned(localSceneEnt, surfaceCount);
        }
        else if (waitForCullState)
        {
            do
            {
                state = Sys_AtomicLoad(&sceneEnt->cull.state);
                iassert(state >= CULL_STATE_SKINNED_PENDING);
                if (state == CULL_STATE_SKINNED_PENDING)
                    Sys_Sleep(0);
            } while (state == CULL_STATE_SKINNED_PENDING);

            iassert(state >= CULL_STATE_DONE);
        }
    }
}

int __cdecl DObjBad(const DObj_s *obj)
{
    if (!obj || !obj->models || !obj->numModels
        || obj->numModels > DOBJ_MAX_SUBMODELS)
    {
        return 1;
    }

    for (int j = obj->numModels - 1; j >= 0; --j)
    {
        if (!obj->models[j] || XModelBad(obj->models[j]))
            return 1;
    }
    return 0;
}

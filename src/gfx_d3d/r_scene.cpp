#include "r_scene.h"
#include <qcommon/mem_track.h>
#include "r_init.h"
#include "r_debug.h"
#include "r_dvars.h"
#include <qcommon/threads.h>
#include <xanim/dobj_utils.h>
#include "r_draw_method.h"
#include "r_dobj_skin.h"
#include "r_xsurface.h"
#include "r_dpvs.h"
#include "rb_logfile.h"
#include "r_model_lighting.h"
#include "rb_fog.h"
#include "r_workercmds.h"
#include "r_bsp.h"
#include "r_primarylights.h"
#include "r_sunshadow.h"
#include "r_add_staticmodel.h"
#include <DynEntity/DynEntity_client.h>
#include "r_spotshadow.h"
#include "r_shadowcookie.h"
#include "r_pretess.h"
#include "r_light.h"
#include <EffectsCore/fx_system.h>
#include "r_drawsurf.h"
#include "rb_postfx.h"
#include "r_meshdata.h"
#include <qcommon/com_bsp.h>
#include "r_cinematic.h"
#include <win32/win_net.h>
#include <universal/profile.h>
#include <universal/sys_atomic.h>
#include "rb_state.h"

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#elif KISAK_SP
#include <cgame/cg_local.h>
#include <cgame/cg_ents.h>
#endif
#include "r_state.h"
#include "r_draw_sunshadow.h"
#include "r_bmodel_surface_stream.h"
#include "r_model_surface_stream.h"
#include "r_reservation_atomic.h"

namespace model_surface_stream = gfx::model_surface_stream;
namespace bmodel_surface_stream = gfx::bmodel_surface_stream;

namespace
{
constexpr uint32_t kModelSurfaceAlignment = alignof(GfxModelRigidSurface);
constexpr uint32_t kHiddenModelSurfaceBytes = sizeof(GfxModelHiddenSurface);
constexpr uint32_t kSkinnedModelSurfaceBytes = sizeof(GfxModelSkinnedSurface);
constexpr uint32_t kRigidModelSurfaceBytes = sizeof(GfxModelRigidSurface);

// XModelDrawInfo::surfId and GfxDrawSurf::objectId are dword offsets from the
// backend-data base.  The static-model producer currently relies on the arena
// being the first field when it publishes startSurfPos / sizeof(uint32_t).
static_assert(offsetof(GfxBackEndData, surfsBuffer) == 0u);
static_assert(
    kHiddenModelSurfaceBytes
    == model_surface_stream::HiddenRecordSize(kModelSurfaceAlignment));
static_assert((kSkinnedModelSurfaceBytes % kModelSurfaceAlignment) == 0u);
static_assert((kRigidModelSurfaceBytes % kModelSurfaceAlignment) == 0u);

bool R_ValidateSceneModelSurfaceStream(
    const GfxSceneEntity &sceneEnt,
    const uint32_t cullState,
    uint32_t *const streamBytesOut,
    uint32_t *const streamSurfacesOut)
{
    if (!streamBytesOut || !streamSurfacesOut || !frontEndDataOut
        || cullState <= CULL_STATE_DONE)
    {
        return false;
    }

    const uint32_t streamSurfaces = sceneEnt.cull.skinnedSurfs.surfCount;
    uint32_t streamBytes = 0u;
    if (!streamSurfaces
        || cullState - CULL_STATE_DONE != streamSurfaces
        || !model_surface_stream::TryMultiply(
            sceneEnt.cull.skinnedSurfs.wordCount,
            static_cast<uint32_t>(sizeof(uint32_t)),
            &streamBytes)
        || !streamBytes || !sceneEnt.cull.skinnedSurfs.firstSurf)
    {
        return false;
    }

    const uintptr_t streamAddress = reinterpret_cast<uintptr_t>(
        sceneEnt.cull.skinnedSurfs.firstSurf);
    const uintptr_t arenaBegin = reinterpret_cast<uintptr_t>(
        frontEndDataOut->surfsBuffer);
    const uintptr_t arenaEnd = arenaBegin
        + sizeof(frontEndDataOut->surfsBuffer);
    const uint32_t publishedBytes = Sys_AtomicLoad(
        &frontEndDataOut->surfPos);
    if (streamAddress < arenaBegin || streamAddress > arenaEnd
        || streamBytes > arenaEnd - streamAddress
        || publishedBytes > sizeof(frontEndDataOut->surfsBuffer)
        || streamAddress - arenaBegin > publishedBytes
        || streamBytes
            > publishedBytes - (streamAddress - arenaBegin))
    {
        return false;
    }

    model_surface_stream::Cursor cursor(
        sceneEnt.cull.skinnedSurfs.firstSurf,
        streamBytes,
        streamSurfaces);
    for (uint32_t index = 0u; index < streamSurfaces; ++index)
    {
        model_surface_stream::View record;
        if (!cursor.Next(
                kHiddenModelSurfaceBytes,
                kSkinnedModelSurfaceBytes,
                kRigidModelSurfaceBytes,
                &record))
        {
            return false;
        }
    }
    if (!cursor.Finished())
        return false;

    *streamBytesOut = streamBytes;
    *streamSurfacesOut = streamSurfaces;
    return true;
}

bool R_ValidateSceneDObjModels(
    const GfxSceneEntity &sceneEnt,
    const DObj_s &obj,
    const uint32_t streamSurfaces)
{
    if (!obj.models || !obj.numModels
        || obj.numModels > DOBJ_MAX_SUBMODELS)
    {
        return false;
    }

    uint32_t expectedSurfaces = 0u;
    for (uint32_t modelIndex = 0u;
         modelIndex < obj.numModels;
         ++modelIndex)
    {
        const int lod = sceneEnt.cull.lods[modelIndex];
        if (lod < 0)
            continue;

        XModel *const model = DObjGetModel(&obj, modelIndex);
        XSurface *surfaces = nullptr;
        const int surfaceCount = XModelGetSurfaces(model, &surfaces, lod);
        Material **const materials = XModelGetSkins(model, lod);
        if (!model || !surfaces || !materials || surfaceCount <= 0
            || expectedSurfaces > streamSurfaces
            || static_cast<uint32_t>(surfaceCount)
                > streamSurfaces - expectedSurfaces)
        {
            return false;
        }
        for (int surfaceIndex = 0;
             surfaceIndex < surfaceCount;
             ++surfaceIndex)
        {
            if (!materials[surfaceIndex])
                return false;
        }
        expectedSurfaces += static_cast<uint32_t>(surfaceCount);
    }
    return expectedSurfaces == streamSurfaces;
}

struct StaticXModelSurfaceView
{
    GfxModelRigidSurface *surface = nullptr;
    uint32_t surfId = 0u;
    int32_t tag = 0;
};

class StaticXModelSurfaceCursor
{
public:
    StaticXModelSurfaceCursor(
        GfxBackEndData *const data,
        const uint32_t firstSurfId) noexcept
        : data_(data), surfId_(firstSurfId)
    {
        if (data_)
            publishedBytes_ = Sys_AtomicLoad(&data_->surfPos);
    }

    [[nodiscard]] bool Next(StaticXModelSurfaceView *const view) noexcept
    {
        if (!view || !data_
            || publishedBytes_ > sizeof(data_->surfsBuffer))
        {
            return false;
        }

        const void *tagAddress = nullptr;
        int32_t tag = 0;
        if (!model_surface_stream::TryResolveWordOffsetRange(
                data_,
                data_->surfsBuffer,
                sizeof(data_->surfsBuffer),
                publishedBytes_,
                surfId_,
                sizeof(int32_t),
                kModelSurfaceAlignment,
                model_surface_stream::kHiddenTag,
                model_surface_stream::kDirectSkinnedTag,
                &tagAddress,
                &tag))
        {
            return false;
        }

        uint32_t recordSize = kHiddenModelSurfaceBytes;
        GfxModelRigidSurface *surface = nullptr;
        if (tag == model_surface_stream::kHiddenTag)
        {
            const GfxModelHiddenSurface *hidden = nullptr;
            if (!model_surface_stream::TryResolveTypedWordOffset(
                    data_,
                    data_->surfsBuffer,
                    sizeof(data_->surfsBuffer),
                    publishedBytes_,
                    surfId_,
                    model_surface_stream::kHiddenTag,
                    model_surface_stream::kHiddenTag,
                    &hidden))
            {
                return false;
            }
        }
        else
        {
            const GfxModelRigidSurface *rigid = nullptr;
            if (!model_surface_stream::TryResolveTypedWordOffset(
                    data_,
                    data_->surfsBuffer,
                    sizeof(data_->surfsBuffer),
                    publishedBytes_,
                    surfId_,
                    model_surface_stream::kRigidTag,
                    model_surface_stream::kDirectSkinnedTag,
                    &rigid))
            {
                return false;
            }
            surface = const_cast<GfxModelRigidSurface *>(rigid);
            recordSize = kRigidModelSurfaceBytes;
        }

        uint32_t nextSurfId = 0u;
        if (!model_surface_stream::TryAdd(
                surfId_,
                recordSize / sizeof(uint32_t),
                &nextSurfId))
        {
            return false;
        }

        StaticXModelSurfaceView next;
        next.surface = surface;
        next.surfId = surfId_;
        next.tag = tag;
        surfId_ = nextSurfId;
        *view = next;
        return true;
    }

private:
    GfxBackEndData *data_ = nullptr;
    uint32_t publishedBytes_ = 0u;
    uint32_t surfId_ = 0u;
};

bool R_TryResolveBModelSurfaceSequence(
    const BModelDrawInfo *const bmodelInfo,
    const uint32_t surfaceCount,
    const BModelSurface **const surfaces)
{
    if (!bmodelInfo || !frontEndDataOut || !rgp.world
        || !rgp.world->dpvs.surfaces || rgp.world->surfaceCount <= 0)
    {
        return false;
    }

    return bmodel_surface_stream::TryResolveSequence<
        BModelSurface,
        GfxScaledPlacement,
        GfxSurface>(
        frontEndDataOut,
        frontEndDataOut->surfsBuffer,
        sizeof(frontEndDataOut->surfsBuffer),
        Sys_AtomicLoad(&frontEndDataOut->surfPos),
        bmodelInfo->surfId,
        surfaceCount,
        rgp.world->dpvs.surfaces,
        static_cast<uint32_t>(rgp.world->surfaceCount),
        surfaces);
}
} // namespace

//struct GfxScene scene      859c8280     gfx_d3d : r_scene.obj
GfxViewParms lockPvsViewParms;
GfxScene scene;

void __cdecl TRACK_r_scene()
{
    track_static_alloc_internal(&scene, sizeof(scene), "scene", 18);
}

uint32_t __cdecl R_AllocSceneDObj()
{
    uint32_t sceneEntIndex = 512u; // [esp+0h] [ebp-4h]

    iassert( rg.registered );
    iassert( rg.inFrame );
    if (!gfx::reservation_atomic::TryReserveIndex(
            &scene.sceneDObjCount,
            static_cast<uint32_t>(ARRAY_COUNT(scene.sceneDObj)),
            &sceneEntIndex))
    {
        R_WarnOncePerFrame(R_WARN_KNOWN_MODELS, 512);
    }
    return sceneEntIndex;
}

uint32_t __cdecl R_AllocSceneModel()
{
    uint32_t sceneEntIndex = 1024u; // [esp+0h] [ebp-4h]

    iassert( rg.registered );
    iassert( rg.inFrame );
    if (!gfx::reservation_atomic::TryReserveIndex(
            &scene.sceneModelCount,
            static_cast<uint32_t>(ARRAY_COUNT(scene.sceneModel)),
            &sceneEntIndex))
    {
        R_WarnOncePerFrame(R_WARN_KNOWN_MODELS, 1024);
    }
    return sceneEntIndex;
}

uint32_t __cdecl R_AllocSceneBrush()
{
    uint32_t sceneEntIndex = 512u; // [esp+0h] [ebp-4h]

    iassert( rg.registered );
    iassert( rg.inFrame );
    if (!gfx::reservation_atomic::TryReserveIndex(
            &scene.sceneBrushCount,
            static_cast<uint32_t>(ARRAY_COUNT(scene.sceneBrush)),
            &sceneEntIndex))
    {
        R_WarnOncePerFrame(R_WARN_KNOWN_MODELS, 512);
    }
    return sceneEntIndex;
}

GfxBrushModel *__cdecl R_GetBrushModel(uint32_t modelIndex)
{
    iassert( rgp.world );
    if (modelIndex >= rgp.world->modelCount)
        MyAssertHandler(
            ".\\r_scene.cpp",
            181,
            0,
            "modelIndex doesn't index rgp.world->modelCount\n\t%i not in [0, %i)",
            modelIndex,
            rgp.world->modelCount);
    return &rgp.world->models[modelIndex];
}

void __cdecl R_AddBrushModelToSceneFromAngles(
    const GfxBrushModel *bmodel,
    const float *origin,
    const float *angles,
    uint16_t entnum)
{
    uint32_t sceneEntIndex; // [esp+4h] [ebp-8h]
    GfxSceneBrush *sceneBrush; // [esp+8h] [ebp-4h]

    iassert( bmodel );
    if (r_drawEntities->current.enabled && r_drawBModels->current.enabled && bmodel->surfaceCount)
    {
        sceneEntIndex = R_AllocSceneBrush();
        if (sceneEntIndex < 0x200)
        {
            sceneBrush = &scene.sceneBrush[sceneEntIndex];
            sceneBrush->bmodel = bmodel;
            sceneBrush->entnum = entnum;
            sceneBrush->placement.origin[0] = *origin;
            sceneBrush->placement.origin[1] = origin[1];
            sceneBrush->placement.origin[2] = origin[2];
            AnglesToQuat(angles, sceneBrush->placement.quat);
        }
    }
}

void __cdecl R_AddDObjToScene(
    const DObj_s *obj,
    cpose_t *pose,
    uint32_t entnum,
    uint32_t renderFxFlags,
    float *lightingOrigin,
    float materialTime)
{
    float s; // [esp+0h] [ebp-38h]
    GfxSceneModel *sceneModel; // [esp+10h] [ebp-28h]
    XModel *model; // [esp+14h] [ebp-24h]
    float radius; // [esp+18h] [ebp-20h]
    float radiusa; // [esp+18h] [ebp-20h]
    GfxEntity *gfxEnt; // [esp+1Ch] [ebp-1Ch]
    float angles[3]; // [esp+20h] [ebp-18h] BYREF
    GfxSceneEntity *sceneEnt; // [esp+2Ch] [ebp-Ch]
    uint32_t sceneEntIndex; // [esp+30h] [ebp-8h]
    uint32_t gfxEntIndex = UINT32_MAX; // [esp+34h] [ebp-4h]

    iassert(Sys_IsMainThread());
    iassert(obj);
    iassert(pose);
    bcassert(entnum, gfxCfg.entCount);

    if (r_drawEntities->current.enabled)
    {
        iassert(scene.dpvs.sceneDObjIndex[entnum] == (65535));
        if (materialTime == 0.0f && !renderFxFlags)
        {
            gfxEntIndex = 0;
        }
        else
        {
            if (!gfx::reservation_atomic::TryReserveIndex(
                    &frontEndDataOut->gfxEntCount,
                    static_cast<uint32_t>(ARRAY_COUNT(frontEndDataOut->gfxEnts)),
                    &gfxEntIndex))
            {
                R_WarnOncePerFrame(R_WARN_KNOWN_SPECIAL_MODELS, 128);
                return;
            }
            gfxEnt = &frontEndDataOut->gfxEnts[gfxEntIndex];
            frontEndDataOut->gfxEnts[gfxEntIndex].materialTime = materialTime;
            gfxEnt->renderFxFlags = renderFxFlags;
        }
        if ((renderFxFlags & 4) != 0 || DObjGetTree(obj) || DObjGetNumModels(obj) != 1)
        {
            sceneEntIndex = R_AllocSceneDObj();
            if (sceneEntIndex < 0x200)
            {
                sceneEnt = &scene.sceneDObj[sceneEntIndex];
                sceneEnt->obj = obj;
                sceneEnt->entnum = entnum;
                scene.dpvs.sceneDObjIndex[entnum] = sceneEntIndex;
                sceneEnt->info.pose = (cpose_t*)pose;
                iassert(
                    R_LoadSceneEntityCullState(sceneEnt)
                    == CULL_STATE_OUT);
                radiusa = DObjGetRadius(obj);
                CG_GetPoseOrigin(pose, sceneEnt->placement.base.origin);
                CG_GetPoseAngles(pose, angles);
                AnglesToQuat(angles, sceneEnt->placement.base.quat);
                sceneEnt->placement.scale = 1.0f;
                s = -radiusa;
                Vec3AddScalar(sceneEnt->placement.base.origin, s, sceneEnt->cull.mins);
                Vec3AddScalar(sceneEnt->placement.base.origin, radiusa, sceneEnt->cull.maxs);
                sceneEnt->lightingOrigin[0] = lightingOrigin[0];
                sceneEnt->lightingOrigin[1] = lightingOrigin[1];
                sceneEnt->lightingOrigin[2] = lightingOrigin[2];
                sceneEnt->gfxEntIndex = gfxEntIndex;
                CG_PredictiveSkinCEntity(sceneEnt);
            }
        }
        else
        {
            sceneEntIndex = R_AllocSceneModel();
            if (sceneEntIndex < 0x400)
            {
                sceneModel = &scene.sceneModel[sceneEntIndex];
                model = DObjGetModel(obj, 0);
                sceneModel->model = model;
                sceneModel->obj = obj;
                sceneModel->entnum = entnum;
                scene.dpvs.sceneXModelIndex[entnum] = sceneEntIndex;
                sceneModel->cachedLightingHandle = &pose->lightingHandle;
                radius = XModelGetRadius(model);
                CG_GetPoseOrigin(pose, sceneModel->placement.base.origin);
                CG_GetPoseAngles(pose, angles);
                AnglesToQuat(angles, sceneModel->placement.base.quat);
                sceneModel->placement.scale = 1.0;
                sceneModel->radius = radius;
                sceneModel->lightingOrigin[0] = *lightingOrigin;
                sceneModel->lightingOrigin[1] = lightingOrigin[1];
                sceneModel->lightingOrigin[2] = lightingOrigin[2];
                sceneModel->gfxEntIndex = gfxEntIndex;
            }
        }
    }
}

GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *material)
{
    uint32_t cloudIndex = UINT32_MAX; // [esp+Ch] [ebp-4h]

    if (gfx::reservation_atomic::TryReserveIndex(
            &frontEndDataOut->cloudCount,
            static_cast<uint32_t>(ARRAY_COUNT(frontEndDataOut->clouds)),
            &cloudIndex))
    {
        if (R_AddParticleCloudDrawSurf(cloudIndex, material))
            return &frontEndDataOut->clouds[cloudIndex];
        else
            return 0;
    }
    else
    {
        R_WarnOncePerFrame(R_WARN_MAX_CLOUDS);
        return 0;
    }
}

void __cdecl R_AddOmniLightToScene(const float *org, float radius, float r, float g, float b)
{
    GfxLight *dst; // [esp+8h] [ebp-4h]

    if (rg.registered && rgp.world)
    {
        iassert( rg.inFrame );
        if (radius > 0.0)
        {
            if (scene.addedLightCount < 32)
            {
                dst = &scene.addedLight[scene.addedLightCount++];
                memset(&dst->type, 0, sizeof(GfxLight));
                dst->def = rgp.dlightDef;
                dst->type = 3;
                dst->origin[0] = *org;
                dst->origin[1] = org[1];
                dst->origin[2] = org[2];
                dst->radius = radius;
                dst->color[0] = r;
                dst->color[1] = g;
                dst->color[2] = b;
                dst->canUseShadowMap = 0;
                dst->spotShadowIndex = -1;
            }
            else
            {
                R_WarnOncePerFrame(R_WARN_MAX_DLIGHTS);
            }
        }
    }
}

void __cdecl R_AddSpotLightToScene(const float *org, const float *dir, float radius, float r, float g, float b)
{
    float v6; // [esp+Ch] [ebp-38h]
    float v7; // [esp+10h] [ebp-34h]
    float v8; // [esp+14h] [ebp-30h]
    float spotLightFovOuter; // [esp+18h] [ebp-2Ch]
    float v10; // [esp+1Ch] [ebp-28h]
    float value; // [esp+20h] [ebp-24h]
    float v12; // [esp+30h] [ebp-14h]
    float spotLightFovInner; // [esp+38h] [ebp-Ch]
    float spotLightOffset; // [esp+3Ch] [ebp-8h]
    GfxLight *dst; // [esp+40h] [ebp-4h]

    if (rg.registered && rgp.world)
    {
        iassert( rg.inFrame );
        if (radius > 0.0)
        {
            if (scene.addedLightCount < 32)
            {
                if (r_spotLightStartRadius->current.value >= (double)r_spotLightEndRadius->current.value)
                {
                    value = r_spotLightStartRadius->current.value + 0.1000000014901161;
                    Dvar_SetFloat((dvar_s *)r_spotLightEndRadius, value);
                }
                if (r_spotLightEndRadius->current.value >= r_spotLightStartRadius->current.value + radius)
                {
                    v10 = r_spotLightStartRadius->current.value + radius - 0.1000000014901161;
                    Dvar_SetFloat((dvar_s *)r_spotLightEndRadius, v10);
                }
                dst = &scene.addedLight[scene.addedLightCount++];
                v12 = (r_spotLightEndRadius->current.value - r_spotLightStartRadius->current.value) / radius;
                spotLightFovOuter = atan(v12);
                spotLightFovInner = spotLightFovOuter * r_spotLightFovInnerFraction->current.value;
                iassert( spotLightFovOuter > 0.0f );
                v8 = tan(spotLightFovOuter);
                spotLightOffset = r_spotLightStartRadius->current.value / v8;
                memset(&dst->type, 0, sizeof(GfxLight));
                dst->def = rgp.dlightDef;
                dst->type = 2;
                dst->origin[0] = *org;
                dst->origin[1] = org[1];
                dst->origin[2] = org[2];
                Vec3Scale(dir, -1.0, dst->dir);
                Vec3Mad(dst->origin, spotLightOffset, dst->dir, dst->origin);
                dst->radius = radius + spotLightOffset;
                dst->color[0] = r;
                dst->color[1] = g;
                dst->color[2] = b;
                Vec3Scale(dst->color, r_spotLightBrightness->current.value, dst->color);
                scene.dynamicSpotLightNearPlaneOffset = spotLightOffset;
                dst->exponent = 1;
                iassert( spotLightFovOuter > spotLightFovInner );
                v7 = cos(spotLightFovInner);
                dst->cosHalfFovInner = v7;
                v6 = cos(spotLightFovOuter);
                dst->cosHalfFovOuter = v6;
                dst->canUseShadowMap = r_spotLightShadows->current.color[0];
                dst->spotShadowIndex = -1;
            }
            else
            {
                R_WarnOncePerFrame(R_WARN_MAX_DLIGHTS);
            }
        }
    }
}

double __cdecl R_GetDefaultNearClip()
{
    float v2; // [esp+4h] [ebp-8h]
    float value; // [esp+8h] [ebp-4h]

    value = r_znear->current.value;
    v2 = 0.0099999998 - value;
    if (v2 < 0.0)
        return value;
    else
        return (float)0.0099999998;
}

void __cdecl R_SetupViewProjectionMatrices(GfxViewParms *viewParms)
{
    MatrixMultiply44(viewParms->viewMatrix.m, viewParms->projectionMatrix.m, viewParms->viewProjectionMatrix.m);
    MatrixInverse44(viewParms->viewProjectionMatrix.m, viewParms->inverseViewProjectionMatrix.m);
}

void __cdecl R_AddBModelSurfacesCamera(
    BModelDrawInfo *bmodelInfo,
    const GfxBrushModel *bmodel,
    GfxDrawSurf **drawSurfs,
    GfxDrawSurf **lastDrawSurfs,
    uint32_t reflectionProbeIndex)
{
    uint16_t surfaceCount; // [esp+4h] [ebp-28h]
    uint32_t surfId; // [esp+8h] [ebp-24h]
    //unsigned __int64 drawSurf; // [esp+Ch] [ebp-20h]
    const Material *material; // [esp+14h] [ebp-18h]
    const BModelSurface *modelSurf; // [esp+18h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+1Ch] [ebp-10h]
    uint32_t region; // [esp+20h] [ebp-Ch]
    uint32_t count; // [esp+28h] [ebp-4h]

    iassert(bmodel);
    if (!bmodel)
        return;
    if (reflectionProbeIndex >= 0x100)
        MyAssertHandler(
            ".\\r_scene.cpp",
            548,
            0,
            "reflectionProbeIndex doesn't index 1 << MTL_SORT_ENVMAP_BITS\n\t%i not in [0, %i)",
            reflectionProbeIndex,
            256);
    if (gfxDrawMethod.emissiveTechType >= (uint32_t)TECHNIQUE_COUNT)
        MyAssertHandler(
            ".\\r_scene.cpp",
            550,
            0,
            "gfxDrawMethod.emissiveTechType doesn't index TECHNIQUE_COUNT\n\t%i not in [0, %i)",
            gfxDrawMethod.emissiveTechType,
            34);
    if (r_drawDecals->current.enabled)
        surfaceCount = bmodel->surfaceCount;
    else
        surfaceCount = bmodel->surfaceCountNoDecal;
    if (!R_TryResolveBModelSurfaceSequence(
            bmodelInfo,
            surfaceCount,
            &modelSurf))
    {
        return;
    }
    surfId = bmodelInfo->surfId;
    for (count = 0; count < surfaceCount; ++count)
    {
        bspSurf = modelSurf->surf;
        material = bspSurf->material;
        iassert(rgp.sortedMaterials[material->info.drawSurf.fields.materialSortedIndex] == material);
        region = material->cameraRegion;
        if (region != 3)
        {
            if (drawSurfs[region] >= lastDrawSurfs[region])
            {
                R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddBModelSurfacesCamera");
                return;
            }
            bcassert(surfId, (1 << MTL_SORT_OBJECT_ID_BITS));

            drawSurfs[region]->packed = material->info.drawSurf.packed;

            drawSurfs[region]->fields.reflectionProbeIndex = reflectionProbeIndex;
            drawSurfs[region]->fields.customIndex = bspSurf->lightmapIndex;
            drawSurfs[region]->fields.objectId = surfId;
            drawSurfs[region]->fields.primaryLightIndex = bspSurf->primaryLightIndex;

            //LODWORD(drawSurf) = ((uint8_t)reflectionProbeIndex << 16) // reflectionProbeIndex
            //    | ((bspSurf->lightmapIndex & 0x1F) << 24) // customIndex
            //    | (uint16_t)surfId // objectId
            //    | *(uint32_t *)&material->info.drawSurf.fields & 0xE0000000; // copy upper 3 bits of LODWORD

            //HIDWORD(drawSurf) = (bspSurf->primaryLightIndex << 10) // PrimaryLightIndex
            //    | HIDWORD(material->info.drawSurf.packed) & 0xFFC003FF // all other bits in HIDWORD
            //    | 0x180000; // this sets surfType to b(0110)

            drawSurfs[region]->fields.surfType = SF_BMODEL;

            ++drawSurfs[region];
        }
        ++modelSurf;
        surfId += sizeof(BModelSurface) / sizeof(uint32_t);
    }
}

GfxDrawSurf *__cdecl R_AddBModelSurfaces(
    BModelDrawInfo *bmodelInfo,
    const GfxBrushModel *bmodel,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf)
{
    uint16_t surfaceCount; // [esp+6h] [ebp-2Ah]
    uint32_t surfId; // [esp+10h] [ebp-20h]
    const Material *material; // [esp+14h] [ebp-1Ch]
    const BModelSurface *modelSurf; // [esp+20h] [ebp-10h]
    uint32_t count; // [esp+2Ch] [ebp-4h]

    iassert( bmodel );
    if (!bmodel)
        return drawSurf;
    if (r_drawDecals->current.enabled)
        surfaceCount = bmodel->surfaceCount;
    else
        surfaceCount = bmodel->surfaceCountNoDecal;
    if (!R_TryResolveBModelSurfaceSequence(
            bmodelInfo,
            surfaceCount,
            &modelSurf))
    {
        return drawSurf;
    }
    surfId = bmodelInfo->surfId;
    for (count = 0; count < surfaceCount; ++count)
    {
        if (drawSurf >= lastDrawSurf)
        {
            R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddBModelSurfaces");
            return drawSurf;
        }
        material = modelSurf->surf->material;
        iassert(rgp.sortedMaterials[material->info.drawSurf.fields.materialSortedIndex] == material);
        if (Material_GetTechnique(material, techType))
        {
            bcassert(surfId, (1 << MTL_SORT_OBJECT_ID_BITS));

			drawSurf->packed = material->info.drawSurf.packed;
			drawSurf->fields.objectId = surfId;
			drawSurf->fields.surfType = SF_BMODEL;
			++drawSurf;
        }
        ++modelSurf;
        surfId += sizeof(BModelSurface) / sizeof(uint32_t);
    }
    return drawSurf;
}

const XSurface *__cdecl R_GetXSurface(
    const GfxModelSkinnedSurface *modelSurf,
    surfaceType_t surfType)
{
    iassert( modelSurf );
    iassert( R_IsModelSurfaceType( surfType ) );
    return modelSurf ? modelSurf->xsurf : nullptr;
}

void __cdecl R_AddXModelSurfacesCamera(
    XModelDrawInfo *modelInfo,
    const XModel *model,
    float *origin,
    uint16_t gfxEntIndex,
    uint32_t lightingHandle,
    uint8_t primaryLightIndex,
    char isShadowReceiver,
    int depthHack,
    GfxDrawSurf **drawSurfs,
    GfxDrawSurf **lastDrawSurfs,
    uint32_t reflectionProbeIndex)
{
    const XSurface *xSurf; // eax
    const XSurface *v12; // eax
    uint32_t surfId; // [esp+8h] [ebp-38h]
    int totalVertCount; // [esp+Ch] [ebp-34h]
    //__int64 drawSurf; // [esp+10h] [ebp-30h]
    int totalTriCount; // [esp+1Ch] [ebp-24h]
    Material **material; // [esp+20h] [ebp-20h]
    uint32_t subMatIndex; // [esp+24h] [ebp-1Ch]
    int skinnedCachedOffset; // [esp+28h] [ebp-18h]
    int lod; // [esp+2Ch] [ebp-14h]
    GfxModelRigidSurface *modelSurf; // [esp+30h] [ebp-10h]
    surfaceType_t surfType; // [esp+34h] [ebp-Ch]
    uint32_t region; // [esp+38h] [ebp-8h]
    uint32_t numsurfs; // [esp+3Ch] [ebp-4h]

    iassert( lightingHandle );
    totalTriCount = 0;
    totalVertCount = 0;
    iassert( model );
    if (!modelInfo || !model || !frontEndDataOut)
        return;
    StaticXModelSurfaceCursor surfaceCursor(
        frontEndDataOut,
        modelInfo->surfId);
    lod = modelInfo->lod;
    numsurfs = XModelGetSurfCount(model, lod);
    material = XModelGetSkins(model, lod);
    iassert( material );
    if (!material)
        return;

    bcassert(reflectionProbeIndex, 1 << MTL_SORT_ENVMAP_BITS);
    bcassert(gfxDrawMethod.emissiveTechType, TECHNIQUE_COUNT);

    for (subMatIndex = 0; subMatIndex < numsurfs; ++subMatIndex)
    {
        StaticXModelSurfaceView surfaceView;
        if (!surfaceCursor.Next(&surfaceView))
            return;
        surfId = surfaceView.surfId;
        if (surfaceView.tag == model_surface_stream::kHiddenTag)
        {
            ++material;
            continue;
        }

        modelSurf = surfaceView.surface;
        skinnedCachedOffset = surfaceView.tag;
        iassert( *material );
        if (!*material)
            return;
        if (rgp.sortedMaterials[(*material)->info.drawSurf.fields.materialSortedIndex] != *material)
            MyAssertHandler(
                ".\\r_scene.cpp",
                709,
                0,
                "%s",
                "rgp.sortedMaterials[(*material)->info.drawSurf.fields.materialSortedIndex] == *material");
        region = (*material)->cameraRegion;
        if (region >= 4u)
            return;
        if (region != 3)
        {
            if (drawSurfs[region] >= lastDrawSurfs[region])
            {
                R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddXModelSurfacesCamera");
                break;
            }
            if (skinnedCachedOffset == -2)
            {
                surfType = SF_BEGIN_XMODEL;
            }
            else
            {
                if (skinnedCachedOffset != -1)
                    MyAssertHandler(
                        ".\\r_scene.cpp",
                        728,
                        0,
                        "%s\n\t(skinnedCachedOffset) = %i",
                        "(skinnedCachedOffset == (-1))",
                        skinnedCachedOffset);
                surfType = SF_XMODEL_RIGID_SKINNED;
            }
            modelSurf->surf.info.gfxEntIndex = gfxEntIndex;
            modelSurf->surf.info.lightingHandle = lightingHandle;

            bcassert(surfId, (1 << MTL_SORT_OBJECT_ID_BITS));

                //drawSurf = (*material)->info.drawSurf.packed;

            drawSurfs[region]->packed = (*material)->info.drawSurf.packed;

                drawSurfs[region]->fields.surfType = surfType;
                drawSurfs[region]->fields.primarySortKey -= depthHack;
                
                drawSurfs[region]->fields.customIndex = isShadowReceiver;
                drawSurfs[region]->fields.reflectionProbeIndex = reflectionProbeIndex;
                drawSurfs[region]->fields.objectId = surfId;
                drawSurfs[region]->fields.primaryLightIndex = primaryLightIndex;

                //HIDWORD(drawSurf) = ((surfType & 0xF) << 18) // surfType
                //    | (((((drawSurf >> 54) & 0x3F) - depthHack) & 0x3F) << 22) // primarySortKey - depthHAck
                //    | HIDWORD(drawSurf) & 0xF003FFFF; // all bits except the 10 above

                //LODWORD(drawSurf) = ((isShadowReceiver & 0x1F) << 24) // customIndex
                //    | (reflectionProbeIndex << 16) & 0xE0FFFFFF // reflectionProbIndex
                //    | surfId // objectId
                //    | drawSurf & 0xE0000000; // top 3 bits of LODWORD
                //HIDWORD(drawSurf) = (primaryLightIndex << 10) | HIDWORD(drawSurf) & 0xFFFC03FF; // primaryLightIndex and rest of bits

                //drawSurfs[region]->packed = drawSurf;

            ++drawSurfs[region];
            if (r_showTriCounts->current.enabled)
            {
                xSurf = R_GetXSurface(&modelSurf->surf, surfType);
                totalTriCount += XSurfaceGetNumTris(xSurf);
            }
            else if (r_showVertCounts->current.enabled)
            {
                v12 = R_GetXSurface(&modelSurf->surf, surfType);
                totalVertCount += XSurfaceGetNumVerts(v12);
            }
        }
        ++material;
    }
    if (r_showTriCounts->current.enabled)
    {
        R_AddXModelDebugString(origin, va("%i", totalTriCount));
    }
    else if (r_showVertCounts->current.enabled)
    {
        R_AddXModelDebugString(origin, va("%i", totalVertCount));
    }
    else if (r_showSurfCounts->current.enabled)
    {
        R_AddXModelDebugString(origin, va("%i", numsurfs));
    }
}

void __cdecl R_AddXModelDebugString(const float *origin, char *string)
{
    R_AddScaledDebugString(&frontEndDataOut->debugGlobals, rg.debugViewParms, origin, colorCyan, string);
}

GfxDrawSurf *__cdecl R_AddXModelSurfaces(
    XModelDrawInfo *modelInfo,
    const XModel *model,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf)
{
    uint32_t surfId; // [esp+10h] [ebp-2Ch]
    Material **material; // [esp+14h] [ebp-28h]
    uint32_t subMatIndex; // [esp+18h] [ebp-24h]
    //unsigned __int64 newDrawSurf; // [esp+1Ch] [ebp-20h]
    int skinnedCachedOffset; // [esp+28h] [ebp-14h]
    int lod; // [esp+2Ch] [ebp-10h]
    GfxModelRigidSurface *modelSurf; // [esp+30h] [ebp-Ch]
    char surfType; // [esp+34h] [ebp-8h]
    uint32_t numsurfs; // [esp+38h] [ebp-4h]

    iassert( model );
    if (!modelInfo || !model || !frontEndDataOut)
        return drawSurf;
    StaticXModelSurfaceCursor surfaceCursor(
        frontEndDataOut,
        modelInfo->surfId);
    lod = modelInfo->lod;
    numsurfs = XModelGetSurfCount(model, lod);
    material = XModelGetSkins(model, lod);
    iassert( material );
    if (!material)
        return drawSurf;
    for (subMatIndex = 0; subMatIndex < numsurfs; ++subMatIndex)
    {
        StaticXModelSurfaceView surfaceView;
        if (!surfaceCursor.Next(&surfaceView))
            return drawSurf;
        surfId = surfaceView.surfId;
        if (surfaceView.tag == model_surface_stream::kHiddenTag)
        {
            ++material;
            continue;
        }

        modelSurf = surfaceView.surface;
        skinnedCachedOffset = surfaceView.tag;
        iassert(*material);
        if (!*material)
            return drawSurf;
        iassert(rgp.sortedMaterials[(*material)->info.drawSurf.fields.materialSortedIndex] == *material);

        if (Material_GetTechnique(*material, techType))
        {
            if (drawSurf >= lastDrawSurf)
            {
                R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddXModelSurfaces");
                return drawSurf;
            }
            if (skinnedCachedOffset == -2)
            {
                surfType = 7;
            }
            else
            {
                iassert(skinnedCachedOffset == -1);
                surfType = 8;
            }

                bcassert(surfId, (1 << MTL_SORT_OBJECT_ID_BITS));

                drawSurf->packed = (*material)->info.drawSurf.packed; // copy all to start with

                drawSurf->fields.surfType = surfType;
                drawSurf->fields.objectId = surfId;
                //HIDWORD(newDrawSurf) = ((surfType & 0xF) << 18)  // surfType
                //    | HIDWORD((*material)->info.drawSurf.packed) & 0xFFC3FFFF; // rest of bits in HIDWORD
                //LODWORD(newDrawSurf) = (uint16_t)surfId  // objectID
                //    | *(uint32_t *)&(*material)->info.drawSurf.fields & 0xFFFF0000; // rest of bits in LODWORD

                //drawSurf->packed = newDrawSurf;

            ++drawSurf;
        }
        ++material;
    }
    return drawSurf;
}

void __cdecl R_AddDObjSurfacesCamera(
    GfxSceneEntity *sceneEnt,
    __int16 lightingHandle,
    uint8_t primaryLightIndex,
    GfxDrawSurf **drawSurfs,
    GfxDrawSurf **lastDrawSurfs)
{
    iassert(sceneEnt);
    const uint32_t cullState = R_LoadSceneEntityCullState(sceneEnt);
    if (!sceneEnt || cullState <= CULL_STATE_DONE
        || !sceneEnt->cull.skinnedSurfs.firstSurf)
        return;

    uint32_t streamBytes = 0u;
    uint32_t streamSurfaces = 0u;
    if (!R_ValidateSceneModelSurfaceStream(
            *sceneEnt,
            cullState,
            &streamBytes,
            &streamSurfaces))
        return;

    model_surface_stream::Cursor cursor(
        sceneEnt->cull.skinnedSurfs.firstSurf,
        streamBytes,
        streamSurfaces);
    const DObj_s *const obj = sceneEnt->obj;
    iassert(obj);
    if (!obj || !R_ValidateSceneDObjModels(
            *sceneEnt,
            *obj,
            streamSurfaces))
        return;
    const uint32_t modelCount = DObjGetNumModels(obj);

    bcassert(gfxDrawMethod.emissiveTechType, TECHNIQUE_COUNT);

    const uint32_t gfxEntIndex = sceneEnt->gfxEntIndex;
    bool isShadowReceiver = false;
    uint32_t depthHack = 0u;
    if (gfxEntIndex)
    {
        if (gfxEntIndex >= ARRAY_COUNT(frontEndDataOut->gfxEnts))
            return;
        isShadowReceiver = sc_enable->current.enabled
            && (frontEndDataOut->gfxEnts[gfxEntIndex].renderFxFlags & 0x100) != 0;
        depthHack = (frontEndDataOut->gfxEnts[gfxEntIndex].renderFxFlags & 2) != 0;
    }

    int totalTriCount = 0;
    int totalVertCount = 0;
    uint32_t totalSurfCount = 0u;
    for (uint32_t modelIndex = 0u; modelIndex < modelCount; ++modelIndex)
    {
        const int lod = sceneEnt->cull.lods[modelIndex];
        if (lod < 0)
            continue;

        XModel *const model = DObjGetModel(obj, modelIndex);
        XSurface *surfaces = nullptr;
        const int surfaceCount = XModelGetSurfaces(model, &surfaces, lod);
        if (!model || !surfaces || surfaceCount <= 0)
            return;
        Material **material = XModelGetSkins(model, lod);
        if (!material)
            return;

        for (int surfaceIndex = 0; surfaceIndex < surfaceCount;
             ++surfaceIndex, ++material)
        {
            model_surface_stream::View record;
            if (!cursor.Next(
                    kHiddenModelSurfaceBytes,
                    kSkinnedModelSurfaceBytes,
                    kRigidModelSurfaceBytes,
                    &record))
            {
                return;
            }
            ++totalSurfCount;
            if (record.kind == model_surface_stream::RecordKind::Hidden)
                continue;

            GfxModelSkinnedSurface *const modelSurf =
                reinterpret_cast<GfxModelSkinnedSurface *>(record.data);
            const surfaceType_t surfType =
                record.kind == model_surface_stream::RecordKind::Rigid
                ? SF_BEGIN_XMODEL
                : SF_XMODEL_SKINNED;
            if (!*material || modelSurf->xsurf != &surfaces[surfaceIndex])
                return;
            iassert(rgp.sortedMaterials[(*material)->info.drawSurf.fields.materialSortedIndex] == *material);
            const uint32_t region = (*material)->cameraRegion;
            if (region >= 4u)
                return;
            if (region != 3)
            {
                if (drawSurfs[region] >= lastDrawSurfs[region])
                {
                    R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddDObjSurfacesCamera");
                    return;
                }

                modelSurf->info.gfxEntIndex = static_cast<uint16_t>(gfxEntIndex);
                modelSurf->info.lightingHandle = static_cast<uint16_t>(lightingHandle);
                const uintptr_t recordAddress = reinterpret_cast<uintptr_t>(modelSurf);
                const uintptr_t dataAddress = reinterpret_cast<uintptr_t>(frontEndDataOut);
                if (recordAddress < dataAddress
                    || recordAddress - dataAddress >= sizeof(frontEndDataOut->surfsBuffer)
                    || ((recordAddress - dataAddress) & 3u) != 0u)
                {
                    return;
                }
                const uint32_t surfId = static_cast<uint32_t>(
                    (recordAddress - dataAddress) / sizeof(uint32_t));
                bcassert(surfId, (1 << MTL_SORT_OBJECT_ID_BITS));

                drawSurfs[region]->packed = (*material)->info.drawSurf.packed;
                drawSurfs[region]->fields.surfType = surfType;
                drawSurfs[region]->fields.primarySortKey -= depthHack;
                drawSurfs[region]->fields.customIndex = isShadowReceiver;
                drawSurfs[region]->fields.reflectionProbeIndex = sceneEnt->reflectionProbeIndex;
                drawSurfs[region]->fields.objectId = surfId;
                drawSurfs[region]->fields.primaryLightIndex = primaryLightIndex;
                ++drawSurfs[region];
                if (r_showTriCounts->current.enabled)
                {
                    const XSurface *const xSurf = R_GetXSurface(modelSurf, surfType);
                    totalTriCount += XSurfaceGetNumTris(xSurf);
                }
                else if (r_showVertCounts->current.enabled)
                {
                    const XSurface *const xSurf = R_GetXSurface(modelSurf, surfType);
                    totalVertCount += XSurfaceGetNumVerts(xSurf);
                }
            }
        }
    }

    if (!cursor.Finished() || totalSurfCount != streamSurfaces)
        return;
    if (r_showTriCounts->current.enabled)
    {
        R_AddXModelDebugString(sceneEnt->placement.base.origin, va("%i", totalTriCount));
    }
    else if (r_showVertCounts->current.enabled)
    {
        R_AddXModelDebugString(sceneEnt->placement.base.origin, va("%i", totalVertCount));
    }
    else if (r_showSurfCounts->current.enabled)
    {
        R_AddXModelDebugString(sceneEnt->placement.base.origin, va("%i", totalSurfCount));
    }
}

GfxDrawSurf *__cdecl R_AddDObjSurfaces(
    GfxSceneEntity *sceneEnt,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf)
{
    iassert(sceneEnt);
    const uint32_t cullState = R_LoadSceneEntityCullState(sceneEnt);
    if (!sceneEnt || cullState <= CULL_STATE_DONE
        || !sceneEnt->cull.skinnedSurfs.firstSurf)
        return drawSurf;

    uint32_t streamBytes = 0u;
    uint32_t streamSurfaces = 0u;
    if (!R_ValidateSceneModelSurfaceStream(
            *sceneEnt,
            cullState,
            &streamBytes,
            &streamSurfaces))
        return drawSurf;
    model_surface_stream::Cursor cursor(
        sceneEnt->cull.skinnedSurfs.firstSurf,
        streamBytes,
        streamSurfaces);

    const DObj_s *const obj = sceneEnt->obj;
    iassert(obj);
    if (!obj || !R_ValidateSceneDObjModels(
            *sceneEnt,
            *obj,
            streamSurfaces))
        return drawSurf;
    const uint32_t modelCount = DObjGetNumModels(obj);
    uint32_t depthHack = 0u;
    if (sceneEnt->gfxEntIndex)
    {
        if (sceneEnt->gfxEntIndex >= ARRAY_COUNT(frontEndDataOut->gfxEnts))
            return drawSurf;
        depthHack = (frontEndDataOut->gfxEnts[sceneEnt->gfxEntIndex].renderFxFlags & 2) != 0;
    }

    uint32_t consumedSurfaces = 0u;
    for (uint32_t modelIndex = 0u; modelIndex < modelCount; ++modelIndex)
    {
        const int lod = sceneEnt->cull.lods[modelIndex];
        if (lod < 0)
            continue;

        XModel *const model = DObjGetModel(obj, modelIndex);
        XSurface *surfaces = nullptr;
        const int surfaceCount = XModelGetSurfaces(model, &surfaces, lod);
        if (!model || !surfaces || surfaceCount <= 0)
            return drawSurf;
        Material **material = XModelGetSkins(model, lod);
        if (!material)
            return drawSurf;

        for (int surfaceIndex = 0; surfaceIndex < surfaceCount;
             ++surfaceIndex, ++material)
        {
            model_surface_stream::View record;
            if (!cursor.Next(
                    kHiddenModelSurfaceBytes,
                    kSkinnedModelSurfaceBytes,
                    kRigidModelSurfaceBytes,
                    &record))
            {
                return drawSurf;
            }
            ++consumedSurfaces;
            if (record.kind == model_surface_stream::RecordKind::Hidden)
                continue;
            GfxModelSkinnedSurface *const modelSurf =
                reinterpret_cast<GfxModelSkinnedSurface *>(record.data);
            if (!*material || modelSurf->xsurf != &surfaces[surfaceIndex])
                return drawSurf;
            iassert(rgp.sortedMaterials[(*material)->info.drawSurf.fields.materialSortedIndex] == *material);
            if (!Material_GetTechnique(*material, techType))
                continue;
            if (drawSurf >= lastDrawSurf)
            {
                R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AddDObjSurfaces");
                return drawSurf;
            }

            const uintptr_t recordAddress = reinterpret_cast<uintptr_t>(record.data);
            const uintptr_t dataAddress = reinterpret_cast<uintptr_t>(frontEndDataOut);
            if (recordAddress < dataAddress
                || recordAddress - dataAddress >= sizeof(frontEndDataOut->surfsBuffer)
                || ((recordAddress - dataAddress) & 3u) != 0u)
            {
                return drawSurf;
            }
            const uint32_t surfId = static_cast<uint32_t>(
                (recordAddress - dataAddress) / sizeof(uint32_t));
            iassert(surfId < (1 << MTL_SORT_OBJECT_ID_BITS));

            const GfxDrawSurf newDrawSurf = (*material)->info.drawSurf;
            drawSurf->packed = newDrawSurf.packed;
            drawSurf->fields.objectId = static_cast<unsigned short>(surfId);
            drawSurf->fields.surfType =
                record.kind == model_surface_stream::RecordKind::Rigid ? 7 : 9;
            drawSurf->fields.primarySortKey =
                newDrawSurf.fields.primarySortKey - depthHack;
            ++drawSurf;
        }
    }

    if (!cursor.Finished() || consumedSurfaces != streamSurfaces)
        return drawSurf;
    return drawSurf;
}

bool __cdecl R_EndFencePending()
{
    _BYTE v2[4]; // [esp+Ch] [ebp-4h] BYREF

    return frontEndDataOut->endFence && frontEndDataOut->endFence->GetData(v2, 4u, 1u) == 1;
}

void __cdecl R_SetEndTime(int endTime)
{
    rg.endTime = endTime;
}

void __cdecl R_WaitEndTime()
{
    //KISAK_NULLSUB();
    PROF_SCOPED("MaxFPSSpin");

    while ((int)(Sys_Milliseconds() - rg.endTime) < 0)
        NET_Sleep(1);
}

void __cdecl R_InitScene()
{
    scene.drawSurfs[0] = (GfxDrawSurf *)&scene;
    scene.drawSurfs[1] = scene.smodelDrawSurfsLight;
    scene.drawSurfs[2] = scene.entDrawSurfsLight;
    scene.drawSurfs[3] = scene.bspDrawSurfsDecal;
    scene.drawSurfs[4] = scene.smodelDrawSurfsDecal;
    scene.drawSurfs[5] = scene.entDrawSurfsDecal;
    scene.drawSurfs[6] = scene.fxDrawSurfsLight;
    scene.drawSurfs[7] = scene.fxDrawSurfsLightAuto;
    scene.drawSurfs[8] = scene.fxDrawSurfsLightDecal;
    scene.drawSurfs[9] = scene.bspDrawSurfsEmissive;
    scene.drawSurfs[10] = scene.smodelDrawSurfsEmissive;
    scene.drawSurfs[11] = scene.entDrawSurfsEmissive;
    scene.drawSurfs[12] = scene.fxDrawSurfsEmissive;
    scene.drawSurfs[13] = scene.fxDrawSurfsEmissiveAuto;
    scene.drawSurfs[14] = scene.fxDrawSurfsEmissiveDecal;
    scene.drawSurfs[15] = scene.bspSunShadowDrawSurfs0;
    scene.drawSurfs[16] = scene.smodelSunShadowDrawSurfs0;
    scene.drawSurfs[17] = scene.entSunShadowDrawSurfs0;
    scene.drawSurfs[18] = scene.bspSunShadowDrawSurfs1;
    scene.drawSurfs[19] = scene.smodelSunShadowDrawSurfs1;
    scene.drawSurfs[20] = scene.entSunShadowDrawSurfs1;
    scene.drawSurfs[21] = scene.bspSpotShadowDrawSurfs0;
    scene.drawSurfs[22] = scene.smodelSpotShadowDrawSurfs0;
    scene.drawSurfs[23] = scene.entSpotShadowDrawSurfs0;
    scene.drawSurfs[24] = scene.bspSpotShadowDrawSurfs1;
    scene.drawSurfs[25] = scene.smodelSpotShadowDrawSurfs1;
    scene.drawSurfs[26] = scene.entSpotShadowDrawSurfs1;
    scene.drawSurfs[27] = scene.bspSpotShadowDrawSurfs2;
    scene.drawSurfs[28] = scene.smodelSpotShadowDrawSurfs2;
    scene.drawSurfs[29] = scene.entSpotShadowDrawSurfs2;
    scene.drawSurfs[30] = scene.bspSpotShadowDrawSurfs3;
    scene.drawSurfs[31] = scene.smodelSpotShadowDrawSurfs3;
    scene.drawSurfs[32] = scene.entSpotShadowDrawSurfs3;
    scene.drawSurfs[33] = scene.shadowDrawSurfs;
    scene.maxDrawSurfCount[0] = 0x2000;
    scene.maxDrawSurfCount[1] = 0x2000;
    scene.maxDrawSurfCount[2] = 0x2000;
    scene.maxDrawSurfCount[3] = 512;
    scene.maxDrawSurfCount[4] = 512;
    scene.maxDrawSurfCount[5] = 512;
    scene.maxDrawSurfCount[6] = 0x2000;
    scene.maxDrawSurfCount[7] = 0x2000;
    scene.maxDrawSurfCount[8] = 0x2000;
    scene.maxDrawSurfCount[9] = 0x2000;
    scene.maxDrawSurfCount[10] = 0x2000;
    scene.maxDrawSurfCount[11] = 0x2000;
    scene.maxDrawSurfCount[12] = 0x2000;
    scene.maxDrawSurfCount[13] = 0x2000;
    scene.maxDrawSurfCount[14] = 0x2000;
    scene.maxDrawSurfCount[15] = 4096;
    scene.maxDrawSurfCount[16] = 4096;
    scene.maxDrawSurfCount[17] = 4096;
    scene.maxDrawSurfCount[18] = 0x2000;
    scene.maxDrawSurfCount[19] = 0x2000;
    scene.maxDrawSurfCount[20] = 0x2000;
    scene.maxDrawSurfCount[21] = 256;
    scene.maxDrawSurfCount[22] = 256;
    scene.maxDrawSurfCount[23] = 512;
    scene.maxDrawSurfCount[24] = 256;
    scene.maxDrawSurfCount[25] = 256;
    scene.maxDrawSurfCount[26] = 512;
    scene.maxDrawSurfCount[27] = 256;
    scene.maxDrawSurfCount[28] = 256;
    scene.maxDrawSurfCount[29] = 512;
    scene.maxDrawSurfCount[30] = 256;
    scene.maxDrawSurfCount[31] = 256;
    scene.maxDrawSurfCount[32] = 512;
    scene.maxDrawSurfCount[33] = 512;
}

void __cdecl R_ClearScene(uint32_t localClientNum)
{
    const uint32_t sceneBrushCount = Sys_AtomicLoad(&scene.sceneBrushCount);
    const uint32_t sceneDObjCount = Sys_AtomicLoad(&scene.sceneDObjCount);
    const uint32_t sceneModelCount = Sys_AtomicLoad(&scene.sceneModelCount);
    uint32_t viewIndex; // [esp+0h] [ebp-4h]

    iassert( rg.inFrame );
    iassert( Sys_IsMainThread() || Sys_IsRenderThread() );
    scene.dpvs.localClientNum = localClientNum;
    Com_Memset(
        scene.sceneDObj,
        0,
        sizeof(scene.sceneDObj[0]) * sceneDObjCount);
    Com_Memset(
        &scene.sceneModel[0].info,
        0,
        sizeof(scene.sceneModel[0]) * sceneModelCount);
    Com_Memset(
        &scene.sceneBrush[0].info.surfId,
        0,
        sizeof(scene.sceneBrush[0]) * sceneBrushCount);
    scene.addedLightCount = 0;
    for (uint32_t drawSurfType = 0u;
         drawSurfType < static_cast<uint32_t>(DRAW_SURF_TYPE_COUNT);
         ++drawSurfType)
        Sys_AtomicStore(&scene.drawSurfCount[drawSurfType], 0u);
    for (viewIndex = 0; viewIndex < 7; ++viewIndex)
        Com_Memset((uint32_t *)scene.sceneModelVisData[viewIndex], 1, sceneModelCount);
    Sys_AtomicStore(&scene.sceneDObjCount, 0u);
    Sys_AtomicStore(&scene.sceneModelCount, 0u);
    Sys_AtomicStore(&scene.sceneBrushCount, 0u);
    if (rgp.world)
        R_ClearDpvsScene();
}

uint32_t __cdecl R_GetLocalClientNum()
{
    return scene.dpvs.localClientNum;
}

void __cdecl R_SetLodOrigin(const refdef_s *refdef)
{
    if (r_lockPvs->modified)
    {
        Dvar_ClearModified((dvar_t*)r_lockPvs);
        R_SetViewParmsForScene(refdef, &lockPvsViewParms);
    }
    R_UpdateLodParms(refdef, &rg.lodParms);
    scene.def.viewOffset[0] = refdef->viewOffset[0];
    scene.def.viewOffset[1] = refdef->viewOffset[1];
    scene.def.viewOffset[2] = refdef->viewOffset[2];
    scene.def.time = refdef->time;
    scene.def.floatTime = (double)scene.def.time * EQUAL_EPSILON;
    R_UpdateFrameFog();
    R_UpdateFrameSun();
}

void R_UpdateFrameFog()
{
    GfxFog *p_fogSettings; // edx
    float lerpPos; // [esp+8h] [ebp-8h]
    int fadeTime; // [esp+Ch] [ebp-4h]

    if (!frontEndDataOut->viewInfoCount)
    {
        if (scene.def.time < rg.fogSettings[4].finishTime)
        {
            fadeTime = rg.fogSettings[4].finishTime - rg.fogSettings[4].startTime;
            if (rg.fogSettings[4].finishTime - rg.fogSettings[4].startTime <= 0)
                fadeTime = 1;
            lerpPos = (double)(scene.def.time - rg.fogSettings[4].startTime) / (double)fadeTime;
            if (lerpPos > 1.0)
                lerpPos = 1.0;
            rg.fogSettings[2].fogStart = (rg.fogSettings[4].fogStart - rg.fogSettings[3].fogStart) * lerpPos
                + rg.fogSettings[3].fogStart;
            rg.fogSettings[2].density = (rg.fogSettings[4].density - rg.fogSettings[3].density) * lerpPos
                + rg.fogSettings[3].density;
            rg.fogSettings[2].color.array[0] = LerpByte(
                rg.fogSettings[3].color.array[0],
                rg.fogSettings[4].color.array[0],
                lerpPos);
            rg.fogSettings[2].color.array[1] = LerpByte(
                rg.fogSettings[3].color.array[1],
                rg.fogSettings[4].color.array[1],
                lerpPos);
            rg.fogSettings[2].color.array[2] = LerpByte(
                rg.fogSettings[3].color.array[2],
                rg.fogSettings[4].color.array[2],
                lerpPos);
            rg.fogSettings[2].color.array[3] = LerpByte(
                rg.fogSettings[3].color.array[3],
                rg.fogSettings[4].color.array[3],
                lerpPos);
        }
        else
        {
            rg.fogSettings[2] = rg.fogSettings[4];
        }
    }
    if (rg.fogIndex)
    {
        frontEndDataOut->fogSettings = rg.fogSettings[2];
    }
    else
    {
        p_fogSettings = &frontEndDataOut->fogSettings;
        frontEndDataOut->fogSettings.startTime = 0;
        p_fogSettings->finishTime = 0;
        p_fogSettings->color.packed = 0;
        p_fogSettings->fogStart = 0.0;
        p_fogSettings->density = 0.0;
    }
}

uint8_t __cdecl LerpByte(uint8_t from, uint8_t to, float frac)
{
    return (int)((double)from + (double)(to - from) * frac);
}

void __cdecl R_SetViewParmsForScene(const refdef_s *refdef, GfxViewParms *viewParms)
{
    float DefaultNearClip; // [esp+Ch] [ebp-24h]

    memset((uint8_t *)viewParms, 0, sizeof(GfxViewParms));
    viewParms->origin[0] = refdef->vieworg[0];
    viewParms->origin[1] = refdef->vieworg[1];
    viewParms->origin[2] = refdef->vieworg[2];
    viewParms->origin[3] = 1.0;
    viewParms->axis[0][0] = refdef->viewaxis[0][0];
    viewParms->axis[0][1] = refdef->viewaxis[0][1];
    viewParms->axis[0][2] = refdef->viewaxis[0][2];
    viewParms->axis[1][0] = refdef->viewaxis[1][0];
    viewParms->axis[1][1] = refdef->viewaxis[1][1];
    viewParms->axis[1][2] = refdef->viewaxis[1][2];
    viewParms->axis[2][0] = refdef->viewaxis[2][0];
    viewParms->axis[2][1] = refdef->viewaxis[2][1];
    viewParms->axis[2][2] = refdef->viewaxis[2][2];
    MatrixForViewer(viewParms->viewMatrix.m, viewParms->origin, viewParms->axis);
    if (refdef->zNear <= 0.0)
        DefaultNearClip = R_GetDefaultNearClip();
    else
        DefaultNearClip = refdef->zNear;
    viewParms->zNear = DefaultNearClip;
    iassert( viewParms->zNear );
    R_SetupProjection(refdef->tanHalfFovX, refdef->tanHalfFovY, viewParms);
    R_SetupViewProjectionMatrices(viewParms);
}

void __cdecl R_SetupProjection(float tanHalfFovX, float tanHalfFovY, GfxViewParms *viewParms)
{
    InfinitePerspectiveMatrix(viewParms->projectionMatrix.m, tanHalfFovX, tanHalfFovY, viewParms->zNear);
    viewParms->depthHackNearClip = -r_znear_depthhack->current.value;
}

bool R_UpdateFrameSun()
{
    bool result; // eax
    bool v1; // [esp+10h] [ebp-24h]
    int v2; // [esp+14h] [ebp-20h]
    int v3; // [esp+18h] [ebp-1Ch]
    float *color; // [esp+2Ch] [ebp-8h]
    float *dir; // [esp+30h] [ebp-4h]

    iassert( rgp.world );
    iassert( rgp.world->sunLight );
    memcpy(&frontEndDataOut->sunLight, rgp.world->sunLight, sizeof(frontEndDataOut->sunLight));
    if (rg.useSunDirOverride)
    {
        if (rg.useSunDirLerp)
        {
            R_LerpDir(
                rg.sunDirOverride,
                rg.sunDirOverrideTarget,
                rg.sunDirLerpBeginTime,
                rg.sunDirLerpEndTime,
                scene.def.time,
                frontEndDataOut->sunLight.dir);
        }
        else
        {
            dir = frontEndDataOut->sunLight.dir;
            frontEndDataOut->sunLight.dir[0] = rg.sunDirOverride[0];
            dir[1] = rg.sunDirOverride[1];
            dir[2] = rg.sunDirOverride[2];
        }
    }
    if (rg.useSunLightOverride)
    {
        color = frontEndDataOut->sunLight.color;
        frontEndDataOut->sunLight.color[0] = rg.sunLightOverride[0];
        color[1] = rg.sunLightOverride[1];
        color[2] = rg.sunLightOverride[2];
    }
    v2 = 0;
    const DvarValue &dvarVal = r_lightTweakSunDirection->current;
    if (sm_enable->current.enabled)
    {
        if (rg.useSunDirOverride
            || ((r_lightTweakSunDirection->reset.vector[0] != r_lightTweakSunDirection->current.vector[0]
                || r_lightTweakSunDirection->reset.vector[1] != r_lightTweakSunDirection->current.vector[1]
                || r_lightTweakSunDirection->reset.vector[2] != r_lightTweakSunDirection->current.vector[2])
                ? (v3 = 0)
                : (v3 = 1),
                !v3))
        {
            v2 = 1;
        }
    }
    frontEndDataOut->prim.hasSunDirChanged = v2;
    v1 = rg.useSunDirOverride
        || AngleDelta(r_lightTweakSunDirection->current.vector[0], r_lightTweakSunDirection->reset.vector[0]) > 5.0
        || AngleDelta(r_lightTweakSunDirection->current.vector[1], r_lightTweakSunDirection->reset.vector[1]) > 5.0;
    result = v1;
    frontEndDataOut->hasApproxSunDirChanged = v1;
    return result;
}

void __cdecl R_LerpDir(
    const float *dirBegin,
    const float *dirEnd,
    int beginLerpTime,
    int endLerpTime,
    int currTime,
    float *result)
{
    float fraction; // [esp+8h] [ebp-28h]
    float v7; // [esp+Ch] [ebp-24h]
    float v8; // [esp+10h] [ebp-20h]
    float v9; // [esp+28h] [ebp-8h]
    float lerpFraction; // [esp+2Ch] [ebp-4h]

    iassert( endLerpTime > beginLerpTime );
    lerpFraction = (double)(currTime - beginLerpTime) / (double)(endLerpTime - beginLerpTime);
    v8 = lerpFraction - 1.0;
    if (v8 < 0.0)
        v9 = (double)(currTime - beginLerpTime) / (double)(endLerpTime - beginLerpTime);
    else
        v9 = 1.0;
    v7 = 0.0 - lerpFraction;
    if (v7 < 0.0)
        fraction = v9;
    else
        fraction = 0.0;
    Vec3Lerp(dirBegin, dirEnd, fraction, result);
    Vec3Normalize(result);
}

void __cdecl R_UpdateLodParms(const refdef_s *refdef, GfxLodParms *lodParms)
{
    float v2; // [esp+8h] [ebp-18h]
    float value; // [esp+14h] [ebp-Ch]
    float invFovScale; // [esp+1Ch] [ebp-4h]

    if (r_lockPvs->current.enabled)
    {
        lodParms->origin[0] = lockPvsViewParms.origin[0];
        lodParms->origin[1] = lockPvsViewParms.origin[1];
        lodParms->origin[2] = lockPvsViewParms.origin[2];
    }
    else
    {
        lodParms->origin[0] = refdef->vieworg[0];
        lodParms->origin[1] = refdef->vieworg[1];
        lodParms->origin[2] = refdef->vieworg[2];
    }
    invFovScale = refdef->tanHalfFovY * 2.118673086166382;
    value = r_lodBiasRigid->current.value;
    lodParms->ramp[0].scale = r_lodScaleRigid->current.value * invFovScale;
    lodParms->ramp[0].bias = value * invFovScale;
    v2 = r_lodBiasSkinned->current.value;
    lodParms->ramp[1].scale = r_lodScaleSkinned->current.value * invFovScale;
    lodParms->ramp[1].bias = v2 * invFovScale;
    lodParms->valid = 1;
}

void __cdecl R_CorrectLodScale(const refdef_s *refdef)
{
    R_UpdateLodParms(refdef, &rg.correctedLodParms);
}

void __cdecl R_RenderScene(const refdef_s *refdef)
{
    GfxViewParms *dpvs; // [esp+0h] [ebp-E8h]
    GfxSceneParms sceneParms; // [esp+40h] [ebp-A8h] BYREF
    GfxViewParms *viewParmsDraw; // [esp+E4h] [ebp-4h]

    iassert( refdef->tanHalfFovX > 0 );
    iassert( refdef->tanHalfFovY > 0 );
    iassert( refdef->height > 0 );
    iassert( refdef->width > 0 );
    if (refdef->localClientNum >= gfxCfg.maxClientViews)
        MyAssertHandler(
            ".\\r_scene.cpp",
            2635,
            0,
            "refdef->localClientNum doesn't index gfxCfg.maxClientViews\n\t%i not in [0, %i)",
            refdef->localClientNum,
            gfxCfg.maxClientViews);
    if (rg.registered && !r_norefresh->current.enabled)
    {
        PROF_SCOPED("R_RenderScene");

        if (r_logFile->current.integer)
            RB_LogPrint("====== R_RenderScene ======\n");
        if (!rgp.world)
            Com_Error(ERR_DROP, "R_RenderScene: NULL worldmodel");
        rg.viewOrg[0] = refdef->vieworg[0];
        rg.viewOrg[1] = refdef->vieworg[1];
        rg.viewOrg[2] = refdef->vieworg[2];
        rg.viewDir[0] = refdef->viewaxis[0][0];
        rg.viewDir[1] = refdef->viewaxis[0][1];
        rg.viewDir[2] = refdef->viewaxis[0][2];
        viewParmsDraw = R_AllocViewParms();
        R_SetViewParmsForScene(refdef, viewParmsDraw);
        R_SetSceneParms(refdef, &sceneParms);
        R_CorrectLodScale(refdef);
        iassert( scene.def.time == refdef->time );
        iassert( rg.lodParms.valid );
        if (r_lockPvs->current.enabled)
            dpvs = &lockPvsViewParms;
        else
            dpvs = viewParmsDraw;
        R_GenerateSortedDrawSurfs(&sceneParms, dpvs, viewParmsDraw);
    }
}

char __cdecl R_DoesDrawSurfListInfoNeedFloatz(GfxDrawSurfListInfo *emissiveInfo)
{
    const MaterialTechnique *technique; // [esp+38h] [ebp-Ch]
    uint32_t surfIndex; // [esp+3Ch] [ebp-8h]

    PROF_SCOPED("R_DoesDrawSurfListInfoNeedFloatz");

    for (surfIndex = 0; ; ++surfIndex)
    {
        if (surfIndex == emissiveInfo->drawSurfCount)
        {
            return 0;
        }
        technique = Material_GetTechnique(rgp.sortedMaterials[emissiveInfo->drawSurfs[surfIndex].fields.materialSortedIndex], emissiveInfo->baseTechType);
        if (technique)
        {
            if ((technique->flags & 0x20) != 0)
                break;
        }
    }

    if (!gfxRenderTargets[R_RENDERTARGET_FLOAT_Z].surface.color)
        Com_Error(ERR_FATAL, "Renderer attempted to use technique that uses floatz buffer, but it wasn't created.\n");

    return 1;
}

void __cdecl R_GenerateSortedDrawSurfs(
    const GfxSceneParms *sceneParms,
    const GfxViewParms *viewParmsDpvs,
    const GfxViewParms *viewParmsDraw)
{
    MaterialTechniqueType EmissiveTechnique; // eax
    char DoesDrawSurfListInfoNeedFloatz; // al
    float v7; // [esp+30h] [ebp-148h]
    float v8; // [esp+4Ch] [ebp-12Ch]
    float v9; // [esp+70h] [ebp-108h]
    float *viewOrigin; // [esp+BCh] [ebp-BCh]
    float v15; // [esp+114h] [ebp-64h]
    float v16; // [esp+118h] [ebp-60h]
    float v17; // [esp+11Ch] [ebp-5Ch]
    float bestError; // [esp+120h] [ebp-58h]
    uint32_t bestNum; // [esp+124h] [ebp-54h]
    float error; // [esp+128h] [ebp-50h]
    uint32_t num; // [esp+12Ch] [ebp-4Ch]
    uint32_t bestDen; // [esp+130h] [ebp-48h]
    uint32_t den; // [esp+134h] [ebp-44h]
    int pointLightCount; // [esp+138h] [ebp-40h]
    int firstDrawSurfCount; // [esp+13Ch] [ebp-3Ch]
    int cameraCellIndex; // [esp+140h] [ebp-38h]
    GfxDrawSurfListInfo *litInfo; // [esp+144h] [ebp-34h]
    GfxDrawSurfListInfo *decalInfo; // [esp+148h] [ebp-30h]
    GfxDrawSurfListInfo *emissiveInfo; // [esp+14Ch] [ebp-2Ch]
    bool usePreTess; // [esp+153h] [ebp-25h]
    GfxViewInfo *viewInfo; // [esp+154h] [ebp-24h]
    int viewInfoIndex; // [esp+158h] [ebp-20h]
    const GfxLight *visibleLights[4]; // [esp+15Ch] [ebp-1Ch] BYREF
    ShadowType dynamicShadowType; // [esp+16Ch] [ebp-Ch]
    SceneEntCmd sceneEntCmd; // [esp+170h] [ebp-8h] BYREF
    int visibleLightCount; // [esp+174h] [ebp-4h]

    iassert(frontEndDataOut->viewInfoCount == rg.viewInfoCount);
    viewInfoIndex = rg.viewInfoCount++;
    frontEndDataOut->viewInfoCount = rg.viewInfoCount;
    bcassert(viewInfoIndex, GFX_MAX_CLIENT_VIEWS);
    frontEndDataOut->viewInfoIndex = viewInfoIndex;
    viewInfo = &frontEndDataOut->viewInfo[viewInfoIndex];
    dynamicShadowType = R_DynamicShadowType();
    rg.sunShadowFull = r_rendererInUse->current.integer == 1;
    if (rg.sunShadowFull)
    {
        rg.sunShadowmapScale = sm_sunShadowScale->current.value;
        bestDen = 1; // denominator
        bestNum = 1; // numerator
        bestError = 1.0f;

        for (den = 1; den <= 10; ++den)
        {
            num = (uint32_t)floor((float)den * rg.sunShadowmapScale + 0.5f);
            error = I_fabs((float)num / (float)den - rg.sunShadowmapScale);
            if (error < bestError)
            {
                bestError = error;
                bestDen = den;
                bestNum = num;
            }
        }

        rg.sunShadowmapScale = (float)bestNum / (float)bestDen;
        rg.sunShadowmapScaleNum = (float)bestNum;
        rg.sunShadowSize = (uint32_t)ceilf(rg.sunShadowmapScale * 1024.0f);
        rg.sunShadowPartitionRatio = 4.0f / rg.sunShadowmapScale;
    }
    else
    {
        rg.sunShadowSize = 1024;
        rg.sunShadowmapScale = 1.0f;
        rg.sunShadowmapScaleNum = 1.0f;
        rg.sunShadowPartitionRatio = 4.0f;
    }
    rg.drawSunShadow = 0;

    iassert(rg.lodParms.valid);
    iassert(rg.correctedLodParms.valid);

    memcpy(&viewInfo->input, &gfxCmdBufInput, sizeof(viewInfo->input));
    viewInfo->input.data = frontEndDataOut;
    viewInfo->sceneDef = scene.def;
    memcpy(&viewInfo->viewParms, viewParmsDraw, sizeof(viewInfo->viewParms));
    viewInfo->sceneViewport = sceneParms->sceneViewport;
    viewInfo->displayViewport = sceneParms->displayViewport;
    viewInfo->scissorViewport = sceneParms->scissorViewport;
    viewInfo->dynamicShadowType = dynamicShadowType;
    viewInfo->localClientNum = sceneParms->localClientNum;
    viewInfo->isRenderingFullScreen = sceneParms->isRenderingFullScreen;
    viewInfo->blurRadius = sceneParms->blurRadius;
    viewInfo->spotShadowCount = 0;
    viewInfo->needsFloatZ = 0;
    Com_Memcpy((char *)viewInfo->shadowableLights, (char *)sceneParms->primaryLights, rgp.world->primaryLightCount * sizeof(GfxLight));
    viewInfo->shadowableLightCount = rgp.world->primaryLightCount;
    scene.shadowableLightIsUsed[0] = 0;
    scene.shadowableLightIsUsed[1] = 0;
    scene.shadowableLightIsUsed[2] = 0;
    scene.shadowableLightIsUsed[3] = 0;
    scene.shadowableLightIsUsed[4] = 0;
    scene.shadowableLightIsUsed[5] = 0;
    scene.shadowableLightIsUsed[6] = 0;
    scene.shadowableLightIsUsed[7] = 0;
    R_SetupDynamicModelLighting(&viewInfo->input);
    R_SetFrameFog(&viewInfo->input);
    R_SetSunConstants(&viewInfo->input);
    DrawSunDirectionDebug(viewParmsDraw->origin, viewParmsDraw->axis[0]); // LWSS ADD from blops
    R_SetDepthOfField(viewInfo, sceneParms);
    R_SetFilmInfo(viewInfo, sceneParms);
    R_SetGlowInfo(viewInfo, sceneParms);
    R_DrawCineWarning();
    R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_ZNEAR, viewParmsDraw->zNear * 0.984375, 0.0, 0.0, 0.0);
    R_SetFullSceneViewMesh(viewInfoIndex, viewInfo);
    R_SetupWorldSurfacesDpvs(viewParmsDpvs);
    R_SetViewFrustumPlanes(viewInfo);
    cameraCellIndex = R_CellForPoint(rgp.world, viewParmsDpvs->origin);
    KISAK_NULLSUB();
    R_FilterEntitiesIntoCells(cameraCellIndex);
    {
        PROF_SCOPED("R_AddWorldSurfacesDpvs");
        R_AddWorldSurfacesDpvs(viewParmsDpvs, cameraCellIndex);
    }
    R_BeginAllStaticModelLighting();
    {
        PROF_SCOPED("WaitFX");
        R_WaitWorkerCmdsOfType(WRKCMD_FIRST_FRONTEND);
    }
    R_AddEmissiveSpotLight(viewInfo);
    usePreTess = !dx.deviceLost && r_pretess->current.enabled;
    if (usePreTess)
        R_BeginPreTess();
    R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_STATIC);
    {
        PROF_SCOPED("bsp surfaces");
        if (gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD)
        {
            R_AddAllBspDrawSurfacesCamera();
            if (!sm_sunEnable->current.enabled && rgp.world->sunPrimaryLightIndex)
                Com_BitClearAssert(scene.shadowableLightIsUsed, rgp.world->sunPrimaryLightIndex, 128);
            Com_Memset(frontEndDataOut->shadowableLightHasShadowMap, 0, 32);
            if (R_GetAllowShadowMaps())
                R_ChooseShadowedLights(viewInfo);
            R_UpdateDrawMethod(frontEndDataOut, viewInfo);
            if (dynamicShadowType == SHADOW_MAP && Com_BitCheckAssert(frontEndDataOut->shadowableLightHasShadowMap, rgp.world->sunPrimaryLightIndex, 32))
            {
                rg.drawSunShadow = 1;
                R_SetupSunShadowMaps(viewParmsDpvs, &viewInfo->sunShadow);
                R_SetSunShadowConstants(&viewInfo->input, &viewInfo->sunShadow.sunProj); // these constants look perfect
                R_SunShadowMaps();
            }
        }
        else
        {
            R_AddAllBspDrawSurfacesCameraNonlit(rgp.world->dpvs.litSurfsBegin, rgp.world->dpvs.litSurfsEnd, 0);
            R_AddAllBspDrawSurfacesCameraNonlit(rgp.world->dpvs.decalSurfsBegin, rgp.world->dpvs.decalSurfsEnd, 3u);
        }
        R_AddAllBspDrawSurfacesCameraNonlit(rgp.world->dpvs.emissiveSurfsBegin, rgp.world->dpvs.emissiveSurfsEnd, 9u);
    }
    R_AddAllStaticModelSurfacesCamera();
    {
        PROF_SCOPED("DynEntPieces_AddDrawSurfs");
        DynEntPieces_AddDrawSurfs();
    }
    {
        PROF_SCOPED("wait for r_dpvs_dynmodel");
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_DYN_MODEL);
    }
    {
        PROF_SCOPED("wait for r_dpvs_dynbrush");
        R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_DYN_BRUSH);
    }
    {
        PROF_SCOPED("R_DrawAllDynEnt");
        R_DrawAllDynEnt(viewInfo);
    }
    
    if (gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD
        && dynamicShadowType == SHADOW_MAP
        && Com_BitCheckAssert(frontEndDataOut->shadowableLightHasShadowMap, rgp.world->sunPrimaryLightIndex, 32))
    {
        {
            PROF_SCOPED("wait for more r_dpvs_static");
            R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_STATIC);
        }
        {
            PROF_SCOPED("R_AddAllBspDrawSurfacesSunShadow");
            R_AddAllBspDrawSurfacesSunShadow();
        }
        {
            PROF_SCOPED("R_AddAllStaticModelSurfacesSunShadow");
            R_AddAllStaticModelSurfacesSunShadow();
        }
    }
    KISAK_NULLSUB();
    {
        PROF_SCOPED("WaitFX");
        R_WaitWorkerCmdsOfType(WRKCMD_UPDATE_FX_NON_DEPENDENT);
        R_WaitWorkerCmdsOfType(WRKCMD_UPDATE_FX_REMAINING);
    }
    R_WaitWorkerCmdsOfType(WRKCMD_DPVS_CELL_SCENE_ENT);
    R_WaitWorkerCmdsOfType(WRKCMD_DPVS_ENTITY);
    R_WaitWorkerCmdsOfType(WRKCMD_SPOT_SHADOW_ENT);
    KISAK_NULLSUB();
    R_DrawAllSceneEnt(viewInfo);
    R_WaitWorkerCmdsOfType(WRKCMD_SKIN_ENT_DELAYED);
    sceneEntCmd.viewInfo = viewInfo;
    R_AddWorkerCmd<WRKCMD_ADD_SCENE_ENT>(sceneEntCmd);
    R_SortAllStaticModelSurfacesCamera();
    visibleLightCount = R_GetVisibleDLights(visibleLights);
    R_GetLightSurfs(visibleLightCount, visibleLights);
    R_GetPointLightShadowSurfs(viewInfo, scene.visLightShadow, visibleLights);
    viewInfo->shadowCookieList.cookieCount = 0;
    if (gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD)
    {
        if (dynamicShadowType == SHADOW_MAP)
        {
            if (Com_BitCheckAssert(frontEndDataOut->shadowableLightHasShadowMap, rgp.world->sunPrimaryLightIndex, 32))
            {
                KISAK_NULLSUB();
                R_AddAllSceneEntSurfacesSunShadow();
                R_SortAllStaticModelSurfacesSunShadow();
                R_GenerateSortedSunShadowDrawSurfs(viewInfo);
            }
            R_GenerateAllSortedSpotShadowDrawSurfs(viewInfo);
        }
        else if (dynamicShadowType == SHADOW_COOKIE)
        {
            ShadowCookieCmd shadowCookieCmd{};
            shadowCookieCmd.viewParmsDpvs = viewParmsDpvs;
            shadowCookieCmd.viewParmsDraw = viewParmsDraw;
            shadowCookieCmd.shadowCookieList = &viewInfo->shadowCookieList;
            shadowCookieCmd.localClientNum = viewInfo->localClientNum;
            R_AddWorkerCmd<WRKCMD_SHADOW_COOKIE>(shadowCookieCmd);
        }
    }
    R_SetAllStaticModelLighting();
    if (dynamicShadowType == SHADOW_COOKIE)
        R_WaitWorkerCmdsOfType(WRKCMD_SHADOW_COOKIE);
    KISAK_NULLSUB();
    R_EmitShadowCookieSurfs(viewInfo);
    R_WaitWorkerCmdsOfType(WRKCMD_ADD_SCENE_ENT);
    if (usePreTess)
        R_EndPreTess();
    litInfo = &viewInfo->litInfo;
    R_InitDrawSurfListInfo(&viewInfo->litInfo);
    litInfo->baseTechType = gfxDrawMethod.baseTechType;
    litInfo->viewInfo = viewInfo;
    viewOrigin = litInfo->viewOrigin;
    litInfo->viewOrigin[0] = viewParmsDraw->origin[0];
    viewOrigin[1] = viewParmsDraw->origin[1];
    viewOrigin[2] = viewParmsDraw->origin[2];
    viewOrigin[3] = viewParmsDraw->origin[3];
    litInfo->cameraView = 1;
    firstDrawSurfCount = frontEndDataOut->drawSurfCount;
    R_MergeAndEmitDrawSurfLists(DRAW_SURF_CAMERA_LIT_BEGIN, 3);
    litInfo->drawSurfs = &frontEndDataOut->drawSurfs[firstDrawSurfCount];
    litInfo->drawSurfCount = frontEndDataOut->drawSurfCount - firstDrawSurfCount;
    pointLightCount = 0;
    if (r_dlightLimit->current.integer && gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD)
    {
        KISAK_NULLSUB();
        pointLightCount = R_EmitPointLightPartitionSurfs(viewInfo, visibleLights, visibleLightCount, viewParmsDpvs->origin);
    }
    viewInfo->pointLightCount = pointLightCount;
    if (!viewInfo->localClientNum)
        CL_UpdateSound();
    FX_RunPhysics(viewInfo->localClientNum);
    DynEntCl_ProcessEntities(viewInfo->localClientNum);
    R_WaitWorkerCmdsOfType(WRKCMD_GENERATE_MARK_VERTS);
    if (!dx.deviceLost && fx_marks->current.enabled)
    {
        if (fx_marks_smodels->current.enabled)
            FX_GenerateMarkVertsForStaticModels(
                viewInfo->localClientNum,
                rgp.world->dpvs.smodelCount,
                rgp.world->dpvs.smodelVisData[0]);
        if (fx_marks_ents->current.enabled)
            R_GenerateMarkVertsForDynamicModels();
    }
    KISAK_NULLSUB();
    R_SortDrawSurfs(
        scene.drawSurfs[DRAW_SURF_FX_CAMERA_LIT],
        Sys_AtomicLoad(&scene.drawSurfCount[DRAW_SURF_FX_CAMERA_LIT]));
    decalInfo = &viewInfo->decalInfo;
    R_InitDrawSurfListInfo(&viewInfo->decalInfo);
    decalInfo->baseTechType = gfxDrawMethod.baseTechType;
    decalInfo->viewInfo = viewInfo;
    decalInfo->viewOrigin[0] = viewParmsDraw->origin[0];
    decalInfo->viewOrigin[1] = viewParmsDraw->origin[1];
    decalInfo->viewOrigin[2] = viewParmsDraw->origin[2];
    decalInfo->viewOrigin[3] = viewParmsDraw->origin[3];
    decalInfo->cameraView = 1;
    firstDrawSurfCount = frontEndDataOut->drawSurfCount;
    R_MergeAndEmitDrawSurfLists(DRAW_SURF_BSP_CAMERA_DECAL, 6);
    decalInfo->drawSurfs = &frontEndDataOut->drawSurfs[firstDrawSurfCount];
    decalInfo->drawSurfCount = frontEndDataOut->drawSurfCount - firstDrawSurfCount;
    R_WaitWorkerCmdsOfType(WRKCMD_GENERATE_FX_VERTS);
    KISAK_NULLSUB();
    R_SortDrawSurfs(
        scene.drawSurfs[DRAW_SURF_FX_CAMERA_EMISSIVE],
        Sys_AtomicLoad(&scene.drawSurfCount[DRAW_SURF_FX_CAMERA_EMISSIVE]));
    emissiveInfo = &viewInfo->emissiveInfo;
    R_InitDrawSurfListInfo(&viewInfo->emissiveInfo);
    EmissiveTechnique = R_GetEmissiveTechnique(viewInfo, gfxDrawMethod.emissiveTechType);
    emissiveInfo->baseTechType = EmissiveTechnique;
    emissiveInfo->viewInfo = viewInfo;
    emissiveInfo->viewOrigin[0] = viewParmsDraw->origin[0];
    emissiveInfo->viewOrigin[1] = viewParmsDraw->origin[1];
    emissiveInfo->viewOrigin[2] = viewParmsDraw->origin[2];
    emissiveInfo->viewOrigin[3] = viewParmsDraw->origin[3];
    emissiveInfo->cameraView = 1;
    if (viewInfo->emissiveSpotLightCount)
    {
        iassert( viewInfo->emissiveSpotLightCount == 1 );
        emissiveInfo->light = &viewInfo->emissiveSpotLight;
    }
    firstDrawSurfCount = frontEndDataOut->drawSurfCount;
    R_MergeAndEmitDrawSurfLists(DRAW_SURF_CAMERA_EMISSIVE_BEGIN, 6);
    emissiveInfo->drawSurfs = &frontEndDataOut->drawSurfs[firstDrawSurfCount];
    emissiveInfo->drawSurfCount = frontEndDataOut->drawSurfCount - firstDrawSurfCount;
    if (!viewInfo->needsFloatZ)
    {
        DoesDrawSurfListInfoNeedFloatz = R_DoesDrawSurfListInfoNeedFloatz(emissiveInfo);
        viewInfo->needsFloatZ = DoesDrawSurfListInfoNeedFloatz;
    }
    R_ShowCull();
}

// LWSS: not sure why this variable was here, guess from initial development. (no xref's)
//static bool g_allowShadowMaps = true;
bool __cdecl R_GetAllowShadowMaps()
{
    //return sm_enable->current.enabled && g_allowShadowMaps;
    return sm_enable->current.enabled;
}

ShadowType __cdecl R_DynamicShadowType()
{
    if (R_GetAllowShadowMaps())
        return SHADOW_MAP;
    else
        return sc_enable->current.enabled ? SHADOW_COOKIE : SHADOW_NONE;
}

void __cdecl R_SetDepthOfField(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms)
{
    Font_s *font; // [esp+24h] [ebp-4h]

    if (r_dof_tweak->current.enabled)
    {
        if (r_dof_nearBlur->current.value < 4.0)
            MyAssertHandler(
                ".\\r_scene.cpp",
                1151,
                0,
                "%s\n\t(r_dof_nearBlur->current.value) = %g",
                "(r_dof_nearBlur->current.value >= 4.0f)",
                r_dof_nearBlur->current.value);
        viewInfo->dof.viewModelStart = r_dof_viewModelStart->current.value;
        viewInfo->dof.viewModelEnd = r_dof_viewModelEnd->current.value;
        viewInfo->dof.nearStart = r_dof_nearStart->current.value;
        viewInfo->dof.nearEnd = r_dof_nearEnd->current.value;
        viewInfo->dof.farStart = r_dof_farStart->current.value;
        viewInfo->dof.farEnd = r_dof_farEnd->current.value;
        viewInfo->dof.nearBlur = r_dof_nearBlur->current.value;
        viewInfo->dof.farBlur = r_dof_farBlur->current.value;
    }
    else if (r_dof_enable->current.enabled)
    {
        qmemcpy(&viewInfo->dof, &sceneParms->dof, sizeof(viewInfo->dof));
    }
    else
    {
        viewInfo->dof.viewModelStart = 0.0;
        viewInfo->dof.viewModelEnd = 0.0;
        viewInfo->dof.nearStart = 0.0;
        viewInfo->dof.nearEnd = 0.0;
        viewInfo->dof.farStart = 0.0;
        viewInfo->dof.farEnd = 0.0;
        viewInfo->dof.nearBlur = 0.0;
        viewInfo->dof.farBlur = 0.0;
    }
    if (R_UsingDepthOfField(viewInfo))
    {
        if (!gfxRenderTargets[R_RENDERTARGET_FLOAT_Z].surface.color)
            Com_Error(
                ERR_FATAL,
                "Depth of field used (enabled via r_dof_enable or r_dof_tweak) with no float-z buffer (r_floatz wasn't enabled wh"
                "en the renderer was started.)\n");
        viewInfo->needsFloatZ = 1;
        if (com_statmon->current.enabled)
        {
            font = R_RegisterFont("fonts/consoleFont", 1);
            R_AddCmdDrawText((char*)"DOF", 0x7FFFFFFF, font, 0.0, 320.0, 1.5, 2.0, 0.0, colorRedFaded, 0);
        }
    }
}

void __cdecl R_SetFilmInfo(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms)
{
    iassert( viewInfo );
    iassert( sceneParms );

    if (r_filmUseTweaks->current.enabled)
    {
        viewInfo->film.enabled = r_filmTweakEnable->current.enabled;
        viewInfo->film.contrast = r_filmTweakContrast->current.value;
        viewInfo->film.brightness = r_filmTweakBrightness->current.value;
        viewInfo->film.desaturation = r_filmTweakDesaturation->current.value;
        viewInfo->film.invert = r_filmTweakInvert->current.enabled;
        viewInfo->film.tintLight[0] = r_filmTweakLightTint->current.vector[0];
        viewInfo->film.tintLight[1] = r_filmTweakLightTint->current.vector[1];
        viewInfo->film.tintLight[2] = r_filmTweakLightTint->current.vector[2];
        viewInfo->film.tintDark[0] = r_filmTweakDarkTint->current.value;
        viewInfo->film.tintDark[1] = r_filmTweakDarkTint->current.vector[1];
        viewInfo->film.tintDark[2] = r_filmTweakDarkTint->current.vector[2];
    }
    else
    {
        memcpy(&viewInfo->film, &sceneParms->film, sizeof(viewInfo->film));
    }

    float desaturationScale = (1.0 - viewInfo->film.desaturation) * r_desaturation->current.value + viewInfo->film.desaturation;
    viewInfo->film.desaturation = viewInfo->film.desaturation * desaturationScale;
    viewInfo->film.contrast = viewInfo->film.contrast * r_contrast->current.value;
    viewInfo->film.brightness = viewInfo->film.brightness + r_brightness->current.value;

    R_UpdateColorManipulation(viewInfo);
}

void __cdecl R_UpdateColorManipulation(GfxViewInfo *viewInfo)
{
    float v1; // [esp+10h] [ebp-50h]
    float v2; // [esp+14h] [ebp-4Ch]
    float desaturation; // [esp+20h] [ebp-40h]
    float colorTintDelta[4]; // [esp+34h] [ebp-2Ch] BYREF
    float tintScale; // [esp+44h] [ebp-1Ch]
    float colorTintBase[4]; // [esp+48h] [ebp-18h] BYREF
    float desaturationScale; // [esp+58h] [ebp-8h]
    float tintBias; // [esp+5Ch] [ebp-4h]

    iassert( viewInfo );
    if (viewInfo->film.enabled)
    {
        desaturation = viewInfo->film.desaturation;
        v2 = (1.0f / 4096.0f) - desaturation;
        if (v2 < 0.0)
            v1 = desaturation;
        else
            v1 = (1.0f / 4096.0f);
        desaturationScale = 1.0f / v1 - 1.0f;
        tintScale = viewInfo->film.contrast * v1;
        tintBias = viewInfo->film.brightness + 0.5f - viewInfo->film.contrast * 0.5f;
        if (viewInfo->film.invert)
        {
            tintScale = -tintScale;
            tintBias = tintBias + 1.0f;
        }
        Vec3Scale(viewInfo->film.tintDark, tintScale, colorTintBase);
        colorTintBase[3] = 0.0f;
        Vec3Sub(viewInfo->film.tintLight, viewInfo->film.tintDark, colorTintDelta);
        Vec3Scale(colorTintDelta, tintScale, colorTintDelta);
        colorTintDelta[3] = 0.0f;
        R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_COLOR_BIAS, tintBias, tintBias, tintBias, desaturationScale);
        R_SetInputCodeConstantFromVec4(&viewInfo->input, CONST_SRC_CODE_COLOR_TINT_BASE, colorTintBase);
        R_SetInputCodeConstantFromVec4(&viewInfo->input, CONST_SRC_CODE_COLOR_TINT_DELTA, colorTintDelta);
    }
    else
    {
        R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_COLOR_BIAS, 0.0f, 0.0f, 0.0f, 4095.0f);
        R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_COLOR_TINT_BASE, (1.0f / 4096.0f), (1.0f / 4096.0f), (1.0f / 4096.0f), 0.0f);
        R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_COLOR_TINT_DELTA, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

void __cdecl R_SetGlowInfo(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms)
{
    Font_s *font; // [esp+24h] [ebp-10h]
    float bloomCutoffRescale; // [esp+28h] [ebp-Ch]
    float bloomCutoff; // [esp+30h] [ebp-4h]

    iassert( viewInfo );
    iassert( sceneParms );
    if (r_glowUseTweaks->current.enabled)
    {
        viewInfo->glow.enabled = r_glowTweakEnable->current.enabled;
        viewInfo->glow.radius = r_glowTweakRadius->current.value;
        viewInfo->glow.bloomIntensity = r_glowTweakBloomIntensity->current.value;
        viewInfo->glow.bloomCutoff = r_glowTweakBloomCutoff->current.value;
        viewInfo->glow.bloomDesaturation = r_glowTweakBloomDesaturation->current.value;
    }
    else
    {
        viewInfo->glow = sceneParms->glow;
    }
    iassert( viewInfo->glow.bloomIntensity >= 0.0f );
    if (viewInfo->glow.bloomIntensity != 0.0)
    {
        bloomCutoff = viewInfo->glow.bloomCutoff;
        bloomCutoffRescale = 1.0 / (1.0 - bloomCutoff);
        R_SetInputCodeConstant(
            &viewInfo->input,
            CONST_SRC_CODE_GLOW_SETUP,
            bloomCutoff,
            bloomCutoffRescale,
            0.0,
            viewInfo->glow.bloomDesaturation);
        R_SetInputCodeConstant(&viewInfo->input, CONST_SRC_CODE_GLOW_APPLY, 0.0, 0.0, 0.0, viewInfo->glow.bloomIntensity);
    }
    if (R_UsingGlow(viewInfo) && com_statmon->current.enabled)
    {
        font = R_RegisterFont("fonts/consoleFont", 1);
        R_AddCmdDrawText((char*)"GLOW", 0x7FFFFFFF, font, 0.0, 340.0, 1.5, 2.0, 0.0, colorRedFaded, 0);
    }
}

void __cdecl R_SetFullSceneViewMesh(int viewInfoIndex, GfxViewInfo *viewInfo)
{
    float width; // [esp+24h] [ebp-14h]
    float height; // [esp+28h] [ebp-10h]
    GfxQuadMeshData *quadMesh; // [esp+2Ch] [ebp-Ch]
    float x; // [esp+30h] [ebp-8h]
    float y; // [esp+34h] [ebp-4h]

    quadMesh = &gfxMeshGlob.fullSceneViewMesh[viewInfoIndex];
    viewInfo->fullSceneViewMesh = quadMesh;
    x = (float)viewInfo->sceneViewport.x;
    y = (float)viewInfo->sceneViewport.y;
    width = (float)viewInfo->sceneViewport.width;
    height = (float)viewInfo->sceneViewport.height;
    if (x != quadMesh->x || y != quadMesh->y || width != quadMesh->width || height != quadMesh->height)
    {
        R_SyncRenderThread();
        R_SetQuadMesh(quadMesh, x, y, width, height, 0.0, 0.0, 1.0, 1.0, 0xFFFFFFFF);
        if (x != quadMesh->x || y != quadMesh->y || width != quadMesh->width || height != quadMesh->height)
            MyAssertHandler(
                ".\\r_scene.cpp",
                1339,
                1,
                "%s",
                "quadMesh->x == x && quadMesh->y == y && quadMesh->width == width && quadMesh->height == height");
    }
}

void __cdecl R_GenerateSortedSunShadowDrawSurfs(GfxViewInfo *viewInfo)
{
    PROF_SCOPED("GenerateSunShadow");
    R_MergeAndEmitSunShadowMapsSurfs(viewInfo);
}

void __cdecl R_AddEmissiveSpotLight(GfxViewInfo *viewInfo)
{
    bool v1; // [esp+8h] [ebp-8h]

    viewInfo->emissiveSpotLightCount = 0;
    v1 = r_dlightLimit->current.integer && gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD;
    if (v1 && !R_CullDynamicSpotLightInCameraView())
    {
        iassert( scene.addedLightCount > 0 );
        iassert( !scene.isAddedLightCulled[0] );
        //iassert( spotLight->type == GFX_LIGHT_TYPE_SPOT );
        iassert(scene.addedLight[0].type == GFX_LIGHT_TYPE_SPOT);
        memcpy(&viewInfo->emissiveSpotLight, scene.addedLight, sizeof(viewInfo->emissiveSpotLight));
        viewInfo->emissiveSpotLightIndex = 0;
        viewInfo->emissiveSpotDrawSurfCount = 0;
        viewInfo->emissiveSpotDrawSurfs = 0;
        viewInfo->emissiveSpotLightCount = 1;
        if (scene.addedLight[0].canUseShadowMap)
            R_AddDynamicShadowableLight(viewInfo, scene.addedLight);
    }
}

void R_GenerateMarkVertsForDynamicModels()
{
    GfxSceneModel *sceneModel; // [esp+4h] [ebp-24h]
    int dobjIndex; // [esp+8h] [ebp-20h]
    uint8_t reflectionProbeIndex; // [esp+Fh] [ebp-19h]
    uint32_t indexCount; // [esp+10h] [ebp-18h] BYREF
    int brushModelIndex; // [esp+14h] [ebp-14h]
    uint16_t entnum; // [esp+18h] [ebp-10h]
    uint16_t lightHandle; // [esp+1Ch] [ebp-Ch]
    int modelIndex; // [esp+20h] [ebp-8h]
    GfxSceneEntity *sceneEntity; // [esp+24h] [ebp-4h]

    FX_BeginGeneratingMarkVertsForEntModels(scene.dpvs.localClientNum, &indexCount);
    for (dobjIndex = 0; dobjIndex != R_GetSceneDObjCount(); ++dobjIndex)
    {
        sceneEntity = &scene.sceneDObj[dobjIndex];
        entnum = sceneEntity->entnum;
        if (entnum < gfxCfg.entnumOrdinaryEnd && (scene.sceneDObjVisData[0][dobjIndex] & 1) != 0)
        {
            lightHandle = sceneEntity->info.pose->lightingHandle;
            FX_GenerateMarkVertsForEntDObj(
                scene.dpvs.localClientNum,
                entnum,
                &indexCount,
                lightHandle,
                sceneEntity->reflectionProbeIndex,
                sceneEntity->obj,
                sceneEntity->info.pose);
        }
    }
    for (modelIndex = 0; modelIndex != R_GetSceneModelCount(); ++modelIndex)
    {
        sceneModel = &scene.sceneModel[modelIndex];
        entnum = sceneModel->entnum;
        if (entnum < gfxCfg.entnumOrdinaryEnd && (scene.sceneModelVisData[0][modelIndex] & 1) != 0)
        {
            reflectionProbeIndex = sceneModel->reflectionProbeIndex;
            iassert( sceneModel->cachedLightingHandle );
            if (sceneModel->obj)
                FX_GenerateMarkVertsForEntXModel(
                    scene.dpvs.localClientNum,
                    entnum,
                    &indexCount,
                    *sceneModel->cachedLightingHandle,
                    reflectionProbeIndex,
                    &sceneModel->placement);
        }
    }
    for (brushModelIndex = 0; brushModelIndex != R_GetSceneBrushCount(); ++brushModelIndex)
    {
        entnum = scene.sceneBrush[brushModelIndex].entnum;
        if (entnum < gfxCfg.entnumOrdinaryEnd && (scene.sceneBrushVisData[0][brushModelIndex] & 1) != 0)
            FX_GenerateMarkVertsForEntBrush(
                scene.dpvs.localClientNum,
                entnum,
                &indexCount,
                scene.sceneBrush[brushModelIndex].reflectionProbeIndex,
                &scene.sceneBrush[brushModelIndex].placement);
    }
    FX_EndGeneratingMarkVertsForEntModels(scene.dpvs.localClientNum);
}

int __cdecl R_GetVisibleDLights(const GfxLight **visibleLights)
{
    int visibleLightCount; // [esp+4h] [ebp-4h]

    KISAK_NULLSUB();
    visibleLightCount = 0;
    if (r_dlightLimit->current.integer && gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD)
    {
        R_CullDynamicPointLightsInCameraView();
        KISAK_NULLSUB();
        return R_GetPointLightPartitions(visibleLights);
    }
    return visibleLightCount;
}

void __cdecl R_GetLightSurfs(int visibleLightCount, const GfxLight **visibleLights)
{
    if (visibleLightCount)
    {
        PROF_SCOPED("R_GetLightSurfs");
        {
            PROF_SCOPED("light surfs bsp");
            R_GetBspLightSurfs(visibleLights, visibleLightCount);
        }
        {
            PROF_SCOPED("light surfs smodel");
            R_GetStaticModelLightSurfs(visibleLights, visibleLightCount);
        }
        {
            PROF_SCOPED("light surfs scene ent");
            R_GetSceneEntLightSurfs(visibleLights, visibleLightCount);
        }
    }
}

void __cdecl R_GetPointLightShadowSurfs(GfxViewInfo *viewInfo, GfxVisibleLight *visibleLights, const GfxLight **lights)
{
    if (viewInfo->emissiveSpotLightCount)
    {
        iassert( viewInfo->emissiveSpotLightCount == 1 );
        if (viewInfo->emissiveSpotLightIndex >= 4)
            MyAssertHandler(
                ".\\r_scene.cpp",
                1650,
                0,
                "viewInfo->emissiveSpotLightIndex doesn't index MAX_VISIBLE_DLIGHTS\n\t%i not in [0, %i)",
                viewInfo->emissiveSpotLightIndex,
                4);
        iassert( viewInfo->emissiveSpotLightIndex == 0 );
        iassert( lights[0]->type == GFX_LIGHT_TYPE_SPOT );
        viewInfo->emissiveSpotDrawSurfCount = visibleLights->drawSurfCount;
        viewInfo->emissiveSpotDrawSurfs = visibleLights->drawSurfs;
    }
}

MaterialTechniqueType __cdecl R_GetEmissiveTechnique(const GfxViewInfo *viewInfo, MaterialTechniqueType baseTech)
{
    if (baseTech != TECHNIQUE_EMISSIVE)
        return baseTech;
    if (!viewInfo->emissiveSpotLightCount || !r_spotLightShadows->current.enabled)
        return TECHNIQUE_EMISSIVE;
    iassert( comWorld.isInUse );
    if (!Com_BitCheckAssert(frontEndDataOut->shadowableLightHasShadowMap, comWorld.primaryLightCount, 32))
        return TECHNIQUE_EMISSIVE;
    iassert( comWorld.isInUse );
    if (comWorld.primaryLightCount + 1 != viewInfo->shadowableLightCount)
        MyAssertHandler(
            ".\\r_scene.cpp",
            1669,
            0,
            "%s",
            "Com_GetPrimaryLightCount() + GFX_MAX_EMISSIVE_SPOT_LIGHTS == viewInfo->shadowableLightCount");
    return TECHNIQUE_EMISSIVE_SHADOW;
}

void __cdecl R_SetSunShadowConstants(GfxCmdBufInput *input, const GfxSunShadowProjection *sunProj)
{
    R_SetInputCodeConstantFromVec4(input, CONST_SRC_CODE_SHADOWMAP_SWITCH_PARTITION, sunProj->switchPartition);
    R_SetInputCodeConstantFromVec4(input, CONST_SRC_CODE_SHADOWMAP_SCALE, sunProj->shadowmapScale);
}

void __cdecl R_SetSunConstants(GfxCmdBufInput *input)
{
    const GfxLight *sun; // [esp+1Ch] [ebp-10h]
    float specularColor[3]; // [esp+20h] [ebp-Ch] BYREF

    iassert( input->data->sunLight.type == GFX_LIGHT_TYPE_DIR );
    sun = &input->data->sunLight;
    Vec3Scale(input->data->sunLight.color, r_specularColorScale->current.value, specularColor);
    R_SetInputCodeConstantFromVec4(input, CONST_SRC_CODE_SUN_POSITION, sun->dir);
    R_SetInputCodeConstant(input, CONST_SRC_CODE_SUN_DIFFUSE, sun->color[0], sun->color[1], sun->color[2], 1.0);
    R_SetInputCodeConstant(input, CONST_SRC_CODE_SUN_SPECULAR, specularColor[0], specularColor[1], specularColor[2], 1.0);
}

void R_DrawCineWarning()
{
    Font_s *font; // [esp+1Ch] [ebp-8h]
    const char *msg; // [esp+20h] [ebp-4h]

    if (com_statmon->current.enabled && R_Cinematic_IsStarted())
    {
        msg = "CINE";
        font = R_RegisterFont("fonts/consoleFont", 1);
        if (R_Cinematic_IsUnderrun())
            msg = "CINE UNDERRUN!";
        R_AddCmdDrawText((char*)msg, 0x7FFFFFFF, font, 0.0, 360.0, 1.5, 2.0, 0.0, colorRedFaded, 0);
    }
}

void __cdecl R_SetSceneParms(const refdef_s *refdef, GfxSceneParms *sceneParms)
{
    bool v2; // [esp+10h] [ebp-4h]

    iassert( refdef );
    iassert( sceneParms );
    iassert(refdef->blurRadius >= 0.0f);

    sceneParms->localClientNum = refdef->localClientNum;
    sceneParms->blurRadius = refdef->blurRadius;
    memcpy(&sceneParms->dof, &refdef->dof, sizeof(sceneParms->dof));
    memcpy(&sceneParms->film, &refdef->film, 0x40u);
    v2 = refdef->width == vidConfig.displayWidth && refdef->height == vidConfig.displayHeight;
    sceneParms->isRenderingFullScreen = v2;
    sceneParms->displayViewport.x = refdef->x;
    sceneParms->displayViewport.y = refdef->y;
    sceneParms->displayViewport.width = refdef->width;
    sceneParms->displayViewport.height = refdef->height;
    sceneParms->sceneViewport.x = vidConfig.sceneWidth * refdef->x / vidConfig.displayWidth;
    sceneParms->sceneViewport.y = vidConfig.sceneHeight * refdef->y / vidConfig.displayHeight;
    sceneParms->sceneViewport.width = vidConfig.sceneWidth * (refdef->width + refdef->x) / vidConfig.displayWidth
        - sceneParms->sceneViewport.x;
    sceneParms->sceneViewport.height = vidConfig.sceneHeight * (refdef->height + refdef->y) / vidConfig.displayHeight
        - sceneParms->sceneViewport.y;
    if (refdef->useScissorViewport)
    {
        sceneParms->scissorViewport.x = vidConfig.sceneWidth * refdef->scissorViewport.x / vidConfig.displayWidth;
        sceneParms->scissorViewport.y = vidConfig.sceneHeight * refdef->scissorViewport.y / vidConfig.displayHeight;
        sceneParms->scissorViewport.width = vidConfig.sceneWidth
            * (refdef->scissorViewport.width + refdef->scissorViewport.x)
            / vidConfig.displayWidth
            - sceneParms->scissorViewport.x;
        sceneParms->scissorViewport.height = vidConfig.sceneHeight
            * (refdef->scissorViewport.height + refdef->scissorViewport.y)
            / vidConfig.displayHeight
            - sceneParms->scissorViewport.y;
    }
    else
    {
        sceneParms->scissorViewport = sceneParms->sceneViewport;
    }
    sceneParms->primaryLights = refdef->primaryLights;
}

void __cdecl R_LinkDObjEntity(uint32_t localClientNum, uint32_t entnum, float *origin, float radius)
{
    R_FilterDObjIntoCells(localClientNum, entnum, origin, radius);
    R_LinkSphereEntityToPrimaryLights(localClientNum, entnum, origin, radius);
}

void __cdecl R_LinkBModelEntity(uint32_t localClientNum, uint32_t entnum, GfxBrushModel *bmodel)
{
    R_FilterBModelIntoCells(localClientNum, entnum, bmodel);
    R_LinkBoxEntityToPrimaryLights(localClientNum, entnum, bmodel->writable.mins, bmodel->writable.maxs);
}

void __cdecl R_UnlinkEntity(uint32_t localClientNum, uint32_t entnum)
{
    R_UnfilterEntFromCells(localClientNum, entnum);
    R_UnlinkEntityFromPrimaryLights(localClientNum, entnum);
}

void __cdecl R_LinkDynEnt(uint32_t dynEntId, DynEntityDrawType drawType, float *mins, float *maxs)
{
    R_FilterDynEntIntoCells(dynEntId, drawType, mins, maxs);
    R_LinkDynEntToPrimaryLights(dynEntId, drawType, mins, maxs);
}

void __cdecl R_UnlinkDynEnt(uint32_t dynEntId, DynEntityDrawType drawType)
{
    R_UnfilterDynEntFromCells(dynEntId, drawType);
    R_UnlinkDynEntFromPrimaryLights(dynEntId, drawType);
}

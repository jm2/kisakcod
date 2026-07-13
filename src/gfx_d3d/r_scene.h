#pragma once

#include <xanim/xanim.h>
#include <universal/sys_atomic.h>

#include "r_init.h"
#include "r_rendercmds.h"

#define GFX_MAX_CLIENT_VIEWS 4
#define MTL_SORT_ENVMAP_BITS 8

struct GfxSceneParms // sizeof=0xA0
{                                       // ...
    int localClientNum;
    float blurRadius;
    GfxDepthOfField dof;
    GfxFilm film;
    GfxGlow glow;
    bool isRenderingFullScreen;
    // padding byte
    // padding byte
    // padding byte
    GfxViewport sceneViewport;
    GfxViewport displayViewport;
    GfxViewport scissorViewport;
    const GfxLight *primaryLights;
};

struct SceneEntCmd // sizeof=0x4
{                                       // ...
    const GfxViewInfo *viewInfo;        // ...
};
RUNTIME_SIZE(SceneEntCmd, 0x4, 0x8);

struct BModelSurface // sizeof=0x8
{
    const GfxScaledPlacement *placement;
    const GfxSurface *surf;
};
RUNTIME_SIZE(BModelSurface, 0x8, 0x10);

struct GfxSkinnedXModelSurfs // sizeof=0x8/0x10
{                                       // ...
    void *firstSurf;
    uint16_t wordCount;
    uint16_t surfCount;
};
RUNTIME_SIZE(GfxSkinnedXModelSurfs, 0x8, 0x10);
RUNTIME_OFFSET(GfxSkinnedXModelSurfs, wordCount, 0x4, 0x8);
RUNTIME_OFFSET(GfxSkinnedXModelSurfs, surfCount, 0x6, 0xA);
struct GfxSceneEntityCull // sizeof=0x44/0x50
{                                       // ...
    volatile uint32_t state;
    float mins[3];
    float maxs[3];
    int8_t lods[32];
    GfxSkinnedXModelSurfs skinnedSurfs;
};
RUNTIME_SIZE(GfxSceneEntityCull, 0x44, 0x50);
RUNTIME_OFFSET(GfxSceneEntityCull, skinnedSurfs, 0x3C, 0x40);

struct cpose_t;
struct GfxModelSkinnedSurface;

union GfxSceneEntityInfo // sizeof=0x4
{                                       // ...
    cpose_t *pose;
    uint16_t *cachedLightingHandle;
};
struct GfxSceneEntity // sizeof=0x80/0xA0 (SP/MP same)
{                                       // ...
    float lightingOrigin[3];
    GfxScaledPlacement placement;
    GfxSceneEntityCull cull;
    uint16_t gfxEntIndex;
    uint16_t entnum;
    const DObj_s *obj;
    GfxSceneEntityInfo info;
    uint8_t reflectionProbeIndex;
    // padding byte
    // padding byte
    // padding byte
};
RUNTIME_SIZE(GfxSceneEntity, 0x80, 0xA0);
RUNTIME_OFFSET(GfxSceneEntity, cull, 0x2C, 0x30);
RUNTIME_OFFSET(GfxSceneEntity, gfxEntIndex, 0x70, 0x80);
RUNTIME_OFFSET(GfxSceneEntity, obj, 0x74, 0x88);
RUNTIME_OFFSET(GfxSceneEntity, info, 0x78, 0x90);
RUNTIME_OFFSET(GfxSceneEntity, reflectionProbeIndex, 0x7C, 0x98);

inline uint32_t R_LoadSceneEntityCullState(
    const GfxSceneEntity *const sceneEnt) noexcept
{
    return sceneEnt ? Sys_AtomicLoad(&sceneEnt->cull.state) : 4u;
}

inline void R_StoreSceneEntityCullState(
    GfxSceneEntity *const sceneEnt,
    const uint32_t state) noexcept
{
    if (sceneEnt)
        Sys_AtomicStore(&sceneEnt->cull.state, state);
}

struct GfxVisibleLight // sizeof=0x2008
{                                       // ...
    int drawSurfCount;                  // ...
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    GfxDrawSurf drawSurfs[1024];        // ...
};

struct GfxShadowCookie // sizeof=0x868
{                                       // ...
    DpvsPlane planes[5];                // ...
    volatile int drawSurfCount;         // ...
    GfxDrawSurf drawSurfs[256];         // ...
};

struct GfxSceneModel // sizeof=0x48
{                                       // ...
    XModelDrawInfo info;
    const XModel *model;
    const DObj_s *obj;
    GfxScaledPlacement placement;
    uint16_t gfxEntIndex;
    uint16_t entnum;
    float radius;
    uint16_t *cachedLightingHandle;
    float lightingOrigin[3];
    uint8_t reflectionProbeIndex;
    // padding byte
    // padding byte
    // padding byte
};
struct GfxSceneBrush // sizeof=0x28
{                                       // ...
    BModelDrawInfo info;
    uint16_t entnum;
    const GfxBrushModel *bmodel;
    GfxPlacement placement;
    uint8_t reflectionProbeIndex;
    // padding byte
    // padding byte
    // padding byte
};

union GfxEntCellRefInfo // sizeof=0x4
{                                       // ...
    float radius;
    GfxBrushModel *bmodel;
};

struct GfxSceneDpvs // sizeof=0x38
{                                       // ...
    uint32_t localClientNum;        // ...
    uint8_t *entVisData[7];     // ...
    uint16_t *sceneXModelIndex; // ...
    uint16_t *sceneDObjIndex;   // ...
    GfxEntCellRefInfo *entInfo[4];      // ...
};

struct __declspec(align(64)) GfxScene // sizeof=0x154D00
{                                       // ...
    GfxDrawSurf bspDrawSurfs[8192];
    GfxDrawSurf smodelDrawSurfsLight[8192]; // ...
    GfxDrawSurf entDrawSurfsLight[8192]; // ...
    GfxDrawSurf bspDrawSurfsDecal[512]; // ...
    GfxDrawSurf smodelDrawSurfsDecal[512]; // ...
    GfxDrawSurf entDrawSurfsDecal[512]; // ...
    GfxDrawSurf bspDrawSurfsEmissive[8192]; // ...
    GfxDrawSurf smodelDrawSurfsEmissive[8192]; // ...
    GfxDrawSurf entDrawSurfsEmissive[8192]; // ...
    GfxDrawSurf fxDrawSurfsEmissive[8192]; // ...
    GfxDrawSurf fxDrawSurfsEmissiveAuto[8192]; // ...
    GfxDrawSurf fxDrawSurfsEmissiveDecal[8192]; // ...
    GfxDrawSurf bspSunShadowDrawSurfs0[4096]; // ...
    GfxDrawSurf smodelSunShadowDrawSurfs0[4096]; // ...
    GfxDrawSurf entSunShadowDrawSurfs0[4096]; // ...
    GfxDrawSurf bspSunShadowDrawSurfs1[8192]; // ...
    GfxDrawSurf smodelSunShadowDrawSurfs1[8192]; // ...
    GfxDrawSurf entSunShadowDrawSurfs1[8192]; // ...
    GfxDrawSurf bspSpotShadowDrawSurfs0[256]; // ...
    GfxDrawSurf smodelSpotShadowDrawSurfs0[256]; // ...
    GfxDrawSurf entSpotShadowDrawSurfs0[512]; // ...
    GfxDrawSurf bspSpotShadowDrawSurfs1[256]; // ...
    GfxDrawSurf smodelSpotShadowDrawSurfs1[256]; // ...
    GfxDrawSurf entSpotShadowDrawSurfs1[512]; // ...
    GfxDrawSurf bspSpotShadowDrawSurfs2[256]; // ...
    GfxDrawSurf smodelSpotShadowDrawSurfs2[256]; // ...
    GfxDrawSurf entSpotShadowDrawSurfs2[512]; // ...
    GfxDrawSurf bspSpotShadowDrawSurfs3[256]; // ...
    GfxDrawSurf smodelSpotShadowDrawSurfs3[256]; // ...
    GfxDrawSurf entSpotShadowDrawSurfs3[512]; // ...
    GfxDrawSurf shadowDrawSurfs[512];   // ...
    uint32_t shadowableLightIsUsed[32]; // ...
    int maxDrawSurfCount[34];           // DRAW_SURF_TYPE_COUNT
    volatile uint32_t drawSurfCount[34]; // DRAW_SURF_TYPE_COUNT
    GfxDrawSurf *drawSurfs[34];         // DRAW_SURF_TYPE_COUNT
    GfxDrawSurf fxDrawSurfsLight[8192]; // ...
    GfxDrawSurf fxDrawSurfsLightAuto[8192]; // ...
    GfxDrawSurf fxDrawSurfsLightDecal[8192]; // ...
    GfxSceneDef def;                    // ...
    int addedLightCount;                // ...
    GfxLight addedLight[32];            // ...
    bool isAddedLightCulled[32];        // ...
    float dynamicSpotLightNearPlaneOffset; // ...
    // padding byte
    // padding byte
    // padding byte
    // padding byte
    GfxVisibleLight visLight[4];        // ...
    GfxVisibleLight visLightShadow[1];  // ...
    GfxShadowCookie cookie[24];         // ...
    uint32_t *entOverflowedDrawBuf; // ...
    volatile uint32_t sceneDObjCount;        // ...
    GfxSceneEntity sceneDObj[512];      // ...
    uint8_t sceneDObjVisData[7][512]; // ...
    volatile uint32_t sceneModelCount;       // ...
    GfxSceneModel sceneModel[1024];     // ...
    uint8_t sceneModelVisData[7][1024]; // ...
    volatile uint32_t sceneBrushCount;       // ...
    GfxSceneBrush sceneBrush[512];      // ...
    uint8_t sceneBrushVisData[3][512]; // ...
    uint32_t sceneDynModelCount;    // ...
    uint32_t sceneDynBrushCount;    // ...
    DpvsPlane shadowFarPlane[2];        // ...
    DpvsPlane shadowNearPlane[2];       // ...
    GfxSceneDpvs dpvs;                  // ...
};

static_assert(
    offsetof(GfxScene, sceneDObjCount) % alignof(uint32_t) == 0u,
    "GfxScene DObj counter lost atomic alignment");
static_assert(
    offsetof(GfxScene, sceneModelCount) % alignof(uint32_t) == 0u,
    "GfxScene model counter lost atomic alignment");
static_assert(
    offsetof(GfxScene, sceneBrushCount) % alignof(uint32_t) == 0u,
    "GfxScene brush counter lost atomic alignment");

void __cdecl TRACK_r_scene();
uint32_t __cdecl R_AllocSceneDObj();
uint32_t __cdecl R_AllocSceneModel();
uint32_t __cdecl R_AllocSceneBrush();
GfxBrushModel *__cdecl R_GetBrushModel(uint32_t modelIndex);
void __cdecl R_AddBrushModelToSceneFromAngles(
    const GfxBrushModel *bmodel,
    const float *origin,
    const float *angles,
    uint16_t entnum);
void __cdecl R_AddDObjToScene(
    const DObj_s *obj,
    cpose_t *pose,
    uint32_t entnum,
    uint32_t renderFxFlags,
    float *lightingOrigin,
    float materialTime);
GfxParticleCloud *__cdecl R_AddParticleCloudToScene(Material *material);
void __cdecl R_AddOmniLightToScene(const float *org, float radius, float r, float g, float b);
void __cdecl R_AddSpotLightToScene(const float *org, const float *dir, float radius, float r, float g, float b);
double __cdecl R_GetDefaultNearClip();
void __cdecl R_SetupViewProjectionMatrices(GfxViewParms *viewParms);
void __cdecl R_AddBModelSurfacesCamera(
    BModelDrawInfo *bmodelInfo,
    const GfxBrushModel *bmodel,
    GfxDrawSurf **drawSurfs,
    GfxDrawSurf **lastDrawSurfs,
    uint32_t reflectionProbeIndex);
GfxDrawSurf *__cdecl R_AddBModelSurfaces(
    BModelDrawInfo *bmodelInfo,
    const GfxBrushModel *bmodel,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf);
const XSurface *__cdecl R_GetXSurface(
    const GfxModelSkinnedSurface *modelSurf,
    surfaceType_t surfType);
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
    uint32_t reflectionProbeIndex);
void __cdecl R_AddXModelDebugString(const float *origin, char *string);
GfxDrawSurf *__cdecl R_AddXModelSurfaces(
    XModelDrawInfo *modelInfo,
    const XModel *model,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf);
void __cdecl R_AddDObjSurfacesCamera(
    GfxSceneEntity *sceneEnt,
    __int16 lightingHandle,
    uint8_t primaryLightIndex,
    GfxDrawSurf **drawSurfs,
    GfxDrawSurf **lastDrawSurfs);
GfxDrawSurf *__cdecl R_AddDObjSurfaces(
    GfxSceneEntity *sceneEnt,
    MaterialTechniqueType techType,
    GfxDrawSurf *drawSurf,
    GfxDrawSurf *lastDrawSurf);
bool __cdecl R_EndFencePending();
void __cdecl R_SetEndTime(int endTime);
void __cdecl R_WaitEndTime();
void __cdecl R_InitScene();
void __cdecl R_ClearScene(uint32_t localClientNum);
uint32_t __cdecl R_GetLocalClientNum();
void __cdecl R_SetLodOrigin(const refdef_s *refdef);
void R_UpdateFrameFog();
uint8_t __cdecl LerpByte(uint8_t from, uint8_t to, float frac);
void __cdecl R_SetViewParmsForScene(const refdef_s *refdef, GfxViewParms *viewParms);
void __cdecl R_SetupProjection(float tanHalfFovX, float tanHalfFovY, GfxViewParms *viewParms);
bool R_UpdateFrameSun();
void __cdecl R_LerpDir(
    const float *dirBegin,
    const float *dirEnd,
    int beginLerpTime,
    int endLerpTime,
    int currTime,
    float *result);
void __cdecl R_UpdateLodParms(const refdef_s *refdef, GfxLodParms *lodParms);
void __cdecl R_CorrectLodScale(const refdef_s *refdef);
void __cdecl R_RenderScene(const refdef_s *refdef);
void __cdecl R_GenerateSortedDrawSurfs(
    const GfxSceneParms *sceneParms,
    const GfxViewParms *viewParmsDpvs,
    const GfxViewParms *viewParmsDraw);
bool __cdecl R_GetAllowShadowMaps();
ShadowType __cdecl R_DynamicShadowType();
void __cdecl R_SetDepthOfField(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms);
void __cdecl R_SetFilmInfo(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms);
void __cdecl R_UpdateColorManipulation(GfxViewInfo *viewInfo);
void __cdecl R_SetGlowInfo(GfxViewInfo *viewInfo, const GfxSceneParms *sceneParms);
void __cdecl R_SetFullSceneViewMesh(int viewInfoIndex, GfxViewInfo *viewInfo);
void __cdecl R_GenerateSortedSunShadowDrawSurfs(GfxViewInfo *viewInfo);
void __cdecl R_AddEmissiveSpotLight(GfxViewInfo *viewInfo);
void R_GenerateMarkVertsForDynamicModels();
int __cdecl R_GetVisibleDLights(const GfxLight **visibleLights);
void __cdecl R_GetLightSurfs(int visibleLightCount, const GfxLight **visibleLights);
void __cdecl R_GetPointLightShadowSurfs(GfxViewInfo *viewInfo, GfxVisibleLight *visibleLights, const GfxLight **lights);
MaterialTechniqueType __cdecl R_GetEmissiveTechnique(const GfxViewInfo *viewInfo, MaterialTechniqueType baseTech);
void __cdecl R_SetSunShadowConstants(GfxCmdBufInput *input, const GfxSunShadowProjection *sunProj);
void __cdecl R_SetSunConstants(GfxCmdBufInput *input);
void R_DrawCineWarning();
void __cdecl R_SetSceneParms(const refdef_s *refdef, GfxSceneParms *sceneParms);
void __cdecl R_LinkDObjEntity(uint32_t localClientNum, uint32_t entnum, float *origin, float radius);
void __cdecl R_LinkBModelEntity(uint32_t localClientNum, uint32_t entnum, GfxBrushModel *bmodel);
void __cdecl R_UnlinkEntity(uint32_t localClientNum, uint32_t entnum);
void __cdecl R_LinkDynEnt(uint32_t dynEntId, DynEntityDrawType drawType, float *mins, float *maxs);
void __cdecl R_UnlinkDynEnt(uint32_t dynEntId, DynEntityDrawType drawType);


extern GfxScene scene;

inline uint32_t R_GetSceneDObjCount() noexcept
{
    return Sys_AtomicLoad(&scene.sceneDObjCount);
}

inline uint32_t R_GetSceneModelCount() noexcept
{
    return Sys_AtomicLoad(&scene.sceneModelCount);
}

inline uint32_t R_GetSceneBrushCount() noexcept
{
    return Sys_AtomicLoad(&scene.sceneBrushCount);
}

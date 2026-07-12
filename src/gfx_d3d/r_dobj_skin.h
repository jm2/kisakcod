#pragma once
#include <universal/kisak_abi.h>
#include "r_scene.h"

struct GfxModelSurfaceInfo // sizeof=0xC
{                                       // ...
    const struct DObjAnimMat *baseMat;
    uint8_t boneIndex;
    uint8_t boneCount;
    uint16_t gfxEntIndex;
    uint16_t lightingHandle;
    // padding byte
    // padding byte
};

struct GfxModelSkinnedSurface // sizeof=0x18
{                                       // ...
    int skinnedCachedOffset;
    XSurface *xsurf;
    GfxModelSurfaceInfo info;
    //$B667868682928995E3CB40CE466D3989 ___u3;
    union
    {
        GfxPackedVertex *skinnedVert;
        int oldSkinnedCachedOffset;
    };
};
static_assert(sizeof(GfxModelSkinnedSurface) == 24);

struct GfxModelRigidSurface // sizeof=0x38
{
    GfxModelSkinnedSurface surf;
    GfxScaledPlacement placement;
};
static_assert(sizeof(GfxModelRigidSurface) == 56);

struct SkinXModelCmd // sizeof=0x1C
{                                       // ...
    void *modelSurfs;
    const DObjAnimMat *mat;
    int surfacePartBits[4];
    uint16_t surfCount;
    // padding byte
    // padding byte
};
RUNTIME_SIZE(SkinXModelCmd, 0x1C, 0x28);
RUNTIME_OFFSET(SkinXModelCmd, mat, 0x4, 0x8);
RUNTIME_OFFSET(SkinXModelCmd, surfacePartBits, 0x8, 0x10);
RUNTIME_OFFSET(SkinXModelCmd, surfCount, 0x18, 0x20);

int __cdecl DObjBad(const DObj_s *obj);
void __cdecl R_SkinSceneDObj(
    GfxSceneEntity *sceneEnt,
    GfxSceneEntity *localSceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix,
    int waitForCullState);
int  R_SkinSceneDObjModels(
    GfxSceneEntity *sceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix);
void __cdecl R_SkinGfxEntityCmd(GfxSceneEntity **data);

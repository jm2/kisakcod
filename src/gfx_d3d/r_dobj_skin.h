#pragma once
#include <cstdint>

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
RUNTIME_SIZE(GfxModelSurfaceInfo, 0xC, 0x10);
RUNTIME_OFFSET(GfxModelSurfaceInfo, baseMat, 0x0, 0x0);
RUNTIME_OFFSET(GfxModelSurfaceInfo, boneIndex, 0x4, 0x8);
RUNTIME_OFFSET(GfxModelSurfaceInfo, boneCount, 0x5, 0x9);
RUNTIME_OFFSET(GfxModelSurfaceInfo, gfxEntIndex, 0x6, 0xA);
RUNTIME_OFFSET(GfxModelSurfaceInfo, lightingHandle, 0x8, 0xC);

enum class GfxModelSurfaceTag : std::int32_t
{
    Hidden = -3,
    Rigid = -2,
    UncachedSkinned = -1,
};

// Hidden records carry only a tag.  Pointer-bearing records that follow them
// still need natural alignment on 64-bit targets, so the native hidden stride
// expands from one dword to two without changing the Win32 stream.
struct alignas(void *) GfxModelHiddenSurface
{
    std::int32_t tag;
};
RUNTIME_SIZE(GfxModelHiddenSurface, 0x4, 0x8);

struct GfxModelSkinnedSurface // sizeof=0x18
{                                       // ...
    std::int32_t skinnedCachedOffset;
    XSurface *xsurf;
    GfxModelSurfaceInfo info;
    //$B667868682928995E3CB40CE466D3989 ___u3;
    union
    {
        GfxPackedVertex *skinnedVert;
        std::uint32_t oldSkinnedCachedOffset;
    };
};
RUNTIME_SIZE(GfxModelSkinnedSurface, 0x18, 0x28);
RUNTIME_OFFSET(GfxModelSkinnedSurface, skinnedCachedOffset, 0x0, 0x0);
RUNTIME_OFFSET(GfxModelSkinnedSurface, xsurf, 0x4, 0x8);
RUNTIME_OFFSET(GfxModelSkinnedSurface, info, 0x8, 0x10);
RUNTIME_OFFSET(GfxModelSkinnedSurface, skinnedVert, 0x14, 0x20);

struct GfxModelRigidSurface // sizeof=0x38
{
    GfxModelSkinnedSurface surf;
    GfxScaledPlacement placement;
};
RUNTIME_SIZE(GfxModelRigidSurface, 0x38, 0x48);
RUNTIME_OFFSET(GfxModelRigidSurface, placement, 0x18, 0x28);

struct SkinXModelCmd // sizeof=0x1C
{                                       // ...
    void *modelSurfs;
    const DObjAnimMat *mat;
    uint32_t surfacePartBits[4];
    uint16_t surfCount;
    // Number of dwords in the bounded heterogeneous record stream.  The
    // 0x20000-byte arena fits in 16 bits when represented this way, and this
    // consumes the retail command's two trailing padding bytes.
    uint16_t modelSurfWordCount;
};
RUNTIME_SIZE(SkinXModelCmd, 0x1C, 0x28);
RUNTIME_OFFSET(SkinXModelCmd, mat, 0x4, 0x8);
RUNTIME_OFFSET(SkinXModelCmd, surfacePartBits, 0x8, 0x10);
RUNTIME_OFFSET(SkinXModelCmd, surfCount, 0x18, 0x20);
RUNTIME_OFFSET(SkinXModelCmd, modelSurfWordCount, 0x1A, 0x22);

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

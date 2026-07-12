#include "xmodel.h"
#include "xanim.h"

struct TestLod // sizeof=0x8
{                                       // ...
    bool enabled;                       // ...
    // padding byte
    // padding byte
    // padding byte
    float dist;                         // ...
};
TestLod g_testLods[4];

const char *__cdecl XModelGetName(const XModel *model)
{
    iassert(model);
    return model->name;
}

int __cdecl XModelGetSurfaces(const XModel *model, XSurface **surfaces, int lod)
{
    iassert(model);
    iassert(surfaces);
    iassert(lod >= 0);
    if (!surfaces)
        return 0;
    *surfaces = nullptr;
    if (!model || lod < 0 || lod >= MAX_LODS || lod >= model->numLods)
        return 0;

    const XModelLodInfo &lodInfo = model->lodInfo[lod];
    bcassert(lodInfo.surfIndex, model->numsurfs);
    if (lodInfo.surfIndex > model->numsurfs
        || lodInfo.numsurfs > model->numsurfs - lodInfo.surfIndex
        || !lodInfo.numsurfs
        || !model->surfs)
    {
        return 0;
    }

    *surfaces = &model->surfs[lodInfo.surfIndex];
    return lodInfo.numsurfs;
}

XSurface *__cdecl XModelGetSurface(const XModel *model, int lod, int surfIndex)
{
    iassert(lod >= 0);
    XSurface *surfaces = nullptr;
    const int surfaceCount = XModelGetSurfaces(model, &surfaces, lod);
    if (!surfaces || surfIndex < 0 || surfIndex >= surfaceCount)
        return nullptr;
    return &surfaces[surfIndex];
}

const XModelLodInfo *__cdecl XModelGetLodInfo(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    if (!model || lod < 0 || lod >= MAX_LODS || lod >= model->numLods)
        return nullptr;
    return &model->lodInfo[lod];
}

uint32_t __cdecl XModelGetSurfCount(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    if (!model || lod < 0 || lod >= MAX_LODS || lod >= model->numLods)
        return 0;
    const XModelLodInfo &lodInfo = model->lodInfo[lod];
    return lodInfo.surfIndex <= model->numsurfs
            && lodInfo.numsurfs <= model->numsurfs - lodInfo.surfIndex
        ? lodInfo.numsurfs
        : 0u;
}

Material **__cdecl XModelGetSkins(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    if (!model || !model->materialHandles || lod < 0
        || lod >= MAX_LODS || lod >= model->numLods)
    {
        return nullptr;
    }
    const XModelLodInfo &lodInfo = model->lodInfo[lod];
    if (lodInfo.surfIndex > model->numsurfs
        || lodInfo.numsurfs > model->numsurfs - lodInfo.surfIndex)
    {
        return nullptr;
    }
    return &model->materialHandles[lodInfo.surfIndex];
}

XModelLodRampType __cdecl XModelGetLodRampType(const XModel *model)
{
    return model && model->lodRampType < XMODEL_LOD_RAMP_COUNT
        ? static_cast<XModelLodRampType>(model->lodRampType)
        : XMODEL_LOD_RAMP_RIGID;
}

int __cdecl XModelGetNumLods(const XModel *model)
{
    return model && model->numLods > 0 && model->numLods <= MAX_LODS
        ? model->numLods
        : 0;
}

double __cdecl XModelGetLodOutDist(const XModel *model)
{
    return *((float *)&model->parentList + 7 * XModelGetNumLods(model));
}

int __cdecl XModelNumBones(const XModel *model)
{
    iassert(model);

    return model->numBones;
}

const DObjAnimMat *__cdecl XModelGetBasePose(const XModel *model)
{
    return model->baseMat;
}

int __cdecl XModelGetLodForDist(const XModel *model, float dist)
{
    float v3; // [esp+0h] [ebp-14h]
    int lodIndex; // [esp+4h] [ebp-10h]
    int lodCount; // [esp+Ch] [ebp-8h]

    if (!model)
        return -1;
    lodCount = XModelGetNumLods(model);
    for (lodIndex = 0; lodIndex < lodCount; ++lodIndex)
    {
        if (g_testLods[lodIndex].enabled)
            v3 = g_testLods[lodIndex].dist;
        else
            v3 = model->lodInfo[lodIndex].dist;
        if (v3 == 0.0 || v3 > (double)dist)
            return lodIndex;
    }
    return -1;
}

void __cdecl XModelSetTestLods(uint32_t lodLevel, float dist)
{
    iassert((unsigned)lodLevel < MAX_LODS);

    g_testLods[lodLevel].dist = dist;
    g_testLods[lodLevel].enabled = dist >= 0.0;
}

double __cdecl XModelGetLodDist(const XModel *model, uint32_t lod)
{
    iassert(model);
    bcassert(lod, model->numLods);

    return model->lodInfo[lod].dist;
}

int __cdecl XModelGetContents(const XModel *model)
{
    iassert(model);

    return model->contents;
}

int __cdecl XModelGetStaticModelCacheVertCount(XModel *model, uint32_t lod)
{
    iassert(model);
    bcassert(lod, MAX_LODS);
    iassert(model->lodInfo[lod].smcIndexPlusOne != 0);

    return 1 << model->lodInfo[lod].smcAllocBits;
}

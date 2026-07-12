#include "dobj_utils.h"
#include "xmodel.h"
#include <qcommon/sys_time.h>
#include <universal/assertive.h>
#include <universal/sys_atomic.h>
#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/r_utils.h>
#endif
#include <universal/profile.h>

#include <bit>
#include <cmath>
#include <limits>

namespace
{
bool DObjModelPoseContractValid(const XModel *const model)
{
    if (!model || !model->numBones
        || model->numRootBones > model->numBones
        || model->numLods <= 0 || model->numLods > MAX_LODS)
    {
        return false;
    }
    if (!model->boneNames || !model->partClassification
        || !model->baseMat || !model->boneInfo)
    {
        return false;
    }

    constexpr uint32_t kHitLocationCount = 0x13u;
    for (uint32_t bone = 0u; bone < model->numBones; ++bone)
    {
        if (model->partClassification[bone] >= kHitLocationCount)
            return false;
    }

    if (model->numBones == model->numRootBones)
        return true;
    if (!model->parentList || !model->quats || !model->trans)
        return false;
    const uint32_t nonRootBoneCount =
        model->numBones - model->numRootBones;
    for (uint32_t child = 0u; child < nonRootBoneCount; ++child)
    {
        const uint32_t boneIndex = model->numRootBones + child;
        const uint32_t parentOffset = model->parentList[child];
        if (!parentOffset || parentOffset > boneIndex)
            return false;
    }
    return true;
}

bool DObjPartBitsWithinModel(
    const int (&partBits)[4],
    const uint32_t modelBoneCount)
{
    if (!modelBoneCount || modelBoneCount > DOBJ_MAX_PARTS)
        return false;
    for (uint32_t bone = modelBoneCount; bone < DOBJ_MAX_PARTS; ++bone)
    {
        if ((static_cast<uint32_t>(partBits[bone >> 5])
                & (UINT32_C(0x80000000) >> (bone & 31u))) != 0u)
        {
            return false;
        }
    }
    return true;
}
} // namespace

static double __cdecl DObj_GetBaseLodDist(const float *origin)
{
#ifdef KISAK_DEDI_HEADLESS
    (void)origin;
    return 0.0;
#else
    return R_GetBaseLodDist(origin);
#endif
}

static double __cdecl DObj_GetAdjustedLodDist(float dist, XModelLodRampType lodRampType)
{
#ifdef KISAK_DEDI_HEADLESS
    (void)lodRampType;
    return dist;
#else
    return R_GetAdjustedLodDist(dist, lodRampType);
#endif
}

DObjAnimMat *__cdecl DObjGetRotTransArray(const DObj_s *obj)
{
    iassert(obj);
    return obj->skel.mat;
}

int __cdecl DObjGetNumModels(const DObj_s *obj)
{
    return obj->numModels;
}

int __cdecl DObjGetSurfaces(const DObj_s *obj, int *partBits, const int8_t *lods)
{
    if (!partBits)
        return 0;
    memset(partBits, 0, 4u * sizeof(partBits[0]));
    if (!obj || !lods || !obj->models || !obj->numModels
        || obj->numModels > DOBJ_MAX_SUBMODELS)
    {
        return 0;
    }

    uint32_t accumulatedBits[4] = {};
    uint32_t boneIndex = 0u;
    uint32_t surfaceCount = 0u;
    for (uint32_t modelIndex = 0u; modelIndex < obj->numModels; ++modelIndex)
    {
        const XModel *const model = obj->models[modelIndex];
        if (!DObjModelPoseContractValid(model)
            || boneIndex >= DOBJ_MAX_PARTS
            || model->numBones > DOBJ_MAX_PARTS - boneIndex)
        {
            return 0;
        }

        const int lod = lods[modelIndex];
        if (lod >= 0)
        {
            if (lod >= MAX_LODS || lod >= model->numLods)
                return 0;
            const XModelLodInfo &lodInfo = model->lodInfo[lod];
            if (!lodInfo.numsurfs || lodInfo.surfIndex > model->numsurfs
                || lodInfo.numsurfs > model->numsurfs - lodInfo.surfIndex
                || !model->surfs || !model->materialHandles
                || !DObjPartBitsWithinModel(
                    lodInfo.partBits,
                    model->numBones)
                || surfaceCount
                    > static_cast<uint32_t>(
                        (std::numeric_limits<int>::max)()) - lodInfo.numsurfs)
            {
                return 0;
            }
            surfaceCount += lodInfo.numsurfs;

            const uint32_t block = boneIndex >> 5;
            const uint32_t shift = boneIndex & 31u;
            const uint32_t source[7] = {
                0u,
                0u,
                0u,
                static_cast<uint32_t>(lodInfo.partBits[0]),
                static_cast<uint32_t>(lodInfo.partBits[1]),
                static_cast<uint32_t>(lodInfo.partBits[2]),
                static_cast<uint32_t>(lodInfo.partBits[3]),
            };
            if (!shift)
            {
                accumulatedBits[0] |= source[3u - block];
                accumulatedBits[1] |= source[4u - block];
                accumulatedBits[2] |= source[5u - block];
                accumulatedBits[3] |= source[6u - block];
            }
            else
            {
                const uint32_t carryShift = 32u - shift;
                accumulatedBits[0] |= source[3u - block] >> shift;
                accumulatedBits[1] |= (source[4u - block] >> shift)
                    | (source[3u - block] << carryShift);
                accumulatedBits[2] |= (source[5u - block] >> shift)
                    | (source[4u - block] << carryShift);
                accumulatedBits[3] |= (source[6u - block] >> shift)
                    | (source[5u - block] << carryShift);
            }
        }
        boneIndex += model->numBones;
    }

    if (boneIndex != obj->numBones)
        return 0;
    for (uint32_t word = 0u; word < 4u; ++word)
        partBits[word] = std::bit_cast<int32_t>(accumulatedBits[word]);
    return static_cast<int>(surfaceCount);
}

void __cdecl DObjGetSurfaceData(const DObj_s *obj, const float *origin, float scale, int8_t *lods)
{
    XModelLodRampType lodRampType; // [esp+Ch] [ebp-18h]
    XModel *model; // [esp+10h] [ebp-14h]
    float adjustedDist; // [esp+14h] [ebp-10h]
    float baseDist; // [esp+18h] [ebp-Ch]
    int modelCount; // [esp+1Ch] [ebp-8h]
    int modelIndex; // [esp+20h] [ebp-4h]

    if (!lods)
        return;
    for (uint32_t index = 0u; index < DOBJ_MAX_SUBMODELS; ++index)
        lods[index] = -1;

    iassert(obj);
    if (!obj || !origin || !obj->models || !obj->numModels
        || obj->numModels > DOBJ_MAX_SUBMODELS
        || !std::isfinite(scale) || scale == 0.0f)
    {
        return;
    }
    modelCount = DObjGetNumModels(obj);
    iassert(modelCount <= DOBJ_MAX_SUBMODELS);
    iassert(scale != 0.0);

    for (modelIndex = 0; modelIndex < modelCount; ++modelIndex)
    {
        model = obj->models[modelIndex];
        if (!DObjModelPoseContractValid(model)
            || model->lodRampType >= XMODEL_LOD_RAMP_COUNT)
        {
            return;
        }
    }

    baseDist = DObj_GetBaseLodDist(origin) * (1.0f / scale);

    iassert(!IS_NAN(scale));
    iassert(!IS_NAN(baseDist));

    if (!std::isfinite(baseDist))
        return;

    for (modelIndex = 0; modelIndex < modelCount; ++modelIndex)
    {
        model = DObjGetModel(obj, modelIndex);
        lodRampType = XModelGetLodRampType(model);
        adjustedDist = DObj_GetAdjustedLodDist(baseDist, lodRampType);
        lods[modelIndex] = static_cast<int8_t>(
            DObjGetLodForDist(obj, modelIndex, adjustedDist));
    }
}

bool __cdecl DObjGetBoneInfo(
    const DObj_s *obj,
    XBoneInfo **boneInfo,
    const uint32_t boneInfoCapacity)
{
    if (!boneInfo || !boneInfoCapacity)
        return false;
    for (uint32_t index = 0u; index < boneInfoCapacity; ++index)
        boneInfo[index] = nullptr;
    if (!obj || !obj->models || !obj->numModels
        || obj->numModels > DOBJ_MAX_SUBMODELS
        || !obj->numBones || obj->numBones > DOBJ_MAX_PARTS
        || obj->numBones > boneInfoCapacity)
    {
        return false;
    }

    uint32_t totalBoneCount = 0u;
    for (uint32_t modelIndex = 0u;
         modelIndex < obj->numModels;
         ++modelIndex)
    {
        const XModel *const model = obj->models[modelIndex];
        if (!model || !model->numBones || !model->boneInfo
            || totalBoneCount > obj->numBones
            || model->numBones > obj->numBones - totalBoneCount)
        {
            return false;
        }
        totalBoneCount += model->numBones;
    }
    if (totalBoneCount != obj->numBones)
        return false;

    uint32_t outputIndex = 0u;
    for (uint32_t modelIndex = 0u;
         modelIndex < obj->numModels;
         ++modelIndex)
    {
        XModel *const model = obj->models[modelIndex];
        for (uint32_t boneIndex = 0u;
             boneIndex < model->numBones;
             ++boneIndex)
        {
            boneInfo[outputIndex++] = &model->boneInfo[boneIndex];
        }
    }
    return true;
}

int __cdecl DObjNumBones(const DObj_s *obj)
{
    return obj->numBones;
}

int __cdecl DObjGetLodForDist(const DObj_s *obj, int modelIndex, float dist)
{
    if (!obj || !obj->models || modelIndex < 0
        || modelIndex >= obj->numModels
        || modelIndex >= DOBJ_MAX_SUBMODELS)
    {
        return -1;
    }
    return XModelGetLodForDist(obj->models[modelIndex], dist);
}

void __cdecl DObjGetSetBones(const DObj_s *obj, int *setPartBits)
{
    setPartBits[0] = obj->skel.partBits.anim[0];
    setPartBits[1] = obj->skel.partBits.anim[1];
    setPartBits[2] = obj->skel.partBits.anim[2];
    setPartBits[3] = obj->skel.partBits.anim[3];
}

uint32_t __cdecl DObjGetRootBoneCount(const DObj_s *obj)
{
    XModel *model;

    model = DObjGetModel(obj, 0);
    iassert(model->numRootBones);
    return model->numRootBones;
}

int __cdecl DObjSetRotTransIndex(DObj_s *obj, const int *partBits, int boneIndex)
{
    DSkel *skel; // [esp+0h] [ebp-Ch]
    int boneIndexHigh; // [esp+4h] [ebp-8h]
    uint32_t boneIndexLow; // [esp+8h] [ebp-4h]

    iassert(obj);
    iassert(obj->skel.mat);
    iassert(boneIndex >= 0);
    iassert(boneIndex < obj->numBones);

    boneIndexHigh = boneIndex >> 5;
    boneIndexLow = 0x80000000 >> (boneIndex & 0x1F);
    if ((boneIndexLow & partBits[boneIndex >> 5]) == 0)
        return 0;
    skel = &obj->skel;

    if ((boneIndexLow & obj->skel.partBits.anim[boneIndexHigh]) != 0)
        return 0;

    iassert(!(skel->partBits.skel[boneIndexHigh] & boneIndexLow));
    skel->partBits.anim[boneIndexHigh] |= boneIndexLow;
    return 1;
}

char __cdecl DObjSetSkelRotTransIndex(DObj_s *obj, const int *partBits, int boneIndex)
{
    DSkel *skel; // [esp+0h] [ebp-Ch]
    int boneIndexHigh; // [esp+4h] [ebp-8h]
    uint32_t boneIndexLow; // [esp+8h] [ebp-4h]

    iassert(obj);
    iassert(obj->skel.mat);
    iassert(boneIndex >= 0);
    iassert(boneIndex < obj->numBones);

    boneIndexHigh = boneIndex >> 5;
    boneIndexLow = 0x80000000 >> (boneIndex & 0x1F);

    if ((boneIndexLow & partBits[boneIndex >> 5]) == 0)
        return true;
    skel = &obj->skel;

    if ((boneIndexLow & obj->skel.partBits.anim[boneIndexHigh]) != 0)
        return false;

    iassert(!(skel->partBits.skel[boneIndexHigh] & boneIndexLow));
    skel->partBits.anim[boneIndexHigh] |= boneIndexLow;
    skel->partBits.skel[boneIndexHigh] |= boneIndexLow;

    return true;
}

void __cdecl DObjSetControlTagAngles(DObj_s *obj, int *partBits, uint32_t boneIndex, float *angles)
{
    if (boneIndex < 254)
    {
        if (DObjSetControlRotTransIndex(obj, partBits, boneIndex))
            DObjSetLocalTagInternal(obj, vec3_origin, angles, boneIndex);

        return;
    }

    iassert((boneIndex == 254) || (boneIndex == 255));
}

XModel *__cdecl DObjGetModel(const DObj_s *obj, int modelIndex)
{
    iassert(obj);
    iassert(modelIndex < obj->numModels);

    // LWSS: blops has this for some reason, seems new
    //if ( modelIndex < 0 || modelIndex >= obj->numModels )
    //    return 0;

    return obj->models[modelIndex];
}

void __cdecl DObjSetLocalTag(
    DObj_s *obj,
    int *partBits,
    uint32_t boneIndex,
    const float *trans,
    const float *angles)
{
    if (boneIndex < 254)
    {
        if (DObjSetRotTransIndex(obj, partBits, boneIndex))
            DObjSetLocalTagInternal(obj, trans, angles, boneIndex);

        return;
    }

    iassert((boneIndex == 254) || (boneIndex == 255));
}

void __cdecl DObjSetLocalTagInternal(const DObj_s *obj, const float *trans, const float *angles, int boneIndex)
{
    DObjAnimMat *rotTrans; // [esp+0h] [ebp-4h]
    DObjAnimMat *rotTransa; // [esp+0h] [ebp-4h]

    rotTrans = DObjGetRotTransArray(obj);
    if (rotTrans)
    {
        rotTransa = &rotTrans[boneIndex];
        if (angles)
            DObjSetAngles(rotTransa, angles);
        else
            DObjClearAngles(rotTransa);
        DObjSetTrans(rotTransa, trans);
    }
}

void __cdecl DObjSetAngles(DObjAnimMat *rotTrans, const float *angles)
{
    float v2; // [esp+8h] [ebp-44h]
    float v3; // [esp+14h] [ebp-38h]
    float v4; // [esp+20h] [ebp-2Ch]
    float yawQuat; // [esp+24h] [ebp-28h]
    float yawQuat_4; // [esp+28h] [ebp-24h]
    float rollQuat; // [esp+2Ch] [ebp-20h]
    float rollQuat_4; // [esp+30h] [ebp-1Ch]
    float pitchQuat; // [esp+34h] [ebp-18h]
    float pitchQuat_4; // [esp+38h] [ebp-14h]
    float tempQuat; // [esp+3Ch] [ebp-10h]
    float tempQuat_4; // [esp+40h] [ebp-Ch]
    float tempQuat_8; // [esp+44h] [ebp-8h]
    float tempQuat_12; // [esp+48h] [ebp-4h]

    v4 = angles[1] * 0.008726646192371845;
    yawQuat_4 = cos(v4);
    yawQuat = sin(v4);
    v3 = *angles * 0.008726646192371845;
    pitchQuat_4 = cos(v3);
    pitchQuat = sin(v3);
    v2 = angles[2] * 0.008726646192371845;
    rollQuat_4 = cos(v2);
    rollQuat = sin(v2);
    tempQuat = -pitchQuat * yawQuat;
    tempQuat_4 = pitchQuat * yawQuat_4;
    tempQuat_8 = pitchQuat_4 * yawQuat;
    tempQuat_12 = pitchQuat_4 * yawQuat_4;
    rotTrans->quat[0] = rollQuat * tempQuat_12 + rollQuat_4 * tempQuat;
    rotTrans->quat[1] = rollQuat_4 * tempQuat_4 + rollQuat * tempQuat_8;
    rotTrans->quat[2] = -rollQuat * tempQuat_4 + rollQuat_4 * tempQuat_8;
    rotTrans->quat[3] = rollQuat_4 * tempQuat_12 - rollQuat * tempQuat;
}

void __cdecl DObjClearAngles(DObjAnimMat *rotTrans)
{
    rotTrans->quat[0] = 0.0f;
    rotTrans->quat[1] = 0.0f;
    rotTrans->quat[2] = 0.0f;
    rotTrans->quat[3] = 1.0f;
}

void __cdecl DObjSetTrans(DObjAnimMat *rotTrans, const float *trans)
{
    rotTrans->transWeight = 0.0;
    rotTrans->trans[0] = trans[0];
    rotTrans->trans[1] = trans[1];
    rotTrans->trans[2] = trans[2];
}

void __cdecl DObjCompleteHierarchyBits(const DObj_s *obj, int *partBits)
{
    int j; // [esp+38h] [ebp-B4h]
    const unsigned __int8 *pos; // [esp+3Ch] [ebp-B0h]
    int newBoneIndex; // [esp+40h] [ebp-ACh]
    int newBoneIndexa; // [esp+40h] [ebp-ACh]
    int newBoneIndexb; // [esp+40h] [ebp-ACh]
    const unsigned __int8 *modelParents; // [esp+44h] [ebp-A8h]
    const unsigned __int8 *duplicateParts; // [esp+48h] [ebp-A4h]
    int numModels; // [esp+4Ch] [ebp-A0h]
    XModel *subModel; // [esp+50h] [ebp-9Ch]
    int startIndex[33]; // [esp+54h] [ebp-98h]
    int localBoneIndex; // [esp+D8h] [ebp-14h]
    unsigned __int8 *parentList; // [esp+DCh] [ebp-10h]
    int objBoneIndex; // [esp+E0h] [ebp-Ch]
    const int *duplicatePartBits; // [esp+E4h] [ebp-8h]
    XModel **models; // [esp+E8h] [ebp-4h]

    PROF_SCOPED("DObjCompleteHierarchyBits");

    iassert(obj);
    iassert(obj->numBones > 0);
    objBoneIndex = obj->numBones - 1;
    numModels = obj->numModels;
    iassert(numModels > 0);
    iassert(obj->duplicateParts);

    duplicatePartBits = (const int *)SL_ConvertToString(obj->duplicateParts);
    duplicateParts = (const unsigned __int8 *)(duplicatePartBits + 4);
    newBoneIndex = 0;
    subModel = 0;
    models = obj->models;
    modelParents = (const unsigned __int8 *)&models[numModels];
    for (j = 0; j < numModels; ++j)
    {
        startIndex[j] = newBoneIndex;
        subModel = models[j];
        newBoneIndex = startIndex[j] + subModel->numBones;
        if (newBoneIndex > objBoneIndex)
            break;
    }
    iassert(j != numModels);
    partBits[0] |= 0x80000000;
    for (parentList = subModel->parentList; ; parentList = subModel->parentList)
    {
        while (1)
        {
            localBoneIndex = objBoneIndex - startIndex[j];
            if (localBoneIndex < 0)
                break;
            if ((partBits[objBoneIndex >> 5] & (0x80000000 >> (objBoneIndex & 0x1F))) != 0)
            {
                if ((duplicatePartBits[objBoneIndex >> 5] & (0x80000000 >> (objBoneIndex & 0x1F))) != 0)
                {
                    for (pos = duplicateParts; ; pos += 2)
                    {
                        iassert(*pos);
                        if (objBoneIndex == *pos - 1)
                            break;
                    }
                    newBoneIndexb = pos[1] - 1;
                    goto LABEL_34;
                }
                newBoneIndexa = localBoneIndex - subModel->numRootBones;
                if (newBoneIndexa >= 0)
                {
                    newBoneIndexb = objBoneIndex - parentList[newBoneIndexa];
                    goto LABEL_34;
                }
                newBoneIndexb = modelParents[j];
                if (newBoneIndexb == 255)
                {
                    --objBoneIndex;
                }
                else
                {
                LABEL_34:
                    iassert((unsigned)newBoneIndexb < obj->numBones);
                    partBits[newBoneIndexb >> 5] |= 0x80000000 >> (newBoneIndexb & 0x1F);
                    --objBoneIndex;
                }
            }
            else
            {
                --objBoneIndex;
            }
        }
        if (--j < 0)
            break;
        subModel = models[j];
    }
}

int __cdecl DObjSetControlRotTransIndex(DObj_s *obj, const int *partBits, int boneIndex)
{
    DSkel *skel; // [esp+0h] [ebp-Ch]
    int boneIndexHigh; // [esp+4h] [ebp-8h]
    uint32_t boneIndexLow; // [esp+8h] [ebp-4h]

    iassert(obj);
    iassert(obj->skel.mat);
    iassert(boneIndex >= 0);
    iassert(boneIndex < obj->numBones);

    boneIndexHigh = boneIndex >> 5;
    boneIndexLow = 0x80000000 >> (boneIndex & 0x1F);

    if ((boneIndexLow & partBits[boneIndex >> 5]) == 0)
        return 0;

    skel = &obj->skel;

    if ((boneIndexLow & obj->skel.partBits.anim[boneIndexHigh]) != 0)
        return 0;
    
    iassert(!(skel->partBits.skel[boneIndexHigh] & boneIndexLow));

    skel->partBits.control[boneIndexHigh] |= boneIndexLow;
    skel->partBits.anim[boneIndexHigh] |= boneIndexLow;
    return 1;
}

bool __cdecl DObjSkelExists(const DObj_s *obj, int timeStamp)
{
    if (obj->skel.timeStamp == timeStamp)
        return obj->skel.mat != 0;
    memset((unsigned __int8 *)&obj->skel, 0, sizeof(obj->skel));
    return 0;
}

void __cdecl DObjClearSkel(const DObj_s *obj)
{
    memset((unsigned __int8 *)&obj->skel.partBits, 0, sizeof(DSkelPartBits));
}

int __cdecl DObjSkelAreBonesUpToDate(const DObj_s *obj, int *partBits)
{
    int i; // [esp+4h] [ebp-4h]

    iassert(obj);

    for (i = 0; i < 4; ++i)
    {
        if ((partBits[i] & ~obj->skel.partBits.skel[i]) != 0)
            return 0;
    }
    return 1;
}

int __cdecl DObjGetAllocSkelSize(const DObj_s *obj)
{
    return 32 * obj->numBones;
}

void __cdecl DObjCreateSkel(DObj_s *obj, char *buf, int timeStamp)
{
    uint32_t AllocSkelSize; // eax
    int i; // [esp+30h] [ebp-4h]

    PROF_SCOPED("DObjCreateSkel");

    AllocSkelSize = DObjGetAllocSkelSize(obj);
    memset((unsigned __int8 *)buf, 0xFFu, AllocSkelSize); // KISAKTODO: this memset is removed in blops, might not be needed

    obj->skel.mat = (DObjAnimMat *)buf;
    obj->skel.timeStamp = timeStamp;

    for (i = 0; i < 4; ++i)
    {
        iassert(!obj->skel.partBits.anim[i]);
        iassert(!obj->skel.partBits.control[i]);
        iassert(!obj->skel.partBits.skel[i]);
    }
}

DObjAnimMat *__cdecl I_dmaGetDObjSkel(const DObj_s *obj)
{
    iassert(obj->skel.mat);
    return obj->skel.mat;
}

void __cdecl DObjGetHidePartBits(const DObj_s *obj, uint32_t *partBits)
{
    partBits[0] = obj->hidePartBits[0];
    partBits[1] = obj->hidePartBits[1];
    partBits[2] = obj->hidePartBits[2];
    partBits[3] = obj->hidePartBits[3];
}

void __cdecl DObjLock(DObj_s *obj)
{
    iassert(obj);
    if (!obj)
        return;

    for (;;)
    {
        if (!Sys_AtomicCompareExchange(&obj->locked, 1u, 0u))
        {
            return;
        }
        Sys_Sleep(0);
    }
}

void __cdecl DObjUnlock(DObj_s *obj)
{
    iassert(obj);
    if (!obj)
        return;

    const uint32_t lockState = Sys_AtomicExchange(&obj->locked, 0u);
    iassert(lockState == 1);
    (void)lockState;
}

// seems blops specific
//int __cdecl DObjGetChildBones(const DObj_s *obj, unsigned __int8 parentBone, unsigned __int8 *children, int maxChildren)
//{
//    unsigned __int8 child_index; // [esp+7h] [ebp-1Dh]
//    int j; // [esp+8h] [ebp-1Ch]
//    unsigned __int8 modelBoneIndex; // [esp+Fh] [ebp-15h]
//    XModel *model; // [esp+14h] [ebp-10h]
//    int numChildBones; // [esp+18h] [ebp-Ch]
//    unsigned __int8 childBoneIndexStart; // [esp+1Fh] [ebp-5h]
//
//    iassert(obj);
//    iassert(parentBone < obj->numBones);
//    iassert(children);
//
//    numChildBones = 0;
//    modelBoneIndex = parentBone;
//    childBoneIndexStart = 0;
//
//    for (j = 0; ; ++j)
//    {
//        if (j >= obj->numModels)
//            return 0;
//        model = obj->localModels[j];
//        if (modelBoneIndex < (int)model->numBones)
//            break;
//        modelBoneIndex -= model->numBones;
//        childBoneIndexStart += model->numBones;
//    }
//
//    for (child_index = 0; child_index < (int)model->numBones; ++child_index)
//    {
//        if (modelBoneIndex == child_index - model->localParentList[child_index - model->numRootBones])
//        {
//            children[numChildBones] = child_index + childBoneIndexStart;
//            if (++numChildBones == maxChildren)
//                return numChildBones;
//        }
//    }
//
//    return numChildBones;
//}

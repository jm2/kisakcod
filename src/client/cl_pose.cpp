#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include "cl_pose.h"

#include <universal/assertive.h>
#include <gfx_d3d/r_scene.h>
#include <qcommon/skel_memory_atomic.h>
#include "client.h"
#include <xanim/dobj_utils.h>

namespace
{
volatile uint32_t s_skelWarningEpoch;
}

char *__cdecl CL_AllocSkelMemory(unsigned int size)
{
    iassert(size);
    if (!size)
        return nullptr;

    clientActive_t *skel_glob = &clients[R_GetLocalClientNum()];
    const skel_memory_atomic::ArenaView arena =
        skel_memory_atomic::MakeArenaView(
            skel_glob->skelMemory,
            sizeof(skel_glob->skelMemory),
            SKEL_MEM_ALIGNMENT);
    iassert(arena.base);
    iassert(skel_glob->skelMemoryStart == arena.base);
    if (!arena.base || skel_glob->skelMemoryStart != arena.base)
        return nullptr;

    uint32_t alignedSize = 0u;
    const bool validSize = skel_memory_atomic::CheckedAlignUp(
        size,
        SKEL_MEM_ALIGNMENT,
        &alignedSize)
        && alignedSize <= arena.capacity;
    iassert(validSize);
    if (!validSize)
        return nullptr;

    const uint32_t offset = skel_memory_atomic::ReserveAligned(
        &skel_glob->skelMemPos,
        size,
        arena.capacity,
        SKEL_MEM_ALIGNMENT);
    if (offset == skel_memory_atomic::kInvalidOffset)
    {
        return nullptr;
    }

    return arena.base + offset;
}

int __cdecl CL_GetSkelTimeStamp()
{
    return skel_memory_atomic::LoadTimestamp(
        &clients[R_GetLocalClientNum()].skelTimeStamp);
}

int __cdecl CL_DObjCreateSkelForBones(const DObj_s *obj, int *partBits, DObjAnimMat **pMatOut)
{
    int timeStamp; // r31
    unsigned int AllocSkelSize; // r3
    DObjAnimMat *buf; // r3

    iassert(obj);

    timeStamp = CL_GetSkelTimeStamp();

    if (DObjSkelExists(obj, timeStamp))
    {
        *pMatOut = I_dmaGetDObjSkel(obj);
        return DObjSkelAreBonesUpToDate(obj, partBits);
    }
    else
    {
        AllocSkelSize = DObjGetAllocSkelSize(obj);
        buf = (DObjAnimMat *)CL_AllocSkelMemory(AllocSkelSize);
        if (buf)
        {
            *pMatOut = buf;
            DObjCreateSkel((DObj_s*)obj, (char *)buf, timeStamp);
            return 0;
        }
        else
        {
            *pMatOut = 0;
            if (skel_memory_atomic::ClaimWarning(
                    &s_skelWarningEpoch,
                    timeStamp))
            {
                Com_PrintWarning(14, "WARNING: CL_SKEL_MEMORY_SIZE exceeded - not calculating skeleton\n");
            }
            return 1;
        }
    }
}

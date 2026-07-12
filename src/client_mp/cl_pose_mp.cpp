#ifndef KISAK_MP
#error This File is MultiPlayer Only
#endif

#include "client_mp.h"

#include <gfx_d3d/r_scene.h>
#include <qcommon/skel_memory_atomic.h>
#include <xanim/dobj_utils.h>

namespace
{
constexpr uint32_t kSkelMemoryAlignment = 16u;
volatile uint32_t s_skelWarningEpoch;
}

char *__cdecl CL_AllocSkelMemory(uint32_t size)
{
    iassert(size);
    if (!size)
        return nullptr;

    clientActive_t *const skelGlob = &clients[R_GetLocalClientNum()];
    const skel_memory_atomic::ArenaView arena =
        skel_memory_atomic::MakeArenaView(
            skelGlob->skelMemory,
            sizeof(skelGlob->skelMemory),
            kSkelMemoryAlignment);
    iassert(arena.base);
    iassert(skelGlob->skelMemoryStart == arena.base);
    if (!arena.base || skelGlob->skelMemoryStart != arena.base)
        return nullptr;

    uint32_t alignedSize = 0u;
    const bool validSize = skel_memory_atomic::CheckedAlignUp(
        size,
        kSkelMemoryAlignment,
        &alignedSize)
        && alignedSize <= arena.capacity;
    iassert(validSize);
    if (!validSize)
        return nullptr;

    const uint32_t offset = skel_memory_atomic::ReserveAligned(
        &skelGlob->skelMemPos,
        size,
        arena.capacity,
        kSkelMemoryAlignment);
    if (offset == skel_memory_atomic::kInvalidOffset)
        return nullptr;

    return arena.base + offset;
}

int __cdecl CL_GetSkelTimeStamp()
{
    return skel_memory_atomic::LoadTimestamp(
        &clients[R_GetLocalClientNum()].skelTimeStamp);
}

int __cdecl CL_DObjCreateSkelForBones(const DObj_s *obj, int *partBits, DObjAnimMat **pMatOut)
{
    char *buf; // [esp+0h] [ebp-Ch]
    uint32_t len; // [esp+4h] [ebp-8h]
    int timeStamp; // [esp+8h] [ebp-4h]

    iassert(obj);

    timeStamp = CL_GetSkelTimeStamp();
    if (DObjSkelExists(obj, timeStamp))
    {
        *pMatOut = I_dmaGetDObjSkel(obj);
        return DObjSkelAreBonesUpToDate(obj, partBits);
    }
    else
    {
        len = DObjGetAllocSkelSize(obj);
        buf = CL_AllocSkelMemory(len);
        if (buf)
        {
            *pMatOut = (DObjAnimMat *)buf;
            DObjCreateSkel((DObj_s*)obj, buf, timeStamp);
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

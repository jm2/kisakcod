#include "qcommon.h"
#include "mem_track.h"
#include "threads.h"
#include <xanim/dobj.h>
#include <universal/sys_atomic.h>
#include <win32/win_local.h>

#define DOBJ_HANDLE_MAX (MAX_GENTITIES - 128) // 2048

static DObj_s objBuf[DOBJ_HANDLE_MAX];
static bool objAlloced[DOBJ_HANDLE_MAX];
static __int16 clientObjMap[CLIENT_DOBJ_HANDLE_MAX];
static __int16 serverObjMap[SERVER_DOBJ_HANDLE_MAX];
static int objFreeCount;
static int com_lastDObjIndex;

// LWSS: used in SP (KISAKTODO: could MP use this?)
static __int16 clientObjMapBuffered[CLIENT_DOBJ_HANDLE_MAX];
static uint8_t serverObjDirty[272];

namespace
{
void Com_LockDObjAllocation()
{
#ifdef KISAK_SP
    Sys_EnterCriticalSection(CRITSECT_DOBJ_ALLOC);
#else
    iassert(Sys_IsMainThread());
#endif
}

void Com_UnlockDObjAllocation()
{
#ifdef KISAK_SP
    Sys_LeaveCriticalSection(CRITSECT_DOBJ_ALLOC);
#endif
}

bool Com_IsReusableDObjSlot(const uint32_t index)
{
    const DObj_s &obj = objBuf[index];
    return Sys_AtomicLoad(&obj.locked) == 0u && !obj.tree && !obj.models
        && !obj.numModels && !obj.duplicateParts;
}

bool Com_HasDObjCapacity()
{
    Com_LockDObjAllocation();
    // Preserve the original one-slot reserve, but reject before any fallible
    // DObj preparation instead of publishing the last object and longjmping.
    const bool hasCapacity = objFreeCount > 1;
    Com_UnlockDObjAllocation();
    return hasCapacity;
}

void Com_ReleaseReservedDObjIndex(const uint32_t index)
{
    iassert(index > 0 && index < DOBJ_HANDLE_MAX);
    Com_LockDObjAllocation();
    iassert(objAlloced[index]);
    iassert(Com_IsReusableDObjSlot(index));
    if (objAlloced[index] && Com_IsReusableDObjSlot(index))
    {
        objAlloced[index] = false;
        ++objFreeCount;
    }
    Com_UnlockDObjAllocation();
}
}

void __cdecl TRACK_dobj_management()
{
    track_static_alloc_internal(objBuf, sizeof(objBuf), "objBuf", 11);
    track_static_alloc_internal(objAlloced, 2048, "objAlloced", 11);
    track_static_alloc_internal(clientObjMap, 2304, "clientObjMap", 11);
    track_static_alloc_internal(serverObjMap, 2048, "serverObjMap", 11);
}

DObj_s *__cdecl Com_GetClientDObj(uint32_t handle, int localClientNum)
{
    iassert(handle >= 0 && handle < CLIENT_DOBJ_HANDLE_MAX);
    iassert(localClientNum == 0);

    handle = handle + CLIENT_DOBJ_HANDLE_MAX * localClientNum;

    iassert(((unsigned)handle < (sizeof(clientObjMap) / (sizeof(clientObjMap[0]) * (sizeof(clientObjMap) != 4 || sizeof(clientObjMap[0]) <= 4)))));
    iassert(((unsigned)clientObjMap[handle] < DOBJ_HANDLE_MAX));

    if (clientObjMap[handle])
        return &objBuf[clientObjMap[handle]];
    else
        return 0;
}

DObj_s *Com_GetClientDObjBuffered(uint32_t handle, int localClientNum)
{
    uint32_t v4; // r31
    uint32_t v5; // r31

    iassert(handle >= 0 && handle < CLIENT_DOBJ_HANDLE_MAX);

    v4 = 2304 * localClientNum + handle;

    //iassert(((unsigned)handle < (sizeof(clientObjMapBuffered) / (sizeof(clientObjMapBuffered[0]) * (sizeof(clientObjMapBuffered) != 4 || sizeof(clientObjMapBuffered[0]) <= 4))));
    v5 = v4;

    iassert((unsigned)clientObjMapBuffered[handle] < DOBJ_HANDLE_MAX);

    if (clientObjMapBuffered[v5])
        return &objBuf[clientObjMapBuffered[v5]];
    else
        return 0;
}

DObj_s *__cdecl Com_GetServerDObj(uint32_t handle)
{
    iassert(((unsigned)handle < (sizeof(serverObjMap) / (sizeof(serverObjMap[0]) * (sizeof(serverObjMap) != 4 || sizeof(serverObjMap[0]) <= 4)))));
    iassert((unsigned)serverObjMap[handle] < DOBJ_HANDLE_MAX);

    if (serverObjMap[handle])
        return &objBuf[serverObjMap[handle]];
    else
        return 0;
}

void Com_ServerDObjClean(int handle)
{
    iassert((unsigned)handle < SERVER_DOBJ_HANDLE_MAX);
    serverObjDirty[handle >> 3] &= ~(1 << (handle & 7));
}

bool Com_ServerDObjDirty(int handle)
{
    iassert((unsigned)handle < SERVER_DOBJ_HANDLE_MAX);
    return ((1 << (handle & 7)) & serverObjDirty[handle >> 3]) != 0;
}

DObj_s *__cdecl Com_ClientDObjCreate(
    DObjModel_s *dobjModels,
    uint16_t numModels,
    XAnimTree_s *tree,
    uint32_t handle,
    int localClientNum)
{
    uint32_t index; // [esp+0h] [ebp-4h]

    iassert(dobjModels);
    iassert(((unsigned)handle < CLIENT_DOBJ_HANDLE_MAX));
    iassert(!Com_GetClientDObj(handle, localClientNum));
    if (!Com_HasDObjCapacity())
    {
        Com_Error(ERR_DROP, "No free DObjs");
        return nullptr;
    }

    DObjCreatePlan plan{};
    DObjPrepareCreate(dobjModels, numModels, tree, 0, &plan);
    index = Com_GetFreeDObjIndex();
    if (!index)
    {
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "No reusable DObj slots");
        return nullptr;
    }
    iassert((unsigned)handle < CLIENT_DOBJ_HANDLE_MAX);
    iassert(handle >= localClientNum * CLIENT_DOBJ_HANDLE_MAX);
    iassert(handle < localClientNum * CLIENT_DOBJ_HANDLE_MAX + CLIENT_DOBJ_HANDLE_MAX);

    iassert((unsigned)index < DOBJ_HANDLE_MAX);

    if (!DObjTryCommitCreatePlan(&plan, &objBuf[index]))
    {
        Com_ReleaseReservedDObjIndex(index);
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "Reserved client DObj slot was not reusable");
        return nullptr;
    }
    clientObjMap[handle] = index;

    return &objBuf[index];
}

int __cdecl Com_GetFreeDObjIndex()
{
    Com_LockDObjAllocation();

    if (objFreeCount <= 1)
    {
        Com_UnlockDObjAllocation();
        return 0;
    }

    for (int i = com_lastDObjIndex + 1; i < DOBJ_HANDLE_MAX; ++i)
    {
        if (!objAlloced[i] && Com_IsReusableDObjSlot(i))
        {
            com_lastDObjIndex = i;

            iassert(i);
            iassert((unsigned)i < DOBJ_HANDLE_MAX);
            iassert((unsigned)i < ARRAY_COUNT(objAlloced));

            objAlloced[i] = 1;

            iassert(objFreeCount);

            --objFreeCount;
            Com_UnlockDObjAllocation();
            return i;
        }
    }
    for (int i = 1; i <= com_lastDObjIndex; ++i)
    {
        if (!objAlloced[i] && Com_IsReusableDObjSlot(i))
        {
            com_lastDObjIndex = i;
            iassert(i);
            iassert((unsigned)i < DOBJ_HANDLE_MAX);

            objAlloced[i] = 1;

            iassert(objFreeCount);

            --objFreeCount;
            Com_UnlockDObjAllocation();
            return i;
        }
    }

    Com_UnlockDObjAllocation();
    return 0;
}

void __cdecl Com_ClientDObjClearAllSkel()
{
    int handleOffset; // [esp+4h] [ebp-4h]

    for (handleOffset = 0; handleOffset < CLIENT_DOBJ_HANDLE_MAX; ++handleOffset)
    {
        if (clientObjMap[handleOffset])
            DObjSkelClear(&objBuf[clientObjMap[handleOffset]]);
    }
}

void __cdecl Com_ServerDObjClearAllSkel()
{
    for (int handle = 0; handle < SERVER_DOBJ_HANDLE_MAX; ++handle)
    {
        if (serverObjMap[handle])
            DObjSkelClear(&objBuf[serverObjMap[handle]]);
    }
}

DObj_s *__cdecl Com_ServerDObjCreate(
    DObjModel_s *dobjModels,
    uint16_t numModels,
    XAnimTree_s *tree,
    uint32_t handle)
{
    uint32_t index; // [esp+0h] [ebp-4h]

    iassert(dobjModels);
    iassert(handle < SERVER_DOBJ_HANDLE_MAX);
    iassert(!Com_GetServerDObj(handle));

    if (!Com_HasDObjCapacity())
    {
        Com_Error(ERR_DROP, "No free DObjs");
        return nullptr;
    }

    DObjCreatePlan plan{};
    DObjPrepareCreate(
        dobjModels,
        numModels,
        tree,
        static_cast<uint16_t>(handle + 1u),
        &plan);
    index = Com_GetFreeDObjIndex();
    if (!index)
    {
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "No reusable DObj slots");
        return nullptr;
    }

    iassert((unsigned)handle < SERVER_DOBJ_HANDLE_MAX);

    iassert((unsigned)index < DOBJ_HANDLE_MAX);

    if (!DObjTryCommitCreatePlan(&plan, &objBuf[index]))
    {
        Com_ReleaseReservedDObjIndex(index);
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "Reserved server DObj slot was not reusable");
        return nullptr;
    }
    serverObjMap[handle] = index;

#ifdef KISAK_SP
    serverObjDirty[(int)handle >> 3] |= 1 << (handle & 7);
#endif

    return &objBuf[index];
}

void __cdecl Com_SafeClientDObjFree(uint32_t handle, int localClientNum)
{
    uint32_t index; // [esp+0h] [ebp-4h]

    iassert(handle < CLIENT_DOBJ_HANDLE_MAX);

    iassert((handle - localClientNum * CLIENT_DOBJ_HANDLE_MAX) < CLIENT_DOBJ_HANDLE_MAX);

    index = clientObjMap[handle];

    if (clientObjMap[handle])
    {
        clientObjMap[handle] = 0;

        iassert((unsigned)index < DOBJ_HANDLE_MAX);
        iassert(Sys_IsMainThread());

#ifdef KISAK_SP
        Sys_EnterCriticalSection(CRITSECT_DOBJ_ALLOC);
#endif
        DObjFree(&objBuf[index]);
        objAlloced[index] = 0;
        ++objFreeCount;
#ifdef KISAK_SP
        Sys_LeaveCriticalSection(CRITSECT_DOBJ_ALLOC);
#endif
    }
}

void __cdecl Com_SafeServerDObjFree(uint32_t handle)
{
    uint32_t index; // [esp+0h] [ebp-4h]

    iassert(handle < SERVER_DOBJ_HANDLE_MAX);

    index = serverObjMap[handle];

    if (serverObjMap[handle])
    {
        serverObjMap[handle] = 0;

#ifdef KISAK_SP
        serverObjDirty[(int)handle >> 3] |= 1 << (handle & 7);
#endif

        iassert((unsigned)index < DOBJ_HANDLE_MAX);
#ifndef KISAK_SP
        iassert(Sys_IsMainThread());
#endif
#ifdef KISAK_SP
        Sys_EnterCriticalSection(CRITSECT_DOBJ_ALLOC);
#endif
        DObjFree(&objBuf[index]);
        objAlloced[index] = 0;
        ++objFreeCount;
#ifdef KISAK_SP
        Sys_LeaveCriticalSection(CRITSECT_DOBJ_ALLOC);
#endif
    }
}

static int g_bDObjInited;
void __cdecl Com_InitDObj()
{
    Com_Memset(objAlloced, 0, ARRAY_COUNT(objAlloced) * sizeof(bool));
    objFreeCount = ARRAY_COUNT(objAlloced) - 1;
#ifdef KISAK_SP
    Com_Memset(serverObjDirty, 0, 272);
#endif
    Com_Memset(clientObjMap, 0, ARRAY_COUNT(clientObjMap) * sizeof(__int16));
    Com_Memset(serverObjMap, 0, ARRAY_COUNT(serverObjMap) * sizeof(__int16));
    com_lastDObjIndex = 1;
    g_bDObjInited = 1;
}

void __cdecl Com_ShutdownDObj()
{
    const char *v0; // eax
    const char *v1; // eax

    if (g_bDObjInited)
    {
        g_bDObjInited = 0;
        for (int i = 0; i < DOBJ_HANDLE_MAX; ++i)
        {
            iassert(!objAlloced[i]);
        }
        for (int i = 0; i < CLIENT_DOBJ_HANDLE_MAX; ++i)
        {
            iassert(!clientObjMap[i]);
        }
#ifdef KISAK_SP
        for (int i = 0; i < CLIENT_DOBJ_HANDLE_MAX; ++i)
        {
            iassert(!clientObjMapBuffered[i]);
        }
#endif
        for (int i = 0; i < SERVER_DOBJ_HANDLE_MAX; ++i)
        {
            iassert(!serverObjMap[i]);
        }
        iassert((objFreeCount == (sizeof(objAlloced) / (sizeof(objAlloced[0]) * (sizeof(objAlloced) != 4 || sizeof(objAlloced[0]) <= 4))) - 1));
    }
}

DObj_s *Com_DObjCloneToBuffer(uint32_t entnum)
{
    uint32_t v2; // r27
    __int16 serverDobjIndex; // r11
    uint32_t v4; // r26
    uint32_t FreeDObjIndex; // r30

    if (entnum >= SERVER_DOBJ_HANDLE_MAX)
    {
        Com_Error(ERR_DROP, "server DObj clone handle %u is out of range", entnum);
        return nullptr;
    }
    if (entnum >= 0x880)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            325,
            0,
            "%s",
            "(unsigned)entnum < ARRAY_COUNT( serverObjMap )");
    v2 = entnum;
    serverDobjIndex = serverObjMap[entnum];
    v4 = serverDobjIndex;
    iassert( serverDobjIndex );
    if (!serverDobjIndex || v4 >= DOBJ_HANDLE_MAX)
    {
        Com_Error(ERR_DROP, "cannot clone a missing server DObj");
        return nullptr;
    }
    if (entnum >= 0x900)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            329,
            0,
            "%s",
            "(unsigned)entnum < ARRAY_COUNT( clientObjMapBuffered )");
    if (clientObjMapBuffered[v2])
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            330,
            0,
            "%s",
            "!clientObjMapBuffered[entnum]");
    if (!Com_HasDObjCapacity())
    {
        Com_Error(ERR_DROP, "No free DObjs");
        return nullptr;
    }

    DObjCreatePlan plan{};
    DObjPrepareClone(&objBuf[v4], &plan);
    FreeDObjIndex = Com_GetFreeDObjIndex();
    if (!FreeDObjIndex)
    {
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "No reusable DObj slots");
        return nullptr;
    }
    if (entnum >= 0x900)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            334,
            0,
            "entnum doesn't index ARRAY_COUNT( clientObjMapBuffered )\n\t%i not in [0, %i)",
            entnum,
            2304);

    if (v4 >= 0x800)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            337,
            0,
            "%s\n\t(serverDobjIndex) = %i",
            "((unsigned)serverDobjIndex < 2048)",
            v4);
    if (FreeDObjIndex >= 0x800)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            338,
            0,
            "%s\n\t(clientDobjIndex) = %i",
            "((unsigned)clientDobjIndex < 2048)",
            FreeDObjIndex);
    if (!DObjTryCommitCreatePlan(&plan, &objBuf[FreeDObjIndex]))
    {
        Com_ReleaseReservedDObjIndex(FreeDObjIndex);
        DObjDiscardCreatePlan(&plan);
        Com_Error(ERR_DROP, "Reserved buffered DObj slot was not reusable");
        return nullptr;
    }
    clientObjMapBuffered[v2] = FreeDObjIndex;

    return &objBuf[FreeDObjIndex];
}

void Com_DObjCloneFromBuffer(uint32_t entnum)
{
    uint32_t v2; // r31

    if (entnum >= CLIENT_DOBJ_HANDLE_MAX)
    {
        Com_Error(ERR_DROP, "buffered DObj clone handle %u is out of range", entnum);
        return;
    }
    if (entnum >= 0x900)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            356,
            0,
            "%s",
            "(unsigned)entnum < ARRAY_COUNT( clientObjMap )");
    v2 = entnum;
    if (clientObjMap[entnum])
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\qcommon\\dobj_management.cpp",
            357,
            0,
            "%s\n\t(entnum) = %i",
            "(!clientObjMap[entnum])",
            entnum);
        Com_Error(ERR_DROP, "cannot replace a live client DObj with a buffered clone");
        return;
    }

    if (clientObjMapBuffered[v2])
    {
        clientObjMap[v2] = clientObjMapBuffered[v2];
        clientObjMapBuffered[v2] = 0;
    }
}

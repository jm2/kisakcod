#include "physicalmemory.h"

#include "assertive.h"
#include <qcommon/mem_track.h>
#include "q_shared.h"
#include <qcommon/qcommon.h>
#include <qcommon/sys_error.h>
#include <qcommon/sys_memory.h>

PhysicalMemory g_mem;
int g_overAllocatedSize;

void KISAK_CDECL PMem_Init()
{
    constexpr std::size_t memorySize = 0x8000000u;
    void *const reservation = Sys_VirtualMemoryReserve(memorySize);
    if (!reservation || !Sys_VirtualMemoryCommit(reservation, memorySize))
    {
        if (reservation)
            Sys_VirtualMemoryRelease(reservation);
        Sys_OutOfMemErrorInternal(".\\universal\\physicalmemory.cpp", 17);
        return;
    }

    PMem_InitPhysicalMemory(
        &g_mem,
        static_cast<uint8_t *>(reservation),
        static_cast<uint32_t>(memorySize));
}

void KISAK_CDECL PMem_DumpMemStats()
{
    double v0; // st7
    int FreeAmount; // eax
    double v2; // st7
    double v3; // st7
    signed int j; // [esp+8h] [ebp-14h]
    uint32_t i; // [esp+Ch] [ebp-10h]
    uint32_t top; // [esp+14h] [ebp-8h]
    uint32_t bottom; // [esp+18h] [ebp-4h]

    for (i = 0; i < g_mem.prim[1].allocListCount; ++i)
    {
        if (i == g_mem.prim[1].allocListCount - 1)
            bottom = g_mem.prim[1].pos;
        else
            bottom = g_mem.prim[1].allocList[i + 1].pos;
        v0 = ConvertToMB(g_mem.prim[1].allocList[i].pos - bottom);
        Com_Printf(16, "%-18.18s %5.1f\n", g_mem.prim[1].allocList[i].name, v0);
    }
    FreeAmount = PMem_GetFreeAmount();
    v2 = ConvertToMB(FreeAmount);
    Com_Printf(16, "free physical      %5.1f\n", v2);
    top = g_mem.prim[0].pos;
    for (j = g_mem.prim[0].allocListCount - 1; j >= 0; --j)
    {
        v3 = ConvertToMB(top - g_mem.prim[0].allocList[j].pos);
        Com_Printf(16, "%-18.18s %5.1f\n", g_mem.prim[0].allocList[j].name, v3);
        top = g_mem.prim[0].allocList[j].pos;
    }
    Com_Printf(16, "------------------------\n");
}

void KISAK_CDECL PMem_InitPhysicalMemory(PhysicalMemory *pmem, uint8_t *memory, uint32_t memorySize)
{
    if (!pmem)
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 277, 0, "%s", "pmem");
    if (!memory)
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 278, 0, "%s", "memory");
    memset((uint8_t *)pmem, 0, sizeof(PhysicalMemory));
    pmem->buf = memory;
    pmem->prim[1].pos = memorySize;
}

void KISAK_CDECL PMem_BeginAlloc(const char *name, uint32_t allocType)
{
    if (allocType >= 2)
    {
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp",
            350,
            0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType,
            2u);
        return;
    }
    PMem_BeginAllocInPrim(&g_mem.prim[allocType], name);
}

void KISAK_CDECL PMem_BeginAllocInPrim(PhysicalMemoryPrim *prim, const char *name)
{
    PhysicalMemoryAllocation *allocEntry; // [esp+0h] [ebp-4h]

    if (!prim)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 330, 0, "%s", "prim");
        return;
    }
    if (!name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 331, 0, "%s", "name");
        return;
    }
    if (prim->allocName)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 332, 0, "%s", "!prim->allocName");
        return;
    }
    if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 333, 0, "%s", "prim->allocListCount < MAX_PHYSICAL_ALLOCATIONS");
        return;
    }
    prim->allocName = name;
    allocEntry = &prim->allocList[prim->allocListCount++];
    allocEntry->name = name;
    allocEntry->pos = prim->pos;
}

void KISAK_CDECL PMem_EndAlloc(const char *name, uint32_t allocType)
{
    if (allocType >= 2)
    {
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp",
            378,
            0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType,
            2u);
        return;
    }
    PMem_EndAllocInPrim(&g_mem.prim[allocType], name);
}

void KISAK_CDECL PMem_EndAllocInPrim(PhysicalMemoryPrim *prim, const char *name)
{
    if (!prim)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 361, 0, "%s", "prim");
        return;
    }
    if (!name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 362, 0, "%s", "name");
        return;
    }
    if (prim->allocName != name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 364, 0, "%s", "prim->allocName == name");
        return;
    }
    if (!prim->allocListCount)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 368, 0, "%s", "prim->allocListCount > 0");
        return;
    }
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 369, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    }
    const PhysicalMemoryAllocation &allocEntry = prim->allocList[prim->allocListCount - 1];
    if (allocEntry.name != name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 370, 0, "%s", "allocEntry.name == name");
        return;
    }
    prim->allocName = nullptr;
}

void KISAK_CDECL PMem_Free(const char *name, uint32_t allocType)
{
    if (allocType >= 2)
    {
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp",
            454,
            0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType,
            2u);
        return;
    }
    PMem_FreeInPrim(&g_mem.prim[allocType], name);
}

void KISAK_CDECL PMem_FreeInPrim(PhysicalMemoryPrim *prim, const char *name)
{
    uint32_t allocIndex; // [esp+0h] [ebp-8h]

    if (!prim)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 437, 0, "%s", "prim");
        return;
    }
    if (!name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 438, 0, "%s", "name");
        return;
    }
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 439, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    }
    for (allocIndex = 0; allocIndex < prim->allocListCount; ++allocIndex)
    {
        if (prim->allocList[allocIndex].name == name)
        {
            PMem_FreeIndex(prim, allocIndex);
            return;
        }
    }
}

void KISAK_CDECL PMem_FreeIndex(PhysicalMemoryPrim *prim, uint32_t allocIndex)
{
    PhysicalMemoryAllocation *allocEntry; // [esp+0h] [ebp-Ch]
    const char *name; // [esp+4h] [ebp-8h]

    if (!prim)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 393, 0, "%s", "prim");
        return;
    }
    if (prim->allocName)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 394, 0, "%s", "!prim->allocName");
        return;
    }
    if (!prim->allocListCount)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 424, 0, "%s", "prim->allocListCount");
        return;
    }
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 395, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    }
    if (allocIndex >= prim->allocListCount)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 408, 0, "%s", "allocIndex < prim->allocListCount");
        return;
    }
    allocEntry = &prim->allocList[allocIndex];
    name = allocEntry->name;
    if (!name)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 400, 0, "%s", "name");
        return;
    }
    allocEntry->name = nullptr;
    if (allocIndex == prim->allocListCount - 1)
    {
        do
        {
            prim->pos = allocEntry->pos;
            if (!--prim->allocListCount)
                break;
            allocEntry = &prim->allocList[prim->allocListCount - 1];
        } while (!allocEntry->name);
    }
    else
    {
        if (!alwaysfails)
        {
            MyAssertHandler(
                ".\\universal\\physicalmemory.cpp",
                411,
                0,
                "freeing '%s' caused a memory hole\n",
                name);
        }
    }
}

int KISAK_CDECL PMem_GetOverAllocatedSize()
{
    return g_overAllocatedSize;
}

uint8_t *KISAK_CDECL PMem_Alloc(
    uint32_t size,
    uint32_t alignment,
    uint32_t type,
    uint32_t allocType)
{
    if (!g_mem.buf || allocType >= ARRAY_COUNT(g_mem.prim) || !size
        || !alignment || (alignment & (alignment - 1)) != 0)
    {
        Com_Error(ERR_FATAL, "Invalid physical-memory allocation request");
        return nullptr;
    }

    PhysicalMemoryPrim *prim = &g_mem.prim[allocType];
    if (!prim->allocName)
    {
        Com_Error(ERR_FATAL, "Physical-memory allocation is outside an allocation scope");
        return nullptr;
    }

    uint8_t *result = PMem_TryAlloc(size, alignment, type, allocType);
    if (!result)
    {
        Sys_OutOfMemErrorInternal(".\\universal\\physicalmemory.cpp", 0);
        return nullptr;
    }
    return result;
}

uint8_t *KISAK_CDECL PMem_TryAlloc(
    uint32_t size,
    uint32_t alignment,
    uint32_t type,
    uint32_t allocType)
{
    (void)type;
    if (!g_mem.buf || allocType >= ARRAY_COUNT(g_mem.prim) || !size
        || !alignment || (alignment & (alignment - 1)) != 0
        || !g_mem.prim[allocType].allocName
        || g_mem.prim[0].pos > g_mem.prim[1].pos)
    {
        g_overAllocatedSize = INT32_MAX;
        return nullptr;
    }

    PhysicalMemoryPrim *prim = &g_mem.prim[allocType];
    uint32_t lowPos = 0;
    const uint32_t alignmentMask = alignment - 1;
    uint64_t missing = 0;

    if (allocType)
    {
        const uint64_t highPos = g_mem.prim[1].pos;
        if (size > highPos)
        {
            missing = static_cast<uint64_t>(g_mem.prim[0].pos) + size - highPos;
        }
        else
        {
            lowPos = static_cast<uint32_t>((highPos - size) & ~static_cast<uint64_t>(alignmentMask));
            if (lowPos < g_mem.prim[0].pos)
                missing = static_cast<uint64_t>(g_mem.prim[0].pos) - lowPos;
        }
    }
    else
    {
        const uint64_t aligned = (static_cast<uint64_t>(prim->pos) + alignmentMask)
            & ~static_cast<uint64_t>(alignmentMask);
        const uint64_t newPos = aligned + size;
        if (newPos > g_mem.prim[1].pos)
        {
            missing = newPos - g_mem.prim[1].pos;
        }
        else
        {
            lowPos = static_cast<uint32_t>(aligned);
        }
    }

    if (missing)
    {
        g_overAllocatedSize = missing > INT32_MAX ? INT32_MAX : static_cast<int>(missing);
        return nullptr;
    }

    if (allocType)
        prim->pos = lowPos;
    else
        prim->pos = lowPos + size;
    g_overAllocatedSize = 0;
    return &g_mem.buf[lowPos];
}

uint32_t KISAK_CDECL PMem_GetFreeAmount()
{
    if (g_mem.prim[0].pos > g_mem.prim[1].pos)
        return 0;
    return g_mem.prim[1].pos - g_mem.prim[0].pos;
}

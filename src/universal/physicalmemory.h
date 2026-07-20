#pragma once

#include "platform_compat.h"

#include <cstddef>
#include <cstdint>

struct PhysicalMemoryAllocation // x86 sizeof=0x8, native64 sizeof=0x10
{                                       // ...
    const char *name;                   // ...
    uint32_t pos;                   // ...
};
struct PhysicalMemoryPrim // x86 sizeof=0x10C, native64 sizeof=0x210
{                                       // ...
    const char *allocName;
    uint32_t allocListCount;        // ...
    uint32_t pos;                   // ...
    PhysicalMemoryAllocation allocList[32]; // ...
};
struct PhysicalMemory // x86 sizeof=0x21C, native64 sizeof=0x428
{                                       // ...
    uint8_t *buf;
    PhysicalMemoryPrim prim[2];         // ...
};

static_assert(offsetof(PhysicalMemoryAllocation, name) == 0);
static_assert(offsetof(PhysicalMemoryPrim, allocName) == 0);
static_assert(offsetof(PhysicalMemory, buf) == 0);

#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(PhysicalMemoryAllocation) == 0x8);
static_assert(offsetof(PhysicalMemoryAllocation, pos) == 0x4);
static_assert(sizeof(PhysicalMemoryPrim) == 0x10C);
static_assert(offsetof(PhysicalMemoryPrim, allocListCount) == 0x4);
static_assert(offsetof(PhysicalMemoryPrim, pos) == 0x8);
static_assert(offsetof(PhysicalMemoryPrim, allocList) == 0xC);
static_assert(sizeof(PhysicalMemory) == 0x21C);
static_assert(offsetof(PhysicalMemory, prim) == 0x4);
#elif UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(PhysicalMemoryAllocation) == 0x10);
static_assert(offsetof(PhysicalMemoryAllocation, pos) == 0x8);
static_assert(sizeof(PhysicalMemoryPrim) == 0x210);
static_assert(offsetof(PhysicalMemoryPrim, allocListCount) == 0x8);
static_assert(offsetof(PhysicalMemoryPrim, pos) == 0xC);
static_assert(offsetof(PhysicalMemoryPrim, allocList) == 0x10);
static_assert(sizeof(PhysicalMemory) == 0x428);
static_assert(offsetof(PhysicalMemory, prim) == 0x8);
#else
#error Unsupported native pointer width for PhysicalMemory
#endif

void KISAK_CDECL PMem_Init();
void KISAK_CDECL PMem_DumpMemStats();
void KISAK_CDECL PMem_InitPhysicalMemory(PhysicalMemory *pmem, uint8_t *memory, uint32_t memorySize);
void KISAK_CDECL PMem_BeginAlloc(const char *name, uint32_t allocType);
void KISAK_CDECL PMem_BeginAllocInPrim(PhysicalMemoryPrim *prim, const char *name);
void KISAK_CDECL PMem_EndAlloc(const char *name, uint32_t allocType);
void KISAK_CDECL PMem_EndAllocInPrim(PhysicalMemoryPrim *prim, const char *name);
void KISAK_CDECL PMem_Free(const char *name, uint32_t allocType);
void KISAK_CDECL PMem_FreeInPrim(PhysicalMemoryPrim *prim, const char *name);
void KISAK_CDECL PMem_FreeIndex(PhysicalMemoryPrim *prim, uint32_t allocIndex);
int KISAK_CDECL PMem_GetOverAllocatedSize();
uint8_t *KISAK_CDECL PMem_Alloc(
    uint32_t size,
    uint32_t alignment,
    uint32_t type,
    uint32_t allocType);
uint8_t *KISAK_CDECL PMem_TryAlloc(
    uint32_t size,
    uint32_t alignment,
    uint32_t type,
    uint32_t allocType);
uint32_t KISAK_CDECL PMem_GetFreeAmount();

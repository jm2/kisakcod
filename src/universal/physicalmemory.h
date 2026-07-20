#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct PhysicalMemoryAllocation // x86 sizeof=0x8, native64 sizeof=0x10
{                                       // ...
    const char *name;                   // ...
    uint32_t pos;                   // ...
};

inline constexpr std::uint32_t MAX_PHYSICAL_ALLOCATIONS = 32u;

struct PhysicalMemoryPrim // x86 sizeof=0x10C, native64 sizeof=0x210
{                                       // ...
    const char *allocName;
    uint32_t allocListCount;        // ...
    uint32_t pos;                   // ...
    PhysicalMemoryAllocation allocList[MAX_PHYSICAL_ALLOCATIONS]; // ...
};
struct PhysicalMemory // x86 sizeof=0x21C, native64 sizeof=0x428
{                                       // ...
    uint8_t *buf;
    PhysicalMemoryPrim prim[2];         // ...
};

RUNTIME_SIZE(PhysicalMemoryAllocation, 0x8, 0x10);
RUNTIME_OFFSET(PhysicalMemoryAllocation, name, 0x0, 0x0);
RUNTIME_OFFSET(PhysicalMemoryAllocation, pos, 0x4, 0x8);

RUNTIME_SIZE(PhysicalMemoryPrim, 0x10C, 0x210);
RUNTIME_OFFSET(PhysicalMemoryPrim, allocName, 0x0, 0x0);
RUNTIME_OFFSET(PhysicalMemoryPrim, allocListCount, 0x4, 0x8);
RUNTIME_OFFSET(PhysicalMemoryPrim, pos, 0x8, 0xC);
RUNTIME_OFFSET(PhysicalMemoryPrim, allocList, 0xC, 0x10);

RUNTIME_SIZE(PhysicalMemory, 0x21C, 0x428);
RUNTIME_OFFSET(PhysicalMemory, buf, 0x0, 0x0);
RUNTIME_OFFSET(PhysicalMemory, prim, 0x4, 0x8);

#if defined(KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING)
// The production global PhysicalMemory object is deliberately hidden in its
// implementation translation unit. Tests that compile that translation unit
// with the dedicated target-only definition may install and capture complete
// state by value, but can never retain a mutable pointer or reference to it.
// A normal production include cannot name this type or either operation.
class PhysicalMemoryGlobalStateTestAccess final
{
public:
    PhysicalMemoryGlobalStateTestAccess() = delete;

    struct Snapshot final
    {
        PhysicalMemory memory{};
        int overAllocatedSize = 0;
        std::uint8_t *retainedBase = nullptr;
        std::uint32_t retainedSize = 0;
        std::uint8_t initializationPhase = 0;
        std::uint8_t runtimeReserved[3]{};
        std::uint32_t initializationWitness = 0;
    };

    [[nodiscard]] static Snapshot Capture() noexcept;
    [[nodiscard]] static Snapshot MakeCanonicalReady(
        const PhysicalMemory &memory,
        std::uint32_t retainedSize,
        int overAllocatedSize = 0) noexcept;
    static void Install(const Snapshot &snapshot) noexcept;
};
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

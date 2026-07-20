#include "physicalmemory.h"
#include "physicalmemory_runtime.h"

#include "assertive.h"
#include <qcommon/mem_track.h>
#include "q_shared.h"
#include <qcommon/qcommon.h>
#include <qcommon/sys_error.h>
#include <qcommon/sys_memory.h>
#include <qcommon/sys_sync.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
constexpr std::uint32_t kPhysicalMemorySize = UINT32_C(0x08000000);
constexpr std::uint32_t kInitializingWitness = UINT32_C(0x494E4954);
constexpr std::uint32_t kReadyWitness = UINT32_C(0x52454144);
constexpr std::uint32_t kPoisonedWitness = UINT32_C(0x504F4953);

struct RetainedExtent final
{
    std::uint8_t *base = nullptr;
    std::uint32_t size = 0;
};

struct RuntimeControl final
{
    RetainedExtent extent{};
    pmem_runtime::InitializationPhase phase =
        pmem_runtime::InitializationPhase::Uninitialized;
    std::uint8_t reserved[3]{};
    std::uint32_t witness = 0;
};

PhysicalMemory g_mem{};
thread_local int g_overAllocatedSize{};
RuntimeControl g_runtime{};

enum class RuntimeReadiness : std::uint8_t
{
    Ready,
    NotReady,
    Corrupt,
};

enum class LegacyDiagnostic : std::uint8_t
{
    None,
    BeginInvalidType,
    EndInvalidType,
    FreeInvalidType,
    RuntimeNotReady,
    RuntimeCorrupt,
    BeginNullPrim,
    BeginNullName,
    BeginBusy,
    BeginFull,
    EndNullPrim,
    EndNullName,
    EndWrongActive,
    EndEmpty,
    EndOverCapacity,
    EndWrongTail,
    FreeIndexNullPrim,
    FreeIndexBusy,
    FreeIndexEmpty,
    FreeIndexOverCapacity,
    FreeIndexInvalidIndex,
    FreeIndexNullName,
    FreeInPrimNullPrim,
    FreeInPrimNullName,
    FreeInPrimOverCapacity,
    FreeHole,
};

struct LegacyOperationResult final
{
    LegacyDiagnostic diagnostic = LegacyDiagnostic::None;
    const char *name = nullptr;
};

std::uint32_t RuntimeWitnessFor(
    const pmem_runtime::InitializationPhase phase) noexcept
{
    switch (phase)
    {
    case pmem_runtime::InitializationPhase::Uninitialized:
        return 0;
    case pmem_runtime::InitializationPhase::Initializing:
        return kInitializingWitness;
    case pmem_runtime::InitializationPhase::Ready:
        return kReadyWitness;
    case pmem_runtime::InitializationPhase::Poisoned:
        return kPoisonedWitness;
    }
    return UINT32_MAX;
}

bool RuntimeReservedIsZero(const RuntimeControl &control) noexcept
{
    return control.reserved[0] == 0 && control.reserved[1] == 0
        && control.reserved[2] == 0;
}

bool RuntimeControlIsCanonical(const RuntimeControl &control) noexcept
{
    if (!RuntimeReservedIsZero(control)
        || control.witness != RuntimeWitnessFor(control.phase))
    {
        return false;
    }

    switch (control.phase)
    {
    case pmem_runtime::InitializationPhase::Uninitialized:
        return control.extent.base == nullptr && control.extent.size == 0;
    case pmem_runtime::InitializationPhase::Initializing:
        return control.extent.base == nullptr && control.extent.size == 0;
    case pmem_runtime::InitializationPhase::Ready:
        return control.extent.base != nullptr && control.extent.size != 0;
    case pmem_runtime::InitializationPhase::Poisoned:
        return control.extent.base != nullptr
            && control.extent.size == kPhysicalMemorySize;
    }
    return false;
}

bool AddressRangeIsValid(
    const void *const base,
    const std::size_t size) noexcept
{
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(base);
    return begin != 0 && size != 0
        && begin <= std::numeric_limits<std::uintptr_t>::max() - size;
}

bool AddressRangesOverlap(
    const void *const leftBase,
    const std::size_t leftSize,
    const void *const rightBase,
    const std::size_t rightSize) noexcept
{
    if (!AddressRangeIsValid(leftBase, leftSize)
        || !AddressRangeIsValid(rightBase, rightSize))
    {
        return true;
    }
    const std::uintptr_t left = reinterpret_cast<std::uintptr_t>(leftBase);
    const std::uintptr_t right = reinterpret_cast<std::uintptr_t>(rightBase);
    return left < right + rightSize && right < left + leftSize;
}

bool RetainedExtentIsDisjointFromControl(
    const RetainedExtent &extent) noexcept
{
    if (!AddressRangeIsValid(extent.base, extent.size))
        return false;
    return !AddressRangesOverlap(
               extent.base, extent.size, &g_mem, sizeof(g_mem))
        && !AddressRangesOverlap(
               extent.base, extent.size, &g_runtime, sizeof(g_runtime));
}

bool PhysicalMemoryIsPristine(const PhysicalMemory &memory) noexcept
{
    if (memory.buf)
        return false;
    for (const PhysicalMemoryPrim &prim : memory.prim)
    {
        if (prim.allocName || prim.allocListCount || prim.pos)
            return false;
        for (const PhysicalMemoryAllocation &entry : prim.allocList)
        {
            if (entry.name || entry.pos)
                return false;
        }
    }
    return true;
}

bool PrimTopologyIsBounded(
    const PhysicalMemoryPrim &prim,
    const bool high,
    const std::uint32_t extentSize) noexcept
{
    if (prim.allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return false;
    const std::uint32_t endpoint = high ? extentSize : 0;
    if (!prim.allocListCount)
        return !prim.allocName && prim.pos == endpoint;
    if (!prim.allocList[prim.allocListCount - 1].name
        || prim.allocList[0].pos != endpoint)
    {
        return false;
    }
    if (prim.allocName)
    {
        if (!prim.allocListCount
            || prim.allocList[prim.allocListCount - 1].name != prim.allocName)
        {
            return false;
        }
    }

    std::uint32_t previous = high ? extentSize : 0;
    for (std::uint32_t index = 0; index < prim.allocListCount; ++index)
    {
        const std::uint32_t position = prim.allocList[index].pos;
        if (position > extentSize)
            return false;
        if ((!high && position < previous) || (high && position > previous))
            return false;
        previous = position;
    }
    if ((!high && prim.pos < previous) || (high && prim.pos > previous))
        return false;
    return true;
}

bool ReadyStateIsCoherent() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime)
        || g_runtime.phase != pmem_runtime::InitializationPhase::Ready
        || !RetainedExtentIsDisjointFromControl(g_runtime.extent)
        || g_mem.buf != g_runtime.extent.base
        || g_mem.prim[0].pos > g_mem.prim[1].pos
        || g_mem.prim[1].pos > g_runtime.extent.size)
    {
        return false;
    }
    return PrimTopologyIsBounded(
               g_mem.prim[0], false, g_runtime.extent.size)
        && PrimTopologyIsBounded(
               g_mem.prim[1], true, g_runtime.extent.size);
}

bool InitializingStateIsCoherent() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime)
        || g_runtime.phase != pmem_runtime::InitializationPhase::Initializing
        || !PhysicalMemoryIsPristine(g_mem))
    {
        return false;
    }
    return g_runtime.extent.base == nullptr && g_runtime.extent.size == 0;
}

bool PoisonedStateIsCoherent() noexcept
{
    return RuntimeControlIsCanonical(g_runtime)
        && g_runtime.phase == pmem_runtime::InitializationPhase::Poisoned
        && g_runtime.extent.size == kPhysicalMemorySize
        && RetainedExtentIsDisjointFromControl(g_runtime.extent)
        && PhysicalMemoryIsPristine(g_mem);
}

RuntimeReadiness GetRuntimeReadiness() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime))
        return RuntimeReadiness::Corrupt;
    switch (g_runtime.phase)
    {
    case pmem_runtime::InitializationPhase::Uninitialized:
        return PhysicalMemoryIsPristine(g_mem)
            ? RuntimeReadiness::NotReady
            : RuntimeReadiness::Corrupt;
    case pmem_runtime::InitializationPhase::Initializing:
        return InitializingStateIsCoherent()
            ? RuntimeReadiness::NotReady
            : RuntimeReadiness::Corrupt;
    case pmem_runtime::InitializationPhase::Poisoned:
        return PoisonedStateIsCoherent()
            ? RuntimeReadiness::NotReady
            : RuntimeReadiness::Corrupt;
    case pmem_runtime::InitializationPhase::Ready:
        break;
    }
    return ReadyStateIsCoherent()
        ? RuntimeReadiness::Ready
        : RuntimeReadiness::Corrupt;
}

void SetRuntimePhase(
    RuntimeControl *const control,
    const pmem_runtime::InitializationPhase phase) noexcept
{
    control->phase = phase;
    control->reserved[0] = 0;
    control->reserved[1] = 0;
    control->reserved[2] = 0;
    control->witness = RuntimeWitnessFor(phase);
}

void InitializePhysicalMemoryNoReport(
    PhysicalMemory *const memory,
    std::uint8_t *const base,
    const std::uint32_t size) noexcept
{
    std::memset(memory, 0, sizeof(*memory));
    memory->buf = base;
    memory->prim[1].pos = size;
}

LegacyOperationResult TryBeginAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::BeginNullPrim, nullptr};
    if (!name)
        return {LegacyDiagnostic::BeginNullName, nullptr};
    if (prim->allocName)
        return {LegacyDiagnostic::BeginBusy, nullptr};
    if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::BeginFull, nullptr};

    prim->allocName = name;
    PhysicalMemoryAllocation &entry =
        prim->allocList[prim->allocListCount++];
    entry.name = name;
    entry.pos = prim->pos;
    return {};
}

LegacyOperationResult TryEndAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::EndNullPrim, nullptr};
    if (!name)
        return {LegacyDiagnostic::EndNullName, nullptr};
    if (prim->allocName != name)
        return {LegacyDiagnostic::EndWrongActive, nullptr};
    if (!prim->allocListCount)
        return {LegacyDiagnostic::EndEmpty, nullptr};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::EndOverCapacity, nullptr};
    const PhysicalMemoryAllocation &entry =
        prim->allocList[prim->allocListCount - 1];
    if (entry.name != name)
        return {LegacyDiagnostic::EndWrongTail, nullptr};

    prim->allocName = nullptr;
    return {};
}

LegacyOperationResult TryFreeIndexNoReport(
    PhysicalMemoryPrim *const prim,
    const std::uint32_t allocIndex) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::FreeIndexNullPrim, nullptr};
    if (prim->allocName)
        return {LegacyDiagnostic::FreeIndexBusy, nullptr};
    if (!prim->allocListCount)
        return {LegacyDiagnostic::FreeIndexEmpty, nullptr};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::FreeIndexOverCapacity, nullptr};
    if (allocIndex >= prim->allocListCount)
        return {LegacyDiagnostic::FreeIndexInvalidIndex, nullptr};

    PhysicalMemoryAllocation *entry = &prim->allocList[allocIndex];
    const char *const name = entry->name;
    if (!name)
        return {LegacyDiagnostic::FreeIndexNullName, nullptr};

    entry->name = nullptr;
    if (allocIndex == prim->allocListCount - 1)
    {
        do
        {
            prim->pos = entry->pos;
            if (!--prim->allocListCount)
                break;
            entry = &prim->allocList[prim->allocListCount - 1];
        } while (!entry->name);
        return {};
    }
    return {LegacyDiagnostic::FreeHole, name};
}

LegacyOperationResult TryFreeInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::FreeInPrimNullPrim, nullptr};
    if (!name)
        return {LegacyDiagnostic::FreeInPrimNullName, nullptr};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::FreeInPrimOverCapacity, nullptr};

    for (std::uint32_t index = 0; index < prim->allocListCount; ++index)
    {
        if (prim->allocList[index].name == name)
            return TryFreeIndexNoReport(prim, index);
    }
    return {};
}

LegacyOperationResult ReadinessDiagnostic() noexcept
{
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::Ready:
        return {};
    case RuntimeReadiness::NotReady:
        return {LegacyDiagnostic::RuntimeNotReady, nullptr};
    case RuntimeReadiness::Corrupt:
        return {LegacyDiagnostic::RuntimeCorrupt, nullptr};
    }
    return {LegacyDiagnostic::RuntimeCorrupt, nullptr};
}

void ReportLegacyDiagnostic(
    const LegacyOperationResult result,
    const std::uint32_t allocType) noexcept
{
    switch (result.diagnostic)
    {
    case LegacyDiagnostic::None:
        return;
    case LegacyDiagnostic::BeginInvalidType:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 350, 0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType, 2u);
        return;
    case LegacyDiagnostic::EndInvalidType:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 378, 0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType, 2u);
        return;
    case LegacyDiagnostic::FreeInvalidType:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 454, 0,
            "allocType doesn't index PHYS_ALLOC_COUNT\n\t%u not in [0, %u)",
            allocType, 2u);
        return;
    case LegacyDiagnostic::RuntimeNotReady:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 456, 0, "%s",
            "physical-memory runtime is ready");
        return;
    case LegacyDiagnostic::RuntimeCorrupt:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 457, 0, "%s",
            "physical-memory runtime state is coherent");
        return;
    case LegacyDiagnostic::BeginNullPrim:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 330, 0, "%s", "prim");
        return;
    case LegacyDiagnostic::BeginNullName:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 331, 0, "%s", "name");
        return;
    case LegacyDiagnostic::BeginBusy:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 332, 0, "%s", "!prim->allocName");
        return;
    case LegacyDiagnostic::BeginFull:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 333, 0, "%s", "prim->allocListCount < MAX_PHYSICAL_ALLOCATIONS");
        return;
    case LegacyDiagnostic::EndNullPrim:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 361, 0, "%s", "prim");
        return;
    case LegacyDiagnostic::EndNullName:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 362, 0, "%s", "name");
        return;
    case LegacyDiagnostic::EndWrongActive:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 364, 0, "%s", "prim->allocName == name");
        return;
    case LegacyDiagnostic::EndEmpty:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 368, 0, "%s", "prim->allocListCount > 0");
        return;
    case LegacyDiagnostic::EndOverCapacity:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 369, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    case LegacyDiagnostic::EndWrongTail:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 370, 0, "%s", "allocEntry.name == name");
        return;
    case LegacyDiagnostic::FreeIndexNullPrim:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 393, 0, "%s", "prim");
        return;
    case LegacyDiagnostic::FreeIndexBusy:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 394, 0, "%s", "!prim->allocName");
        return;
    case LegacyDiagnostic::FreeIndexEmpty:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 424, 0, "%s", "prim->allocListCount");
        return;
    case LegacyDiagnostic::FreeIndexOverCapacity:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 395, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    case LegacyDiagnostic::FreeIndexInvalidIndex:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 408, 0, "%s", "allocIndex < prim->allocListCount");
        return;
    case LegacyDiagnostic::FreeIndexNullName:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 400, 0, "%s", "name");
        return;
    case LegacyDiagnostic::FreeInPrimNullPrim:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 437, 0, "%s", "prim");
        return;
    case LegacyDiagnostic::FreeInPrimNullName:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 438, 0, "%s", "name");
        return;
    case LegacyDiagnostic::FreeInPrimOverCapacity:
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 439, 0, "%s", "prim->allocListCount <= MAX_PHYSICAL_ALLOCATIONS");
        return;
    case LegacyDiagnostic::FreeHole:
        if (!alwaysfails)
        {
            MyAssertHandler(
                ".\\universal\\physicalmemory.cpp", 411, 0,
                "freeing '%s' caused a memory hole\n", result.name);
        }
        return;
    }
}

int LegacyShortfallFor(const pmem_runtime::AllocationResult &result) noexcept
{
    if (result.status == pmem_runtime::AllocationStatus::Success)
        return 0;
    if (result.status != pmem_runtime::AllocationStatus::Exhausted)
        return INT_MAX;
    return result.additionalBytes > static_cast<std::uint64_t>(INT_MAX)
        ? INT_MAX
        : static_cast<int>(result.additionalBytes);
}

void PublishPoisonedExtent(
    std::uint8_t *const base,
    const std::uint32_t size) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    g_mem = {};
    g_runtime.extent = {base, size};
    SetRuntimePhase(
        &g_runtime, pmem_runtime::InitializationPhase::Poisoned);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
}

pmem_runtime::AllocationResult TryAllocateNoLock(
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    const std::uint32_t allocType) noexcept
{
    (void)type;
    pmem_runtime::AllocationResult result{};
    if (allocType >= ARRAY_COUNT(g_mem.prim) || !size || !alignment
        || (alignment & (alignment - 1)) != 0)
    {
        result.status = pmem_runtime::AllocationStatus::InvalidRequest;
        return result;
    }

    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        result.status = pmem_runtime::AllocationStatus::NotReady;
        return result;
    case RuntimeReadiness::Corrupt:
        result.status = pmem_runtime::AllocationStatus::CorruptState;
        return result;
    case RuntimeReadiness::Ready:
        break;
    }

    PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    if (!prim.allocName)
    {
        result.status = pmem_runtime::AllocationStatus::ScopeInactive;
        return result;
    }

    const std::uint32_t alignmentMask = alignment - 1;
    const std::uint64_t baseRemainder =
        reinterpret_cast<std::uintptr_t>(g_mem.buf) & alignmentMask;
    std::uint64_t allocationPosition = 0;
    if (allocType)
    {
        const std::uint64_t highPosition = g_mem.prim[1].pos;
        const std::uint64_t lowPosition = g_mem.prim[0].pos;
        const std::uint64_t lowRemainder =
            (baseRemainder + (lowPosition & alignmentMask))
            & alignmentMask;
        const std::uint64_t lowPadding = lowRemainder
            ? alignment - lowRemainder
            : 0;
        const std::uint64_t firstAligned = lowPosition + lowPadding;
        const std::uint64_t requiredEnd = firstAligned + size;
        if (requiredEnd > highPosition)
        {
            result.additionalBytes = requiredEnd - highPosition;
        }
        else
        {
            const std::uint64_t rawPosition = highPosition - size;
            const std::uint64_t absoluteRemainder =
                (baseRemainder + (rawPosition & alignmentMask))
                & alignmentMask;
            allocationPosition = rawPosition - absoluteRemainder;
        }
    }
    else
    {
        const std::uint64_t absoluteRemainder =
            (baseRemainder + (prim.pos & alignmentMask)) & alignmentMask;
        const std::uint64_t padding = absoluteRemainder
            ? alignment - absoluteRemainder
            : 0;
        allocationPosition = static_cast<std::uint64_t>(prim.pos) + padding;
        const std::uint64_t newPosition = allocationPosition + size;
        if (newPosition > g_mem.prim[1].pos)
            result.additionalBytes = newPosition - g_mem.prim[1].pos;
    }

    if (result.additionalBytes)
    {
        result.status = pmem_runtime::AllocationStatus::Exhausted;
        return result;
    }

    if (allocType)
        prim.pos = static_cast<std::uint32_t>(allocationPosition);
    else
        prim.pos = static_cast<std::uint32_t>(allocationPosition + size);
    result.address = &g_mem.buf[static_cast<std::uint32_t>(allocationPosition)];
    result.status = pmem_runtime::AllocationStatus::Success;
    return result;
}

pmem_runtime::AllocationResult TryAllocateAndPublishLegacyShortfall(
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    const std::uint32_t allocType) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const pmem_runtime::AllocationResult result =
        TryAllocateNoLock(size, alignment, type, allocType);
    g_overAllocatedSize = LegacyShortfallFor(result);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return result;
}
} // namespace

#if defined(KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING)
PhysicalMemoryGlobalStateTestAccess::Snapshot
PhysicalMemoryGlobalStateTestAccess::Capture() noexcept
{
    Snapshot snapshot{};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    snapshot.memory = g_mem;
    snapshot.overAllocatedSize = g_overAllocatedSize;
    snapshot.retainedBase = g_runtime.extent.base;
    snapshot.retainedSize = g_runtime.extent.size;
    snapshot.initializationPhase =
        static_cast<std::uint8_t>(g_runtime.phase);
    snapshot.runtimeReserved[0] = g_runtime.reserved[0];
    snapshot.runtimeReserved[1] = g_runtime.reserved[1];
    snapshot.runtimeReserved[2] = g_runtime.reserved[2];
    snapshot.initializationWitness = g_runtime.witness;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return snapshot;
}

PhysicalMemoryGlobalStateTestAccess::Snapshot
PhysicalMemoryGlobalStateTestAccess::MakeCanonicalReady(
    const PhysicalMemory &memory,
    const std::uint32_t retainedSize,
    const int overAllocatedSize) noexcept
{
    Snapshot snapshot{};
    snapshot.memory = memory;
    snapshot.overAllocatedSize = overAllocatedSize;
    snapshot.retainedBase = memory.buf;
    snapshot.retainedSize = retainedSize;
    snapshot.initializationPhase = static_cast<std::uint8_t>(
        pmem_runtime::InitializationPhase::Ready);
    snapshot.initializationWitness = kReadyWitness;
    return snapshot;
}

void PhysicalMemoryGlobalStateTestAccess::Install(
    const Snapshot &snapshot) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    g_mem = snapshot.memory;
    g_overAllocatedSize = snapshot.overAllocatedSize;
    g_runtime.extent.base = snapshot.retainedBase;
    g_runtime.extent.size = snapshot.retainedSize;
    g_runtime.phase = static_cast<pmem_runtime::InitializationPhase>(
        snapshot.initializationPhase);
    g_runtime.reserved[0] = snapshot.runtimeReserved[0];
    g_runtime.reserved[1] = snapshot.runtimeReserved[1];
    g_runtime.reserved[2] = snapshot.runtimeReserved[2];
    g_runtime.witness = snapshot.initializationWitness;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
}
#endif

pmem_runtime::InitializationStatus KISAK_CDECL
pmem_runtime::TryInitialize() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (!RuntimeControlIsCanonical(g_runtime))
    {
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::CorruptState;
    }
    switch (g_runtime.phase)
    {
    case InitializationPhase::Ready:
    {
        const bool coherent = ReadyStateIsCoherent();
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return coherent
            ? InitializationStatus::AlreadyInitialized
            : InitializationStatus::CorruptState;
    }
    case InitializationPhase::Initializing:
        if (!InitializingStateIsCoherent())
        {
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            return InitializationStatus::CorruptState;
        }
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::Busy;
    case InitializationPhase::Poisoned:
        if (!PoisonedStateIsCoherent())
        {
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            return InitializationStatus::CorruptState;
        }
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::Poisoned;
    case InitializationPhase::Uninitialized:
        if (!PhysicalMemoryIsPristine(g_mem))
        {
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            return InitializationStatus::CorruptState;
        }
        SetRuntimePhase(&g_runtime, InitializationPhase::Initializing);
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        break;
    }

    void *const reservation = Sys_VirtualMemoryReserve(kPhysicalMemorySize);
    if (!reservation)
    {
        Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        if (!InitializingStateIsCoherent())
        {
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            return InitializationStatus::CorruptState;
        }
        g_runtime = {};
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::ReserveFailed;
    }

    std::uint8_t *const base = static_cast<std::uint8_t *>(reservation);
    const RetainedExtent candidateExtent{base, kPhysicalMemorySize};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const bool initializingCoherent = InitializingStateIsCoherent();
    const bool candidateValid =
        RetainedExtentIsDisjointFromControl(candidateExtent);
    if (!initializingCoherent || !candidateValid)
    {
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        const bool released = Sys_VirtualMemoryRelease(reservation);
        if (!released)
        {
            PublishPoisonedExtent(base, kPhysicalMemorySize);
            return InitializationStatus::ReleaseFailed;
        }
        Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        if (InitializingStateIsCoherent())
            g_runtime = {};
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::CorruptState;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);

    if (!Sys_VirtualMemoryCommit(reservation, kPhysicalMemorySize))
    {
        const bool released = Sys_VirtualMemoryRelease(reservation);
        Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        if (!InitializingStateIsCoherent())
        {
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            if (!released)
            {
                PublishPoisonedExtent(base, kPhysicalMemorySize);
                return InitializationStatus::ReleaseFailed;
            }
            return InitializationStatus::CorruptState;
        }
        if (released)
        {
            g_runtime = {};
            Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
            return InitializationStatus::CommitFailed;
        }
        g_mem = {};
        g_runtime.extent = candidateExtent;
        SetRuntimePhase(&g_runtime, InitializationPhase::Poisoned);
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        return InitializationStatus::ReleaseFailed;
    }

    PhysicalMemory initialized{};
    InitializePhysicalMemoryNoReport(
        &initialized, base, kPhysicalMemorySize);
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (!InitializingStateIsCoherent()
        || !RetainedExtentIsDisjointFromControl(candidateExtent))
    {
        Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
        const bool released = Sys_VirtualMemoryRelease(reservation);
        if (!released)
        {
            PublishPoisonedExtent(base, kPhysicalMemorySize);
            return InitializationStatus::ReleaseFailed;
        }
        return InitializationStatus::CorruptState;
    }
    g_mem = initialized;
    g_runtime.extent = candidateExtent;
    SetRuntimePhase(&g_runtime, InitializationPhase::Ready);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return InitializationStatus::Success;
}

pmem_runtime::AllocationResult KISAK_CDECL pmem_runtime::TryAllocate(
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    const std::uint32_t allocType) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const AllocationResult result =
        TryAllocateNoLock(size, alignment, type, allocType);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return result;
}

void KISAK_CDECL PMem_Init()
{
    const pmem_runtime::InitializationStatus status =
        pmem_runtime::TryInitialize();
    if (status == pmem_runtime::InitializationStatus::Success
        || status == pmem_runtime::InitializationStatus::AlreadyInitialized)
    {
        return;
    }
    Sys_OutOfMemErrorInternal(".\\universal\\physicalmemory.cpp", 17);
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

void KISAK_CDECL PMem_InitPhysicalMemory(
    PhysicalMemory *const pmem,
    std::uint8_t *const memory,
    const std::uint32_t memorySize)
{
    if (!pmem)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 277, 0, "%s", "pmem");
        return;
    }
    if (!memory)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 278, 0, "%s", "memory");
        return;
    }
    if (!memorySize)
    {
        MyAssertHandler(".\\universal\\physicalmemory.cpp", 279, 0, "%s", "memorySize");
        return;
    }
    InitializePhysicalMemoryNoReport(pmem, memory, memorySize);
}

void KISAK_CDECL PMem_BeginAlloc(
    const char *const name,
    const std::uint32_t allocType)
{
    LegacyOperationResult result{};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (allocType >= ARRAY_COUNT(g_mem.prim))
        result.diagnostic = LegacyDiagnostic::BeginInvalidType;
    else
    {
        result = ReadinessDiagnostic();
        if (result.diagnostic == LegacyDiagnostic::None)
            result = TryBeginAllocInPrimNoReport(&g_mem.prim[allocType], name);
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    ReportLegacyDiagnostic(result, allocType);
}

void KISAK_CDECL PMem_BeginAllocInPrim(
    PhysicalMemoryPrim *const prim,
    const char *const name)
{
    ReportLegacyDiagnostic(TryBeginAllocInPrimNoReport(prim, name), 0);
}

void KISAK_CDECL PMem_EndAlloc(
    const char *const name,
    const std::uint32_t allocType)
{
    LegacyOperationResult result{};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (allocType >= ARRAY_COUNT(g_mem.prim))
        result.diagnostic = LegacyDiagnostic::EndInvalidType;
    else
    {
        result = ReadinessDiagnostic();
        if (result.diagnostic == LegacyDiagnostic::None)
            result = TryEndAllocInPrimNoReport(&g_mem.prim[allocType], name);
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    ReportLegacyDiagnostic(result, allocType);
}

void KISAK_CDECL PMem_EndAllocInPrim(
    PhysicalMemoryPrim *const prim,
    const char *const name)
{
    ReportLegacyDiagnostic(TryEndAllocInPrimNoReport(prim, name), 0);
}

void KISAK_CDECL PMem_Free(
    const char *const name,
    const std::uint32_t allocType)
{
    LegacyOperationResult result{};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (allocType >= ARRAY_COUNT(g_mem.prim))
        result.diagnostic = LegacyDiagnostic::FreeInvalidType;
    else
    {
        result = ReadinessDiagnostic();
        if (result.diagnostic == LegacyDiagnostic::None)
            result = TryFreeInPrimNoReport(&g_mem.prim[allocType], name);
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    ReportLegacyDiagnostic(result, allocType);
}

void KISAK_CDECL PMem_FreeInPrim(
    PhysicalMemoryPrim *const prim,
    const char *const name)
{
    ReportLegacyDiagnostic(TryFreeInPrimNoReport(prim, name), 0);
}

void KISAK_CDECL PMem_FreeIndex(
    PhysicalMemoryPrim *const prim,
    const std::uint32_t allocIndex)
{
    ReportLegacyDiagnostic(TryFreeIndexNoReport(prim, allocIndex), 0);
}

int KISAK_CDECL PMem_GetOverAllocatedSize()
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const int shortfall = g_overAllocatedSize;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return shortfall;
}

std::uint8_t *KISAK_CDECL PMem_Alloc(
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    const std::uint32_t allocType)
{
    const pmem_runtime::AllocationResult result =
        TryAllocateAndPublishLegacyShortfall(
            size, alignment, type, allocType);
    switch (result.status)
    {
    case pmem_runtime::AllocationStatus::Success:
        return result.address;
    case pmem_runtime::AllocationStatus::InvalidRequest:
        Com_Error(ERR_FATAL, "Invalid physical-memory allocation request");
        return nullptr;
    case pmem_runtime::AllocationStatus::ScopeInactive:
        Com_Error(
            ERR_FATAL,
            "Physical-memory allocation is outside an allocation scope");
        return nullptr;
    case pmem_runtime::AllocationStatus::NotReady:
    case pmem_runtime::AllocationStatus::CorruptState:
        Com_Error(ERR_FATAL, "Physical-memory runtime is not ready");
        return nullptr;
    case pmem_runtime::AllocationStatus::Exhausted:
        Sys_OutOfMemErrorInternal(".\\universal\\physicalmemory.cpp", 0);
        return nullptr;
    }
    return nullptr;
}

std::uint8_t *KISAK_CDECL PMem_TryAlloc(
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    const std::uint32_t allocType)
{
    const pmem_runtime::AllocationResult result =
        TryAllocateAndPublishLegacyShortfall(
            size, alignment, type, allocType);
    return result.status == pmem_runtime::AllocationStatus::Success
        ? result.address
        : nullptr;
}

std::uint32_t KISAK_CDECL PMem_GetFreeAmount()
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const std::uint32_t freeAmount = ReadyStateIsCoherent()
        ? g_mem.prim[1].pos - g_mem.prim[0].pos
        : 0;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return freeAmount;
}

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
static_assert(kPhysicalMemorySize <= static_cast<std::uint32_t>(INT_MAX));
constexpr std::uint32_t kInitializingWitness = UINT32_C(0x494E4954);
constexpr std::uint32_t kReadyWitness = UINT32_C(0x52454144);
constexpr std::uint32_t kPoisonedWitness = UINT32_C(0x504F4953);
constexpr std::size_t kOwnedNameCapacity = MAX_QPATH;
static_assert(kOwnedNameCapacity == 64u);

struct RetainedExtent final
{
    std::uint8_t *base = nullptr;
    std::uint32_t size = 0;
};

struct OwnedAllocationName final
{
    std::uintptr_t identity = 0;
    std::uintptr_t identityWitness = 0;
    char text[kOwnedNameCapacity]{};
};

struct OwnedNameState final
{
    OwnedAllocationName names[2][MAX_PHYSICAL_ALLOCATIONS]{};
};

struct RuntimeControl final
{
    RetainedExtent extent{};
    OwnedNameState ownedNames{};
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
    const char *borrowedName = nullptr;
    bool ownsName = false;
    char ownedName[kOwnedNameCapacity]{};
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

template <std::size_t Capacity>
void CopyBoundedName(
    char (&destination)[Capacity],
    const char *const source) noexcept
{
    std::memset(destination, 0, sizeof(destination));
    if (!source)
        return;
    for (std::size_t index = 0; index + 1 < sizeof(destination); ++index)
    {
        const char value = source[index];
        destination[index] = value;
        if (!value)
            return;
    }
}

std::uintptr_t OwnedNameIdentityWitness(
    const std::uintptr_t identity,
    const std::uint32_t type,
    const std::uint32_t index) noexcept
{
    if (!identity)
        return 0;
    const std::uintptr_t salt = static_cast<std::uintptr_t>(
        UINT64_C(0x9E3779B97F4A7C15)
        ^ static_cast<std::uint64_t>(type) * UINT64_C(0xD1B54A32D192ED03)
        ^ static_cast<std::uint64_t>(index) * UINT64_C(0x94D049BB133111EB));
    return ~(identity ^ salt);
}

OwnedAllocationName CaptureOwnedName(
    const char *const source,
    const std::uint32_t type,
    const std::uint32_t index) noexcept
{
    OwnedAllocationName owned{};
    if (!source)
        return owned;
    CopyBoundedName(owned.text, source);
    owned.identity = reinterpret_cast<std::uintptr_t>(source);
    owned.identityWitness =
        OwnedNameIdentityWitness(owned.identity, type, index);
    return owned;
}

bool OwnedNameTextIsCanonical(
    const char (&text)[kOwnedNameCapacity]) noexcept
{
    bool terminated = false;
    for (const char value : text)
    {
        if (terminated && value)
            return false;
        if (!value)
            terminated = true;
    }
    return terminated;
}

bool OwnedNameIsPristine(const OwnedAllocationName &name) noexcept
{
    if (name.identity || name.identityWitness)
        return false;
    for (const char value : name.text)
    {
        if (value)
            return false;
    }
    return true;
}

bool OwnedNamesArePristine(const OwnedNameState &names) noexcept
{
    for (const auto &primNames : names.names)
    {
        for (const OwnedAllocationName &name : primNames)
        {
            if (!OwnedNameIsPristine(name))
                return false;
        }
    }
    return true;
}

bool OwnedNamesMatchMemory() noexcept
{
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        const PhysicalMemoryPrim &prim = g_mem.prim[type];
        if (prim.allocListCount > MAX_PHYSICAL_ALLOCATIONS)
            return false;
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const PhysicalMemoryAllocation &allocation =
                prim.allocList[index];
            const OwnedAllocationName &owned =
                g_runtime.ownedNames.names[type][index];
            const bool live = index < prim.allocListCount && allocation.name;
            if (!live)
            {
                if (allocation.name || !OwnedNameIsPristine(owned))
                    return false;
                continue;
            }
            if (!owned.identity
                || owned.identityWitness
                    != OwnedNameIdentityWitness(owned.identity, type, index)
                || !OwnedNameTextIsCanonical(owned.text)
                || allocation.name != owned.text)
            {
                return false;
            }
        }
    }
    return true;
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
        || g_mem.prim[1].pos > g_runtime.extent.size
        || !OwnedNamesMatchMemory())
    {
        return false;
    }
    return PrimTopologyIsBounded(
               g_mem.prim[0], false, g_runtime.extent.size)
        && PrimTopologyIsBounded(
               g_mem.prim[1], true, g_runtime.extent.size);
}

bool UninitializedStateIsCoherent() noexcept
{
    return RuntimeControlIsCanonical(g_runtime)
        && g_runtime.phase == pmem_runtime::InitializationPhase::Uninitialized
        && PhysicalMemoryIsPristine(g_mem)
        && OwnedNamesArePristine(g_runtime.ownedNames);
}

bool InitializingStateIsCoherent() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime)
        || g_runtime.phase != pmem_runtime::InitializationPhase::Initializing
        || !PhysicalMemoryIsPristine(g_mem)
        || !OwnedNamesArePristine(g_runtime.ownedNames))
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
        && PhysicalMemoryIsPristine(g_mem)
        && OwnedNamesArePristine(g_runtime.ownedNames);
}

RuntimeReadiness GetRuntimeReadiness() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime))
        return RuntimeReadiness::Corrupt;
    switch (g_runtime.phase)
    {
    case pmem_runtime::InitializationPhase::Uninitialized:
        return UninitializedStateIsCoherent()
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

LegacyOperationResult ValidateBeginAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::BeginNullPrim};
    if (!name)
        return {LegacyDiagnostic::BeginNullName};
    if (prim->allocName)
        return {LegacyDiagnostic::BeginBusy};
    if (prim->allocListCount >= MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::BeginFull};
    return {};
}

void BeginAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    prim->allocName = name;
    PhysicalMemoryAllocation &entry =
        prim->allocList[prim->allocListCount++];
    entry.name = name;
    entry.pos = prim->pos;
}

LegacyOperationResult TryBeginAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    const LegacyOperationResult validation =
        ValidateBeginAllocInPrimNoReport(prim, name);
    if (validation.diagnostic != LegacyDiagnostic::None)
        return validation;

    BeginAllocInPrimNoReport(prim, name);
    return {};
}

LegacyOperationResult TryBeginGlobalAllocNoReport(
    const std::uint32_t allocType,
    const char *const name) noexcept
{
    PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    const LegacyOperationResult validation =
        ValidateBeginAllocInPrimNoReport(&prim, name);
    if (validation.diagnostic != LegacyDiagnostic::None)
        return validation;

    OwnedAllocationName &owned =
        g_runtime.ownedNames.names[allocType][prim.allocListCount];
    owned = CaptureOwnedName(name, allocType, prim.allocListCount);
    BeginAllocInPrimNoReport(&prim, owned.text);
    return {};
}

LegacyOperationResult TryEndAllocInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::EndNullPrim};
    if (!name)
        return {LegacyDiagnostic::EndNullName};
    if (prim->allocName != name)
        return {LegacyDiagnostic::EndWrongActive};
    if (!prim->allocListCount)
        return {LegacyDiagnostic::EndEmpty};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::EndOverCapacity};
    const PhysicalMemoryAllocation &entry =
        prim->allocList[prim->allocListCount - 1];
    if (entry.name != name)
        return {LegacyDiagnostic::EndWrongTail};

    prim->allocName = nullptr;
    return {};
}

LegacyOperationResult TryEndGlobalAllocNoReport(
    const std::uint32_t allocType,
    const char *const name) noexcept
{
    if (!name)
        return {LegacyDiagnostic::EndNullName};
    PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    if (!prim.allocName || !prim.allocListCount
        || prim.allocListCount > MAX_PHYSICAL_ALLOCATIONS)
    {
        return {LegacyDiagnostic::EndWrongActive};
    }
    const OwnedAllocationName &owned =
        g_runtime.ownedNames.names[allocType][prim.allocListCount - 1];
    if (owned.identity != reinterpret_cast<std::uintptr_t>(name))
        return {LegacyDiagnostic::EndWrongActive};
    return TryEndAllocInPrimNoReport(&prim, prim.allocName);
}

LegacyOperationResult TryFreeIndexNoReport(
    PhysicalMemoryPrim *const prim,
    const std::uint32_t allocIndex) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::FreeIndexNullPrim};
    if (prim->allocName)
        return {LegacyDiagnostic::FreeIndexBusy};
    if (!prim->allocListCount)
        return {LegacyDiagnostic::FreeIndexEmpty};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::FreeIndexOverCapacity};
    if (allocIndex >= prim->allocListCount)
        return {LegacyDiagnostic::FreeIndexInvalidIndex};

    PhysicalMemoryAllocation *entry = &prim->allocList[allocIndex];
    const char *const name = entry->name;
    if (!name)
        return {LegacyDiagnostic::FreeIndexNullName};

    const bool freesTail = allocIndex == prim->allocListCount - 1;
    LegacyOperationResult hole{};
    if (!freesTail)
    {
        hole.diagnostic = LegacyDiagnostic::FreeHole;
        hole.borrowedName = name;
    }

    entry->name = nullptr;
    if (freesTail)
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
    return hole;
}

LegacyOperationResult TryFreeInPrimNoReport(
    PhysicalMemoryPrim *const prim,
    const char *const name) noexcept
{
    if (!prim)
        return {LegacyDiagnostic::FreeInPrimNullPrim};
    if (!name)
        return {LegacyDiagnostic::FreeInPrimNullName};
    if (prim->allocListCount > MAX_PHYSICAL_ALLOCATIONS)
        return {LegacyDiagnostic::FreeInPrimOverCapacity};

    for (std::uint32_t index = 0; index < prim->allocListCount; ++index)
    {
        if (prim->allocList[index].name == name)
            return TryFreeIndexNoReport(prim, index);
    }
    return {};
}

LegacyOperationResult TryFreeGlobalIndexNoReport(
    const std::uint32_t allocType,
    const std::uint32_t allocIndex) noexcept
{
    PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    const std::uint32_t previousCount = prim.allocListCount;
    LegacyOperationResult result =
        TryFreeIndexNoReport(&prim, allocIndex);
    if (result.diagnostic != LegacyDiagnostic::None
        && result.diagnostic != LegacyDiagnostic::FreeHole)
    {
        return result;
    }

    if (result.diagnostic == LegacyDiagnostic::FreeHole)
    {
        CopyBoundedName(result.ownedName, result.borrowedName);
        result.borrowedName = nullptr;
        result.ownsName = true;
    }

    g_runtime.ownedNames.names[allocType][allocIndex] = {};
    for (std::uint32_t index = prim.allocListCount;
         index < previousCount;
         ++index)
    {
        g_runtime.ownedNames.names[allocType][index] = {};
    }
    return result;
}

LegacyOperationResult TryFreeGlobalAllocNoReport(
    const std::uint32_t allocType,
    const char *const name) noexcept
{
    if (!name)
        return {LegacyDiagnostic::FreeInPrimNullName};
    const PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    const std::uintptr_t identity = reinterpret_cast<std::uintptr_t>(name);
    for (std::uint32_t index = 0; index < prim.allocListCount; ++index)
    {
        if (g_runtime.ownedNames.names[allocType][index].identity == identity)
            return TryFreeGlobalIndexNoReport(allocType, index);
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
        return {LegacyDiagnostic::RuntimeNotReady};
    case RuntimeReadiness::Corrupt:
        return {LegacyDiagnostic::RuntimeCorrupt};
    }
    return {LegacyDiagnostic::RuntimeCorrupt};
}

pmem_runtime::DiagnosticSnapshot
TryCaptureDiagnosticSnapshotNoLock() noexcept
{
    pmem_runtime::DiagnosticSnapshot snapshot{};
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        return snapshot;
    case RuntimeReadiness::Corrupt:
        snapshot.status =
            pmem_runtime::DiagnosticSnapshotStatus::CorruptState;
        return snapshot;
    case RuntimeReadiness::Ready:
        break;
    }

    const PhysicalMemoryPrim &high = g_mem.prim[1];
    snapshot.highCount = high.allocListCount;
    for (std::uint32_t index = 0; index < high.allocListCount; ++index)
    {
        const PhysicalMemoryAllocation &allocation = high.allocList[index];
        const std::uint32_t bottom = index + 1 < high.allocListCount
            ? high.allocList[index + 1].pos
            : high.pos;
        pmem_runtime::DiagnosticEntry &entry = snapshot.high[index];
        entry.bytes = allocation.pos - bottom;
        if (allocation.name)
        {
            CopyBoundedName(
                entry.name,
                g_runtime.ownedNames.names[1][index].text);
            entry.kind = pmem_runtime::DiagnosticEntryKind::Allocation;
        }
        else
        {
            CopyBoundedName(entry.name, "<hole>");
            entry.kind = pmem_runtime::DiagnosticEntryKind::Hole;
        }
    }

    const PhysicalMemoryPrim &low = g_mem.prim[0];
    snapshot.lowCount = low.allocListCount;
    for (std::uint32_t index = 0; index < low.allocListCount; ++index)
    {
        const PhysicalMemoryAllocation &allocation = low.allocList[index];
        const std::uint32_t top = index + 1 < low.allocListCount
            ? low.allocList[index + 1].pos
            : low.pos;
        pmem_runtime::DiagnosticEntry &entry = snapshot.low[index];
        entry.bytes = top - allocation.pos;
        if (allocation.name)
        {
            CopyBoundedName(
                entry.name,
                g_runtime.ownedNames.names[0][index].text);
            entry.kind = pmem_runtime::DiagnosticEntryKind::Allocation;
        }
        else
        {
            CopyBoundedName(entry.name, "<hole>");
            entry.kind = pmem_runtime::DiagnosticEntryKind::Hole;
        }
    }

    snapshot.freeBytes = high.pos - low.pos;
    snapshot.status = pmem_runtime::DiagnosticSnapshotStatus::Success;
    return snapshot;
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
            const char *const reportedName = result.ownsName
                ? result.ownedName
                : result.borrowedName;
            MyAssertHandler(
                ".\\universal\\physicalmemory.cpp", 411, 0,
                "freeing '%s' caused a memory hole\n", reportedName);
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
    g_runtime = {};
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
using TestOwnedNameBinding =
    PhysicalMemoryGlobalStateTestAccess::OwnedNameBindingSnapshot;

const char *LogicalizeTestNamePointer(
    const char *const pointer,
    TestOwnedNameBinding *const binding) noexcept
{
    *binding = {};
    if (!pointer)
        return nullptr;
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const OwnedAllocationName &owned =
                g_runtime.ownedNames.names[type][index];
            if (pointer == owned.text)
            {
                binding->type = static_cast<std::uint8_t>(type);
                binding->index = static_cast<std::uint8_t>(index);
                return owned.identity
                    ? reinterpret_cast<const char *>(owned.identity)
                    : reinterpret_cast<const char *>(
                        static_cast<std::uintptr_t>(1));
            }
        }
    }
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(pointer);
    const std::uintptr_t controlBegin =
        reinterpret_cast<std::uintptr_t>(&g_runtime);
    if (address >= controlBegin
        && address - controlBegin < sizeof(g_runtime))
    {
        return reinterpret_cast<const char *>(
            static_cast<std::uintptr_t>(1));
    }
    const std::uintptr_t memoryBegin =
        reinterpret_cast<std::uintptr_t>(&g_mem);
    if (address >= memoryBegin && address - memoryBegin < sizeof(g_mem))
    {
        return reinterpret_cast<const char *>(
            static_cast<std::uintptr_t>(1));
    }
    const std::uintptr_t shortfallBegin =
        reinterpret_cast<std::uintptr_t>(&g_overAllocatedSize);
    if (address >= shortfallBegin
        && address - shortfallBegin < sizeof(g_overAllocatedSize))
    {
        return reinterpret_cast<const char *>(
            static_cast<std::uintptr_t>(1));
    }
    return pointer;
}

PhysicalMemoryGlobalStateTestAccess::Snapshot
PhysicalMemoryGlobalStateTestAccess::Capture() noexcept
{
    static_assert(OWNED_NAME_CAPACITY == kOwnedNameCapacity);
    Snapshot snapshot{};
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    snapshot.memory = g_mem;
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        PhysicalMemoryPrim &prim = snapshot.memory.prim[type];
        prim.allocName = LogicalizeTestNamePointer(
            prim.allocName, &snapshot.allocNameBindings[type]);
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            PhysicalMemoryAllocation &allocation = prim.allocList[index];
            allocation.name = LogicalizeTestNamePointer(
                allocation.name,
                &snapshot.allocationNameBindings[type][index]);
        }
    }
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const OwnedAllocationName &source =
                g_runtime.ownedNames.names[type][index];
            OwnedNameSnapshot &destination = snapshot.ownedNames[type][index];
            destination.identity = source.identity;
            destination.identityWitness = source.identityWitness;
            for (std::size_t byte = 0; byte < kOwnedNameCapacity; ++byte)
                destination.text[byte] = source.text[byte];
        }
    }
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
    for (std::uint32_t type = 0; type < ARRAY_COUNT(memory.prim); ++type)
    {
        const PhysicalMemoryPrim &sourcePrim = memory.prim[type];
        const std::uint32_t boundedCount =
            sourcePrim.allocListCount <= MAX_PHYSICAL_ALLOCATIONS
            ? sourcePrim.allocListCount
            : MAX_PHYSICAL_ALLOCATIONS;
        for (std::uint32_t index = 0; index < boundedCount; ++index)
        {
            const char *const sourceName = sourcePrim.allocList[index].name;
            if (!sourceName)
                continue;
            OwnedNameSnapshot &owned = snapshot.ownedNames[type][index];
            owned.identity = reinterpret_cast<std::uintptr_t>(sourceName);
            owned.identityWitness =
                OwnedNameIdentityWitness(owned.identity, type, index);
            CopyBoundedName(owned.text, sourceName);
            snapshot.allocationNameBindings[type][index].type =
                static_cast<std::uint8_t>(type);
            snapshot.allocationNameBindings[type][index].index =
                static_cast<std::uint8_t>(index);
        }
        if (boundedCount
            && sourcePrim.allocName
            && sourcePrim.allocName
                == sourcePrim.allocList[boundedCount - 1].name)
        {
            snapshot.allocNameBindings[type].type =
                static_cast<std::uint8_t>(type);
            snapshot.allocNameBindings[type].index =
                static_cast<std::uint8_t>(boundedCount - 1);
        }
    }
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
    g_runtime = {};
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const OwnedNameSnapshot &source = snapshot.ownedNames[type][index];
            OwnedAllocationName &destination =
                g_runtime.ownedNames.names[type][index];
            destination.identity = source.identity;
            destination.identityWitness = source.identityWitness;
            for (std::size_t byte = 0; byte < kOwnedNameCapacity; ++byte)
                destination.text[byte] = source.text[byte];
        }
    }
    g_mem = snapshot.memory;
    for (std::uint32_t type = 0; type < ARRAY_COUNT(g_mem.prim); ++type)
    {
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const OwnedNameBindingSnapshot &binding =
                snapshot.allocationNameBindings[type][index];
            if (binding.type < ARRAY_COUNT(g_mem.prim)
                && binding.index < MAX_PHYSICAL_ALLOCATIONS)
            {
                const OwnedNameSnapshot &boundName =
                    snapshot.ownedNames[binding.type][binding.index];
                const char *const logicalPointer = boundName.identity
                    ? reinterpret_cast<const char *>(boundName.identity)
                    : reinterpret_cast<const char *>(
                        static_cast<std::uintptr_t>(1));
                if (g_mem.prim[type].allocList[index].name == logicalPointer)
                {
                    g_mem.prim[type].allocList[index].name =
                        g_runtime.ownedNames.names[
                            binding.type][binding.index].text;
                }
            }
        }
        const OwnedNameBindingSnapshot &binding =
            snapshot.allocNameBindings[type];
        if (binding.type < ARRAY_COUNT(g_mem.prim)
            && binding.index < MAX_PHYSICAL_ALLOCATIONS)
        {
            const OwnedNameSnapshot &boundName =
                snapshot.ownedNames[binding.type][binding.index];
            const char *const logicalPointer = boundName.identity
                ? reinterpret_cast<const char *>(boundName.identity)
                : reinterpret_cast<const char *>(
                    static_cast<std::uintptr_t>(1));
            if (g_mem.prim[type].allocName == logicalPointer)
            {
                g_mem.prim[type].allocName =
                    g_runtime.ownedNames.names[
                        binding.type][binding.index].text;
            }
        }
    }
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
        if (!UninitializedStateIsCoherent())
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

pmem_runtime::DiagnosticSnapshot KISAK_CDECL
pmem_runtime::TryCaptureDiagnosticSnapshot() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    const DiagnosticSnapshot snapshot =
        TryCaptureDiagnosticSnapshotNoLock();
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return snapshot;
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
    const pmem_runtime::DiagnosticSnapshot snapshot =
        pmem_runtime::TryCaptureDiagnosticSnapshot();
    if (snapshot.status
        == pmem_runtime::DiagnosticSnapshotStatus::NotReady)
    {
        Com_Printf(16, "physical memory unavailable (not initialized)\n");
        return;
    }
    if (snapshot.status
        != pmem_runtime::DiagnosticSnapshotStatus::Success)
    {
        Com_Printf(16, "physical memory unavailable (corrupt state)\n");
        return;
    }

    for (std::uint32_t index = 0; index < snapshot.highCount; ++index)
    {
        const pmem_runtime::DiagnosticEntry &entry = snapshot.high[index];
        const double megabytes = ConvertToMB(static_cast<int>(entry.bytes));
        Com_Printf(16, "%-18.18s %5.1f\n", entry.name, megabytes);
    }
    const double freeMegabytes =
        ConvertToMB(static_cast<int>(snapshot.freeBytes));
    Com_Printf(16, "free physical      %5.1f\n", freeMegabytes);
    for (std::uint32_t remaining = snapshot.lowCount;
         remaining != 0;
         --remaining)
    {
        const pmem_runtime::DiagnosticEntry &entry =
            snapshot.low[remaining - 1];
        const double megabytes = ConvertToMB(static_cast<int>(entry.bytes));
        Com_Printf(16, "%-18.18s %5.1f\n", entry.name, megabytes);
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
            result = TryBeginGlobalAllocNoReport(allocType, name);
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
            result = TryEndGlobalAllocNoReport(allocType, name);
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
            result = TryFreeGlobalAllocNoReport(allocType, name);
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

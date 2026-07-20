#include "physicalmemory.h"
#include "physicalmemory_checked.h"
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
constexpr char kProcessInitAllocationName[] = "$init";
constexpr std::uint32_t kProcessInitBegunWitness = UINT32_C(0x4245474E);
constexpr std::uint32_t kProcessInitEndedWitness = UINT32_C(0x454E4444);

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

enum class ProcessInitPhase : std::uint8_t
{
    Dormant,
    Begun,
    Ended,
};

struct ProcessInitControl final
{
    ProcessInitPhase phase = ProcessInitPhase::Dormant;
    std::uint8_t reserved[3]{};
    std::uint32_t witness = 0;
};

struct RuntimeControl final
{
    RetainedExtent extent{};
    OwnedNameState ownedNames{};
    ProcessInitControl processInit{};
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
    ProcessInitProtected,
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

std::uint32_t ProcessInitWitnessFor(
    const ProcessInitPhase phase) noexcept
{
    switch (phase)
    {
    case ProcessInitPhase::Dormant:
        return 0;
    case ProcessInitPhase::Begun:
        return kProcessInitBegunWitness;
    case ProcessInitPhase::Ended:
        return kProcessInitEndedWitness;
    }
    return UINT32_MAX;
}

bool ProcessInitControlIsCanonical(
    const ProcessInitControl &control) noexcept
{
    const std::uint32_t expectedWitness =
        ProcessInitWitnessFor(control.phase);
    return control.reserved[0] == 0 && control.reserved[1] == 0
        && control.reserved[2] == 0
        && expectedWitness != UINT32_MAX
        && control.witness == expectedWitness;
}

bool ProcessInitControlIsDormant(
    const ProcessInitControl &control) noexcept
{
    return ProcessInitControlIsCanonical(control)
        && control.phase == ProcessInitPhase::Dormant;
}

bool RuntimeReservedIsZero(const RuntimeControl &control) noexcept
{
    return control.reserved[0] == 0 && control.reserved[1] == 0
        && control.reserved[2] == 0;
}

bool RuntimeControlIsCanonical(const RuntimeControl &control) noexcept
{
    if (!ProcessInitControlIsCanonical(control.processInit)
        || !RuntimeReservedIsZero(control)
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
               extent.base, extent.size, &g_runtime, sizeof(g_runtime))
        && !AddressRangesOverlap(
               extent.base, extent.size,
               kProcessInitAllocationName,
               sizeof(kProcessInitAllocationName));
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

bool ProcessInitBindingIsCoherent() noexcept
{
    const ProcessInitControl &control = g_runtime.processInit;
    if (!ProcessInitControlIsCanonical(control))
        return false;
    if (control.phase == ProcessInitPhase::Dormant)
    {
        // Dormant means that the checked process controller is not enrolled.
        // It deliberately imposes no constraint on the still-legacy startup
        // allocation before the later all-sites-at-once cutover.
        return true;
    }

    const PhysicalMemoryPrim &high = g_mem.prim[1];
    if (!high.allocListCount
        || high.allocListCount > MAX_PHYSICAL_ALLOCATIONS)
    {
        return false;
    }
    const PhysicalMemoryAllocation &allocation = high.allocList[0];
    const OwnedAllocationName &owned = g_runtime.ownedNames.names[1][0];
    const std::uintptr_t identity =
        reinterpret_cast<std::uintptr_t>(kProcessInitAllocationName);
    if (allocation.name != owned.text
        || allocation.pos != g_runtime.extent.size
        || owned.identity != identity
        || owned.identityWitness != OwnedNameIdentityWitness(identity, 1, 0)
        || !OwnedNameTextIsCanonical(owned.text)
        || std::strcmp(owned.text, kProcessInitAllocationName) != 0)
    {
        return false;
    }

    if (control.phase == ProcessInitPhase::Begun)
    {
        return high.allocListCount == 1
            && high.allocName == owned.text;
    }
    if (control.phase == ProcessInitPhase::Ended)
        return high.allocName != owned.text;
    return false;
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
        || !OwnedNamesMatchMemory()
        || !ProcessInitBindingIsCoherent())
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
        && ProcessInitControlIsDormant(g_runtime.processInit)
        && PhysicalMemoryIsPristine(g_mem)
        && OwnedNamesArePristine(g_runtime.ownedNames);
}

bool InitializingStateIsCoherent() noexcept
{
    if (!RuntimeControlIsCanonical(g_runtime)
        || g_runtime.phase != pmem_runtime::InitializationPhase::Initializing
        || !ProcessInitControlIsDormant(g_runtime.processInit)
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
        && ProcessInitControlIsDormant(g_runtime.processInit)
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

struct AuthenticatedReceiptLocation final
{
    pmem_runtime::AllocationReceiptPhase phase =
        pmem_runtime::AllocationReceiptPhase::Pristine;
    std::uint32_t allocType = ARRAY_COUNT(g_mem.prim);
    std::uint32_t index = MAX_PHYSICAL_ALLOCATIONS;
    bool matched = false;
};

bool AllocationReceiptPhaseIsValid(
    const pmem_runtime::AllocationReceiptPhase phase) noexcept
{
    switch (phase)
    {
    case pmem_runtime::AllocationReceiptPhase::Pristine:
    case pmem_runtime::AllocationReceiptPhase::Begun:
    case pmem_runtime::AllocationReceiptPhase::Ended:
    case pmem_runtime::AllocationReceiptPhase::Freed:
        return true;
    }
    return false;
}

bool StorageIsOutsideManagedMemoryReadyNoLock(
    const void *const storage,
    const std::size_t size) noexcept
{
    if (!AddressRangeIsValid(storage, size))
        return false;
    return !AddressRangesOverlap(
               storage, size, &g_mem, sizeof(g_mem))
        && !AddressRangesOverlap(
               storage, size, &g_runtime, sizeof(g_runtime))
        && !AddressRangesOverlap(
               storage, size,
               g_runtime.extent.base, g_runtime.extent.size);
}

bool AllocationReceiptStorageIsDisjoint(
    const physical_memory::AllocationReceipt *const receipt) noexcept
{
    return StorageIsOutsideManagedMemoryReadyNoLock(
        receipt, sizeof(*receipt));
}

AuthenticatedReceiptLocation AuthenticateReceiptLocationNoLock(
    const physical_memory::AllocationReceipt &receipt) noexcept
{
    using pmem_runtime::AllocationReceiptPhase;
    if (pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            receipt, g_mem, 0, 0, nullptr,
            AllocationReceiptPhase::Pristine))
    {
        return {AllocationReceiptPhase::Pristine, 0, 0, true};
    }

    for (std::uint32_t allocType = 0;
         allocType < ARRAY_COUNT(g_mem.prim);
         ++allocType)
    {
        const PhysicalMemoryPrim &prim = g_mem.prim[allocType];
        for (std::uint32_t index = 0;
             index < MAX_PHYSICAL_ALLOCATIONS;
             ++index)
        {
            const OwnedAllocationName &owned =
                g_runtime.ownedNames.names[allocType][index];
            const bool live = index < prim.allocListCount
                && prim.allocList[index].name != nullptr;

            // Freed authority is receipt-local and must survive reuse of this
            // structural allocation-list index by a later live receipt.
            if (pmem_runtime::detail::
                    AuthenticateAllocationReceiptNoLock(
                        receipt, g_mem, allocType, index, owned.text,
                        AllocationReceiptPhase::Freed))
            {
                return {
                    AllocationReceiptPhase::Freed,
                    allocType,
                    index,
                    true};
            }

            if (live)
            {
                if (pmem_runtime::detail::
                        AuthenticateAllocationReceiptNoLock(
                            receipt, g_mem, allocType, index, owned.text,
                            AllocationReceiptPhase::Begun))
                {
                    return {
                        AllocationReceiptPhase::Begun,
                        allocType,
                        index,
                        true};
                }
                if (pmem_runtime::detail::
                        AuthenticateAllocationReceiptNoLock(
                            receipt, g_mem, allocType, index, owned.text,
                            AllocationReceiptPhase::Ended))
                {
                    return {
                        AllocationReceiptPhase::Ended,
                        allocType,
                        index,
                        true};
                }
            }
        }
    }
    return {};
}

pmem_runtime::AllocationReceiptStatus MapAllocationScopeStatus(
    const physical_memory::AllocationScopeStatus status) noexcept
{
    using physical_memory::AllocationScopeStatus;
    using pmem_runtime::AllocationReceiptStatus;
    switch (status)
    {
    case AllocationScopeStatus::Success:
        return AllocationReceiptStatus::Success;
    case AllocationScopeStatus::InvalidArgument:
        return AllocationReceiptStatus::InvalidArgument;
    case AllocationScopeStatus::InvalidAllocationType:
        return AllocationReceiptStatus::InvalidAllocationType;
    case AllocationScopeStatus::MalformedState:
        return AllocationReceiptStatus::CorruptState;
    case AllocationScopeStatus::Busy:
        return AllocationReceiptStatus::Busy;
    case AllocationScopeStatus::CapacityExceeded:
        return AllocationReceiptStatus::CapacityExceeded;
    case AllocationScopeStatus::ReceiptInUse:
        return AllocationReceiptStatus::ReceiptInUse;
    case AllocationScopeStatus::ReceiptMismatch:
        return AllocationReceiptStatus::ReceiptMismatch;
    case AllocationScopeStatus::WrongPhase:
        return AllocationReceiptStatus::WrongPhase;
    case AllocationScopeStatus::AlreadyComplete:
        return AllocationReceiptStatus::AlreadyComplete;
    }
    return AllocationReceiptStatus::CorruptState;
}

pmem_runtime::AllocationReceiptStatus
TryBeginAllocationReceiptReadyNoLock(
    const char *const name,
    const std::uint32_t allocType,
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    using pmem_runtime::AllocationReceiptPhase;
    using pmem_runtime::AllocationReceiptStatus;
    if (name == nullptr || receipt == nullptr)
        return AllocationReceiptStatus::InvalidArgument;
    if (allocType >= ARRAY_COUNT(g_mem.prim))
        return AllocationReceiptStatus::InvalidAllocationType;
    if (!AllocationReceiptStorageIsDisjoint(receipt))
        return AllocationReceiptStatus::InvalidArgument;

    PhysicalMemoryPrim &prim = g_mem.prim[allocType];
    const std::uint32_t index = prim.allocListCount;
    if (index >= MAX_PHYSICAL_ALLOCATIONS)
        return AllocationReceiptStatus::CapacityExceeded;
    OwnedAllocationName &owned =
        g_runtime.ownedNames.names[allocType][index];
    if (!OwnedNameIsPristine(owned))
        return AllocationReceiptStatus::CorruptState;

    owned = CaptureOwnedName(name, allocType, index);
    const physical_memory::AllocationScopeStatus rawStatus =
        physical_memory::TryBegin(
            &g_mem, allocType, owned.text, receipt);
    if (rawStatus != physical_memory::AllocationScopeStatus::Success)
    {
        owned = {};
        return MapAllocationScopeStatus(rawStatus);
    }
    if (!pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            *receipt, g_mem, allocType, index, owned.text,
            AllocationReceiptPhase::Begun))
    {
        return AllocationReceiptStatus::CorruptState;
    }
    return AllocationReceiptStatus::Success;
}

pmem_runtime::AllocationReceiptStatus
TryEndAllocationReceiptReadyNoLock(
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    using pmem_runtime::AllocationReceiptPhase;
    using pmem_runtime::AllocationReceiptStatus;
    if (receipt == nullptr)
        return AllocationReceiptStatus::InvalidArgument;
    if (!AllocationReceiptStorageIsDisjoint(receipt))
        return AllocationReceiptStatus::InvalidArgument;

    const AuthenticatedReceiptLocation location =
        AuthenticateReceiptLocationNoLock(*receipt);
    if (!location.matched)
        return AllocationReceiptStatus::ReceiptMismatch;
    switch (location.phase)
    {
    case AllocationReceiptPhase::Pristine:
        return AllocationReceiptStatus::WrongPhase;
    case AllocationReceiptPhase::Ended:
    case AllocationReceiptPhase::Freed:
        return AllocationReceiptStatus::AlreadyComplete;
    case AllocationReceiptPhase::Begun:
        break;
    }

    const physical_memory::AllocationScopeStatus rawStatus =
        physical_memory::TryEnd(receipt);
    if (rawStatus != physical_memory::AllocationScopeStatus::Success)
        return MapAllocationScopeStatus(rawStatus);
    const OwnedAllocationName &owned =
        g_runtime.ownedNames.names[location.allocType][location.index];
    if (!pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            *receipt, g_mem, location.allocType, location.index,
            owned.text, AllocationReceiptPhase::Ended))
    {
        return AllocationReceiptStatus::CorruptState;
    }
    return AllocationReceiptStatus::Success;
}

pmem_runtime::AllocationReceiptStatus
TryFreeAllocationReceiptReadyNoLock(
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    using pmem_runtime::AllocationReceiptPhase;
    using pmem_runtime::AllocationReceiptStatus;
    if (receipt == nullptr)
        return AllocationReceiptStatus::InvalidArgument;
    if (!AllocationReceiptStorageIsDisjoint(receipt))
        return AllocationReceiptStatus::InvalidArgument;

    const AuthenticatedReceiptLocation location =
        AuthenticateReceiptLocationNoLock(*receipt);
    if (!location.matched)
        return AllocationReceiptStatus::ReceiptMismatch;
    switch (location.phase)
    {
    case AllocationReceiptPhase::Pristine:
    case AllocationReceiptPhase::Begun:
        return AllocationReceiptStatus::WrongPhase;
    case AllocationReceiptPhase::Freed:
        return AllocationReceiptStatus::AlreadyComplete;
    case AllocationReceiptPhase::Ended:
        break;
    }

    PhysicalMemoryPrim &prim = g_mem.prim[location.allocType];
    const std::uint32_t previousCount = prim.allocListCount;
    const physical_memory::AllocationScopeStatus rawStatus =
        physical_memory::TryFree(receipt);
    if (rawStatus != physical_memory::AllocationScopeStatus::Success)
        return MapAllocationScopeStatus(rawStatus);

    g_runtime.ownedNames.names[location.allocType][location.index] = {};
    for (std::uint32_t index = prim.allocListCount;
         index < previousCount;
         ++index)
    {
        g_runtime.ownedNames.names[location.allocType][index] = {};
    }
    const OwnedAllocationName &owned =
        g_runtime.ownedNames.names[location.allocType][location.index];
    if (!pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
            *receipt, g_mem, location.allocType, location.index,
            owned.text, AllocationReceiptPhase::Freed))
    {
        return AllocationReceiptStatus::CorruptState;
    }
    return AllocationReceiptStatus::Success;
}

pmem_runtime::AllocationReceiptStatus
TryAuthenticateAllocationReceiptReadyNoLock(
    const physical_memory::AllocationReceipt *const receipt,
    const std::uint32_t expectedAllocationType,
    const pmem_runtime::AllocationReceiptPhase expectedPhase) noexcept
{
    using pmem_runtime::AllocationReceiptStatus;
    if (receipt == nullptr || !AllocationReceiptPhaseIsValid(expectedPhase))
        return AllocationReceiptStatus::InvalidArgument;
    if (expectedAllocationType >= ARRAY_COUNT(g_mem.prim))
        return AllocationReceiptStatus::InvalidAllocationType;
    if (!AllocationReceiptStorageIsDisjoint(receipt))
        return AllocationReceiptStatus::InvalidArgument;

    const AuthenticatedReceiptLocation location =
        AuthenticateReceiptLocationNoLock(*receipt);
    if (!location.matched)
        return AllocationReceiptStatus::ReceiptMismatch;
    // A pristine receipt has not selected a prim yet. Every bound phase must
    // authenticate the caller's exact retained allocation-type witness.
    if (location.phase
            != pmem_runtime::AllocationReceiptPhase::Pristine
        && location.allocType != expectedAllocationType)
    {
        return AllocationReceiptStatus::ReceiptMismatch;
    }
    return location.phase == expectedPhase
        ? AllocationReceiptStatus::Success
        : AllocationReceiptStatus::WrongPhase;
}

pmem_runtime::AllocationReceiptStatus
TryAuthenticateAllocationRangeReadyNoLock(
    const physical_memory::AllocationReceipt *const receipt,
    const std::uint32_t expectedAllocationType,
    const void *const storage,
    const std::size_t size,
    const pmem_runtime::AllocationReceiptPhase expectedPhase) noexcept
{
    using pmem_runtime::AllocationReceiptPhase;
    using pmem_runtime::AllocationReceiptStatus;
    if (receipt == nullptr || !AddressRangeIsValid(storage, size)
        || !AllocationReceiptPhaseIsValid(expectedPhase))
    {
        return AllocationReceiptStatus::InvalidArgument;
    }
    if (expectedAllocationType >= ARRAY_COUNT(g_mem.prim))
        return AllocationReceiptStatus::InvalidAllocationType;
    if (!AllocationReceiptStorageIsDisjoint(receipt))
        return AllocationReceiptStatus::InvalidArgument;
    if (expectedPhase == AllocationReceiptPhase::Pristine
        || expectedPhase == AllocationReceiptPhase::Freed)
    {
        return AllocationReceiptStatus::WrongPhase;
    }

    const AuthenticatedReceiptLocation location =
        AuthenticateReceiptLocationNoLock(*receipt);
    if (!location.matched)
        return AllocationReceiptStatus::ReceiptMismatch;
    if (location.allocType != expectedAllocationType)
        return AllocationReceiptStatus::ReceiptMismatch;
    if (location.phase != expectedPhase)
        return AllocationReceiptStatus::WrongPhase;

    const OwnedAllocationName &owned =
        g_runtime.ownedNames.names[location.allocType][location.index];
    return pmem_runtime::detail::AuthenticateAllocationRangeNoLock(
               *receipt, g_mem, location.allocType, location.index,
               owned.text, storage, size, expectedPhase)
        ? AllocationReceiptStatus::Success
        : AllocationReceiptStatus::InvalidArgument;
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

void SetProcessInitPhase(
    ProcessInitControl *const control,
    const ProcessInitPhase phase) noexcept
{
    control->phase = phase;
    control->reserved[0] = 0;
    control->reserved[1] = 0;
    control->reserved[2] = 0;
    control->witness = ProcessInitWitnessFor(phase);
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
    if (allocType == 1
        && g_runtime.processInit.phase != ProcessInitPhase::Dormant
        && name == kProcessInitAllocationName)
    {
        return {LegacyDiagnostic::ProcessInitProtected};
    }
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
    if (allocType == 1
        && g_runtime.processInit.phase == ProcessInitPhase::Begun
        && name == kProcessInitAllocationName)
    {
        return {LegacyDiagnostic::ProcessInitProtected};
    }
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
    if (allocType == 1 && allocIndex == 0
        && g_runtime.processInit.phase != ProcessInitPhase::Dormant)
    {
        return {LegacyDiagnostic::ProcessInitProtected};
    }
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
    case LegacyDiagnostic::ProcessInitProtected:
        MyAssertHandler(
            ".\\universal\\physicalmemory.cpp", 465, 0, "%s",
            "process initialization allocation remains owned for process life");
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
    snapshot.processInitPhase =
        static_cast<std::uint8_t>(g_runtime.processInit.phase);
    snapshot.processInitReserved[0] = g_runtime.processInit.reserved[0];
    snapshot.processInitReserved[1] = g_runtime.processInit.reserved[1];
    snapshot.processInitReserved[2] = g_runtime.processInit.reserved[2];
    snapshot.processInitWitness = g_runtime.processInit.witness;
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

std::uintptr_t
PhysicalMemoryGlobalStateTestAccess::ProcessInitAllocationNameAddress()
    noexcept
{
    return reinterpret_cast<std::uintptr_t>(kProcessInitAllocationName);
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
    g_runtime.processInit.phase =
        static_cast<ProcessInitPhase>(snapshot.processInitPhase);
    g_runtime.processInit.reserved[0] = snapshot.processInitReserved[0];
    g_runtime.processInit.reserved[1] = snapshot.processInitReserved[1];
    g_runtime.processInit.reserved[2] = snapshot.processInitReserved[2];
    g_runtime.processInit.witness = snapshot.processInitWitness;
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

bool KISAK_CDECL pmem_runtime::StorageIsOutsideManagedMemory(
    const void *const storage,
    const std::size_t size) noexcept
{
    bool outside = false;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    if (GetRuntimeReadiness() == RuntimeReadiness::Ready)
    {
        outside = StorageIsOutsideManagedMemoryReadyNoLock(storage, size);
        if (!ReadyStateIsCoherent())
            outside = false;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return outside;
}

pmem_runtime::AllocationReceiptStatus KISAK_CDECL
pmem_runtime::TryBeginAllocationReceipt(
    const char *const name,
    const std::uint32_t allocType,
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    AllocationReceiptStatus status = AllocationReceiptStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = AllocationReceiptStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        status = TryBeginAllocationReceiptReadyNoLock(
            name, allocType, receipt);
        if (!ReadyStateIsCoherent())
            status = AllocationReceiptStatus::CorruptState;
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::AllocationReceiptStatus KISAK_CDECL
pmem_runtime::TryEndAllocationReceipt(
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    AllocationReceiptStatus status = AllocationReceiptStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = AllocationReceiptStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        status = TryEndAllocationReceiptReadyNoLock(receipt);
        if (!ReadyStateIsCoherent())
            status = AllocationReceiptStatus::CorruptState;
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::AllocationReceiptStatus KISAK_CDECL
pmem_runtime::TryFreeAllocationReceipt(
    physical_memory::AllocationReceipt *const receipt) noexcept
{
    AllocationReceiptStatus status = AllocationReceiptStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = AllocationReceiptStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        status = TryFreeAllocationReceiptReadyNoLock(receipt);
        if (!ReadyStateIsCoherent())
            status = AllocationReceiptStatus::CorruptState;
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::AllocationReceiptStatus KISAK_CDECL
pmem_runtime::TryAuthenticateAllocationReceipt(
    const physical_memory::AllocationReceipt *const receipt,
    const std::uint32_t expectedAllocationType,
    const AllocationReceiptPhase expectedPhase) noexcept
{
    AllocationReceiptStatus status = AllocationReceiptStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = AllocationReceiptStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        status = TryAuthenticateAllocationReceiptReadyNoLock(
            receipt, expectedAllocationType, expectedPhase);
        if (!ReadyStateIsCoherent())
            status = AllocationReceiptStatus::CorruptState;
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::AllocationReceiptStatus KISAK_CDECL
pmem_runtime::TryAuthenticateAllocationRange(
    const physical_memory::AllocationReceipt *const receipt,
    const std::uint32_t expectedAllocationType,
    const void *const storage,
    const std::size_t size,
    const AllocationReceiptPhase expectedPhase) noexcept
{
    AllocationReceiptStatus status = AllocationReceiptStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = AllocationReceiptStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        status = TryAuthenticateAllocationRangeReadyNoLock(
            receipt, expectedAllocationType, storage, size, expectedPhase);
        if (!ReadyStateIsCoherent())
            status = AllocationReceiptStatus::CorruptState;
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::ProcessInitAllocationStatus KISAK_CDECL
pmem_runtime::TryBeginProcessInitAllocation() noexcept
{
    ProcessInitAllocationStatus status =
        ProcessInitAllocationStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = ProcessInitAllocationStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        switch (g_runtime.processInit.phase)
        {
        case ProcessInitPhase::Begun:
            status = ProcessInitAllocationStatus::Busy;
            break;
        case ProcessInitPhase::Ended:
            status = ProcessInitAllocationStatus::AlreadyComplete;
            break;
        case ProcessInitPhase::Dormant:
        {
            const PhysicalMemoryPrim &high = g_mem.prim[1];
            if (high.allocListCount || high.allocName
                || high.pos != g_runtime.extent.size)
            {
                status = ProcessInitAllocationStatus::Busy;
                break;
            }
            const LegacyOperationResult result =
                TryBeginGlobalAllocNoReport(
                    1, kProcessInitAllocationName);
            if (result.diagnostic != LegacyDiagnostic::None)
                break;
            SetProcessInitPhase(
                &g_runtime.processInit, ProcessInitPhase::Begun);
            if (ReadyStateIsCoherent())
                status = ProcessInitAllocationStatus::Success;
            break;
        }
        }
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
}

pmem_runtime::ProcessInitAllocationStatus KISAK_CDECL
pmem_runtime::TryEndProcessInitAllocation() noexcept
{
    ProcessInitAllocationStatus status =
        ProcessInitAllocationStatus::CorruptState;
    Sys_EnterCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    switch (GetRuntimeReadiness())
    {
    case RuntimeReadiness::NotReady:
        status = ProcessInitAllocationStatus::NotReady;
        break;
    case RuntimeReadiness::Corrupt:
        break;
    case RuntimeReadiness::Ready:
        switch (g_runtime.processInit.phase)
        {
        case ProcessInitPhase::Dormant:
            status = ProcessInitAllocationStatus::WrongPhase;
            break;
        case ProcessInitPhase::Ended:
            status = ProcessInitAllocationStatus::AlreadyComplete;
            break;
        case ProcessInitPhase::Begun:
        {
            PhysicalMemoryPrim &high = g_mem.prim[1];
            const LegacyOperationResult result =
                TryEndAllocInPrimNoReport(&high, high.allocName);
            if (result.diagnostic != LegacyDiagnostic::None)
                break;
            SetProcessInitPhase(
                &g_runtime.processInit, ProcessInitPhase::Ended);
            if (ReadyStateIsCoherent())
                status = ProcessInitAllocationStatus::Success;
            break;
        }
        }
        break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICAL_MEMORY);
    return status;
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

#include <database/db_zone_runtime_facade.h>
#include <database/db_zone_memory.h>

#include <qcommon/sys_sync.h>

#include <cstddef>
#include <limits>

namespace db::zone_runtime
{
using registry_ownership::RegistryOwnershipStatus;

namespace
{
inline constexpr std::size_t kPhysicalAllocationNamePotentialReadBytes = 63u;

enum class RuntimeBoundaryState : std::uint8_t
{
    Idle,
    Active,
    Poisoned,
};

enum class ThreadBoundaryClassification : std::uint8_t
{
    Idle,
    Active,
    Torn,
};

FastCriticalSection s_runtimeSerializer{};
RuntimeBoundaryState s_runtimeState{};
RuntimeBoundaryState s_runtimeStateMirror{};
std::uintptr_t s_activeThreadIdentity{};
std::uintptr_t s_activeThreadIdentityMirror{};
std::uint64_t s_activeSerial{};
std::uint64_t s_activeSerialMirror{};
std::uint64_t s_nextSerial{};
std::uint64_t s_nextSerialMirror{};

thread_local std::uint8_t s_threadIdentity{};
thread_local RuntimeBoundaryState s_retainedThreadState{};
thread_local RuntimeBoundaryState s_retainedThreadStateMirror{};
thread_local std::uintptr_t s_retainedThreadIdentity{};
thread_local std::uintptr_t s_retainedThreadIdentityMirror{};
thread_local std::uint64_t s_retainedSerial{};
thread_local std::uint64_t s_retainedSerialMirror{};

[[nodiscard]] std::uintptr_t CurrentThreadIdentityValue() noexcept
{
    return reinterpret_cast<std::uintptr_t>(&s_threadIdentity);
}

[[nodiscard]] ThreadBoundaryClassification ClassifyThreadBoundary() noexcept
{
    if (s_retainedThreadState == RuntimeBoundaryState::Idle
        && s_retainedThreadStateMirror == RuntimeBoundaryState::Idle
        && s_retainedThreadIdentity == 0
        && s_retainedThreadIdentityMirror == 0
        && s_retainedSerial == 0 && s_retainedSerialMirror == 0)
    {
        return ThreadBoundaryClassification::Idle;
    }
    const std::uintptr_t identity = CurrentThreadIdentityValue();
    if (s_retainedThreadState == RuntimeBoundaryState::Active
        && s_retainedThreadStateMirror == RuntimeBoundaryState::Active
        && s_retainedThreadIdentity == identity
        && s_retainedThreadIdentityMirror == identity
        && s_retainedSerial != 0
        && s_retainedSerialMirror == s_retainedSerial)
    {
        return ThreadBoundaryClassification::Active;
    }
    return ThreadBoundaryClassification::Torn;
}

[[nodiscard]] bool GlobalBoundaryIsIdle() noexcept
{
    return s_runtimeState == RuntimeBoundaryState::Idle
        && s_runtimeStateMirror == RuntimeBoundaryState::Idle
        && s_activeThreadIdentity == 0
        && s_activeThreadIdentityMirror == 0
        && s_activeSerial == 0 && s_activeSerialMirror == 0
        && s_nextSerial == s_nextSerialMirror;
}

[[nodiscard]] bool GlobalBoundaryMatchesThread() noexcept
{
    return s_runtimeState == RuntimeBoundaryState::Active
        && s_runtimeStateMirror == RuntimeBoundaryState::Active
        && s_activeThreadIdentity == s_retainedThreadIdentity
        && s_activeThreadIdentityMirror == s_retainedThreadIdentityMirror
        && s_activeSerial == s_retainedSerial
        && s_activeSerialMirror == s_retainedSerialMirror
        && s_nextSerial == s_nextSerialMirror
        && s_nextSerial == s_retainedSerial;
}

[[nodiscard]] bool AddressRangesAreDisjoint(
    const void *const leftStorage,
    const std::size_t leftSize,
    const void *const rightStorage,
    const std::size_t rightSize) noexcept
{
    if (!leftStorage || leftSize == 0 || !rightStorage || rightSize == 0)
        return false;
    const std::uintptr_t left =
        reinterpret_cast<std::uintptr_t>(leftStorage);
    const std::uintptr_t right =
        reinterpret_cast<std::uintptr_t>(rightStorage);
    const std::uintptr_t maximum =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (leftSize > maximum - left || rightSize > maximum - right)
        return false;
    return left + leftSize <= right || right + rightSize <= left;
}

template <typename Hidden>
[[nodiscard]] bool IsSeparateFrom(
    const void *const output,
    const std::size_t outputSize,
    const Hidden &hidden) noexcept
{
    return AddressRangesAreDisjoint(
        output, outputSize, &hidden, sizeof(hidden));
}

[[nodiscard]] bool WritableOutputIsSeparateFromBoundary(
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment) noexcept
{
    return output && outputSize != 0 && outputAlignment != 0
        && reinterpret_cast<std::uintptr_t>(output) % outputAlignment == 0
        && IsSeparateFrom(output, outputSize, s_runtimeSerializer)
        && IsSeparateFrom(output, outputSize, s_runtimeState)
        && IsSeparateFrom(output, outputSize, s_runtimeStateMirror)
        && IsSeparateFrom(output, outputSize, s_activeThreadIdentity)
        && IsSeparateFrom(output, outputSize, s_activeThreadIdentityMirror)
        && IsSeparateFrom(output, outputSize, s_activeSerial)
        && IsSeparateFrom(output, outputSize, s_activeSerialMirror)
        && IsSeparateFrom(output, outputSize, s_nextSerial)
        && IsSeparateFrom(output, outputSize, s_nextSerialMirror)
        && IsSeparateFrom(output, outputSize, s_threadIdentity)
        && IsSeparateFrom(output, outputSize, s_retainedThreadState)
        && IsSeparateFrom(output, outputSize, s_retainedThreadStateMirror)
        && IsSeparateFrom(output, outputSize, s_retainedThreadIdentity)
        && IsSeparateFrom(
            output, outputSize, s_retainedThreadIdentityMirror)
        && IsSeparateFrom(output, outputSize, s_retainedSerial)
        && IsSeparateFrom(output, outputSize, s_retainedSerialMirror);
}

void PoisonAccessInternal() noexcept
{
    s_runtimeState = RuntimeBoundaryState::Poisoned;
    s_runtimeStateMirror = RuntimeBoundaryState::Poisoned;
    s_retainedThreadState = RuntimeBoundaryState::Poisoned;
    s_retainedThreadStateMirror = RuntimeBoundaryState::Poisoned;
}

[[nodiscard]] ZoneRuntimeFacadeStatus AuthenticateAccessInternal() noexcept
{
    const ThreadBoundaryClassification thread = ClassifyThreadBoundary();
    if (thread == ThreadBoundaryClassification::Idle)
        return ZoneRuntimeFacadeStatus::InvalidState;
    if (thread != ThreadBoundaryClassification::Active)
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    if (!Sys_IsWriteLocked(&s_runtimeSerializer)
        || !GlobalBoundaryMatchesThread())
    {
        PoisonAccessInternal();
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    }
    return ZoneRuntimeFacadeStatus::Success;
}

[[nodiscard]] RegistryOwnershipStatus MapTableStatusToRegistryStatus(
    const ZoneRuntimeTableStatus status) noexcept
{
    using RegistryStatus = registry_ownership::RegistryOwnershipStatus;
    switch (status)
    {
    case ZoneRuntimeTableStatus::Success:
        return RegistryStatus::Success;
    case ZoneRuntimeTableStatus::Busy:
        return RegistryStatus::Busy;
    case ZoneRuntimeTableStatus::InvalidArgument:
    case ZoneRuntimeTableStatus::InvalidSlot:
        return RegistryStatus::InvalidArgument;
    case ZoneRuntimeTableStatus::InvalidKey:
    case ZoneRuntimeTableStatus::StaleKey:
        return RegistryStatus::InvalidKey;
    case ZoneRuntimeTableStatus::CapacityExceeded:
        return RegistryStatus::CapacityExceeded;
    case ZoneRuntimeTableStatus::UnsafeFailure:
        return RegistryStatus::UnsafeFailure;
    case ZoneRuntimeTableStatus::InvalidState:
    case ZoneRuntimeTableStatus::InvalidPhase:
    case ZoneRuntimeTableStatus::GenerationExhausted:
    case ZoneRuntimeTableStatus::Retry:
    case ZoneRuntimeTableStatus::Rejected:
    case ZoneRuntimeTableStatus::CountMismatch:
        return RegistryStatus::InvalidState;
    default:
        return RegistryStatus::UnsafeFailure;
    }
}

[[nodiscard]] RegistryOwnershipStatus MapCallbackContextStatusToRegistryStatus(
    const ZoneRuntimeCallbackContextStatus status) noexcept
{
    switch (status)
    {
    case ZoneRuntimeCallbackContextStatus::Success:
        return RegistryOwnershipStatus::Success;
    case ZoneRuntimeCallbackContextStatus::Busy:
        return RegistryOwnershipStatus::Busy;
    case ZoneRuntimeCallbackContextStatus::InvalidArgument:
    case ZoneRuntimeCallbackContextStatus::InvalidSlot:
        return RegistryOwnershipStatus::InvalidArgument;
    case ZoneRuntimeCallbackContextStatus::InvalidKey:
    case ZoneRuntimeCallbackContextStatus::StaleKey:
        return RegistryOwnershipStatus::InvalidKey;
    case ZoneRuntimeCallbackContextStatus::InvalidPhase:
    case ZoneRuntimeCallbackContextStatus::GenerationExhausted:
        return RegistryOwnershipStatus::InvalidState;
    case ZoneRuntimeCallbackContextStatus::UnsafeFailure:
    default:
        return RegistryOwnershipStatus::UnsafeFailure;
    }
}

[[nodiscard]] constexpr bool IsKnownTableStatus(
    const ZoneRuntimeTableStatus status) noexcept
{
    switch (status)
    {
    case ZoneRuntimeTableStatus::Success:
    case ZoneRuntimeTableStatus::Busy:
    case ZoneRuntimeTableStatus::InvalidArgument:
    case ZoneRuntimeTableStatus::InvalidSlot:
    case ZoneRuntimeTableStatus::InvalidState:
    case ZoneRuntimeTableStatus::InvalidKey:
    case ZoneRuntimeTableStatus::StaleKey:
    case ZoneRuntimeTableStatus::InvalidPhase:
    case ZoneRuntimeTableStatus::GenerationExhausted:
    case ZoneRuntimeTableStatus::UnsafeFailure:
    case ZoneRuntimeTableStatus::Retry:
    case ZoneRuntimeTableStatus::Rejected:
    case ZoneRuntimeTableStatus::CountMismatch:
    case ZoneRuntimeTableStatus::CapacityExceeded:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownRegistryStatus(
    const RegistryOwnershipStatus status) noexcept
{
    switch (status)
    {
    case RegistryOwnershipStatus::Success:
    case RegistryOwnershipStatus::NoChange:
    case RegistryOwnershipStatus::Busy:
    case RegistryOwnershipStatus::InvalidArgument:
    case RegistryOwnershipStatus::InvalidState:
    case RegistryOwnershipStatus::InvalidKey:
    case RegistryOwnershipStatus::CapacityExceeded:
    case RegistryOwnershipStatus::RefCountExhausted:
    case RegistryOwnershipStatus::OwnershipMismatch:
    case RegistryOwnershipStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] RegistryOwnershipStatus
AccessStatusForRegistryOperation() noexcept
{
    const ZoneRuntimeFacadeStatus status = AuthenticateAccessInternal();
    return status == ZoneRuntimeFacadeStatus::Success
        ? RegistryOwnershipStatus::Success
        : (status == ZoneRuntimeFacadeStatus::UnsafeFailure
            ? RegistryOwnershipStatus::UnsafeFailure
            : RegistryOwnershipStatus::InvalidState);
}

template <typename Operation>
[[nodiscard]] RegistryOwnershipStatus RunRegistryOperation(
    Operation operation) noexcept
{
    RegistryOwnershipStatus access = AccessStatusForRegistryOperation();
    if (access != RegistryOwnershipStatus::Success)
        return access;
    const RegistryOwnershipStatus result = operation();
    if (!IsKnownRegistryStatus(result)
        || result == RegistryOwnershipStatus::UnsafeFailure)
    {
        PoisonAccessInternal();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    access = AccessStatusForRegistryOperation();
    if (access != RegistryOwnershipStatus::Success)
    {
        PoisonAccessInternal();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    return result;
}
} // namespace

void ZoneRuntimeFacade::poisonAccess() noexcept
{
    PoisonAccessInternal();
}

ZoneRuntimeFacadeStatus ZoneRuntimeFacade::authenticateAccess() noexcept
{
    return AuthenticateAccessInternal();
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::authenticateTableOperationAccess() noexcept
{
    const ZoneRuntimeFacadeStatus access = authenticateAccess();
    if (access != ZoneRuntimeFacadeStatus::Success)
    {
        return access == ZoneRuntimeFacadeStatus::UnsafeFailure
            ? ZoneRuntimeTableStatus::UnsafeFailure
            : ZoneRuntimeTableStatus::InvalidState;
    }

    using RegistryStatus = registry_ownership::RegistryOwnershipStatus;
    const RegistryStatus registryStatus = registry_ownership::
        RegistryOwnershipCoordinatorFacade::ValidateInactive();
    if (registryStatus == RegistryStatus::Success)
        return ZoneRuntimeTableStatus::Success;
    if (registryStatus == RegistryStatus::InvalidState
        || registryStatus == RegistryStatus::Busy)
    {
        return ZoneRuntimeTableStatus::InvalidState;
    }
    poisonAccess();
    return ZoneRuntimeTableStatus::UnsafeFailure;
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::authenticateKeyedTableOperationAccess(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return authoritySpanIsSeparated(
               &key, sizeof(key), alignof(zone_load::ZoneLoadContextKey))
        ? ZoneRuntimeTableStatus::Success
        : ZoneRuntimeTableStatus::InvalidArgument;
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::authenticateCompositeTableOperationAccess(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;

    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    ZoneRuntimeTableStatus status = table.validateInitializedHeader();
    if (!IsKnownTableStatus(status)
        || status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (status != ZoneRuntimeTableStatus::Success)
        return status;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    if (!zone_slots::IsUsableZoneSlot(physicalSlot))
        return ZoneRuntimeTableStatus::InvalidSlot;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    status = table.authenticateExactEntry(physicalSlot, key);
    if (!IsKnownTableStatus(status)
        || status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (status != ZoneRuntimeTableStatus::Success)
        return status;
    return table.entries_[physicalSlot].generationBindingPristine()
        ? ZoneRuntimeTableStatus::InvalidPhase
        : ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::authenticatePendingCopyInspectionOutput(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment) noexcept
{
    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    return completeTableOperation(
        table.authenticateExactPendingCopyOutput(
            physicalSlot,
            key,
            output,
            outputSize,
            outputAlignment));
}

RegistryOwnershipStatus
ZoneRuntimeFacade::authenticateRegistryOutputAccess() noexcept
{
    RegistryOwnershipStatus status = AccessStatusForRegistryOperation();
    if (status != RegistryOwnershipStatus::Success)
        return status;
    status = registry_ownership::RegistryOwnershipCoordinatorFacade::
        ValidateActive();
    if (!IsKnownRegistryStatus(status)
        || status == RegistryOwnershipStatus::UnsafeFailure)
    {
        poisonAccess();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    return status;
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::completeTableOperation(
    const ZoneRuntimeTableStatus status) noexcept
{
    if (!IsKnownTableStatus(status)
        || status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (authenticateTableOperationAccess()
        != ZoneRuntimeTableStatus::Success)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return status;
}

bool ZoneRuntimeFacade::authoritySpanIsSeparated(
    const void *const storage,
    const std::size_t storageSize,
    const std::size_t storageAlignment) noexcept
{
    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    return WritableOutputIsSeparateFromBoundary(
               storage, storageSize, storageAlignment)
        && AddressRangesAreDisjoint(
            storage, storageSize, &table, sizeof(table))
        && ZoneRuntimeCallbackContextOwner::SpanIsSeparated(
            storage, storageSize, storageAlignment)
        && registry_ownership::RegistryOwnershipCoordinatorFacade::
            WritableOutputIsSeparated(
                storage, storageSize, storageAlignment);
}

ZoneRuntimeFacadeStatus ZoneRuntimeFacade::TryBeginAccess() noexcept
{
    const ThreadBoundaryClassification thread = ClassifyThreadBoundary();
    if (thread == ThreadBoundaryClassification::Active)
        return ZoneRuntimeFacadeStatus::InvalidState;
    if (thread != ThreadBoundaryClassification::Idle)
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    if (!Sys_TryLockWrite(&s_runtimeSerializer))
        return ZoneRuntimeFacadeStatus::Busy;
    if (!GlobalBoundaryIsIdle()
        || s_nextSerial == (std::numeric_limits<std::uint64_t>::max)())
    {
        poisonAccess();
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    }

    const std::uint64_t serial = s_nextSerial + 1;
    const std::uintptr_t identity = CurrentThreadIdentityValue();
    s_nextSerial = serial;
    s_nextSerialMirror = serial;
    s_activeThreadIdentity = identity;
    s_activeThreadIdentityMirror = identity;
    s_activeSerial = serial;
    s_activeSerialMirror = serial;
    s_runtimeState = RuntimeBoundaryState::Active;
    s_runtimeStateMirror = RuntimeBoundaryState::Active;
    s_retainedThreadIdentity = identity;
    s_retainedThreadIdentityMirror = identity;
    s_retainedSerial = serial;
    s_retainedSerialMirror = serial;
    s_retainedThreadState = RuntimeBoundaryState::Active;
    s_retainedThreadStateMirror = RuntimeBoundaryState::Active;
    return ZoneRuntimeFacadeStatus::Success;
}

ZoneRuntimeFacadeStatus ZoneRuntimeFacade::FinishAccess() noexcept
{
    const ThreadBoundaryClassification thread = ClassifyThreadBoundary();
    if (thread == ThreadBoundaryClassification::Torn)
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    if (thread == ThreadBoundaryClassification::Idle)
    {
        if (!Sys_TryLockWrite(&s_runtimeSerializer))
            return ZoneRuntimeFacadeStatus::Busy;
        if (!GlobalBoundaryIsIdle())
        {
            poisonAccess();
            return ZoneRuntimeFacadeStatus::UnsafeFailure;
        }
        Sys_UnlockWrite(&s_runtimeSerializer);
        return ZoneRuntimeFacadeStatus::InvalidState;
    }

    if (authenticateAccess() != ZoneRuntimeFacadeStatus::Success)
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    using RegistryStatus = registry_ownership::RegistryOwnershipStatus;
    const RegistryStatus registryStatus = registry_ownership::
        RegistryOwnershipCoordinatorFacade::ValidateInactive();
    if (registryStatus == RegistryStatus::InvalidState
        || registryStatus == RegistryStatus::Busy)
    {
        return ZoneRuntimeFacadeStatus::InvalidState;
    }
    if (registryStatus != RegistryStatus::Success)
    {
        poisonAccess();
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    }

    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    const ZoneRuntimeTableStatus tableStatus = table.validateReleaseSafety();
    if (tableStatus == ZoneRuntimeTableStatus::Busy)
        return ZoneRuntimeFacadeStatus::InvalidState;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
    {
        poisonAccess();
        return ZoneRuntimeFacadeStatus::UnsafeFailure;
    }

    s_activeThreadIdentity = 0;
    s_activeThreadIdentityMirror = 0;
    s_activeSerial = 0;
    s_activeSerialMirror = 0;
    s_runtimeState = RuntimeBoundaryState::Idle;
    s_runtimeStateMirror = RuntimeBoundaryState::Idle;
    s_retainedThreadIdentity = 0;
    s_retainedThreadIdentityMirror = 0;
    s_retainedSerial = 0;
    s_retainedSerialMirror = 0;
    s_retainedThreadState = RuntimeBoundaryState::Idle;
    s_retainedThreadStateMirror = RuntimeBoundaryState::Idle;
    Sys_UnlockWrite(&s_runtimeSerializer);
    return ZoneRuntimeFacadeStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryInitializeRuntimeTable() noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(
        TryInitializeZoneRuntimeTable(&ProductionZoneRuntimeTable()));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryClaimGeneration(
    const std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *const inOutKey) noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (inOutKey
        && !authoritySpanIsSeparated(
            inOutKey,
            sizeof(*inOutKey),
            alignof(zone_load::ZoneLoadContextKey)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryClaimZoneRuntimeGeneration(
        &ProductionZoneRuntimeTable(), physicalSlot, inOutKey));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBindGenerationCallbacks(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (!authoritySpanIsSeparated(
            &callbacks,
            sizeof(callbacks),
            alignof(ZoneRuntimeGenerationCallbacks))
        || !authoritySpanIsSeparated(
            &key, sizeof(key), alignof(zone_load::ZoneLoadContextKey))
        || callbacks.context != nullptr)
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryBindZoneRuntimeGenerationCallbacks(
        &ProductionZoneRuntimeTable(), physicalSlot, key, callbacks));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginPhysicalAllocation(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const char *const name,
    const std::uint32_t allocationType) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (name
        && !authoritySpanIsSeparated(
            name, kPhysicalAllocationNamePotentialReadBytes, 1))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryBeginZoneRuntimePhysicalAllocation(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        name,
        allocationType));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryAllocateMemory(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    pmem_runtime::AllocationResult *const outResult) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (outResult
        && !authoritySpanIsSeparated(
            outResult,
            sizeof(*outResult),
            alignof(pmem_runtime::AllocationResult)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryAllocateZoneRuntimeMemory(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        size,
        alignment,
        type,
        outResult));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBindStorage(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    void *const slab,
    const std::size_t slabCapacity,
    const zone_runtime_storage::ZoneRuntimeStoragePlan *const plan) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if ((slab && slabCapacity != 0
            && !authoritySpanIsSeparated(slab, slabCapacity, 1))
        || (plan
            && !authoritySpanIsSeparated(
                plan,
                sizeof(*plan),
                alignof(zone_runtime_storage::ZoneRuntimeStoragePlan))))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryBindZoneRuntimeStorage(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        slab,
        slabCapacity,
        plan));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginStreamGeneration(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryBeginZoneRuntimeStreamGeneration(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBindStreams(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const XZoneMemory *const zoneIdentity,
    const relocation::BlockView *const blocks,
    const std::size_t blockCount) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (blockCount > (std::numeric_limits<std::size_t>::max)()
            / sizeof(relocation::BlockView)
        || (zoneIdentity
            && !authoritySpanIsSeparated(
                zoneIdentity, sizeof(*zoneIdentity), alignof(XZoneMemory)))
        || (blocks && blockCount != 0
            && !authoritySpanIsSeparated(
                blocks,
                blockCount * sizeof(*blocks),
                alignof(relocation::BlockView))))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (zoneIdentity && blocks
        && blockCount == relocation::kBlockCount)
    {
        // The table independently proves allocation-receipt containment. The
        // facade also separates each retained write target from its complete
        // authority set before the child can publish the stream binding.
        for (std::size_t index = 0; index < blockCount; ++index)
        {
            if (blocks[index].base != 0 && blocks[index].size != 0
                && !authoritySpanIsSeparated(
                    reinterpret_cast<const void *>(blocks[index].base),
                    blocks[index].size,
                    1))
            {
                return ZoneRuntimeTableStatus::InvalidArgument;
            }
        }
    }
    return completeTableOperation(TryBindZoneRuntimeStreams(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        zoneIdentity,
        blocks,
        blockCount));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginPendingCopies(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryBeginZoneRuntimePendingCopies(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryAppendPendingCopy(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t assetEntryIndex) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryAppendZoneRuntimePendingCopy(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        assetEntryIndex));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryGetPendingCopyView(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimePendingCopyView *const outView) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (!pmem_runtime::StorageIsOutsideManagedMemory(
            outView, sizeof(*outView))
        || !authoritySpanIsSeparated(
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimePendingCopyView))
        || !AddressRangesAreDisjoint(
            &key, sizeof(key), outView, sizeof(*outView)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const ZoneRuntimeTableStatus outputAuthentication =
        authenticatePendingCopyInspectionOutput(
            physicalSlot,
            key,
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimePendingCopyView));
    if (outputAuthentication != ZoneRuntimeTableStatus::Success)
        return outputAuthentication;
    ZoneRuntimePendingCopyView candidate{};
    const ZoneRuntimeTableStatus status = completeTableOperation(
        TryGetZoneRuntimePendingCopyView(
            &ProductionZoneRuntimeTable(),
            physicalSlot,
            key,
            &candidate));
    if (status != ZoneRuntimeTableStatus::Success)
        return status;
    if (!candidate || candidate.key != key)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    *outView = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryReadPendingCopy(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t expectedRecordCount,
    const std::uint32_t ordinal,
    zone_pending_copy::PendingCopyRecord *const outRecord) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (expectedRecordCount
            > zone_pending_copy::kPendingCopyRecordCapacity
        || ordinal >= expectedRecordCount)
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (!pmem_runtime::StorageIsOutsideManagedMemory(
            outRecord, sizeof(*outRecord))
        || !authoritySpanIsSeparated(
            outRecord,
            sizeof(*outRecord),
            alignof(zone_pending_copy::PendingCopyRecord))
        || !AddressRangesAreDisjoint(
            &key, sizeof(key), outRecord, sizeof(*outRecord)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const ZoneRuntimeTableStatus outputAuthentication =
        authenticatePendingCopyInspectionOutput(
            physicalSlot,
            key,
            outRecord,
            sizeof(*outRecord),
            alignof(zone_pending_copy::PendingCopyRecord));
    if (outputAuthentication != ZoneRuntimeTableStatus::Success)
        return outputAuthentication;
    zone_pending_copy::PendingCopyRecord candidate{};
    const ZoneRuntimeTableStatus status = completeTableOperation(
        TryReadZoneRuntimePendingCopy(
            &ProductionZoneRuntimeTable(),
            physicalSlot,
            key,
            expectedRecordCount,
            ordinal,
            &candidate));
    if (status != ZoneRuntimeTableStatus::Success)
        return status;
    if (candidate.key != key
        || candidate.assetEntryIndex
            < zone_pending_copy::kFirstAssetEntryIndex
        || candidate.assetEntryIndex
            > zone_pending_copy::kLastAssetEntryIndex
        || candidate.reserved != 0)
    {
        poisonAccess();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    *outRecord = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginScriptStringOwnership(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    script_string_journal::ScriptStringJournal *const journal,
    script_string_journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t storageCapacity,
    const std::uint32_t expectedCount) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (journal
        && !authoritySpanIsSeparated(
            journal,
            sizeof(*journal),
            alignof(script_string_journal::ScriptStringJournal)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (storage && storageCapacity != 0)
    {
        if (static_cast<std::size_t>(storageCapacity)
                > (std::numeric_limits<std::size_t>::max)()
                    / sizeof(*storage)
            || !authoritySpanIsSeparated(
                storage,
                static_cast<std::size_t>(storageCapacity)
                    * sizeof(*storage),
                alignof(
                    script_string_journal::ScriptStringJournalEntry)))
        {
            return ZoneRuntimeTableStatus::InvalidArgument;
        }
    }
    return completeTableOperation(
        TryBeginZoneRuntimeScriptStringOwnership(
            &ProductionZoneRuntimeTable(),
            physicalSlot,
            key,
            journal,
            storage,
            storageCapacity,
            expectedCount));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryStageScriptString(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_adapter::ScriptStringSourceView &source,
    std::uint32_t *const outStringId) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (!authoritySpanIsSeparated(
            &source,
            sizeof(source),
            alignof(script_string_adapter::ScriptStringSourceView))
        || (outStringId
            && !authoritySpanIsSeparated(
                outStringId, sizeof(*outStringId), alignof(std::uint32_t))))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const script_string_adapter::ScriptStringSourceView sourceSnapshot =
        source;
    if (sourceSnapshot.bytes && sourceSnapshot.byteCount != 0
        && !authoritySpanIsSeparated(
            sourceSnapshot.bytes, sourceSnapshot.byteCount, 1))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryStageZoneRuntimeScriptString(
        &ProductionZoneRuntimeTable(),
        physicalSlot,
        key,
        sourceSnapshot,
        outStringId));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TrySealScriptStrings(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TrySealZoneRuntimeScriptStrings(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginScriptStringTransfer(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryBeginZoneRuntimeScriptStringTransfer(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryTransferNextScriptString(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryTransferNextZoneRuntimeScriptString(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryPrepareScriptStringCommit(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateCompositeTableOperationAccess(physicalSlot, key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryPrepareZoneRuntimeScriptStringCommit(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryPrepareAdmission(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryPrepareZoneRuntimeAdmission(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryInvalidateStreams(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryInvalidateZoneRuntimeStreams(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryEndPhysicalAllocation(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryEndZoneRuntimePhysicalAllocation(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryCommitGeneration(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryCommitZoneRuntimeGeneration(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginGenerationAbandonment(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(
        TryBeginZoneRuntimeGenerationAbandonment(
            &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryContinueGenerationAbandonment(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(
        TryContinueZoneRuntimeGenerationAbandonment(
            &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryUnloadGeneration(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryUnloadZoneRuntimeGeneration(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryBeginPendingCopyDrain(
    const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    if (!authoritySpanIsSeparated(
            &callback,
            sizeof(callback),
            alignof(zone_pending_copy::PendingCopyDrainCallback))
        || (callback.context
            && !authoritySpanIsSeparated(callback.context, 1, 1)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    return completeTableOperation(TryBeginZoneRuntimePendingCopyDrain(
        &ProductionZoneRuntimeTable(), callback));
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::TryDrainNextPendingCopy() noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryDrainNextZoneRuntimePendingCopy(
        &ProductionZoneRuntimeTable()));
}

ZoneRuntimeTableStatus
ZoneRuntimeFacade::TryFinishPendingCopyDrain() noexcept
{
    const ZoneRuntimeTableStatus access = authenticateTableOperationAccess();
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryFinishZoneRuntimePendingCopyDrain(
        &ProductionZoneRuntimeTable()));
}

ZoneRuntimeTableStatus ZoneRuntimeFacade::TryResetTerminalReceipt(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus access =
        authenticateKeyedTableOperationAccess(key);
    if (access != ZoneRuntimeTableStatus::Success)
        return access;
    return completeTableOperation(TryResetZoneRuntimeTerminalReceipt(
        &ProductionZoneRuntimeTable(), physicalSlot, key));
}

RegistryOwnershipStatus
ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership() noexcept
{
    return RunRegistryOperation([]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryBeginStandalone();
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryBorrowRegistryOwnership(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus tableAccess =
        authenticateTableOperationAccess();
    if (tableAccess != ZoneRuntimeTableStatus::Success)
        return MapTableStatusToRegistryStatus(tableAccess);
    if (!authoritySpanIsSeparated(
            &key, sizeof(key), alignof(zone_load::ZoneLoadContextKey)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }

    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    ZoneRuntimeGenerationView view{};
    const ZoneRuntimeTableStatus tableStatus = completeTableOperation(
        TryGetZoneRuntimeGeneration(
            &table, physicalSlot, key, &view));
    if (tableStatus != ZoneRuntimeTableStatus::Success)
    {
        if (tableStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            poisonAccess();
        return MapTableStatusToRegistryStatus(tableStatus);
    }
    if (!zone_slots::IsUsableZoneSlot(physicalSlot)
        || !view || view.key != key
        || view.entry != &table.entries_[physicalSlot])
    {
        poisonAccess();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    return RunRegistryOperation([&view, &key]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryBorrow(view.entry->scriptStringOwnership(), key);
    });
}

RegistryOwnershipStatus
ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback(
    const ZoneRuntimeCallbackContext *const context,
    const zone_load::ZoneLoadContextKey key) noexcept
{
    const ZoneRuntimeTableStatus tableAccess =
        authenticateTableOperationAccess();
    if (tableAccess != ZoneRuntimeTableStatus::Success)
        return MapTableStatusToRegistryStatus(tableAccess);

    const ZoneRuntimeCallbackContextStatus contextStatus =
        ZoneRuntimeCallbackContextOwner::TryAuthenticate(
            context, key, ZoneRuntimeCallbackContextPhase::Bound);
    if (contextStatus != ZoneRuntimeCallbackContextStatus::Success)
    {
        const RegistryOwnershipStatus mapped =
            MapCallbackContextStatusToRegistryStatus(contextStatus);
        if (mapped == RegistryOwnershipStatus::UnsafeFailure)
            poisonAccess();
        return mapped;
    }
    if (!zone_slots::IsUsableZoneSlot(key.slot))
    {
        poisonAccess();
        return RegistryOwnershipStatus::UnsafeFailure;
    }

    ZoneRuntimeTable &table = ProductionZoneRuntimeTable();
    const ZoneRuntimeTableStatus callbackStatus = completeTableOperation(
        table.authenticateExactRegistryLifecycleCallback(
            context, key.slot, key));
    if (callbackStatus != ZoneRuntimeTableStatus::Success)
        return MapTableStatusToRegistryStatus(callbackStatus);

    const RegistryOwnershipStatus callbackBorrow = RunRegistryOperation(
        [&table, &key]() noexcept {
            return registry_ownership::RegistryOwnershipCoordinatorFacade::
                TryBorrowActiveRuntimeCallback(
                    table.entries_[key.slot].scriptStringOwnership(), key);
        });
    if (callbackBorrow != RegistryOwnershipStatus::Busy)
        return callbackBorrow;

    // Only a coordinator Busy that retained no authority may restore the
    // exact consumed marker. The same callback window can then return Retry
    // and invoke this one-shot admission again.
    if (authenticateTableOperationAccess()
        != ZoneRuntimeTableStatus::Success)
    {
        poisonAccess();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    const ZoneRuntimeTableStatus restoreStatus = completeTableOperation(
        table.restoreExactRegistryLifecycleCallback(
            context,
            key.slot,
            key,
            ZoneRuntimeTable::ValidationDepth::StructuralOnly));
    if (restoreStatus != ZoneRuntimeTableStatus::Success)
    {
        poisonAccess();
        return RegistryOwnershipStatus::UnsafeFailure;
    }
    return callbackBorrow;
}

RegistryOwnershipStatus ZoneRuntimeFacade::FinishRegistryOwnership() noexcept
{
    return RunRegistryOperation([]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            Finish();
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryAddDatabaseUser4(
    const std::uint32_t stringId) noexcept
{
    return RunRegistryOperation([stringId]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryAddDatabaseUser4(stringId);
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryAddDatabaseUsers4(
    const std::uint32_t *const stringIds,
    const std::uint32_t count,
    registry_ownership::RegistryOwnershipBulkResult *const outResult) noexcept
{
    const RegistryOwnershipStatus access =
        authenticateRegistryOutputAccess();
    if (access != RegistryOwnershipStatus::Success)
        return access;
    if (outResult
        && !authoritySpanIsSeparated(
            outResult,
            sizeof(*outResult),
            alignof(registry_ownership::RegistryOwnershipBulkResult)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    if (outResult && stringIds && count != 0
        && count <= script_string::kRegistryOwnershipBulkCapacity
        && !authoritySpanIsSeparated(
            stringIds,
            static_cast<std::size_t>(count) * sizeof(*stringIds),
            alignof(std::uint32_t)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    return RunRegistryOperation([stringIds, count, outResult]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryAddDatabaseUsers4(stringIds, count, outResult);
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryInternBoundedName(
    const char *const bytes,
    const std::uint32_t byteCount,
    registry_ownership::RegistryOwnershipName *const outName) noexcept
{
    const RegistryOwnershipStatus access =
        authenticateRegistryOutputAccess();
    if (access != RegistryOwnershipStatus::Success)
        return access;
    if (outName
        && !authoritySpanIsSeparated(
            outName,
            sizeof(*outName),
            alignof(registry_ownership::RegistryOwnershipName)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    if (outName && bytes && byteCount != 0
        && byteCount <= UINT32_C(65531)
        && !authoritySpanIsSeparated(
            bytes, static_cast<std::size_t>(byteCount), 1))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    return RunRegistryOperation([bytes, byteCount, outName]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryInternBoundedName(bytes, byteCount, outName);
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryReAddRetainedDefaultName(
    const char *const name) noexcept
{
    const RegistryOwnershipStatus access =
        authenticateRegistryOutputAccess();
    if (access != RegistryOwnershipStatus::Success)
        return access;
    if (name && !authoritySpanIsSeparated(name, 1, 1))
        return RegistryOwnershipStatus::InvalidArgument;
    return RunRegistryOperation([name]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryReAddRetainedDefaultName(name);
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryReAddRetainedDefaultNames(
    const char *const *const names,
    const std::uint32_t count,
    registry_ownership::RegistryOwnershipBulkResult *const outResult) noexcept
{
    const RegistryOwnershipStatus access =
        authenticateRegistryOutputAccess();
    if (access != RegistryOwnershipStatus::Success)
        return access;
    if (outResult
        && !authoritySpanIsSeparated(
            outResult,
            sizeof(*outResult),
            alignof(registry_ownership::RegistryOwnershipBulkResult)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    if (outResult && names && count != 0
        && count <= script_string::kRegistryOwnershipBulkCapacity
        && !authoritySpanIsSeparated(
            names,
            static_cast<std::size_t>(count) * sizeof(*names),
            alignof(const char *)))
    {
        return RegistryOwnershipStatus::InvalidArgument;
    }
    return RunRegistryOperation([names, count, outResult]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryReAddRetainedDefaultNames(names, count, outResult);
    });
}

RegistryOwnershipStatus
ZoneRuntimeFacade::TryTransferDatabaseUsers4To8() noexcept
{
    return RunRegistryOperation([]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryTransferDatabaseUsers4To8();
    });
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryShutdownDatabaseUser8() noexcept
{
    return RunRegistryOperation([]() noexcept {
        return registry_ownership::RegistryOwnershipCoordinatorFacade::
            TryShutdownDatabaseUser8();
    });
}

#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
void ZoneRuntimeFacadeTestAccess::ResetForTesting() noexcept
{
    s_runtimeSerializer.readCount = 0;
    s_runtimeSerializer.writeCount = 0;
    s_runtimeState = RuntimeBoundaryState::Idle;
    s_runtimeStateMirror = RuntimeBoundaryState::Idle;
    s_activeThreadIdentity = 0;
    s_activeThreadIdentityMirror = 0;
    s_activeSerial = 0;
    s_activeSerialMirror = 0;
    s_nextSerial = 0;
    s_nextSerialMirror = 0;
    s_retainedThreadState = RuntimeBoundaryState::Idle;
    s_retainedThreadStateMirror = RuntimeBoundaryState::Idle;
    s_retainedThreadIdentity = 0;
    s_retainedThreadIdentityMirror = 0;
    s_retainedSerial = 0;
    s_retainedSerialMirror = 0;
}

void ZoneRuntimeFacadeTestAccess::SetGlobalStateForTesting(
    const std::uint8_t state,
    const std::uint8_t stateMirror,
    const std::uintptr_t threadIdentity,
    const std::uintptr_t threadIdentityMirror,
    const std::uint64_t activeSerial,
    const std::uint64_t activeSerialMirror,
    const std::uint64_t nextSerial,
    const std::uint64_t nextSerialMirror) noexcept
{
    s_runtimeState = static_cast<RuntimeBoundaryState>(state);
    s_runtimeStateMirror = static_cast<RuntimeBoundaryState>(stateMirror);
    s_activeThreadIdentity = threadIdentity;
    s_activeThreadIdentityMirror = threadIdentityMirror;
    s_activeSerial = activeSerial;
    s_activeSerialMirror = activeSerialMirror;
    s_nextSerial = nextSerial;
    s_nextSerialMirror = nextSerialMirror;
}

void ZoneRuntimeFacadeTestAccess::SetThreadStateForTesting(
    const std::uint8_t state,
    const std::uint8_t stateMirror,
    const std::uintptr_t threadIdentity,
    const std::uintptr_t threadIdentityMirror,
    const std::uint64_t serial,
    const std::uint64_t serialMirror) noexcept
{
    s_retainedThreadState = static_cast<RuntimeBoundaryState>(state);
    s_retainedThreadStateMirror =
        static_cast<RuntimeBoundaryState>(stateMirror);
    s_retainedThreadIdentity = threadIdentity;
    s_retainedThreadIdentityMirror = threadIdentityMirror;
    s_retainedSerial = serial;
    s_retainedSerialMirror = serialMirror;
}

std::uintptr_t ZoneRuntimeFacadeTestAccess::CurrentThreadIdentity() noexcept
{
    return CurrentThreadIdentityValue();
}
#endif
} // namespace db::zone_runtime

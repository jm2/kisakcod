#include "db_load_legacy_bridge.h"

#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_facade.h>

#include <cstring>

namespace db::load_legacy_bridge
{
namespace
{
using db::registry_ownership::RegistryOwnershipStatus;
using db::zone_runtime::ZoneRuntimeFacade;
using db::zone_runtime::ZoneRuntimeFacadeStatus;

constexpr std::uint32_t kLegacyBridgeMaxBytes = 65531u;

[[nodiscard]] bool ByteCountIsRepresentable(
    const std::uint32_t byteCount) noexcept
{
    return byteCount != 0u && byteCount <= kLegacyBridgeMaxBytes;
}

void ResetStringId(LegacyBridgeStringId *const outString) noexcept
{
    if (!outString)
        return;
    outString->stringId = 0u;
    outString->canonicalName = nullptr;
}

void PublishStringId(
    LegacyBridgeStringId *const outString,
    const db::registry_ownership::RegistryOwnershipName &source) noexcept
{
    if (!outString)
        return;
    outString->stringId = source.stringId;
    outString->canonicalName = source.canonicalName;
}

[[nodiscard]] LegacyBridgeStatus MapRegistryStatus(
    const db::registry_ownership::RegistryOwnershipStatus status) noexcept
{
    using db::registry_ownership::RegistryOwnershipStatus;
    switch (status)
    {
    case RegistryOwnershipStatus::Success:
    case RegistryOwnershipStatus::NoChange:
        return LegacyBridgeStatus::Success;
    case RegistryOwnershipStatus::Busy:
        return LegacyBridgeStatus::Busy;
    case RegistryOwnershipStatus::InvalidArgument:
        return LegacyBridgeStatus::InvalidArgument;
    case RegistryOwnershipStatus::InvalidState:
        return LegacyBridgeStatus::InvalidState;
    case RegistryOwnershipStatus::OwnershipMismatch:
        return LegacyBridgeStatus::OwnershipMismatch;
    case RegistryOwnershipStatus::RefCountExhausted:
        return LegacyBridgeStatus::RefCountExhausted;
    case RegistryOwnershipStatus::CapacityExceeded:
        return LegacyBridgeStatus::CapacityExceeded;
    case RegistryOwnershipStatus::InvalidKey:
        return LegacyBridgeStatus::InvalidState;
    case RegistryOwnershipStatus::UnsafeFailure:
    default:
        return LegacyBridgeStatus::UnsafeFailure;
    }
}

[[nodiscard]] LegacyBridgeStatus CleanupFailed(
    const ZoneRuntimeFacadeStatus accessStatus,
    const RegistryOwnershipStatus finishStatus) noexcept
{
    static_cast<void>(accessStatus);
    static_cast<void>(finishStatus);
    return LegacyBridgeStatus::UnsafeFailure;
}

[[nodiscard]] LegacyBridgeStatus RunWithStandaloneOwnership(
    const std::uint32_t stringId,
    const char *const bytes,
    const std::uint32_t byteCount,
    LegacyBridgeStringId *const outString,
    const bool writeOutput) noexcept
{
    ResetStringId(outString);

    if (ZoneRuntimeFacade::TryBeginAccess()
        != ZoneRuntimeFacadeStatus::Success)
    {
        return LegacyBridgeStatus::UnsafeFailure;
    }

    if (ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
        != RegistryOwnershipStatus::Success)
    {
        const ZoneRuntimeFacadeStatus abandoned =
            ZoneRuntimeFacade::FinishAccess();
        return CleanupFailed(abandoned, RegistryOwnershipStatus::UnsafeFailure);
    }

    RegistryOwnershipStatus opStatus = RegistryOwnershipStatus::UnsafeFailure;
    if (writeOutput)
    {
        if (!bytes || !ByteCountIsRepresentable(byteCount))
        {
            (void)ZoneRuntimeFacade::FinishRegistryOwnership();
            (void)ZoneRuntimeFacade::FinishAccess();
            return LegacyBridgeStatus::InvalidArgument;
        }
        if (bytes[byteCount - 1u] != '\0')
        {
            (void)ZoneRuntimeFacade::FinishRegistryOwnership();
            (void)ZoneRuntimeFacade::FinishAccess();
            return LegacyBridgeStatus::InvalidArgument;
        }
        db::registry_ownership::RegistryOwnershipName interned{};
        opStatus = ZoneRuntimeFacade::TryInternBoundedName(
            bytes, byteCount, &interned);
        if (opStatus == RegistryOwnershipStatus::Success)
            PublishStringId(outString, interned);
    }
    else
    {
        opStatus = ZoneRuntimeFacade::TryAddDatabaseUser4(stringId);
    }

    const RegistryOwnershipStatus finishStatus =
        ZoneRuntimeFacade::FinishRegistryOwnership();
    const ZoneRuntimeFacadeStatus accessStatus =
        ZoneRuntimeFacade::FinishAccess();

    if (finishStatus != RegistryOwnershipStatus::Success
        || accessStatus != ZoneRuntimeFacadeStatus::Success)
    {
        return CleanupFailed(accessStatus, finishStatus);
    }

    const LegacyBridgeStatus mapped = MapRegistryStatus(opStatus);
    if (mapped == LegacyBridgeStatus::UnsafeFailure
        && opStatus != RegistryOwnershipStatus::UnsafeFailure)
    {
        return LegacyBridgeStatus::InvalidState;
    }
    return mapped;
}

[[nodiscard]] LegacyBridgeStatus RunTransferOrShutdown(
    const bool isTransfer) noexcept
{
    if (ZoneRuntimeFacade::TryBeginAccess()
        != ZoneRuntimeFacadeStatus::Success)
    {
        return LegacyBridgeStatus::UnsafeFailure;
    }
    if (ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership()
        != RegistryOwnershipStatus::Success)
    {
        const ZoneRuntimeFacadeStatus abandoned =
            ZoneRuntimeFacade::FinishAccess();
        return CleanupFailed(abandoned, RegistryOwnershipStatus::UnsafeFailure);
    }

    const RegistryOwnershipStatus opStatus = isTransfer
        ? ZoneRuntimeFacade::TryTransferDatabaseUsers4To8()
        : ZoneRuntimeFacade::TryShutdownDatabaseUser8();

    const RegistryOwnershipStatus finishStatus =
        ZoneRuntimeFacade::FinishRegistryOwnership();
    const ZoneRuntimeFacadeStatus accessStatus =
        ZoneRuntimeFacade::FinishAccess();

    if (finishStatus != RegistryOwnershipStatus::Success
        || accessStatus != ZoneRuntimeFacadeStatus::Success)
    {
        return CleanupFailed(accessStatus, finishStatus);
    }
    return MapRegistryStatus(opStatus);
}
} // namespace

LegacyBridgeStatus DbLoadLegacyBridge::TryInternUser4String(
    const char *const name,
    LegacyBridgeStringId *const outString) noexcept
{
    if (!name)
        return LegacyBridgeStatus::InvalidArgument;
    const std::uint32_t byteCount =
        static_cast<std::uint32_t>(std::strlen(name)) + 1u;
    return DbLoadLegacyBridge::TryInternUser4StringOfSize(
        name, byteCount, outString);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *const bytes,
    const std::uint32_t byteCount,
    LegacyBridgeStringId *const outString) noexcept
{
    if (!outString)
        return LegacyBridgeStatus::InvalidArgument;
    return RunWithStandaloneOwnership(
        0u, bytes, byteCount, outString, true);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryAddUser4(
    const std::uint32_t stringId) noexcept
{
    if (stringId == 0u || stringId > 0xFFFFu)
        return LegacyBridgeStatus::InvalidArgument;
    return RunWithStandaloneOwnership(
        stringId, nullptr, 0u, nullptr, false);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryTransferUsers4To8() noexcept
{
    return RunTransferOrShutdown(true);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryShutdownUser8() noexcept
{
    return RunTransferOrShutdown(false);
}
} // namespace db::load_legacy_bridge

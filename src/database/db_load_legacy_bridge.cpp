#include "db_load_legacy_bridge.h"

#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_facade.h>

#include <cstring>

namespace db::load_legacy_bridge
{
namespace
{
// Maximum representable byte count for a user-4/user-8 string in the
// registry-owned string table. SL_GetString/OfSize cap themselves at this
// value; the bridge refuses to intern larger payloads so the lower
// adapter's status precedence stays the same.
constexpr std::uint32_t kLegacyBridgeMaxBytes = 65531u;

[[nodiscard]] bool ByteCountIsRepresentable(const std::uint32_t byteCount) noexcept
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
} // namespace

LegacyBridgeStatus DbLoadLegacyBridge::TryInternUser4String(
    const char *const name,
    LegacyBridgeStringId *const outString) noexcept
{
    ResetStringId(outString);

    if (!name)
        return LegacyBridgeStatus::InvalidArgument;

    const std::uint32_t byteCount = static_cast<std::uint32_t>(std::strlen(name))
        + 1u;
    return TryInternUser4StringOfSize(name, byteCount, outString);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *const bytes,
    const std::uint32_t byteCount,
    LegacyBridgeStringId *const outString) noexcept
{
    ResetStringId(outString);

    if (!outString || !bytes || !ByteCountIsRepresentable(byteCount)
        || bytes[byteCount - 1u] != '\0')
    {
        return LegacyBridgeStatus::InvalidArgument;
    }

    db::registry_ownership::RegistryOwnershipName interned{};
    const db::registry_ownership::RegistryOwnershipStatus status =
        db::zone_runtime::ZoneRuntimeFacade::TryInternBoundedName(
            bytes, byteCount, &interned);
    const LegacyBridgeStatus mapped = MapRegistryStatus(status);
    if (mapped == LegacyBridgeStatus::Success)
        PublishStringId(outString, interned);
    return mapped;
}

LegacyBridgeStatus DbLoadLegacyBridge::TryAddUser4(
    const std::uint32_t stringId) noexcept
{
    if (stringId == 0u || stringId > 0xFFFFu)
        return LegacyBridgeStatus::InvalidArgument;

    const db::registry_ownership::RegistryOwnershipStatus status =
        db::zone_runtime::ZoneRuntimeFacade::TryAddDatabaseUser4(stringId);
    return MapRegistryStatus(status);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryTransferUsers4To8() noexcept
{
    const db::registry_ownership::RegistryOwnershipStatus status =
        db::zone_runtime::ZoneRuntimeFacade::TryTransferDatabaseUsers4To8();
    return MapRegistryStatus(status);
}

LegacyBridgeStatus DbLoadLegacyBridge::TryShutdownUser8() noexcept
{
    const db::registry_ownership::RegistryOwnershipStatus status =
        db::zone_runtime::ZoneRuntimeFacade::TryShutdownDatabaseUser8();
    return MapRegistryStatus(status);
}
} // namespace db::load_legacy_bridge
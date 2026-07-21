#include <database/db_load_legacy_bridge.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_facade.h>

#include <cstddef>
#include <cstdint>

namespace db::zone_runtime
{
using db::registry_ownership::RegistryOwnershipName;
using db::registry_ownership::RegistryOwnershipStatus;

// Stubs so the bridge links without dragging in the full production
// runtime table. Each stub reports a Busy status; the bridge tests do
// not exercise these paths because every test in this file returns on
// the bridge's own parameter validation before reaching the facade.
RegistryOwnershipStatus ZoneRuntimeFacade::TryAddDatabaseUser4(
    std::uint32_t) noexcept
{
    return RegistryOwnershipStatus::Busy;
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryInternBoundedName(
    const char *const,
    std::uint32_t,
    RegistryOwnershipName *const outName) noexcept
{
    if (outName)
    {
        outName->stringId = 0u;
        outName->canonicalName = nullptr;
    }
    return RegistryOwnershipStatus::Busy;
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryTransferDatabaseUsers4To8() noexcept
{
    return RegistryOwnershipStatus::Busy;
}

RegistryOwnershipStatus ZoneRuntimeFacade::TryShutdownDatabaseUser8() noexcept
{
    return RegistryOwnershipStatus::Busy;
}
} // namespace db::zone_runtime

namespace db::load_legacy_bridge
{
[[nodiscard]] bool TestInvalidArgumentOnNullOutput() noexcept
{
    return DbLoadLegacyBridge::TryInternUser4String(
               "hello",
               static_cast<LegacyBridgeStringId *>(nullptr))
        == LegacyBridgeStatus::InvalidArgument;
}

[[nodiscard]] bool TestInvalidArgumentOnNullBytes() noexcept
{
    LegacyBridgeStringId outName{};
    return DbLoadLegacyBridge::TryInternUser4String(
               static_cast<const char *>(nullptr), &outName)
        == LegacyBridgeStatus::InvalidArgument;
}

[[nodiscard]] bool TestInvalidArgumentOnMissingTerminator() noexcept
{
    char bytes[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    LegacyBridgeStringId outName{};
    return DbLoadLegacyBridge::TryInternUser4StringOfSize(
               bytes,
               static_cast<std::uint32_t>(sizeof(bytes)),
               &outName)
        == LegacyBridgeStatus::InvalidArgument;
}

[[nodiscard]] bool TestAddUser4RejectsZeroStringId() noexcept
{
    return DbLoadLegacyBridge::TryAddUser4(0u)
        == LegacyBridgeStatus::InvalidArgument;
}

[[nodiscard]] bool TestAddUser4RejectsOversizeStringId() noexcept
{
    return DbLoadLegacyBridge::TryAddUser4(0x10000u)
        == LegacyBridgeStatus::InvalidArgument;
}

[[nodiscard]] bool RunAll() noexcept
{
    struct Probe
    {
        const char *label;
        bool (*fn)() noexcept;
    };
    constexpr Probe probes[] = {
        {"null output rejected", &TestInvalidArgumentOnNullOutput},
        {"null bytes rejected", &TestInvalidArgumentOnNullBytes},
        {"missing terminator rejected",
         &TestInvalidArgumentOnMissingTerminator},
        {"zero stringId rejected", &TestAddUser4RejectsZeroStringId},
        {"oversize stringId rejected", &TestAddUser4RejectsOversizeStringId},
    };
    for (const Probe &probe : probes)
    {
        if (!probe.fn())
            return false;
    }
    return true;
}
} // namespace db::load_legacy_bridge

int main()
{
    return db::load_legacy_bridge::RunAll() ? 0 : 1;
}
#pragma once

#include <cstddef>
#include <cstdint>

namespace db::load_legacy_bridge
{
enum class LegacyBridgeStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidState,
    OwnershipMismatch,
    RefCountExhausted,
    CapacityExceeded,
    UnsafeFailure,
};

struct LegacyBridgeStringId final
{
    std::uint32_t stringId = 0;
    const char *canonicalName = nullptr;
};

class DbLoadLegacyBridge final
{
public:
    DbLoadLegacyBridge() = delete;
    ~DbLoadLegacyBridge() noexcept = default;
    DbLoadLegacyBridge(const DbLoadLegacyBridge &) = delete;
    DbLoadLegacyBridge &operator=(const DbLoadLegacyBridge &) = delete;
    DbLoadLegacyBridge(DbLoadLegacyBridge &&) = delete;
    DbLoadLegacyBridge &operator=(DbLoadLegacyBridge &&) = delete;

    [[nodiscard]] static LegacyBridgeStatus TryInternUser4String(
        const char *name,
        LegacyBridgeStringId *outString) noexcept;

    [[nodiscard]] static LegacyBridgeStatus TryInternUser4StringOfSize(
        const char *bytes,
        std::uint32_t byteCount,
        LegacyBridgeStringId *outString) noexcept;

    [[nodiscard]] static LegacyBridgeStatus TryAddUser4(
        std::uint32_t stringId) noexcept;

    [[nodiscard]] static LegacyBridgeStatus TryTransferUsers4To8() noexcept;

    [[nodiscard]] static LegacyBridgeStatus TryShutdownUser8() noexcept;
};
} // namespace db::load_legacy_bridge

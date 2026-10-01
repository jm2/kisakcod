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

    // A registry session for the db_registry unload sequences (DB_ShutdownXAssets,
    // DB_LoadXAssets), which walk the asset hash under db_hashCritSect and move
    // user-4 names through the calls above. Each call opens a registry window
    // that takes db_hashCritSect itself, after the script-string transaction
    // (the coordinator's lock order), so it refuses a caller holding the hash.
    // A session opens one window in place of the sequence's Sys_LockWrite, and
    // the calls above made on its thread run inside that window.
    //
    // BeginSession waits as Sys_LockWrite does and returns with the hash held;
    // the caller must not hold it, and sessions do not nest. If no window opens,
    // the session takes the hash itself and every call in it fails with
    // InvalidState. FinishSession releases the hash and returns the session's
    // first failure, so the sequence reports it with the hash free (an error
    // raised under the hash self-deadlocks in Com_ErrorCleanup's localization).
    // UnsafeFailure: the registry poisoned inside the window, which keeps the
    // hash (fail closed).
    static void BeginSession() noexcept;
    [[nodiscard]] static bool InSession() noexcept;
    [[nodiscard]] static LegacyBridgeStatus SessionStatus() noexcept;
    [[nodiscard]] static LegacyBridgeStatus FinishSession() noexcept;
};
} // namespace db::load_legacy_bridge

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

    // A registry session for the db_registry unload sequence (DB_ShutdownXAssets,
    // and DB_LoadXAssets when it unloads zones), which walks the asset hash under
    // db_hashCritSect and moves user-4 references through this bridge. Each call
    // above runs in a standalone registry window, and a window takes
    // db_hashCritSect itself, after the script-string transaction (the
    // coordinator's lock order), so it refuses a caller that already holds the
    // hash. A session opens one window in place of the sequence's Sys_LockWrite:
    // the window holds db_hashCritSect until FinishSession, and the calls above,
    // made on the session's thread, run inside it.
    //
    // BeginSession waits as Sys_LockWrite does and returns with db_hashCritSect
    // write-locked; the caller must not hold it, and sessions do not nest. If
    // the window cannot open, the session takes the hash directly and every call
    // in it fails. The session keeps its first failure (SessionStatus), and
    // FinishSession releases the hash and returns it, so the sequence reports it
    // only once the hash is free: an error raised under the hash self-deadlocks
    // when Com_ErrorCleanup looks up the localized message.
    static void BeginSession() noexcept;
    [[nodiscard]] static bool InSession() noexcept;
    [[nodiscard]] static LegacyBridgeStatus SessionStatus() noexcept;
    [[nodiscard]] static LegacyBridgeStatus FinishSession() noexcept;
};
} // namespace db::load_legacy_bridge

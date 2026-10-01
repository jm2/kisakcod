#include "db_load_legacy_bridge.h"

#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_facade.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>

#include <cstring>

extern FastCriticalSection db_hashCritSect;

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

// This thread's registry session (DbLoadLegacyBridge::BeginSession).
enum class SessionState : std::uint8_t
{
    None,
    Window,
    HashOnly,
};
thread_local SessionState t_session = SessionState::None;
thread_local std::uint32_t t_sessionDepth = 0;
thread_local LegacyBridgeStatus t_sessionStatus = LegacyBridgeStatus::Success;

// Opens a standalone registry window: facade access, then registry ownership,
// which takes db_hashCritSect. Busy means another holder; nothing is kept.
[[nodiscard]] LegacyBridgeStatus TryOpenWindow() noexcept
{
    const ZoneRuntimeFacadeStatus access = ZoneRuntimeFacade::TryBeginAccess();
    if (access != ZoneRuntimeFacadeStatus::Success)
    {
        return access == ZoneRuntimeFacadeStatus::Busy
            ? LegacyBridgeStatus::Busy
            : LegacyBridgeStatus::UnsafeFailure;
    }
    const RegistryOwnershipStatus ownership =
        ZoneRuntimeFacade::TryBeginStandaloneRegistryOwnership();
    if (ownership == RegistryOwnershipStatus::Success)
        return LegacyBridgeStatus::Success;
    if (ZoneRuntimeFacade::FinishAccess() != ZoneRuntimeFacadeStatus::Success)
        return LegacyBridgeStatus::UnsafeFailure;
    return ownership == RegistryOwnershipStatus::Busy
        ? LegacyBridgeStatus::Busy
        : LegacyBridgeStatus::UnsafeFailure;
}

// Outside a session: waits out another thread's window or db_hashCritSect
// hold, as Sys_LockWrite does, holding nothing between attempts. Busy is that
// contention, unless this thread holds a fast critical section: the hash, say,
// whose counts cannot name its holder. Such a caller fails instead of waiting
// on itself.
[[nodiscard]] LegacyBridgeStatus OpenWindowWaiting() noexcept
{
    LegacyBridgeStatus status = TryOpenWindow();
    while (status == LegacyBridgeStatus::Busy && !Sys_HoldsFastCriticalSection())
    {
        Sys_Sleep(0);
        status = TryOpenWindow();
    }
    return status;
}

[[nodiscard]] LegacyBridgeStatus CloseWindow() noexcept
{
    const RegistryOwnershipStatus finishStatus =
        ZoneRuntimeFacade::FinishRegistryOwnership();
    const ZoneRuntimeFacadeStatus accessStatus =
        ZoneRuntimeFacade::FinishAccess();
    return finishStatus == RegistryOwnershipStatus::Success
            && accessStatus == ZoneRuntimeFacadeStatus::Success
        ? LegacyBridgeStatus::Success
        : LegacyBridgeStatus::UnsafeFailure;
}

// Runs one registry operation in this thread's session window, keeping the
// session's first failure, or else in a window of its own.
template <typename Operation>
[[nodiscard]] LegacyBridgeStatus RunInWindow(const Operation &operation) noexcept
{
    if (t_sessionDepth != 0)
    {
        const LegacyBridgeStatus status = t_session == SessionState::Window
            ? MapRegistryStatus(operation())
            : t_sessionStatus;
        if (t_sessionStatus == LegacyBridgeStatus::Success)
            t_sessionStatus = status;
        return status;
    }
    if (OpenWindowWaiting() != LegacyBridgeStatus::Success)
        return LegacyBridgeStatus::UnsafeFailure;
    const RegistryOwnershipStatus status = operation();
    if (CloseWindow() != LegacyBridgeStatus::Success)
        return LegacyBridgeStatus::UnsafeFailure;
    return MapRegistryStatus(status);
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
    ResetStringId(outString);
    return RunInWindow([&]() noexcept {
        if (!bytes || !ByteCountIsRepresentable(byteCount)
            || bytes[byteCount - 1u] != '\0')
        {
            return RegistryOwnershipStatus::InvalidArgument;
        }
        db::registry_ownership::RegistryOwnershipName interned{};
        const RegistryOwnershipStatus status =
            ZoneRuntimeFacade::TryInternBoundedName(bytes, byteCount, &interned);
        if (status == RegistryOwnershipStatus::Success)
            PublishStringId(outString, interned);
        return status;
    });
}

LegacyBridgeStatus DbLoadLegacyBridge::TryAddUser4(
    const std::uint32_t stringId) noexcept
{
    if (stringId == 0u || stringId > 0xFFFFu)
        return LegacyBridgeStatus::InvalidArgument;
    return RunInWindow([stringId]() noexcept {
        return ZoneRuntimeFacade::TryAddDatabaseUser4(stringId);
    });
}

LegacyBridgeStatus DbLoadLegacyBridge::TryTransferUsers4To8() noexcept
{
    return RunInWindow([]() noexcept {
        return ZoneRuntimeFacade::TryTransferDatabaseUsers4To8();
    });
}

LegacyBridgeStatus DbLoadLegacyBridge::TryShutdownUser8() noexcept
{
    return RunInWindow([]() noexcept {
        return ZoneRuntimeFacade::TryShutdownDatabaseUser8();
    });
}

void DbLoadLegacyBridge::BeginSession() noexcept
{
    if (t_sessionDepth++ != 0)
    {
        // Sessions do not nest: the outer one keeps the hash and the window.
        if (t_sessionStatus == LegacyBridgeStatus::Success)
            t_sessionStatus = LegacyBridgeStatus::InvalidState;
        return;
    }
    // Wait out other holders, as Sys_LockWrite does; nothing is held between
    // attempts, so waiting cannot invert the lock order.
    LegacyBridgeStatus status = TryOpenWindow();
    while (status == LegacyBridgeStatus::Busy)
    {
        Sys_Sleep(0);
        status = TryOpenWindow();
    }
    if (status == LegacyBridgeStatus::Success)
    {
        t_session = SessionState::Window;
        return;
    }
    t_sessionStatus = LegacyBridgeStatus::InvalidState;
    Sys_LockWrite(&db_hashCritSect);
    t_session = SessionState::HashOnly;
}

bool DbLoadLegacyBridge::InSession() noexcept
{
    return t_sessionDepth != 0;
}

LegacyBridgeStatus DbLoadLegacyBridge::SessionStatus() noexcept
{
    return t_sessionDepth != 0 ? t_sessionStatus : LegacyBridgeStatus::Success;
}

LegacyBridgeStatus DbLoadLegacyBridge::FinishSession() noexcept
{
    if (t_sessionDepth == 0)
        return LegacyBridgeStatus::InvalidState;
    if (--t_sessionDepth != 0)
        return t_sessionStatus;
    LegacyBridgeStatus status = t_sessionStatus;
    if (t_session == SessionState::Window)
    {
        // A poisoned window keeps db_hashCritSect, failing closed.
        if (CloseWindow() != LegacyBridgeStatus::Success
            && status == LegacyBridgeStatus::Success)
        {
            status = LegacyBridgeStatus::UnsafeFailure;
        }
    }
    else
    {
        Sys_UnlockWrite(&db_hashCritSect);
    }
    t_session = SessionState::None;
    t_sessionStatus = LegacyBridgeStatus::Success;
    return status;
}
} // namespace db::load_legacy_bridge

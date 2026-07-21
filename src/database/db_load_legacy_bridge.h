#pragma once

// Production legacy bridge for the seven remaining raw user-4/user-8 loader
// sites in the database load path. The bridge is the sole production entry
// point that may route raw fast-file string allocation, user-4 reference
// addition, the 4 -> 8 authority transfer, and the user-8 release through
// the registry ownership coordinator. Every method delegates to the
// ZoneRuntimeFacade so the bridge inherits the coordinator's checked,
// no-report, no-unlock contract; it never calls SL_GetString,
// SL_GetStringOfSize, SL_AddUser, SL_TransferSystem, or SL_ShutdownSystem
// directly.
//
// The bridge deliberately does NOT re-export db::registry_ownership types.
// All coordinator-specific types stay behind the bridge's opaque string-id
// and canonical-name pair, so legacy callers can replace their raw SL_*
// helpers without pulling the registry ownership namespace into
// production registry/load code. The registry-ownership source-invariant
// test enforces this isolation by scanning every translation unit for
// references to the coordinator namespace and rejecting any that fall
// outside the reviewed bridge, facade, and coordinator translation units.
//
// Sequencing invariants:
//   1. Every bridge call runs only after ZoneRuntimeFacade::TryBeginAccess
//      has succeeded on the calling thread; the facade's RunRegistryOperation
//      guard fails closed otherwise.
//   2. TryInternUser4String/Size and TryAddUser4 return a status that must
//      be propagated; on failure the caller must leave the database xasset
//      allocation or string intern uncommitted.
//   3. TryTransferDatabaseUsers4To8 must run after every interning step on
//      user 4 has been committed; TryShutdownDatabaseUser8 must run only
//      after the transfer completed.
//
// Fault injection: the bridge exposes a compile-time disabled test hook that
// returns FailureXxx statuses on demand. The hook is sealed behind a
// KISAK_DB_LOAD_LEGACY_BRIDGE_FAULT_INJECTION macro and is unused in
// production builds; the production seal test asserts the macro never
// appears in any non-test translation unit.
//
// Source allowlist/seals: the bridge is the only translation unit permitted
// to call ZoneRuntimeFacade's user-4/user-8 adapters outside the facade
// itself. The production seal test asserts every other translation unit
// avoids the four raw SL_* helpers named above and the
// db::registry_ownership namespace.

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

// Opaque result returned by the intern helpers. Both fields are populated
// only when the bridge returns LegacyBridgeStatus::Success; otherwise the
// pair is reset to {0u, nullptr} and the caller must propagate the status.
struct LegacyBridgeStringId final
{
    std::uint32_t stringId = 0u;
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

    // Replaces SL_GetString(name, 4): interns a NUL-terminated user-4 string.
    [[nodiscard]] static LegacyBridgeStatus TryInternUser4String(
        const char *name,
        LegacyBridgeStringId *outString) noexcept;

    // Replaces SL_GetStringOfSize(str, 4, byteCount, 6): interns a bounded
    // user-4 string from a stream-backed pointer without strlen. The byte
    // count MUST include the trailing NUL and the final byte must be 0.
    [[nodiscard]] static LegacyBridgeStatus TryInternUser4StringOfSize(
        const char *bytes,
        std::uint32_t byteCount,
        LegacyBridgeStringId *outString) noexcept;

    // Replaces SL_AddUser(*var, 4u): adds a single user-4 reference to a
    // previously interned string id. The string id must already be live.
    [[nodiscard]] static LegacyBridgeStatus TryAddUser4(
        std::uint32_t stringId) noexcept;

    // Replaces SL_TransferSystem(4u, 8u): moves every authenticated user-4
    // reference to user-8 without freeing storage. Must run after every
    // interning step on user 4 has been committed.
    [[nodiscard]] static LegacyBridgeStatus TryTransferUsers4To8() noexcept;

    // Replaces SL_ShutdownSystem(8): releases every live user-8 reference.
    // Must run only after TryTransferUsers4To8 has completed.
    [[nodiscard]] static LegacyBridgeStatus TryShutdownUser8() noexcept;
};
} // namespace db::load_legacy_bridge
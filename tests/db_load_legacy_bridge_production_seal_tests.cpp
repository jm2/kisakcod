#include <database/db_load_legacy_bridge.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace db::load_legacy_bridge
{
static_assert(std::is_empty_v<DbLoadLegacyBridge>);
static_assert(std::is_final_v<DbLoadLegacyBridge>);
static_assert(!std::is_default_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_copy_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_copy_assignable_v<DbLoadLegacyBridge>);
static_assert(!std::is_move_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_move_assignable_v<DbLoadLegacyBridge>);
static_assert(std::is_nothrow_destructible_v<DbLoadLegacyBridge>);

static_assert(noexcept(DbLoadLegacyBridge::TryInternUser4String(
    nullptr, nullptr)));
static_assert(noexcept(DbLoadLegacyBridge::TryInternUser4StringOfSize(
    nullptr, std::uint32_t{0}, nullptr)));
static_assert(noexcept(DbLoadLegacyBridge::TryAddUser4(std::uint32_t{0})));
static_assert(noexcept(DbLoadLegacyBridge::TryTransferUsers4To8()));
static_assert(noexcept(DbLoadLegacyBridge::TryShutdownUser8()));

static_assert(std::is_standard_layout_v<LegacyBridgeStringId>);
static_assert(std::is_trivially_copyable_v<LegacyBridgeStringId>);

using InternStringFn = LegacyBridgeStatus (*)(
    const char *, LegacyBridgeStringId *) noexcept;
using InternStringOfSizeFn = LegacyBridgeStatus (*)(
    const char *, std::uint32_t, LegacyBridgeStringId *) noexcept;
using AddUserFn = LegacyBridgeStatus (*)(std::uint32_t) noexcept;
using TransferFn = LegacyBridgeStatus (*)() noexcept;
using ShutdownFn = LegacyBridgeStatus (*)() noexcept;

static_assert(std::is_same_v<
    decltype(&DbLoadLegacyBridge::TryInternUser4String),
    InternStringFn>);
static_assert(std::is_same_v<
    decltype(&DbLoadLegacyBridge::TryInternUser4StringOfSize),
    InternStringOfSizeFn>);
static_assert(std::is_same_v<
    decltype(&DbLoadLegacyBridge::TryAddUser4),
    AddUserFn>);
static_assert(std::is_same_v<
    decltype(&DbLoadLegacyBridge::TryTransferUsers4To8),
    TransferFn>);
static_assert(std::is_same_v<
    decltype(&DbLoadLegacyBridge::TryShutdownUser8),
    ShutdownFn>);
} // namespace db::load_legacy_bridge

int main()
{
    return 0;
}

#include <database/db_load_legacy_bridge.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace db::load_legacy_bridge
{
struct LegacyBridgeTestAccess final
{
    template <typename Bridge>
    static constexpr bool CanMapRegistryStatus = requires
    {
        Bridge::mapRegistryStatusForTesting();
    };

    template <typename Bridge>
    static constexpr bool CanDirectlyCallZoneRuntimeFacade = requires
    {
        Bridge::tryDirectZoneRuntimeFacadeCallForTesting();
    };
};

static_assert(std::is_empty_v<DbLoadLegacyBridge>);
static_assert(std::is_final_v<DbLoadLegacyBridge>);
static_assert(!std::is_default_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_copy_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_copy_assignable_v<DbLoadLegacyBridge>);
static_assert(!std::is_move_constructible_v<DbLoadLegacyBridge>);
static_assert(!std::is_move_assignable_v<DbLoadLegacyBridge>);
static_assert(std::is_nothrow_destructible_v<DbLoadLegacyBridge>);

static_assert(
    !LegacyBridgeTestAccess::CanMapRegistryStatus<DbLoadLegacyBridge>);
static_assert(
    !LegacyBridgeTestAccess::CanDirectlyCallZoneRuntimeFacade<
        DbLoadLegacyBridge>);

static_assert(noexcept(DbLoadLegacyBridge::TryInternUser4String(
    static_cast<const char *>(nullptr),
    static_cast<LegacyBridgeStringId *>(nullptr))));
static_assert(noexcept(DbLoadLegacyBridge::TryInternUser4StringOfSize(
    static_cast<const char *>(nullptr),
    std::uint32_t{0},
    static_cast<LegacyBridgeStringId *>(nullptr))));
static_assert(noexcept(DbLoadLegacyBridge::TryAddUser4(std::uint32_t{0})));
static_assert(noexcept(DbLoadLegacyBridge::TryTransferUsers4To8()));
static_assert(noexcept(DbLoadLegacyBridge::TryShutdownUser8()));
} // namespace db::load_legacy_bridge

int main()
{
    return 0;
}
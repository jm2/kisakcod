#include <database/db_zone_runtime_facade.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace db::zone_runtime
{
static_assert(sizeof(ZoneRuntimePendingCopyView) == 0x18u);
static_assert(std::is_standard_layout_v<ZoneRuntimePendingCopyView>);
static_assert(std::is_trivially_copyable_v<ZoneRuntimePendingCopyView>);

using FacadePendingCopyViewOperation = ZoneRuntimeTableStatus (*)(
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    ZoneRuntimePendingCopyView *) noexcept;
using FacadePendingCopyReadOperation = ZoneRuntimeTableStatus (*)(
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    std::uint32_t,
    std::uint32_t,
    zone_pending_copy::PendingCopyRecord *) noexcept;

static_assert(std::is_same_v<
    decltype(&ZoneRuntimeFacade::TryGetPendingCopyView),
    FacadePendingCopyViewOperation>);
static_assert(std::is_same_v<
    decltype(&ZoneRuntimeFacade::TryReadPendingCopy),
    FacadePendingCopyReadOperation>);

struct ZoneRuntimeFacadeTestAccess final
{
    template <typename Facade>
    static constexpr bool CanAuthenticate = requires
    {
        Facade::authenticateAccess();
    };

    template <typename Facade>
    static constexpr bool CanAuthenticateTableOperation = requires
    {
        Facade::authenticateTableOperationAccess();
    };

    template <typename Facade>
    static constexpr bool CanAuthenticateCompositeTableOperation = requires
    {
        Facade::authenticateCompositeTableOperationAccess(
            std::uint32_t{0}, zone_load::ZoneLoadContextKey{});
    };

    template <typename Facade>
    static constexpr bool CanAuthenticatePendingCopyInspectionOutput =
        requires
    {
        Facade::authenticatePendingCopyInspectionOutput(
            std::uint32_t{0},
            zone_load::ZoneLoadContextKey{},
            nullptr,
            std::size_t{0},
            std::size_t{0});
    };

    template <typename Facade>
    static constexpr bool CanAuthenticateRegistryOutput = requires
    {
        Facade::authenticateRegistryOutputAccess();
    };

    template <typename Facade>
    static constexpr bool CanCompleteTableOperation = requires
    {
        Facade::completeTableOperation(ZoneRuntimeTableStatus::Success);
    };

    template <typename Facade>
    static constexpr bool CanInspectAuthoritySpan = requires
    {
        Facade::authoritySpanIsSeparated(
            nullptr, std::size_t{0}, std::size_t{0});
    };

    template <typename Facade>
    static constexpr bool CanPoison = requires
    {
        Facade::poisonAccess();
    };
};

static_assert(std::is_empty_v<ZoneRuntimeFacade>);
static_assert(std::is_final_v<ZoneRuntimeFacade>);
static_assert(!std::is_default_constructible_v<ZoneRuntimeFacade>);
static_assert(!std::is_copy_constructible_v<ZoneRuntimeFacade>);
static_assert(!std::is_copy_assignable_v<ZoneRuntimeFacade>);
static_assert(!std::is_move_constructible_v<ZoneRuntimeFacade>);
static_assert(!std::is_move_assignable_v<ZoneRuntimeFacade>);
static_assert(std::is_nothrow_destructible_v<ZoneRuntimeFacade>);

static_assert(
    !ZoneRuntimeFacadeTestAccess::CanAuthenticate<ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::CanAuthenticateTableOperation<
        ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::CanAuthenticateCompositeTableOperation<
        ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::
        CanAuthenticatePendingCopyInspectionOutput<ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::CanAuthenticateRegistryOutput<
        ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::CanCompleteTableOperation<
        ZoneRuntimeFacade>);
static_assert(
    !ZoneRuntimeFacadeTestAccess::CanInspectAuthoritySpan<
        ZoneRuntimeFacade>);
static_assert(!ZoneRuntimeFacadeTestAccess::CanPoison<ZoneRuntimeFacade>);

static_assert(noexcept(ZoneRuntimeFacade::TryBeginAccess()));
static_assert(noexcept(ZoneRuntimeFacade::FinishAccess()));
static_assert(noexcept(ZoneRuntimeFacade::TryInitializeRuntimeTable()));
} // namespace db::zone_runtime

int main()
{
    return 0;
}

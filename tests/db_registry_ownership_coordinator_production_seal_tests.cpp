#include <database/db_registry_ownership_coordinator.h>

#include <cstdint>
#include <type_traits>

namespace db::registry_ownership
{
// Recreating the test-helper name without the opt-in macro must not grant any
// mutation or token-extraction capability.
struct RegistryOwnershipCoordinatorTestAccess final
{
    template <typename Coordinator>
    static constexpr bool CanReachBatch = requires(Coordinator *coordinator)
    {
        &coordinator->operationBatch_;
    };

    template <typename Coordinator>
    static constexpr bool CanReachStandaloneToken = requires(
        Coordinator *coordinator)
    {
        &coordinator->standaloneTransaction_;
    };

    template <typename Coordinator>
    static constexpr bool CanReplaceBorrowedController = requires(
        Coordinator *coordinator)
    {
        coordinator->borrowedController_ = nullptr;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateKey = requires(Coordinator *coordinator)
    {
        coordinator->borrowedKey_ = zone_load::ZoneLoadContextKey{};
    };

    template <typename Coordinator>
    static constexpr bool CanMutateSerial = requires(Coordinator *coordinator)
    {
        coordinator->serial_ = 1;
    };

    template <typename Coordinator>
    static constexpr bool CanMutatePhase = requires(Coordinator *coordinator)
    {
        coordinator->phase_ = RegistryOwnershipCoordinatorPhase::Ready;
    };

    template <typename Coordinator>
    static constexpr bool CanMutateHashReceipt = requires(
        Coordinator *coordinator)
    {
        coordinator->hashLockRetained_ = false;
    };
};

static_assert(!RegistryOwnershipCoordinatorTestAccess::CanReachBatch<
    RegistryOwnershipCoordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanReachStandaloneToken<
    RegistryOwnershipCoordinator>);
static_assert(
    !RegistryOwnershipCoordinatorTestAccess::CanReplaceBorrowedController<
        RegistryOwnershipCoordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateKey<
    RegistryOwnershipCoordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateSerial<
    RegistryOwnershipCoordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutatePhase<
    RegistryOwnershipCoordinator>);
static_assert(!RegistryOwnershipCoordinatorTestAccess::CanMutateHashReceipt<
    RegistryOwnershipCoordinator>);

using BeginStandaloneFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *) noexcept;
using BorrowFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *,
    const zone_load::ZoneLoadContextKey &) noexcept;
using AddUser4Function = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *, std::uint32_t) noexcept;
using InternNameFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *,
    const char *,
    std::uint32_t,
    int,
    RegistryOwnershipName *) noexcept;
using RetainedNameFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *, const char *) noexcept;
using SweepFunction = RegistryOwnershipStatus (*)(
    RegistryOwnershipCoordinator *) noexcept;

static_assert(std::is_same_v<
    decltype(&TryBeginStandaloneRegistryOwnershipCoordinator),
    BeginStandaloneFunction>);
static_assert(std::is_same_v<
    decltype(&TryBorrowRegistryOwnershipCoordinator), BorrowFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryAddDatabaseUser4), AddUser4Function>);
static_assert(std::is_same_v<
    decltype(&TryRegistryInternBoundedName), InternNameFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryReAddRetainedDefaultName), RetainedNameFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryTransferDatabaseUsers4To8), SweepFunction>);
static_assert(std::is_same_v<
    decltype(&TryRegistryShutdownDatabaseUser8), SweepFunction>);
static_assert(!std::is_copy_constructible_v<RegistryOwnershipCoordinator>);
static_assert(!std::is_move_constructible_v<RegistryOwnershipCoordinator>);
} // namespace db::registry_ownership

int main()
{
    return 0;
}

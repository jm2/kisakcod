#include <database/db_zone_stream_ownership.h>

#include <type_traits>

namespace db::zone_stream_ownership
{
// Recreating a plausible test-helper name in a production include must not
// confer mutation authority.  Dependent requires-expressions turn each
// private-access denial into a positive compile contract.
struct ZoneStreamOwnershipTestAccess final
{
    template <typename Receipt>
    static constexpr bool CanMutateReceiptKey = requires(Receipt *receipt)
    {
        receipt->key_ = zone_load::ZoneLoadContextKey{};
    };

    template <typename Receipt>
    static constexpr bool CanMutateReceiptSelf = requires(Receipt *receipt)
    {
        receipt->self_ = receipt;
    };

    template <typename Active>
    static constexpr bool CanReachBlocks = requires(Active *active)
    {
        &active->blocks_;
    };

    template <typename Active>
    static constexpr bool CanReplaceReceipt = requires(Active *active)
    {
        active->receipt_ = nullptr;
    };

    template <typename Active>
    static constexpr bool CanMutatePhase = requires(Active *active)
    {
        active->phaseWord_ = 1;
    };
};

static_assert(!ZoneStreamOwnershipTestAccess::CanMutateReceiptKey<
    ZoneStreamGenerationReceipt>);
static_assert(!ZoneStreamOwnershipTestAccess::CanMutateReceiptSelf<
    ZoneStreamGenerationReceipt>);
static_assert(!ZoneStreamOwnershipTestAccess::CanReachBlocks<
    ActiveZoneStreamBinding>);
static_assert(!ZoneStreamOwnershipTestAccess::CanReplaceReceipt<
    ActiveZoneStreamBinding>);
static_assert(!ZoneStreamOwnershipTestAccess::CanMutatePhase<
    ActiveZoneStreamBinding>);
static_assert(!std::is_copy_constructible_v<ZoneStreamGenerationReceipt>);
static_assert(!std::is_move_constructible_v<ZoneStreamGenerationReceipt>);
static_assert(!std::is_copy_constructible_v<ActiveZoneStreamBinding>);
static_assert(!std::is_move_constructible_v<ActiveZoneStreamBinding>);
} // namespace db::zone_stream_ownership

int main()
{
    return 0;
}

#include <database/db_zone_runtime_table.h>

// Production includes intentionally do not opt in to
// KISAK_DB_ZONE_RUNTIME_TABLE_TESTING. Recreating the test helper's public
// name must therefore confer no access to either runtime-table owner's private
// mutable state. Dependent requires-expressions turn each independent access
// denial into a positive compile contract: restoring either friendship makes
// the corresponding static assertions fail.
namespace db::zone_runtime
{
struct ZoneRuntimeTableTestAccess
{
    template <typename Table>
    static constexpr bool CanReachEntries = requires(Table *const table)
    {
        &table->entries_;
    };

    template <typename Table>
    static constexpr bool CanMutateReserved = requires(Table *const table)
    {
        table->reserved_ = 1u;
    };

    template <typename Entry>
    static constexpr bool CanReachMutableLifecycle = requires(
        Entry *const entry)
    {
        &entry->lifecycle_;
    };

    template <typename Entry>
    static constexpr bool CanReachMutableOwnership = requires(
        Entry *const entry)
    {
        &entry->scriptStringOwnership_;
    };

    template <typename Entry>
    static constexpr bool CanMutateKey = requires(Entry *const entry)
    {
        entry->key_ = zone_load::ZoneLoadContextKey{};
    };
};

static_assert(
    !ZoneRuntimeTableTestAccess::CanReachEntries<ZoneRuntimeTable>,
    "production must not grant same-name access to the private entry table");
static_assert(
    !ZoneRuntimeTableTestAccess::CanMutateReserved<ZoneRuntimeTable>,
    "production must not grant same-name access to the reserved header");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachMutableLifecycle<ZoneRuntimeEntry>,
    "production must not expose raw mutable generation lifecycle state");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachMutableOwnership<ZoneRuntimeEntry>,
    "production must not expose raw mutable script-string ownership state");
static_assert(
    !ZoneRuntimeTableTestAccess::CanMutateKey<ZoneRuntimeEntry>,
    "production must not grant same-name access to durable generation keys");
} // namespace db::zone_runtime

int main()
{
    return 0;
}

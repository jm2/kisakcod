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

    template <typename Entry>
    static constexpr bool CanReachReceiptCapsule = requires(
        Entry *const entry)
    {
        &entry->receiptCapsule_;
    };

    template <typename Table>
    static constexpr bool CanReachActiveStreamBinding = requires(
        Table *const table)
    {
        &table->activeZoneStreamBinding_;
    };

    template <typename Table>
    static constexpr bool CanReachPendingCopyLedger = requires(
        Table *const table)
    {
        &table->pendingCopyLedger_;
    };

    template <typename Capsule>
    static constexpr bool CanReachAllocationReceipt = requires(
        Capsule *const capsule)
    {
        &capsule->allocationReceipt_;
    };

    template <typename Capsule>
    static constexpr bool CanReachStreamGenerationReceipt = requires(
        Capsule *const capsule)
    {
        &capsule->streamGenerationReceipt_;
    };

    template <typename Capsule>
    static constexpr bool CanReachPendingAdmissionReceipt = requires(
        Capsule *const capsule)
    {
        &capsule->pendingCopyAdmissionReceipt_;
    };

    template <typename Capsule>
    static constexpr bool CanReachStorageBinding = requires(
        Capsule *const capsule)
    {
        &capsule->storageBinding_;
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
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachReceiptCapsule<ZoneRuntimeEntry>,
    "production must not expose the per-entry receipt composition");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachActiveStreamBinding<ZoneRuntimeTable>,
    "production must not expose the process-wide stream authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachPendingCopyLedger<ZoneRuntimeTable>,
    "production must not expose the process-wide pending-copy authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachAllocationReceipt<
        ZoneRuntimeReceiptCapsule>);
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachStreamGenerationReceipt<
        ZoneRuntimeReceiptCapsule>);
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachPendingAdmissionReceipt<
        ZoneRuntimeReceiptCapsule>);
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachStorageBinding<
        ZoneRuntimeReceiptCapsule>);
} // namespace db::zone_runtime

int main()
{
    return 0;
}

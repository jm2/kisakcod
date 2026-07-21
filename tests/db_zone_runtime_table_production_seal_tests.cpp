#include <database/db_zone_runtime_table.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Production includes intentionally do not opt in to
// KISAK_DB_ZONE_RUNTIME_TABLE_TESTING. Recreating the test helper's public
// name must therefore confer no access to any runtime-table owner's private
// mutable state. Dependent requires-expressions turn each independent access
// denial into a positive compile contract: restoring any friendship makes the
// corresponding static assertions fail.
namespace db::zone_runtime
{
static_assert(sizeof(ZoneRuntimeGenerationCallbacks)
    == (sizeof(void *) == 4 ? 0x14u : 0x28u));
static_assert(sizeof(ZoneRuntimePendingCopyView) == 0x18u);
static_assert(std::is_standard_layout_v<ZoneRuntimePendingCopyView>);
static_assert(std::is_trivially_copyable_v<ZoneRuntimePendingCopyView>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimePendingCopyView::key),
    zone_load::ZoneLoadContextKey>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimePendingCopyView::recordCount),
    std::uint32_t>);
static_assert(std::is_same_v<
    decltype(ZoneRuntimePendingCopyView::reserved),
    std::uint32_t>);
static_assert(sizeof(ZoneRuntimeGenerationBinding)
    == (sizeof(void *) == 4 ? 0x50u : 0x78u));
static_assert(sizeof(ZoneRuntimeReceiptCapsule)
    == (sizeof(void *) == 4 ? 0xD0u : 0x120u));
static_assert(sizeof(ZoneRuntimeEntry)
    == (sizeof(void *) == 4 ? 0x190u : 0x228u));
static_assert(sizeof(ZoneRuntimeTable)
    == (sizeof(void *) == 4 ? 0xF568u : 0x109A0u));

using PendingCopyViewOperation = ZoneRuntimeTableStatus (*)(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    ZoneRuntimePendingCopyView *) noexcept;
using PendingCopyReadOperation = ZoneRuntimeTableStatus (*)(
    ZoneRuntimeTable *,
    std::uint32_t,
    const zone_load::ZoneLoadContextKey &,
    std::uint32_t,
    std::uint32_t,
    zone_pending_copy::PendingCopyRecord *) noexcept;

static_assert(std::is_same_v<
    decltype(&TryGetZoneRuntimePendingCopyView),
    PendingCopyViewOperation>);
static_assert(std::is_same_v<
    decltype(&TryReadZoneRuntimePendingCopy),
    PendingCopyReadOperation>);

struct ZoneRuntimeTableTestAccess
{
    template <typename Access>
    static constexpr bool CanSetPendingCopyReadHook = requires
    {
        &Access::SetPendingCopyReadHook;
    };

    template <typename Table>
    static constexpr bool CanReachEntries = requires(Table *const table)
    {
        &table->entries_;
    };

    template <typename Table>
    static constexpr bool CanMutateState = requires(Table *const table)
    {
        table->state_ = 1u;
    };

    template <typename Table>
    static constexpr bool CanMutateSharedState = requires(Table *const table)
    {
        table->sharedState_ = 1u;
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

    template <typename Entry>
    static constexpr bool CanReachGenerationBinding = requires(
        Entry *const entry)
    {
        &entry->generationBinding_;
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

    template <typename Table>
    static constexpr bool CanReachPendingDrainCallback = requires(
        Table *const table)
    {
        &table->pendingDrainCallback_;
    };

    template <typename Binding>
    static constexpr bool CanReachGenerationCallbacks = requires(
        Binding *const binding)
    {
        &binding->callbacks_;
    };

    template <typename Binding>
    static constexpr bool CanMutateGenerationStage = requires(
        Binding *const binding)
    {
        binding->setupStage_ = ZoneRuntimeSetupStage::CallbacksBound;
    };

    template <typename Binding>
    static constexpr bool CanReachCallbackMarker = requires(
        Binding *const binding)
    {
        &binding->callbackMarker_;
    };

    template <typename Table>
    static constexpr bool CanAuthenticateRegistryLifecycleCallback =
        requires(
            Table *const table,
            const zone_load::ZoneLoadContextKey &key)
    {
        table->authenticateExactRegistryLifecycleCallback(1u, key);
    };

    template <typename Table>
    static constexpr bool CanRestoreRegistryLifecycleCallback = requires(
        Table *const table,
        const zone_load::ZoneLoadContextKey &key)
    {
        table->restoreExactRegistryLifecycleCallback(1u, key);
    };

    template <typename Table, typename Binding>
    static constexpr bool CanAuthenticateLifecycleCallbackMarker = requires(
        Table *const table,
        const zone_load::ZoneLoadContextKey &key,
        typename Binding::CallbackMarker marker)
    {
        table->authenticateExactLifecycleCallbackMarker(1u, key, marker);
    };

    template <typename Table, typename Entry>
    static constexpr bool CanAuthenticatePendingCopyRead = requires(
        Table *const table,
        const zone_load::ZoneLoadContextKey &key,
        const Entry **outEntry)
    {
        table->authenticateExactPendingCopyRead(1u, key, outEntry);
    };

    template <typename Table>
    static constexpr bool CanAuthenticatePendingCopyOutput = requires(
        Table *const table,
        const zone_load::ZoneLoadContextKey &key,
        const void *const output)
    {
        table->authenticateExactPendingCopyOutput(
            1u, key, output, sizeof(std::uint32_t), alignof(std::uint32_t));
    };

    template <typename Table, typename Entry>
    static constexpr bool CanReachPendingCopyAdmissionReceipt = requires(
        const Entry *const entry)
    {
        Table::pendingCopyAdmissionReceipt(entry);
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
    !ZoneRuntimeTableTestAccess::CanSetPendingCopyReadHook<
        ZoneRuntimeTableTestAccess>,
    "production must not expose the staged pending-copy read test seam");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachEntries<ZoneRuntimeTable>,
    "production must not grant same-name access to the private entry table");
static_assert(
    !ZoneRuntimeTableTestAccess::CanMutateState<ZoneRuntimeTable>,
    "production must not grant same-name access to the table state");
static_assert(
    !ZoneRuntimeTableTestAccess::CanMutateSharedState<ZoneRuntimeTable>,
    "production must not grant same-name access to shared authority state");
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
    !ZoneRuntimeTableTestAccess::CanReachGenerationBinding<ZoneRuntimeEntry>,
    "production must not expose the exact-key generation binding");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachActiveStreamBinding<ZoneRuntimeTable>,
    "production must not expose the process-wide stream authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachPendingCopyLedger<ZoneRuntimeTable>,
    "production must not expose the process-wide pending-copy authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachPendingDrainCallback<ZoneRuntimeTable>,
    "production must not expose retained pending-drain callbacks");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachGenerationCallbacks<
        ZoneRuntimeGenerationBinding>,
    "production must not expose retained generation callbacks");
static_assert(
    !ZoneRuntimeTableTestAccess::CanMutateGenerationStage<
        ZoneRuntimeGenerationBinding>,
    "production must not mutate the composite setup stage");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachCallbackMarker<
        ZoneRuntimeGenerationBinding>,
    "production must not inspect or forge callback registry authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanAuthenticateRegistryLifecycleCallback<
        ZoneRuntimeTable>,
    "production must not bypass the private runtime facade callback gate");
static_assert(
    !ZoneRuntimeTableTestAccess::CanRestoreRegistryLifecycleCallback<
        ZoneRuntimeTable>,
    "production must not restore consumed callback registry authority");
static_assert(
    !ZoneRuntimeTableTestAccess::CanAuthenticateLifecycleCallbackMarker<
        ZoneRuntimeTable,
        ZoneRuntimeGenerationBinding>,
    "production must not authenticate a callback marker without consuming it");
static_assert(
    !ZoneRuntimeTableTestAccess::CanAuthenticatePendingCopyRead<
        ZoneRuntimeTable,
        ZoneRuntimeEntry>,
    "production must not bypass exact pending-copy read authentication");
static_assert(
    !ZoneRuntimeTableTestAccess::CanAuthenticatePendingCopyOutput<
        ZoneRuntimeTable>,
    "production must not bypass exact pending-copy output authentication");
static_assert(
    !ZoneRuntimeTableTestAccess::CanReachPendingCopyAdmissionReceipt<
        ZoneRuntimeTable,
        ZoneRuntimeEntry>,
    "production must not recover the retained pending-copy receipt pointer");
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

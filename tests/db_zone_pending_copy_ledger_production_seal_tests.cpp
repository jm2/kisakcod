#include <database/db_zone_pending_copy_ledger.h>

#include <type_traits>
#include <utility>

// A normal production include intentionally leaves the testing macro off.
// Recreating the helper's public name must not recover friendship with either
// stable owner. Each dependent requires-expression is a positive compile seal:
// accidentally exposing any private mutation path flips its assertion.
namespace db::zone_pending_copy
{
struct PendingCopyLedgerTestAccess
{
    template <typename Ledger>
    static constexpr bool CanReachRecords = requires(Ledger *const ledger)
    {
        &ledger->records_;
    };

    template <typename Ledger>
    static constexpr bool CanReachGenerations = requires(
        Ledger *const ledger)
    {
        &ledger->generations_;
    };

    template <typename Ledger>
    static constexpr bool CanMutateCallbackState = requires(
        Ledger *const ledger)
    {
        ledger->callbackActive_ = 1;
    };

    template <typename Receipt>
    static constexpr bool CanReachLedger = requires(Receipt *const receipt)
    {
        &receipt->ledger_;
    };

    template <typename Receipt>
    static constexpr bool CanMutateGeneration = requires(
        Receipt *const receipt)
    {
        receipt->generationIndex_ = 0;
    };

    template <typename Receipt>
    static constexpr bool CanReset = requires(Receipt *const receipt)
    {
        receipt->reset();
    };
};

static_assert(
    !PendingCopyLedgerTestAccess::CanReachRecords<PendingCopyLedger>);
static_assert(
    !PendingCopyLedgerTestAccess::CanReachGenerations<PendingCopyLedger>);
static_assert(
    !PendingCopyLedgerTestAccess::CanMutateCallbackState<PendingCopyLedger>);
static_assert(
    !PendingCopyLedgerTestAccess::CanReachLedger<
        PendingCopyAdmissionReceipt>);
static_assert(
    !PendingCopyLedgerTestAccess::CanMutateGeneration<
        PendingCopyAdmissionReceipt>);
static_assert(
    !PendingCopyLedgerTestAccess::CanReset<PendingCopyAdmissionReceipt>);

static_assert(!std::is_copy_constructible_v<PendingCopyLedger>);
static_assert(!std::is_move_constructible_v<PendingCopyLedger>);
static_assert(!std::is_copy_constructible_v<PendingCopyAdmissionReceipt>);
static_assert(!std::is_move_constructible_v<PendingCopyAdmissionReceipt>);
static_assert(noexcept(TryInitializePendingCopyLedger(nullptr)));
static_assert(noexcept(TryBeginPendingCopyDrain(nullptr)));
static_assert(noexcept(TryFinishPendingCopyDrain(nullptr)));
static_assert(noexcept(FinalizePreparedPendingCopyAdmission(
    std::declval<PendingCopyAdmissionReceipt &>())));
} // namespace db::zone_pending_copy

int main()
{
    return 0;
}

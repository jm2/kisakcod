#include <database/db_zone_pending_copy_ledger.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
namespace pending = db::zone_pending_copy;
namespace lifecycle = db::zone_load;

using pending::PendingCopyAdmissionCompletion;
using pending::PendingCopyAdmissionPhase;
using pending::PendingCopyAdmissionReceipt;
using pending::PendingCopyLedger;
using pending::PendingCopyLedgerTestAccess;
using pending::PendingCopyRecord;
using pending::PendingCopyStatus;

int g_failures = 0;

#define CHECK(expression)                                                       \
    do                                                                          \
    {                                                                           \
        if (!(expression))                                                      \
        {                                                                       \
            std::fprintf(                                                       \
                stderr,                                                         \
                "pending-copy ledger test failed at %s:%d: %s\n",              \
                __FILE__,                                                       \
                __LINE__,                                                       \
                #expression);                                                   \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

struct GenerationFixture final
{
    lifecycle::ZoneLoadContextSlot slot{};
    lifecycle::ZoneLoadContextKey key{};

    explicit GenerationFixture(
        const std::uint32_t slotIndex,
        const std::uint64_t initialGeneration = 0)
    {
        CHECK(
            lifecycle::TryInitializeZoneLoadContextSlot(
                &slot, slotIndex, initialGeneration)
            == lifecycle::ZoneLoadContextStatus::Success);
        CHECK(
            lifecycle::TryClaimZoneLoadContext(&slot, &key)
            == lifecycle::ZoneLoadContextStatus::Success);
    }
};

void CompleteNoop(void *) noexcept {}
void CompleteOther(void *) noexcept {}

void TestTypeAndApiContracts()
{
    static_assert(pending::kPendingCopyRecordCapacity == 2048);
    static_assert(pending::kPendingCopyGenerationCapacity == 8);
    static_assert(sizeof(PendingCopyRecord) == 0x18);
    static_assert(!std::is_copy_constructible_v<PendingCopyAdmissionReceipt>);
    static_assert(!std::is_copy_assignable_v<PendingCopyAdmissionReceipt>);
    static_assert(!std::is_move_constructible_v<PendingCopyAdmissionReceipt>);
    static_assert(!std::is_move_assignable_v<PendingCopyAdmissionReceipt>);
    static_assert(!std::is_trivially_copyable_v<PendingCopyAdmissionReceipt>);
    static_assert(!std::is_copy_constructible_v<PendingCopyLedger>);
    static_assert(!std::is_move_constructible_v<PendingCopyLedger>);
    static_assert(!std::is_trivially_copyable_v<PendingCopyLedger>);
    static_assert(std::is_nothrow_destructible_v<PendingCopyAdmissionReceipt>);
    static_assert(std::is_nothrow_destructible_v<PendingCopyLedger>);
    static_assert(noexcept(pending::TryInitializePendingCopyLedger(nullptr)));
    static_assert(noexcept(pending::TryBeginPendingCopyAdmission(
        nullptr, nullptr, nullptr, std::declval<const lifecycle::ZoneLoadContextKey &>())));
    static_assert(noexcept(pending::TryAppendPendingCopyRecord(
        nullptr, std::declval<const lifecycle::ZoneLoadContextKey &>(), 1)));
    static_assert(noexcept(pending::TryReadPendingCopyRecord(
        nullptr,
        std::declval<const lifecycle::ZoneLoadContextKey &>(),
        0,
        nullptr)));
    static_assert(noexcept(pending::TryPreparePendingCopyAdmission(
        nullptr,
        std::declval<const lifecycle::ZoneLoadContextKey &>(),
        std::declval<const PendingCopyAdmissionCompletion &>())));
    static_assert(noexcept(pending::TryDiscardPendingCopyAdmission(
        nullptr, std::declval<const lifecycle::ZoneLoadContextKey &>())));
}

void TestInitializationAndArgumentRejection()
{
    PendingCopyLedger ledger;
    PendingCopyAdmissionReceipt receipt;
    GenerationFixture generation(1);

    CHECK(!ledger.initialized());
    CHECK(ledger.recordCount() == 0);
    CHECK(ledger.generationCount() == 0);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Pristine);
    CHECK(receipt.canonical());
    CHECK(pending::TryInitializePendingCopyLedger(nullptr)
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);
    CHECK(ledger.initialized());
    CHECK(ledger.canonical());
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);

    lifecycle::ZoneLoadContextKey invalid{};
    CHECK(pending::TryBeginPendingCopyAdmission(
        nullptr, &receipt, &generation.slot, generation.key)
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, nullptr, &generation.slot, generation.key)
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, nullptr, generation.key)
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, &generation.slot, invalid)
        == PendingCopyStatus::InvalidKey);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Pristine);
    CHECK(ledger.generationCount() == 0);

    CHECK(
        lifecycle::TryCommitZoneLoadContext(
            &generation.slot, generation.key)
        == lifecycle::ZoneLoadContextStatus::Success);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, &generation.slot, generation.key)
        == PendingCopyStatus::InvalidPhase);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Pristine);
}

void TestAppendReadPrepareAndDiscard()
{
    PendingCopyLedger ledger;
    PendingCopyAdmissionReceipt receipt;
    GenerationFixture generation(3);
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, &generation.slot, generation.key)
        == PendingCopyStatus::Success);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Collecting);
    CHECK(receipt.key() == generation.key);
    CHECK(receipt.lifecycle() == &generation.slot);
    CHECK(receipt.recordCount() == 0);
    CHECK(ledger.generationCount() == 1);

    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 0)
        == PendingCopyStatus::InvalidRecord);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, pending::kLastAssetEntryIndex + 1)
        == PendingCopyStatus::InvalidRecord);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 17)
        == PendingCopyStatus::Success);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 31)
        == PendingCopyStatus::Success);
    CHECK(receipt.recordCount() == 2);
    CHECK(ledger.recordCount() == 2);

    PendingCopyRecord output{generation.key, 99, 0};
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt, generation.key, 0, &output)
        == PendingCopyStatus::Success);
    CHECK((output == PendingCopyRecord{generation.key, 17, 0}));
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt, generation.key, 1, &output)
        == PendingCopyStatus::Success);
    CHECK((output == PendingCopyRecord{generation.key, 31, 0}));
    const PendingCopyRecord sentinel{generation.key, 1234, 0};
    output = sentinel;
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt, generation.key, 2, &output)
        == PendingCopyStatus::InvalidRecord);
    CHECK(output == sentinel);

    GenerationFixture staleGeneration(3, generation.key.generation);
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt, staleGeneration.key, 0, &output)
        == PendingCopyStatus::StaleKey);
    CHECK(output == sentinel);

    const PendingCopyAdmissionCompletion completion{nullptr, CompleteNoop};
    CHECK(pending::TryPreparePendingCopyAdmission(
        &receipt, generation.key, completion)
        == PendingCopyStatus::Success);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Prepared);
    CHECK(pending::TryPreparePendingCopyAdmission(
        &receipt, generation.key, completion)
        == PendingCopyStatus::Success);
    CHECK(pending::TryPreparePendingCopyAdmission(
        &receipt,
        generation.key,
        PendingCopyAdmissionCompletion{nullptr, CompleteOther})
        == PendingCopyStatus::InvalidState);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 45)
        == PendingCopyStatus::InvalidPhase);

    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipt, generation.key)
        == PendingCopyStatus::Success);
    CHECK(receipt.phase() == PendingCopyAdmissionPhase::Discarded);
    CHECK(receipt.recordCount() == 0);
    CHECK(ledger.recordCount() == 0);
    CHECK(ledger.generationCount() == 0);
    CHECK(ledger.canonical());

    // Exact terminal authority wins before any now-unrelated ledger state.
    PendingCopyLedgerTestAccess::SetLedgerReserved(&ledger, 1);
    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipt, generation.key)
        == PendingCopyStatus::AlreadyComplete);
    PendingCopyLedgerTestAccess::SetLedgerReserved(&ledger, 0);
}

void TestCapacityAndFailureAtomicity()
{
    PendingCopyLedger ledger;
    PendingCopyAdmissionReceipt receipt;
    GenerationFixture generation(4);
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, &generation.slot, generation.key)
        == PendingCopyStatus::Success);

    for (std::uint32_t index = 0;
        index < pending::kPendingCopyRecordCapacity;
        ++index)
    {
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt,
            generation.key,
            pending::kFirstAssetEntryIndex
                + (index % pending::kLastAssetEntryIndex))
            == PendingCopyStatus::Success);
    }
    CHECK(receipt.recordCount() == pending::kPendingCopyRecordCapacity);
    CHECK(ledger.recordCount() == pending::kPendingCopyRecordCapacity);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 7)
        == PendingCopyStatus::CapacityExceeded);
    CHECK(receipt.recordCount() == pending::kPendingCopyRecordCapacity);
    CHECK(ledger.recordCount() == pending::kPendingCopyRecordCapacity);

    const PendingCopyRecord sentinel{generation.key, 222, 0};
    PendingCopyRecord output = sentinel;
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt,
        generation.key,
        pending::kPendingCopyRecordCapacity,
        &output)
        == PendingCopyStatus::InvalidRecord);
    CHECK(output == sentinel);

    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipt, generation.key)
        == PendingCopyStatus::Success);
    CHECK(ledger.canonical());
}

void TestCorruptionAndSerialExhaustion()
{
    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        GenerationFixture generation(5);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        PendingCopyLedgerTestAccess::SetNextGenerationSerial(
            &ledger, UINT64_MAX);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::GenerationExhausted);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Pristine);
        CHECK(ledger.generationCount() == 0);
        CHECK(ledger.recordCount() == 0);
    }

    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        GenerationFixture generation(6);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 19)
            == PendingCopyStatus::Success);

        PendingCopyLedgerTestAccess::SetDescriptorFirstRecord(&ledger, 0, 1);
        CHECK(pending::TryPreparePendingCopyAdmission(
            &receipt,
            generation.key,
            PendingCopyAdmissionCompletion{nullptr, CompleteNoop})
            == PendingCopyStatus::UnsafeFailure);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Collecting);
        CHECK(receipt.recordCount() == 0);
        PendingCopyLedgerTestAccess::SetDescriptorFirstRecord(&ledger, 0, 0);

        PendingCopyLedgerTestAccess::SetReceiptReserved(&receipt, 1);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 20)
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetReceiptReserved(&receipt, 0);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 20)
            == PendingCopyStatus::Success);

        PendingCopyLedgerTestAccess::SetDescriptorReserved(&ledger, 0, 1);
        CHECK(pending::TryPreparePendingCopyAdmission(
            &receipt,
            generation.key,
            PendingCopyAdmissionCompletion{nullptr, CompleteNoop})
            == PendingCopyStatus::UnsafeFailure);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Collecting);
        PendingCopyLedgerTestAccess::SetDescriptorReserved(&ledger, 0, 0);
        CHECK(pending::TryDiscardPendingCopyAdmission(
            &receipt, generation.key)
            == PendingCopyStatus::Success);
    }
}
} // namespace

int main()
{
    TestTypeAndApiContracts();
    TestInitializationAndArgumentRejection();
    TestAppendReadPrepareAndDiscard();
    TestCapacityAndFailureAtomicity();
    TestCorruptionAndSerialExhaustion();
    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "pending-copy ledger tests failed: %d failure(s)\n",
            g_failures);
        return 1;
    }
    std::puts("pending-copy ledger core tests passed");
    return 0;
}

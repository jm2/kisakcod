#include <database/db_zone_pending_copy_ledger.h>

#include <array>
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
using pending::PendingCopyDrainCallback;
using pending::PendingCopyDrainCallbackStatus;
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

void PrepareCommitAndFinalize(
    PendingCopyAdmissionReceipt *const receipt,
    GenerationFixture *const generation,
    const PendingCopyAdmissionCompletion &completion =
        {nullptr, CompleteNoop})
{
    CHECK(pending::TryPreparePendingCopyAdmission(
        receipt, generation->key, completion)
        == PendingCopyStatus::Success);
    CHECK(lifecycle::TryCommitZoneLoadContext(
        &generation->slot, generation->key)
        == lifecycle::ZoneLoadContextStatus::Success);
    pending::FinalizePreparedPendingCopyAdmission(*receipt);
}

struct AdmissionReentryState final
{
    PendingCopyLedger *ledger = nullptr;
    PendingCopyAdmissionReceipt *receipt = nullptr;
    PendingCopyAdmissionReceipt *otherReceipt = nullptr;
    GenerationFixture *otherGeneration = nullptr;
    PendingCopyAdmissionReceipt *terminalReceipt = nullptr;
    lifecycle::ZoneLoadContextKey terminalKey{};
    std::uint32_t calls = 0;
    PendingCopyStatus initializeStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus beginStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus discardStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus resetStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus terminalResetStatus = PendingCopyStatus::UnsafeFailure;
};

void CompleteWithReentry(void *const opaque) noexcept
{
    auto &state = *static_cast<AdmissionReentryState *>(opaque);
    ++state.calls;
    CHECK(state.receipt->phase()
        == PendingCopyAdmissionPhase::Admitting);
    pending::FinalizePreparedPendingCopyAdmission(*state.receipt);
    state.initializeStatus =
        pending::TryInitializePendingCopyLedger(state.ledger);
    state.beginStatus = pending::TryBeginPendingCopyAdmission(
        state.ledger,
        state.otherReceipt,
        &state.otherGeneration->slot,
        state.otherGeneration->key);
    state.discardStatus = pending::TryDiscardPendingCopyAdmission(
        state.receipt, state.receipt->key());
    state.resetStatus = pending::TryResetPendingCopyAdmissionReceipt(
        state.receipt, state.receipt->key());
    state.terminalResetStatus =
        pending::TryResetPendingCopyAdmissionReceipt(
            state.terminalReceipt, state.terminalKey);
}

struct DrainState final
{
    PendingCopyLedger *ledger = nullptr;
    PendingCopyAdmissionReceipt *receipt = nullptr;
    std::array<PendingCopyRecord,
        pending::kPendingCopyRecordCapacity> records{};
    std::uint32_t recordCount = 0;
    std::uint32_t calls = 0;
    std::uint32_t retryOrdinal = UINT32_MAX;
    bool retried = false;
    bool mutateRetryCopy = false;
    PendingCopyStatus reenterDrainStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus reenterFinishStatus = PendingCopyStatus::UnsafeFailure;
    PendingCopyStatus resetStatus = PendingCopyStatus::UnsafeFailure;
};

PendingCopyDrainCallbackStatus ConsumeOrdered(
    void *const opaque,
    PendingCopyRecord record) noexcept
{
    auto &state = *static_cast<DrainState *>(opaque);
    ++state.calls;
    state.reenterDrainStatus = pending::TryDrainNextPendingCopy(
        state.ledger,
        PendingCopyDrainCallback{&state, ConsumeOrdered});
    state.reenterFinishStatus =
        pending::TryFinishPendingCopyDrain(state.ledger);
    state.resetStatus = pending::TryResetPendingCopyAdmissionReceipt(
        state.receipt, state.receipt->key());
    if (state.recordCount == state.retryOrdinal && !state.retried)
    {
        state.retried = true;
        if (state.mutateRetryCopy)
            record.assetEntryIndex = 777;
        return PendingCopyDrainCallbackStatus::Retry;
    }
    if (state.recordCount < state.records.size())
        state.records[state.recordCount] = record;
    ++state.recordCount;
    return PendingCopyDrainCallbackStatus::Success;
}

PendingCopyDrainCallbackStatus ConsumeUnknown(
    void *, PendingCopyRecord) noexcept
{
    return static_cast<PendingCopyDrainCallbackStatus>(UINT8_C(0xFF));
}

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
    static_assert(noexcept(pending::FinalizePreparedPendingCopyAdmission(
        std::declval<PendingCopyAdmissionReceipt &>())));
    static_assert(noexcept(pending::TryBeginPendingCopyDrain(nullptr)));
    static_assert(noexcept(pending::TryDrainNextPendingCopy(
        nullptr, std::declval<const PendingCopyDrainCallback &>())));
    static_assert(noexcept(pending::TryFinishPendingCopyDrain(nullptr)));
    static_assert(noexcept(pending::TryResetPendingCopyAdmissionReceipt(
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
    auto *const overlappingReceipt =
        reinterpret_cast<PendingCopyAdmissionReceipt *>(&ledger);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, overlappingReceipt, &generation.slot, generation.key)
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
    CHECK(pending::TryBeginPendingCopyDrain(&ledger)
        == PendingCopyStatus::Busy);

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

    CHECK(pending::TryReadPendingCopyRecord(
        &receipt,
        generation.key,
        0,
        reinterpret_cast<PendingCopyRecord *>(&ledger))
        == PendingCopyStatus::InvalidArgument);
    CHECK(ledger.canonical());
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt,
        generation.key,
        0,
        reinterpret_cast<PendingCopyRecord *>(&receipt))
        == PendingCopyStatus::InvalidArgument);
    CHECK(receipt.canonical());
    CHECK(pending::TryReadPendingCopyRecord(
        &receipt,
        generation.key,
        0,
        reinterpret_cast<PendingCopyRecord *>(&generation.slot))
        == PendingCopyStatus::InvalidArgument);
    CHECK(generation.slot.canonical());

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
    CHECK(pending::TryBeginPendingCopyDrain(&ledger)
        == PendingCopyStatus::Busy);
    CHECK(pending::TryPreparePendingCopyAdmission(
        &receipt, generation.key, completion)
        == PendingCopyStatus::Success);
    CHECK(pending::TryPreparePendingCopyAdmission(
        &receipt,
        generation.key,
        PendingCopyAdmissionCompletion{&ledger, CompleteNoop})
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
        GenerationFixture generation(5);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);

        PendingCopyLedgerTestAccess::SetRecordCount(
            &ledger, pending::kPendingCopyRecordCapacity + 1);
        CHECK(!ledger.canonical());
        PendingCopyLedgerTestAccess::SetRecordCount(&ledger, 0);
        CHECK(ledger.canonical());

        PendingCopyLedgerTestAccess::SetGenerationCount(
            &ledger, pending::kPendingCopyGenerationCapacity + 1);
        CHECK(!ledger.canonical());
        PendingCopyLedgerTestAccess::SetGenerationCount(&ledger, 0);
        CHECK(ledger.canonical());

        PendingCopyLedgerTestAccess::SetDrainCursor(&ledger, 1);
        CHECK(!ledger.canonical());
        PendingCopyLedgerTestAccess::SetDrainCursor(&ledger, 0);
        CHECK(ledger.canonical());

        PendingCopyLedgerTestAccess::SetRecord(
            &ledger,
            pending::kPendingCopyRecordCapacity - 1,
            PendingCopyRecord{generation.key, 1, 0});
        CHECK(!ledger.canonical());
        PendingCopyLedgerTestAccess::SetRecord(
            &ledger,
            pending::kPendingCopyRecordCapacity - 1,
            PendingCopyRecord{});
        CHECK(ledger.canonical());

        PendingCopyLedgerTestAccess::SetLedgerPhaseWitness(&ledger, 0);
        CHECK(!ledger.canonical());
    }

    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        PendingCopyAdmissionReceipt exhaustedReceipt;
        GenerationFixture generation(5);
        GenerationFixture exhaustedGeneration(6);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        PendingCopyLedgerTestAccess::SetNextGenerationSerial(
            &ledger, UINT64_MAX - 1);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 18)
            == PendingCopyStatus::Success);
        PrepareCommitAndFinalize(&receipt, &generation);
        CHECK(ledger.canonical());
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger,
            &exhaustedReceipt,
            &exhaustedGeneration.slot,
            exhaustedGeneration.key)
            == PendingCopyStatus::GenerationExhausted);
        CHECK(exhaustedReceipt.phase()
            == PendingCopyAdmissionPhase::Pristine);
        CHECK(ledger.generationCount() == 1);
        CHECK(ledger.recordCount() == 1);
        CHECK(pending::TryDiscardPendingCopyAdmission(
            &receipt, generation.key)
            == PendingCopyStatus::Success);
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

        PendingCopyLedgerTestAccess::SetNextGenerationSerial(&ledger, 1);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 20)
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetNextGenerationSerial(&ledger, 2);

        PendingCopyLedgerTestAccess::SetDescriptorFirstRecord(&ledger, 0, 1);
        CHECK(pending::TryPreparePendingCopyAdmission(
            &receipt,
            generation.key,
            PendingCopyAdmissionCompletion{nullptr, CompleteNoop})
            == PendingCopyStatus::UnsafeFailure);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Collecting);
        CHECK(receipt.recordCount() == 0);
        PendingCopyLedgerTestAccess::SetDescriptorFirstRecord(&ledger, 0, 0);

        PendingCopyLedgerTestAccess::SetRecord(
            &ledger, 0, PendingCopyRecord{generation.key, 19, 1});
        CHECK(pending::TryPreparePendingCopyAdmission(
            &receipt,
            generation.key,
            PendingCopyAdmissionCompletion{nullptr, CompleteNoop})
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetRecord(
            &ledger, 0, PendingCopyRecord{generation.key, 19, 0});

        PendingCopyLedgerTestAccess::SetDescriptorRecordCount(&ledger, 0, 2);
        CHECK(pending::TryPreparePendingCopyAdmission(
            &receipt,
            generation.key,
            PendingCopyAdmissionCompletion{nullptr, CompleteNoop})
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetDescriptorRecordCount(&ledger, 0, 1);

        PendingCopyLedgerTestAccess::SetReceiptGenerationIndex(&receipt, 1);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 20)
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetReceiptGenerationIndex(&receipt, 0);

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

    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        GenerationFixture generation(6);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        PendingCopyLedgerTestAccess::SetReceiptPhaseWitness(&receipt, 0);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 19)
            == PendingCopyStatus::UnsafeFailure);
    }

    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt firstReceipt;
        PendingCopyAdmissionReceipt secondReceipt;
        GenerationFixture first(5);
        GenerationFixture second(6);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &firstReceipt, &first.slot, first.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &firstReceipt, first.key, 21)
            == PendingCopyStatus::Success);
        PrepareCommitAndFinalize(&firstReceipt, &first);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &secondReceipt, &second.slot, second.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &secondReceipt, second.key, 22)
            == PendingCopyStatus::Success);
        PrepareCommitAndFinalize(&secondReceipt, &second);
        CHECK(ledger.canonical());

        PendingCopyLedgerTestAccess::SetDescriptorSerial(&ledger, 1, 1);
        PendingCopyLedgerTestAccess::SetReceiptGenerationSerial(
            &secondReceipt, 1);
        CHECK(!ledger.canonical());
        CHECK(pending::TryBeginPendingCopyDrain(&ledger)
            == PendingCopyStatus::UnsafeFailure);
        PendingCopyLedgerTestAccess::SetDescriptorSerial(&ledger, 1, 2);
        PendingCopyLedgerTestAccess::SetReceiptGenerationSerial(
            &secondReceipt, 2);
        CHECK(ledger.canonical());
    }
}

void TestFinalizationExactlyOnceAndZeroRecordTerminal()
{
    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        PendingCopyAdmissionReceipt otherReceipt;
        PendingCopyAdmissionReceipt terminalReceipt;
        GenerationFixture terminalGeneration(6);
        GenerationFixture generation(7);
        GenerationFixture otherGeneration(8);
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger,
            &terminalReceipt,
            &terminalGeneration.slot,
            terminalGeneration.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryDiscardPendingCopyAdmission(
            &terminalReceipt, terminalGeneration.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipt, generation.key, 81)
            == PendingCopyStatus::Success);

        AdmissionReentryState state;
        state.ledger = &ledger;
        state.receipt = &receipt;
        state.otherReceipt = &otherReceipt;
        state.otherGeneration = &otherGeneration;
        state.terminalReceipt = &terminalReceipt;
        state.terminalKey = terminalGeneration.key;
        const PendingCopyAdmissionCompletion completion{
            &state, CompleteWithReentry};
        PrepareCommitAndFinalize(&receipt, &generation, completion);
        CHECK(state.calls == 1);
        CHECK(state.initializeStatus == PendingCopyStatus::Busy);
        CHECK(state.beginStatus == PendingCopyStatus::Busy);
        CHECK(state.discardStatus == PendingCopyStatus::Busy);
        CHECK(state.resetStatus == PendingCopyStatus::Busy);
        CHECK(state.terminalResetStatus == PendingCopyStatus::Success);
        CHECK(terminalReceipt.phase()
            == PendingCopyAdmissionPhase::Pristine);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Admitted);
        CHECK(receipt.recordCount() == 1);
        CHECK(ledger.canonical());

        pending::FinalizePreparedPendingCopyAdmission(receipt);
        CHECK(state.calls == 1);
        CHECK(pending::TryDiscardPendingCopyAdmission(
            &receipt, generation.key)
            == PendingCopyStatus::Success);
    }

    {
        PendingCopyLedger ledger;
        PendingCopyAdmissionReceipt receipt;
        GenerationFixture generation(9);
        std::uint32_t completions = 0;
        const auto complete = [](void *const opaque) noexcept {
            ++*static_cast<std::uint32_t *>(opaque);
        };
        CHECK(pending::TryInitializePendingCopyLedger(&ledger)
            == PendingCopyStatus::Success);
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipt, &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        PrepareCommitAndFinalize(
            &receipt,
            &generation,
            PendingCopyAdmissionCompletion{&completions, complete});
        CHECK(completions == 1);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Drained);
        CHECK(ledger.recordCount() == 0);
        CHECK(ledger.generationCount() == 0);
        CHECK(ledger.canonical());
        pending::FinalizePreparedPendingCopyAdmission(receipt);
        CHECK(completions == 1);
        CHECK(pending::TryBeginPendingCopyDrain(&ledger)
            == PendingCopyStatus::AlreadyComplete);
        CHECK(pending::TryResetPendingCopyAdmissionReceipt(
            &receipt, generation.key)
            == PendingCopyStatus::Success);
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Pristine);
        CHECK(receipt.canonical());
    }
}

void TestGenerationCapacityOrderedDrainAndRetry()
{
    PendingCopyLedger ledger;
    std::array<PendingCopyAdmissionReceipt,
        pending::kPendingCopyGenerationCapacity> receipts;
    std::array<GenerationFixture *,
        pending::kPendingCopyGenerationCapacity> generations{};
    GenerationFixture generation0(10);
    GenerationFixture generation1(11);
    GenerationFixture generation2(12);
    GenerationFixture generation3(13);
    GenerationFixture generation4(14);
    GenerationFixture generation5(15);
    GenerationFixture generation6(16);
    GenerationFixture generation7(17);
    GenerationFixture overflowGeneration(18);
    generations = {
        &generation0,
        &generation1,
        &generation2,
        &generation3,
        &generation4,
        &generation5,
        &generation6,
        &generation7,
    };

    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);
    for (std::uint32_t generationIndex = 0;
        generationIndex < generations.size();
        ++generationIndex)
    {
        GenerationFixture &generation = *generations[generationIndex];
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger,
            &receipts[generationIndex],
            &generation.slot,
            generation.key)
            == PendingCopyStatus::Success);
        constexpr std::uint32_t kRecordsPerGeneration =
            pending::kPendingCopyRecordCapacity
            / pending::kPendingCopyGenerationCapacity;
        for (std::uint32_t ordinal = 0;
            ordinal < kRecordsPerGeneration;
            ++ordinal)
        {
            CHECK(pending::TryAppendPendingCopyRecord(
                &receipts[generationIndex],
                generation.key,
                100 + generationIndex * kRecordsPerGeneration + ordinal)
                == PendingCopyStatus::Success);
        }
        PrepareCommitAndFinalize(
            &receipts[generationIndex], &generation);
    }
    CHECK(ledger.generationCount()
        == pending::kPendingCopyGenerationCapacity);
    CHECK(ledger.recordCount() == pending::kPendingCopyRecordCapacity);
    PendingCopyAdmissionReceipt overflowReceipt;
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger,
        &overflowReceipt,
        &overflowGeneration.slot,
        overflowGeneration.key)
        == PendingCopyStatus::GenerationCapacityExceeded);

    CHECK(pending::TryBeginPendingCopyDrain(nullptr)
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryBeginPendingCopyDrain(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryBeginPendingCopyDrain(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryDrainNextPendingCopy(
        &ledger, PendingCopyDrainCallback{})
        == PendingCopyStatus::InvalidArgument);
    CHECK(pending::TryFinishPendingCopyDrain(&ledger)
        == PendingCopyStatus::InvalidState);

    DrainState state;
    state.ledger = &ledger;
    state.receipt = &receipts[0];
    state.retryOrdinal = 5;
    state.mutateRetryCopy = true;
    const PendingCopyDrainCallback callback{&state, ConsumeOrdered};
    std::uint32_t retryCount = 0;
    while (state.recordCount < pending::kPendingCopyRecordCapacity)
    {
        const PendingCopyStatus status =
            pending::TryDrainNextPendingCopy(&ledger, callback);
        if (status == PendingCopyStatus::Retry)
            ++retryCount;
        else
            CHECK(status == PendingCopyStatus::Success);
    }
    CHECK(state.retried);
    CHECK(retryCount == 1);
    CHECK(state.calls == pending::kPendingCopyRecordCapacity + 1);
    CHECK(state.reenterDrainStatus == PendingCopyStatus::Busy);
    CHECK(state.reenterFinishStatus == PendingCopyStatus::Busy);
    CHECK(state.resetStatus == PendingCopyStatus::Busy);
    for (std::uint32_t index = 0; index < state.recordCount; ++index)
    {
        CHECK(state.records[index].assetEntryIndex == 100 + index);
        CHECK(state.records[index].key
            == generations[index
                / (pending::kPendingCopyRecordCapacity
                    / pending::kPendingCopyGenerationCapacity)]->key);
    }
    CHECK(pending::TryDrainNextPendingCopy(&ledger, callback)
        == PendingCopyStatus::AlreadyComplete);
    CHECK(pending::TryFinishPendingCopyDrain(&ledger)
        == PendingCopyStatus::Success);
    CHECK(ledger.canonical());
    CHECK(ledger.recordCount() == 0);
    CHECK(ledger.generationCount() == 0);
    for (const PendingCopyAdmissionReceipt &receipt : receipts)
    {
        CHECK(receipt.phase() == PendingCopyAdmissionPhase::Drained);
        CHECK(receipt.recordCount() == 0);
    }
    CHECK(pending::TryFinishPendingCopyDrain(&ledger)
        == PendingCopyStatus::AlreadyComplete);
}

void TestStableCompactionAndStaleTerminalAuthority()
{
    PendingCopyLedger ledger;
    std::array<PendingCopyAdmissionReceipt, 3> receipts;
    GenerationFixture first(19);
    GenerationFixture middle(20);
    GenerationFixture last(21);
    std::array<GenerationFixture *, 3> generations{
        &first, &middle, &last};
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);

    for (std::uint32_t index = 0; index < generations.size(); ++index)
    {
        GenerationFixture &generation = *generations[index];
        CHECK(pending::TryBeginPendingCopyAdmission(
            &ledger, &receipts[index], &generation.slot, generation.key)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipts[index], generation.key, 200 + index * 2)
            == PendingCopyStatus::Success);
        CHECK(pending::TryAppendPendingCopyRecord(
            &receipts[index], generation.key, 201 + index * 2)
            == PendingCopyStatus::Success);
        PrepareCommitAndFinalize(&receipts[index], &generation);
    }

    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipts[1], middle.key)
        == PendingCopyStatus::Success);
    CHECK(receipts[1].phase() == PendingCopyAdmissionPhase::Discarded);
    CHECK(ledger.generationCount() == 2);
    CHECK(ledger.recordCount() == 4);
    PendingCopyRecord output{};
    CHECK(pending::TryReadPendingCopyRecord(
        &receipts[2], last.key, 0, &output)
        == PendingCopyStatus::Success);
    CHECK(output.assetEntryIndex == 204);

    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipts[0], first.key)
        == PendingCopyStatus::Success);
    CHECK(ledger.generationCount() == 1);
    CHECK(ledger.recordCount() == 2);
    CHECK(pending::TryReadPendingCopyRecord(
        &receipts[2], last.key, 1, &output)
        == PendingCopyStatus::Success);
    CHECK(output.assetEntryIndex == 205);
    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipts[2], last.key)
        == PendingCopyStatus::Success);
    CHECK(ledger.generationCount() == 0);
    CHECK(ledger.recordCount() == 0);
    CHECK(ledger.canonical());

    // A terminal receipt remains authoritative while a newer generation with
    // the same logical slot is active in the ledger.
    GenerationFixture newer(middle.key.slot, middle.key.generation);
    PendingCopyAdmissionReceipt newerReceipt;
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &newerReceipt, &newer.slot, newer.key)
        == PendingCopyStatus::Success);
    CHECK(pending::TryDiscardPendingCopyAdmission(
        &receipts[1], middle.key)
        == PendingCopyStatus::AlreadyComplete);
    CHECK(ledger.generationCount() == 1);
    CHECK(newerReceipt.phase() == PendingCopyAdmissionPhase::Collecting);
    CHECK(pending::TryResetPendingCopyAdmissionReceipt(
        &receipts[1], newer.key)
        == PendingCopyStatus::StaleKey);
    CHECK(pending::TryResetPendingCopyAdmissionReceipt(
        &receipts[1], middle.key)
        == PendingCopyStatus::Success);
    CHECK(pending::TryDiscardPendingCopyAdmission(
        &newerReceipt, newer.key)
        == PendingCopyStatus::Success);
}

void TestUnknownDrainResultPoisonsLedger()
{
    PendingCopyLedger ledger;
    PendingCopyAdmissionReceipt receipt;
    GenerationFixture generation(22);
    CHECK(pending::TryInitializePendingCopyLedger(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryBeginPendingCopyAdmission(
        &ledger, &receipt, &generation.slot, generation.key)
        == PendingCopyStatus::Success);
    CHECK(pending::TryAppendPendingCopyRecord(
        &receipt, generation.key, 301)
        == PendingCopyStatus::Success);
    PrepareCommitAndFinalize(&receipt, &generation);
    CHECK(pending::TryBeginPendingCopyDrain(&ledger)
        == PendingCopyStatus::Success);
    CHECK(pending::TryDrainNextPendingCopy(
        &ledger, PendingCopyDrainCallback{nullptr, ConsumeUnknown})
        == PendingCopyStatus::UnsafeFailure);
    CHECK(!ledger.canonical());
    CHECK(pending::TryFinishPendingCopyDrain(&ledger)
        == PendingCopyStatus::UnsafeFailure);
}
} // namespace

int main()
{
    TestTypeAndApiContracts();
    TestInitializationAndArgumentRejection();
    TestAppendReadPrepareAndDiscard();
    TestCapacityAndFailureAtomicity();
    TestCorruptionAndSerialExhaustion();
    TestFinalizationExactlyOnceAndZeroRecordTerminal();
    TestGenerationCapacityOrderedDrainAndRetry();
    TestStableCompactionAndStaleTerminalAuthority();
    TestUnknownDrainResultPoisonsLedger();
    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "pending-copy ledger tests failed: %d failure(s)\n",
            g_failures);
        return 1;
    }
    std::puts("pending-copy ledger tests passed");
    return 0;
}

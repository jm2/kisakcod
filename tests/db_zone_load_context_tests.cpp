#define KISAK_DB_ZONE_LOAD_CONTEXT_TESTING 1
#include <database/db_zone_load_context.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
namespace zone_load = db::zone_load;

using zone_load::TryBeginZoneLoadContextAbandonment;
using zone_load::TryClaimZoneLoadContext;
using zone_load::TryCommitZoneLoadContext;
using zone_load::TryFinishZoneLoadContextAbandonment;
using zone_load::TryInitializeZoneLoadContextSlot;
using zone_load::TryUnloadZoneLoadContext;
using zone_load::ZoneLoadCleanupCallbackStatus;
using zone_load::ZoneLoadCleanupCallbacks;
using zone_load::ZoneLoadCleanupOperation;
using zone_load::ZoneLoadContextKey;
using zone_load::ZoneLoadContextPhase;
using zone_load::ZoneLoadContextSlot;
using zone_load::ZoneLoadContextSlotTestAccess;
using zone_load::ZoneLoadContextStatus;
using zone_load::ZoneLoadTerminalKind;

int failures = 0;

void Check(
    const bool condition,
    const char *const expression,
    const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr std::array<ZoneLoadCleanupOperation, 9>
    kAbandonmentCleanupOrder{
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate,
        ZoneLoadCleanupOperation::AbortNativeAdapterTransactions,
        ZoneLoadCleanupOperation::
            MakePartialAssetsAndStagedReferencesUnreachable,
        ZoneLoadCleanupOperation::
            InvalidateAliasDirectStreamAndDelayState,
        ZoneLoadCleanupOperation::ReleaseGeometry,
        ZoneLoadCleanupOperation::
            TearDownNativeArenaWorkspaceAndSidecars,
        ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation,
        ZoneLoadCleanupOperation::FreePhysicalMemory,
        ZoneLoadCleanupOperation::
            ClearRegistryLoadingQueueGateAndSignal,
    };

constexpr std::array<ZoneLoadCleanupOperation, 6>
    kLiveUnloadCleanupOrder{
        ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences,
        ZoneLoadCleanupOperation::
            InvalidateAliasDirectStreamAndDelayState,
        ZoneLoadCleanupOperation::ReleaseGeometry,
        ZoneLoadCleanupOperation::
            TearDownNativeArenaWorkspaceAndSidecars,
        ZoneLoadCleanupOperation::FreePhysicalMemory,
        ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles,
    };

constexpr std::uint8_t kInitializedFlag = UINT8_C(1) << 0;
constexpr std::uint8_t kCleanupActiveFlag = UINT8_C(1) << 1;
constexpr std::uint8_t kCleanupPoisonedFlag = UINT8_C(1) << 2;

struct CleanupRecorder
{
    std::array<ZoneLoadCleanupOperation, 32> operations{};
    std::size_t operationCount = 0;
    ZoneLoadCleanupOperation injectedOperation =
        ZoneLoadCleanupOperation::ReleaseSlot;
    ZoneLoadCleanupCallbackStatus injectedStatus =
        ZoneLoadCleanupCallbackStatus::Success;
    std::uint32_t injectionsRemaining = 0;
    ZoneLoadContextSlot *reentrantSlot = nullptr;
    ZoneLoadContextKey reentrantKey{};
    ZoneLoadContextStatus reentrantStatus =
        ZoneLoadContextStatus::InvalidState;
    bool reenterOnce = false;
    bool observedCleanupActive = false;
};

ZoneLoadCleanupCallbackStatus RecordCleanup(
    void *const context,
    const ZoneLoadCleanupOperation operation) noexcept
{
    CleanupRecorder *const recorder =
        static_cast<CleanupRecorder *>(context);
    if (!recorder
        || recorder->operationCount >= recorder->operations.size())
    {
        return ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    }
    recorder->operations[recorder->operationCount++] = operation;

    if (recorder->reenterOnce)
    {
        recorder->reenterOnce = false;
        recorder->observedCleanupActive =
            recorder->reentrantSlot
            && recorder->reentrantSlot->cleanupActive();
        recorder->reentrantStatus =
            TryFinishZoneLoadContextAbandonment(
                recorder->reentrantSlot,
                recorder->reentrantKey,
                {recorder, RecordCleanup});
    }

    if (operation == recorder->injectedOperation
        && recorder->injectionsRemaining != 0)
    {
        --recorder->injectionsRemaining;
        return recorder->injectedStatus;
    }
    return ZoneLoadCleanupCallbackStatus::Success;
}

ZoneLoadCleanupCallbackStatus ReturnUnknownCleanupStatus(
    void *,
    ZoneLoadCleanupOperation) noexcept
{
    return static_cast<ZoneLoadCleanupCallbackStatus>(UINT8_C(0xFF));
}

ZoneLoadCleanupCallbacks MakeCallbacks(
    CleanupRecorder *const recorder) noexcept
{
    return {recorder, RecordCleanup};
}

bool OperationsEqual(
    const CleanupRecorder &recorder,
    const ZoneLoadCleanupOperation *const expected,
    const std::size_t expectedCount)
{
    if (recorder.operationCount != expectedCount)
        return false;
    for (std::size_t index = 0; index < expectedCount; ++index)
    {
        if (recorder.operations[index] != expected[index])
            return false;
    }
    return true;
}

ZoneLoadContextKey InitializeAndClaim(
    ZoneLoadContextSlot *const slot,
    const std::uint32_t slotIndex,
    const std::uint64_t initialGeneration = 0)
{
    ZoneLoadContextKey key{};
    CHECK(
        TryInitializeZoneLoadContextSlot(
            slot, slotIndex, initialGeneration)
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryClaimZoneLoadContext(slot, &key)
        == ZoneLoadContextStatus::Success);
    return key;
}

void TestLayoutNoexceptAndInitialization()
{
    static_assert(sizeof(ZoneLoadContextKey) == 0x10);
    static_assert(alignof(ZoneLoadContextKey) == 8);
    static_assert(std::is_standard_layout_v<ZoneLoadContextKey>);
    static_assert(std::is_trivially_copyable_v<ZoneLoadContextKey>);
    static_assert(sizeof(ZoneLoadContextSlot) == 0x10);
    static_assert(alignof(ZoneLoadContextSlot) == 8);
    static_assert(std::is_standard_layout_v<ZoneLoadContextSlot>);
    static_assert(std::is_trivially_destructible_v<ZoneLoadContextSlot>);
    static_assert(!std::is_copy_constructible_v<ZoneLoadContextSlot>);
    static_assert(!std::is_move_constructible_v<ZoneLoadContextSlot>);
    static_assert(sizeof(ZoneLoadContextPhase) == 1);
    static_assert(sizeof(ZoneLoadTerminalKind) == 1);
    static_assert(sizeof(ZoneLoadCleanupOperation) == 1);
    static_assert(sizeof(ZoneLoadCleanupCallbackStatus) == 1);
    static_assert(sizeof(ZoneLoadContextStatus) == 1);
    static_assert(noexcept(TryInitializeZoneLoadContextSlot(
        nullptr, 0, 0)));
    static_assert(noexcept(TryClaimZoneLoadContext(nullptr, nullptr)));
    static_assert(noexcept(TryCommitZoneLoadContext(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryBeginZoneLoadContextAbandonment(
        nullptr, std::declval<const ZoneLoadContextKey &>())));
    static_assert(noexcept(TryFinishZoneLoadContextAbandonment(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        std::declval<const ZoneLoadCleanupCallbacks &>())));
    static_assert(noexcept(TryUnloadZoneLoadContext(
        nullptr,
        std::declval<const ZoneLoadContextKey &>(),
        std::declval<const ZoneLoadCleanupCallbacks &>())));
    static_assert(noexcept(zone_load::ZoneLoadContextKeyMatches(
        nullptr, std::declval<const ZoneLoadContextKey &>())));

    ZoneLoadContextSlot slot{};
    CHECK(!slot.initialized());
    CHECK(slot.slotIndex() == zone_load::kInvalidZoneLoadSlot);
    CHECK(slot.generation() == 0);
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
    CHECK(slot.terminalKind() == ZoneLoadTerminalKind::None);
    CHECK(
        slot.nextCleanupOperation()
        == ZoneLoadCleanupOperation::CancelLoadInputAndInflate);
    CHECK(!slot.cleanupActive());
    CHECK(!slot.cleanupPoisoned());

    CHECK(
        TryInitializeZoneLoadContextSlot(nullptr, 3)
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(
        TryInitializeZoneLoadContextSlot(
            &slot, zone_load::kInvalidZoneLoadSlot)
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(
        TryInitializeZoneLoadContextSlot(&slot, 3, 41)
        == ZoneLoadContextStatus::Success);
    CHECK(slot.initialized());
    CHECK(slot.slotIndex() == 3);
    CHECK(slot.generation() == 41);
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
    CHECK(
        TryInitializeZoneLoadContextSlot(&slot, 3, 41)
        == ZoneLoadContextStatus::InvalidPhase);
}

void TestClaimCommitAndFailureAtomicity()
{
    ZoneLoadContextSlot uninitialized{};
    ZoneLoadContextKey sentinel{
        UINT64_C(0x1020304050607080),
        9,
        0};
    const ZoneLoadContextKey sentinelBefore = sentinel;
    CHECK(
        TryClaimZoneLoadContext(&uninitialized, &sentinel)
        == ZoneLoadContextStatus::InvalidState);
    CHECK(sentinel == sentinelBefore);
    CHECK(
        TryClaimZoneLoadContext(nullptr, &sentinel)
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(
        TryClaimZoneLoadContext(&uninitialized, nullptr)
        == ZoneLoadContextStatus::InvalidArgument);

    ZoneLoadContextSlot slot{};
    CHECK(
        TryInitializeZoneLoadContextSlot(&slot, 17)
        == ZoneLoadContextStatus::Success);

    ZoneLoadContextKey allZero{0, 0, 0};
    const ZoneLoadContextKey allZeroBefore = allZero;
    CHECK(
        TryClaimZoneLoadContext(&slot, &allZero)
        == ZoneLoadContextStatus::InvalidKey);
    CHECK(allZero == allZeroBefore);
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);

    ZoneLoadContextKey malformed{};
    malformed.slot = 17;
    const ZoneLoadContextKey malformedBefore = malformed;
    CHECK(
        TryClaimZoneLoadContext(&slot, &malformed)
        == ZoneLoadContextStatus::InvalidKey);
    CHECK(malformed == malformedBefore);
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
    CHECK(slot.generation() == 0);

    ZoneLoadContextKey key{};
    CHECK(
        TryClaimZoneLoadContext(&slot, &key)
        == ZoneLoadContextStatus::Success);
    CHECK(key.generation == 1);
    CHECK(key.slot == 17);
    CHECK(key.reserved == 0);
    CHECK(slot.phase() == ZoneLoadContextPhase::Loading);
    CHECK(slot.generation() == 1);
    CHECK(zone_load::ZoneLoadContextKeyMatches(&slot, key));

    CHECK(
        TryClaimZoneLoadContext(&slot, &key)
        == ZoneLoadContextStatus::Success);
    CHECK(slot.generation() == 1);

    ZoneLoadContextKey secondClaim{};
    CHECK(
        TryClaimZoneLoadContext(&slot, &secondClaim)
        == ZoneLoadContextStatus::InvalidPhase);
    CHECK(!secondClaim);
    CHECK(slot.phase() == ZoneLoadContextPhase::Loading);

    ZoneLoadContextKey stale = key;
    ++stale.generation;
    CHECK(
        TryCommitZoneLoadContext(&slot, stale)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(slot.phase() == ZoneLoadContextPhase::Loading);
    stale = key;
    ++stale.slot;
    CHECK(
        TryCommitZoneLoadContext(&slot, stale)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryCommitZoneLoadContext(nullptr, key)
        == ZoneLoadContextStatus::InvalidArgument);

    CHECK(
        TryCommitZoneLoadContext(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(slot.phase() == ZoneLoadContextPhase::Live);
    CHECK(
        slot.nextCleanupOperation()
        == kLiveUnloadCleanupOrder.front());
    CHECK(
        TryCommitZoneLoadContext(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(slot.phase() == ZoneLoadContextPhase::Live);
    CHECK(
        TryClaimZoneLoadContext(&slot, &key)
        == ZoneLoadContextStatus::InvalidPhase);
}

void TestLoadingAbandonmentOrderAndTerminalIdempotency()
{
    ZoneLoadContextSlot slot{};
    const ZoneLoadContextKey key = InitializeAndClaim(&slot, 2);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
    CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Abandoned);
    CHECK(
        slot.nextCleanupOperation()
        == kAbandonmentCleanupOrder.front());

    CleanupRecorder recorder{};
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &slot, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::Success);
    CHECK(OperationsEqual(
        recorder,
        kAbandonmentCleanupOrder.data(),
        kAbandonmentCleanupOrder.size()));
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
    CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Abandoned);
    CHECK(slot.generation() == key.generation);
    CHECK(
        slot.nextCleanupOperation()
        == ZoneLoadCleanupOperation::CancelLoadInputAndInflate);

    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextKey releasedClaim = key;
    CHECK(
        TryClaimZoneLoadContext(&slot, &releasedClaim)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(releasedClaim == key);
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &slot, key, {})
        == ZoneLoadContextStatus::Success);
    CHECK(recorder.operationCount == kAbandonmentCleanupOrder.size());
    CHECK(
        TryCommitZoneLoadContext(&slot, key)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryUnloadZoneLoadContext(&slot, key, {})
        == ZoneLoadContextStatus::InvalidPhase);
}

void TestLiveUnloadOrderAndIdempotency()
{
    ZoneLoadContextSlot slot{};
    const ZoneLoadContextKey key = InitializeAndClaim(&slot, 31);
    CHECK(
        TryUnloadZoneLoadContext(&slot, key, {})
        == ZoneLoadContextStatus::InvalidPhase);
    CHECK(slot.phase() == ZoneLoadContextPhase::Loading);
    CHECK(
        TryCommitZoneLoadContext(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::InvalidPhase);
    CHECK(slot.phase() == ZoneLoadContextPhase::Live);
    CHECK(slot.terminalKind() == ZoneLoadTerminalKind::None);
    CHECK(
        slot.nextCleanupOperation()
        == kLiveUnloadCleanupOrder.front());

    CleanupRecorder recorder{};
    CHECK(
        TryUnloadZoneLoadContext(
            &slot, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::Success);
    CHECK(OperationsEqual(
        recorder,
        kLiveUnloadCleanupOrder.data(),
        kLiveUnloadCleanupOrder.size()));
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
    CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Unloaded);
    CHECK(
        TryUnloadZoneLoadContext(&slot, key, {})
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::InvalidPhase);
    CHECK(
        TryFinishZoneLoadContextAbandonment(&slot, key, {})
        == ZoneLoadContextStatus::InvalidPhase);
}

void TestRetryAtEveryCleanupBoundary()
{
    for (std::size_t failedIndex = 0;
         failedIndex < kAbandonmentCleanupOrder.size();
         ++failedIndex)
    {
        ZoneLoadContextSlot slot{};
        const ZoneLoadContextKey key =
            InitializeAndClaim(
                &slot, static_cast<std::uint32_t>(100 + failedIndex));
        CHECK(
            TryBeginZoneLoadContextAbandonment(&slot, key)
            == ZoneLoadContextStatus::Success);

        CleanupRecorder recorder{};
        recorder.injectedOperation =
            kAbandonmentCleanupOrder[failedIndex];
        recorder.injectedStatus =
            ZoneLoadCleanupCallbackStatus::Retry;
        recorder.injectionsRemaining = 1;
        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::Retry);
        CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
        CHECK(!slot.cleanupPoisoned());
        CHECK(
            slot.nextCleanupOperation()
            == kAbandonmentCleanupOrder[failedIndex]);
        CHECK(
            TryUnloadZoneLoadContext(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::InvalidPhase);
        CHECK(recorder.operationCount == failedIndex + 1);
        for (std::size_t index = 0; index <= failedIndex; ++index)
            CHECK(
                recorder.operations[index]
                == kAbandonmentCleanupOrder[index]);

        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::Success);
        CHECK(
            recorder.operationCount
            == kAbandonmentCleanupOrder.size() + 1);
        for (std::size_t index = 0; index < failedIndex; ++index)
            CHECK(
                recorder.operations[index]
                == kAbandonmentCleanupOrder[index]);
        CHECK(
            recorder.operations[failedIndex]
            == kAbandonmentCleanupOrder[failedIndex]);
        for (std::size_t index = failedIndex;
             index < kAbandonmentCleanupOrder.size();
             ++index)
        {
            CHECK(
                recorder.operations[index + 1]
                == kAbandonmentCleanupOrder[index]);
        }
        CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
        CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Abandoned);
    }
}

void TestLiveUnloadRetryAtEveryCleanupBoundary()
{
    for (std::size_t failedIndex = 0;
         failedIndex < kLiveUnloadCleanupOrder.size();
         ++failedIndex)
    {
        ZoneLoadContextSlot slot{};
        const ZoneLoadContextKey key =
            InitializeAndClaim(
                &slot, static_cast<std::uint32_t>(150 + failedIndex));
        CHECK(
            TryCommitZoneLoadContext(&slot, key)
            == ZoneLoadContextStatus::Success);

        CleanupRecorder recorder{};
        recorder.injectedOperation =
            kLiveUnloadCleanupOrder[failedIndex];
        recorder.injectedStatus =
            ZoneLoadCleanupCallbackStatus::Retry;
        recorder.injectionsRemaining = 1;
        CHECK(
            TryUnloadZoneLoadContext(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::Retry);
        CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
        CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Unloaded);
        CHECK(!slot.cleanupPoisoned());
        CHECK(
            slot.nextCleanupOperation()
            == kLiveUnloadCleanupOrder[failedIndex]);
        CHECK(
            TryBeginZoneLoadContextAbandonment(&slot, key)
            == ZoneLoadContextStatus::InvalidPhase);
        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::InvalidPhase);
        CHECK(recorder.operationCount == failedIndex + 1);
        for (std::size_t index = 0; index <= failedIndex; ++index)
        {
            CHECK(
                recorder.operations[index]
                == kLiveUnloadCleanupOrder[index]);
        }

        CHECK(
            TryUnloadZoneLoadContext(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::Success);
        CHECK(
            recorder.operationCount
            == kLiveUnloadCleanupOrder.size() + 1);
        for (std::size_t index = 0; index < failedIndex; ++index)
        {
            CHECK(
                recorder.operations[index]
                == kLiveUnloadCleanupOrder[index]);
        }
        CHECK(
            recorder.operations[failedIndex]
            == kLiveUnloadCleanupOrder[failedIndex]);
        for (std::size_t index = failedIndex;
             index < kLiveUnloadCleanupOrder.size();
             ++index)
        {
            CHECK(
                recorder.operations[index + 1]
                == kLiveUnloadCleanupOrder[index]);
        }
        CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
        CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Unloaded);
    }
}

void TestUnsafeCleanupPoisonsEveryBoundary()
{
    for (std::size_t failedIndex = 0;
         failedIndex < kAbandonmentCleanupOrder.size();
         ++failedIndex)
    {
        ZoneLoadContextSlot slot{};
        const ZoneLoadContextKey key =
            InitializeAndClaim(
                &slot, static_cast<std::uint32_t>(200 + failedIndex));
        CHECK(
            TryBeginZoneLoadContextAbandonment(&slot, key)
            == ZoneLoadContextStatus::Success);

        CleanupRecorder recorder{};
        recorder.injectedOperation =
            kAbandonmentCleanupOrder[failedIndex];
        recorder.injectedStatus =
            ZoneLoadCleanupCallbackStatus::UnsafeFailure;
        recorder.injectionsRemaining = 1;
        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
        CHECK(slot.cleanupPoisoned());
        CHECK(
            slot.nextCleanupOperation()
            == kAbandonmentCleanupOrder[failedIndex]);
        const std::size_t operationCount = recorder.operationCount;
        CHECK(
            TryBeginZoneLoadContextAbandonment(&slot, key)
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(
            TryFinishZoneLoadContextAbandonment(
                &slot, key, {})
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(recorder.operationCount == operationCount);
        ZoneLoadContextKey nullKey{};
        CHECK(
            TryClaimZoneLoadContext(&slot, &nullKey)
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(!nullKey);
    }

    ZoneLoadContextSlot unknownSlot{};
    const ZoneLoadContextKey unknownKey =
        InitializeAndClaim(&unknownSlot, 299);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &unknownSlot, unknownKey)
        == ZoneLoadContextStatus::Success);
    int context = 0;
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &unknownSlot,
            unknownKey,
            {&context, ReturnUnknownCleanupStatus})
        == ZoneLoadContextStatus::UnsafeFailure);
    CHECK(unknownSlot.cleanupPoisoned());
    CHECK(unknownSlot.phase() == ZoneLoadContextPhase::Abandoning);
}

void TestLiveUnloadUnsafeCleanupPoisonsEveryBoundary()
{
    for (std::size_t failedIndex = 0;
         failedIndex < kLiveUnloadCleanupOrder.size();
         ++failedIndex)
    {
        ZoneLoadContextSlot slot{};
        const ZoneLoadContextKey key =
            InitializeAndClaim(
                &slot, static_cast<std::uint32_t>(300 + failedIndex));
        CHECK(
            TryCommitZoneLoadContext(&slot, key)
            == ZoneLoadContextStatus::Success);

        CleanupRecorder recorder{};
        recorder.injectedOperation =
            kLiveUnloadCleanupOrder[failedIndex];
        recorder.injectedStatus =
            ZoneLoadCleanupCallbackStatus::UnsafeFailure;
        recorder.injectionsRemaining = 1;
        CHECK(
            TryUnloadZoneLoadContext(
                &slot, key, MakeCallbacks(&recorder))
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
        CHECK(slot.terminalKind() == ZoneLoadTerminalKind::Unloaded);
        CHECK(slot.cleanupPoisoned());
        CHECK(
            slot.nextCleanupOperation()
            == kLiveUnloadCleanupOrder[failedIndex]);
        const std::size_t operationCount = recorder.operationCount;
        CHECK(
            TryUnloadZoneLoadContext(&slot, key, {})
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(
            TryBeginZoneLoadContextAbandonment(&slot, key)
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(
            TryFinishZoneLoadContextAbandonment(&slot, key, {})
            == ZoneLoadContextStatus::UnsafeFailure);
        CHECK(recorder.operationCount == operationCount);
    }
}

void TestInvalidCallbacksDoNotChangeOwnership()
{
    ZoneLoadContextSlot liveSlot{};
    const ZoneLoadContextKey liveKey =
        InitializeAndClaim(&liveSlot, 401);
    CHECK(
        TryCommitZoneLoadContext(&liveSlot, liveKey)
        == ZoneLoadContextStatus::Success);

    CHECK(
        TryUnloadZoneLoadContext(&liveSlot, liveKey, {})
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(liveSlot.phase() == ZoneLoadContextPhase::Live);
    CHECK(liveSlot.terminalKind() == ZoneLoadTerminalKind::None);
    CHECK(
        liveSlot.nextCleanupOperation()
        == kLiveUnloadCleanupOrder.front());
    const ZoneLoadCleanupCallbacks missingContext{
        nullptr,
        RecordCleanup};
    CHECK(
        TryUnloadZoneLoadContext(
            &liveSlot, liveKey, missingContext)
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(liveSlot.phase() == ZoneLoadContextPhase::Live);

    ZoneLoadContextSlot loadingSlot{};
    const ZoneLoadContextKey loadingKey =
        InitializeAndClaim(&loadingSlot, 402);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &loadingSlot, loadingKey)
        == ZoneLoadContextStatus::Success);
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &loadingSlot, loadingKey, {})
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(loadingSlot.phase() == ZoneLoadContextPhase::Abandoning);
    CHECK(
        loadingSlot.terminalKind()
        == ZoneLoadTerminalKind::Abandoned);
    CHECK(
        loadingSlot.nextCleanupOperation()
        == ZoneLoadCleanupOperation::CancelLoadInputAndInflate);

    CleanupRecorder recorder{};
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &loadingSlot, loadingKey, missingContext)
        == ZoneLoadContextStatus::InvalidArgument);
    CHECK(recorder.operationCount == 0);
}

void TestReentrantCleanupFailsBusyWithoutReordering()
{
    ZoneLoadContextSlot slot{};
    const ZoneLoadContextKey key = InitializeAndClaim(&slot, 500);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&slot, key)
        == ZoneLoadContextStatus::Success);

    CleanupRecorder recorder{};
    recorder.reentrantSlot = &slot;
    recorder.reentrantKey = key;
    recorder.reenterOnce = true;
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &slot, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::Success);
    CHECK(recorder.observedCleanupActive);
    CHECK(recorder.reentrantStatus == ZoneLoadContextStatus::Busy);
    CHECK(OperationsEqual(
        recorder,
        kAbandonmentCleanupOrder.data(),
        kAbandonmentCleanupOrder.size()));
    CHECK(slot.phase() == ZoneLoadContextPhase::Empty);
}

void TestKeyMatchesAcrossActiveAndTerminalStates()
{
    ZoneLoadContextSlot slot{};
    const ZoneLoadContextKey key = InitializeAndClaim(&slot, 550);
    CHECK(zone_load::ZoneLoadContextKeyMatches(&slot, key));
    CHECK(
        TryCommitZoneLoadContext(&slot, key)
        == ZoneLoadContextStatus::Success);
    CHECK(zone_load::ZoneLoadContextKeyMatches(&slot, key));

    CleanupRecorder recorder{};
    recorder.injectedOperation = kLiveUnloadCleanupOrder.front();
    recorder.injectedStatus = ZoneLoadCleanupCallbackStatus::Retry;
    recorder.injectionsRemaining = 1;
    CHECK(
        TryUnloadZoneLoadContext(
            &slot, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::Retry);
    CHECK(slot.phase() == ZoneLoadContextPhase::Abandoning);
    CHECK(zone_load::ZoneLoadContextKeyMatches(&slot, key));

    recorder.injectedStatus =
        ZoneLoadCleanupCallbackStatus::UnsafeFailure;
    recorder.injectionsRemaining = 1;
    CHECK(
        TryUnloadZoneLoadContext(
            &slot, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::UnsafeFailure);
    CHECK(slot.cleanupPoisoned());
    CHECK(zone_load::ZoneLoadContextKeyMatches(&slot, key));

    ZoneLoadContextKey malformed = key;
    malformed.reserved = 1;
    CHECK(!zone_load::ZoneLoadContextKeyMatches(&slot, malformed));

    ZoneLoadContextSlot released{};
    const ZoneLoadContextKey releasedKey =
        InitializeAndClaim(&released, 551);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &released, releasedKey)
        == ZoneLoadContextStatus::Success);
    CleanupRecorder releasedRecorder{};
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &released,
            releasedKey,
            MakeCallbacks(&releasedRecorder))
        == ZoneLoadContextStatus::Success);
    CHECK(released.phase() == ZoneLoadContextPhase::Empty);
    CHECK(
        !zone_load::ZoneLoadContextKeyMatches(
            &released, releasedKey));
}

void TestStaleCrossSlotAndAbaRejection()
{
    ZoneLoadContextSlot first{};
    ZoneLoadContextSlot second{};
    const ZoneLoadContextKey firstKey =
        InitializeAndClaim(&first, 600);
    const ZoneLoadContextKey secondKey =
        InitializeAndClaim(&second, 601);
    CHECK(firstKey.generation == secondKey.generation);
    CHECK(
        TryCommitZoneLoadContext(&first, secondKey)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&second, firstKey)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        !zone_load::ZoneLoadContextKeyMatches(&first, secondKey));

    CHECK(
        TryBeginZoneLoadContextAbandonment(&first, firstKey)
        == ZoneLoadContextStatus::Success);
    CleanupRecorder firstCleanup{};
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &first, firstKey, MakeCallbacks(&firstCleanup))
        == ZoneLoadContextStatus::Success);
    CHECK(!zone_load::ZoneLoadContextKeyMatches(&first, firstKey));

    ZoneLoadContextKey replacementKey{};
    CHECK(
        TryClaimZoneLoadContext(&first, &replacementKey)
        == ZoneLoadContextStatus::Success);
    CHECK(replacementKey.slot == firstKey.slot);
    CHECK(replacementKey.generation == firstKey.generation + 1);
    CHECK(
        !zone_load::ZoneLoadContextKeyMatches(&first, firstKey));
    CHECK(
        zone_load::ZoneLoadContextKeyMatches(
            &first, replacementKey));
    CHECK(
        TryCommitZoneLoadContext(&first, firstKey)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryBeginZoneLoadContextAbandonment(&first, firstKey)
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &first, firstKey, MakeCallbacks(&firstCleanup))
        == ZoneLoadContextStatus::StaleKey);
    CHECK(
        TryUnloadZoneLoadContext(
            &first, firstKey, MakeCallbacks(&firstCleanup))
        == ZoneLoadContextStatus::StaleKey);
    CHECK(first.phase() == ZoneLoadContextPhase::Loading);
}

void TestGenerationWrapFailsClosed()
{
    constexpr std::uint64_t maximumGeneration =
        (std::numeric_limits<std::uint64_t>::max)();

    ZoneLoadContextSlot exhausted{};
    CHECK(
        TryInitializeZoneLoadContextSlot(
            &exhausted, 700, maximumGeneration)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextKey output{};
    CHECK(
        TryClaimZoneLoadContext(&exhausted, &output)
        == ZoneLoadContextStatus::GenerationExhausted);
    CHECK(!output);
    CHECK(exhausted.generation() == maximumGeneration);
    CHECK(exhausted.phase() == ZoneLoadContextPhase::Empty);

    ZoneLoadContextSlot lastGeneration{};
    ZoneLoadContextKey key = InitializeAndClaim(
        &lastGeneration,
        701,
        maximumGeneration - 1);
    CHECK(key.generation == maximumGeneration);
    CHECK(key.generation != 0);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &lastGeneration, key)
        == ZoneLoadContextStatus::Success);
    CleanupRecorder recorder{};
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &lastGeneration, key, MakeCallbacks(&recorder))
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextKey replacement{};
    CHECK(
        TryClaimZoneLoadContext(
            &lastGeneration, &replacement)
        == ZoneLoadContextStatus::GenerationExhausted);
    CHECK(!replacement);
    CHECK(
        lastGeneration.generation() == maximumGeneration);
    CHECK(lastGeneration.phase() == ZoneLoadContextPhase::Empty);
}

void TestCorruptionAndMalformedKeysFailClosed()
{
    ZoneLoadContextSlot corruptPhase{};
    CHECK(
        TryInitializeZoneLoadContextSlot(&corruptPhase, 800)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetPhase(
        &corruptPhase,
        static_cast<ZoneLoadContextPhase>(UINT8_C(0xFF)));
    ZoneLoadContextKey output{};
    CHECK(
        TryClaimZoneLoadContext(&corruptPhase, &output)
        == ZoneLoadContextStatus::InvalidState);
    CHECK(!output);

    ZoneLoadContextSlot corruptFlags{};
    CHECK(
        TryInitializeZoneLoadContextSlot(&corruptFlags, 801)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetFlags(
        &corruptFlags, UINT8_C(0x80));
    CHECK(
        TryClaimZoneLoadContext(&corruptFlags, &output)
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot stableRelease{};
    const ZoneLoadContextKey stableReleaseKey =
        InitializeAndClaim(&stableRelease, 802);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &stableRelease, stableReleaseKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetCleanupOperation(
        &stableRelease,
        ZoneLoadCleanupOperation::ReleaseSlot);
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &stableRelease, stableReleaseKey, {})
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot activeAndPoisoned{};
    const ZoneLoadContextKey activeAndPoisonedKey =
        InitializeAndClaim(&activeAndPoisoned, 803);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &activeAndPoisoned, activeAndPoisonedKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetFlags(
        &activeAndPoisoned,
        static_cast<std::uint8_t>(
            kInitializedFlag
            | kCleanupActiveFlag
            | kCleanupPoisonedFlag));
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &activeAndPoisoned, activeAndPoisonedKey, {})
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot loadingTerminal{};
    const ZoneLoadContextKey loadingTerminalKey =
        InitializeAndClaim(&loadingTerminal, 804);
    ZoneLoadContextSlotTestAccess::SetTerminalKind(
        &loadingTerminal, ZoneLoadTerminalKind::Unloaded);
    CHECK(
        TryCommitZoneLoadContext(
            &loadingTerminal, loadingTerminalKey)
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot liveOperation{};
    const ZoneLoadContextKey liveOperationKey =
        InitializeAndClaim(&liveOperation, 805);
    CHECK(
        TryCommitZoneLoadContext(&liveOperation, liveOperationKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetCleanupOperation(
        &liveOperation,
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate);
    CHECK(
        TryUnloadZoneLoadContext(
            &liveOperation, liveOperationKey, {})
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot abandonmentRecipe{};
    const ZoneLoadContextKey abandonmentRecipeKey =
        InitializeAndClaim(&abandonmentRecipe, 806);
    CHECK(
        TryBeginZoneLoadContextAbandonment(
            &abandonmentRecipe, abandonmentRecipeKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetCleanupOperation(
        &abandonmentRecipe,
        ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences);
    CHECK(
        TryFinishZoneLoadContextAbandonment(
            &abandonmentRecipe, abandonmentRecipeKey, {})
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot unloadRecipe{};
    const ZoneLoadContextKey unloadRecipeKey =
        InitializeAndClaim(&unloadRecipe, 807);
    CHECK(
        TryCommitZoneLoadContext(&unloadRecipe, unloadRecipeKey)
        == ZoneLoadContextStatus::Success);
    ZoneLoadContextSlotTestAccess::SetPhase(
        &unloadRecipe, ZoneLoadContextPhase::Abandoning);
    ZoneLoadContextSlotTestAccess::SetTerminalKind(
        &unloadRecipe, ZoneLoadTerminalKind::Unloaded);
    ZoneLoadContextSlotTestAccess::SetCleanupOperation(
        &unloadRecipe,
        ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation);
    CHECK(
        TryUnloadZoneLoadContext(
            &unloadRecipe, unloadRecipeKey, {})
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot corruptOperation{};
    const ZoneLoadContextKey corruptOperationKey =
        InitializeAndClaim(&corruptOperation, 808);
    ZoneLoadContextSlotTestAccess::SetCleanupOperation(
        &corruptOperation,
        static_cast<ZoneLoadCleanupOperation>(UINT8_C(0xFF)));
    CHECK(
        TryCommitZoneLoadContext(
            &corruptOperation, corruptOperationKey)
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot corruptTerminal{};
    const ZoneLoadContextKey corruptTerminalKey =
        InitializeAndClaim(&corruptTerminal, 809);
    ZoneLoadContextSlotTestAccess::SetTerminalKind(
        &corruptTerminal,
        static_cast<ZoneLoadTerminalKind>(UINT8_C(0xFF)));
    CHECK(
        TryCommitZoneLoadContext(
            &corruptTerminal, corruptTerminalKey)
        == ZoneLoadContextStatus::InvalidState);

    ZoneLoadContextSlot slot{};
    const ZoneLoadContextKey key = InitializeAndClaim(&slot, 810);
    ZoneLoadContextKey malformed = key;
    malformed.reserved = 1;
    CHECK(
        TryCommitZoneLoadContext(&slot, malformed)
        == ZoneLoadContextStatus::InvalidKey);
    malformed = key;
    malformed.generation = 0;
    CHECK(
        TryCommitZoneLoadContext(&slot, malformed)
        == ZoneLoadContextStatus::InvalidKey);
    CHECK(slot.phase() == ZoneLoadContextPhase::Loading);
}
} // namespace

int main()
{
    TestLayoutNoexceptAndInitialization();
    TestClaimCommitAndFailureAtomicity();
    TestLoadingAbandonmentOrderAndTerminalIdempotency();
    TestLiveUnloadOrderAndIdempotency();
    TestRetryAtEveryCleanupBoundary();
    TestLiveUnloadRetryAtEveryCleanupBoundary();
    TestUnsafeCleanupPoisonsEveryBoundary();
    TestLiveUnloadUnsafeCleanupPoisonsEveryBoundary();
    TestInvalidCallbacksDoNotChangeOwnership();
    TestReentrantCleanupFailsBusyWithoutReordering();
    TestKeyMatchesAcrossActiveAndTerminalStates();
    TestStaleCrossSlotAndAbaRejection();
    TestGenerationWrapFailsClosed();
    TestCorruptionAndMalformedKeysFailClosed();

    if (failures != 0)
        return 1;
    std::puts("Zone-load context lifecycle tests passed");
    return 0;
}

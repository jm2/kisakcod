#include "EffectsCore/fx_archive_physics_batch_control.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace archive = fx::archive;

static_assert(sizeof(archive::ArchivePhysicsBatchOperation) == 1);
static_assert(std::is_standard_layout_v<
              archive::ArchivePhysicsBatchCallbacks>);
static_assert(std::is_trivially_copyable_v<
              archive::ArchivePhysicsBatchCallbacks>);
static_assert(std::is_same_v<
              archive::ArchivePhysicsBatchPerformCallback,
              archive::RestoreControlOperationStatus (*)(
                  void *,
                  archive::ArchivePhysicsBatchOperation,
                  std::size_t) noexcept>);
static_assert(noexcept(archive::RunArchivePhysicsRetirementBatch(
    std::declval<const archive::ArchivePhysicsBatchCallbacks &>(),
    nullptr,
    0,
    0,
    nullptr)));
static_assert(noexcept(archive::RunArchivePhysicsReconstructionBatch(
    std::declval<const archive::ArchivePhysicsBatchCallbacks &>(),
    nullptr,
    0,
    0,
    nullptr)));
static_assert(std::is_same_v<
              decltype(archive::RunArchivePhysicsRetirementBatch(
                  std::declval<
                      const archive::ArchivePhysicsBatchCallbacks &>(),
                  nullptr,
                  0,
                  0,
                  nullptr)),
              archive::RestoreControlOperationStatus>);

namespace
{
using Operation = archive::ArchivePhysicsBatchOperation;
using Status = archive::RestoreControlOperationStatus;

constexpr std::size_t kEntryCapacity = 8;
constexpr std::size_t kOperationCount = 4;
constexpr std::size_t kTraceCapacity = 2 * kEntryCapacity;

enum class BatchKind : std::uint8_t
{
    Retirement,
    Reconstruction,
};

struct TraceEvent
{
    Operation operation = Operation::ValidateRetirement;
    std::size_t entryIndex = 0;
};

struct Fixture
{
    std::array<std::array<Status, kEntryCapacity>, kOperationCount>
        results{};
    std::array<TraceEvent, kTraceCapacity> trace{};
    std::array<bool, kEntryCapacity> retired{};
    std::array<bool, kEntryCapacity> reconstructed{};
    std::size_t traceCount = 0;
    bool traceOverflow = false;
    bool callbackArgumentInvalid = false;

    Fixture() noexcept
    {
        SetAllResults(Status::Success);
    }

    void SetAllResults(const Status status) noexcept
    {
        for (auto &operationResults : results)
            operationResults.fill(status);
    }

    void ResetObservations() noexcept
    {
        traceCount = 0;
        traceOverflow = false;
        callbackArgumentInvalid = false;
        retired.fill(false);
        reconstructed.fill(false);
    }

    void Reset() noexcept
    {
        SetAllResults(Status::Success);
        ResetObservations();
    }
};

int failures = 0;

void Check(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

constexpr std::size_t OperationIndex(const Operation operation) noexcept
{
    switch (operation)
    {
    case Operation::ValidateRetirement:
        return 0;
    case Operation::Retire:
        return 1;
    case Operation::ValidateReconstruction:
        return 2;
    case Operation::Reconstruct:
        return 3;
    }
    return kOperationCount;
}

constexpr Operation PreflightOperation(const BatchKind kind) noexcept
{
    return kind == BatchKind::Retirement
        ? Operation::ValidateRetirement
        : Operation::ValidateReconstruction;
}

constexpr Operation CommitOperation(const BatchKind kind) noexcept
{
    return kind == BatchKind::Retirement
        ? Operation::Retire
        : Operation::Reconstruct;
}

Status Perform(
    void *const opaque,
    const Operation operation,
    const std::size_t entryIndex) noexcept
{
    auto *const fixture = static_cast<Fixture *>(opaque);
    const std::size_t operationIndex = OperationIndex(operation);
    if (!fixture || operationIndex >= kOperationCount
        || entryIndex >= kEntryCapacity)
    {
        if (fixture)
            fixture->callbackArgumentInvalid = true;
        return Status::UnsafeFailure;
    }

    if (fixture->traceCount == fixture->trace.size())
    {
        fixture->traceOverflow = true;
    }
    else
    {
        fixture->trace[fixture->traceCount++] = {operation, entryIndex};
    }

    const Status status = fixture->results[operationIndex][entryIndex];
    if (status == Status::Success)
    {
        if (operation == Operation::Retire)
            fixture->retired[entryIndex] = true;
        else if (operation == Operation::Reconstruct)
            fixture->reconstructed[entryIndex] = true;
    }
    return status;
}

archive::ArchivePhysicsBatchCallbacks CallbacksFor(
    Fixture *const fixture) noexcept
{
    return {fixture, Perform};
}

Status RunBatch(
    const BatchKind kind,
    const archive::ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount,
    std::size_t *const outCompletedCount) noexcept
{
    return kind == BatchKind::Retirement
        ? archive::RunArchivePhysicsRetirementBatch(
              callbacks,
              planIndices,
              selectedCount,
              entryCount,
              outCompletedCount)
        : archive::RunArchivePhysicsReconstructionBatch(
              callbacks,
              planIndices,
              selectedCount,
              entryCount,
              outCompletedCount);
}

bool TraceMatches(
    const Fixture &fixture,
    const BatchKind kind,
    const std::size_t *const planIndices,
    const std::size_t preflightCount,
    const std::size_t commitCount) noexcept
{
    if (fixture.traceOverflow || fixture.callbackArgumentInvalid
        || fixture.traceCount != preflightCount + commitCount)
    {
        return false;
    }

    const Operation preflight = PreflightOperation(kind);
    const Operation commit = CommitOperation(kind);
    for (std::size_t index = 0; index < preflightCount; ++index)
    {
        if (fixture.trace[index].operation != preflight
            || fixture.trace[index].entryIndex != planIndices[index])
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < commitCount; ++index)
    {
        const TraceEvent &event = fixture.trace[preflightCount + index];
        if (event.operation != commit
            || event.entryIndex != planIndices[index])
        {
            return false;
        }
    }
    return true;
}

bool MutationMatchesPrefix(
    const Fixture &fixture,
    const BatchKind kind,
    const std::size_t *const planIndices,
    const std::size_t completedCount) noexcept
{
    std::array<bool, kEntryCapacity> expected{};
    for (std::size_t index = 0; index < completedCount; ++index)
        expected[planIndices[index]] = true;

    const auto &actual = kind == BatchKind::Retirement
        ? fixture.retired
        : fixture.reconstructed;
    const auto &other = kind == BatchKind::Retirement
        ? fixture.reconstructed
        : fixture.retired;
    return actual == expected
        && other == std::array<bool, kEntryCapacity>{};
}

void CheckRejectedWithoutCallbacks(
    const BatchKind kind,
    Fixture *const fixture,
    const archive::ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount,
    const char *const message)
{
    if (fixture)
        fixture->ResetObservations();
    std::size_t completedCount = 99;
    const Status status = RunBatch(
        kind,
        callbacks,
        planIndices,
        selectedCount,
        entryCount,
        &completedCount);
    Check(status == Status::UnsafeFailure, message);
    Check(completedCount == 0,
          "invalid batch arguments clear completed count");
    if (fixture)
    {
        Check(fixture->traceCount == 0 && !fixture->traceOverflow,
              "invalid batch arguments invoke no callback");
        Check(MutationMatchesPrefix(*fixture, kind, nullptr, 0),
              "invalid batch arguments mutate no entry");
    }
}

void TestEmptySelections()
{
    for (const BatchKind kind :
         {BatchKind::Retirement, BatchKind::Reconstruction})
    {
        Fixture fixture{};
        std::size_t completedCount = 77;
        Status status = RunBatch(
            kind,
            CallbacksFor(&fixture),
            nullptr,
            0,
            0,
            &completedCount);
        Check(status == Status::Success,
              "empty zero-capacity batch succeeds");
        Check(completedCount == 0,
              "empty batch reports zero completed entries");
        Check(fixture.traceCount == 0,
              "empty batch invokes no callbacks");

        const std::size_t ignoredPlan =
            (std::numeric_limits<std::size_t>::max)();
        completedCount = 88;
        status = RunBatch(
            kind,
            CallbacksFor(&fixture),
            &ignoredPlan,
            0,
            kEntryCapacity,
            &completedCount);
        Check(status == Status::Success && completedCount == 0,
              "empty selection ignores plan storage and entry capacity");
        Check(fixture.traceCount == 0,
              "reused empty batch remains callback-free");
    }
}

void TestInvalidArguments()
{
    constexpr std::array<std::size_t, 3> validPlan{0, 2, 1};
    for (const BatchKind kind :
         {BatchKind::Retirement, BatchKind::Reconstruction})
    {
        Fixture fixture{};
        const archive::ArchivePhysicsBatchCallbacks validCallbacks =
            CallbacksFor(&fixture);

        CheckRejectedWithoutCallbacks(
            kind,
            &fixture,
            {nullptr, Perform},
            validPlan.data(),
            validPlan.size(),
            kEntryCapacity,
            "null callback context fails closed");
        CheckRejectedWithoutCallbacks(
            kind,
            &fixture,
            {&fixture, nullptr},
            validPlan.data(),
            validPlan.size(),
            kEntryCapacity,
            "null perform callback fails closed");
        CheckRejectedWithoutCallbacks(
            kind,
            &fixture,
            validCallbacks,
            nullptr,
            1,
            kEntryCapacity,
            "nonempty selection requires plan storage");
        CheckRejectedWithoutCallbacks(
            kind,
            &fixture,
            validCallbacks,
            validPlan.data(),
            2,
            1,
            "selected count cannot exceed entry count");
        CheckRejectedWithoutCallbacks(
            kind,
            &fixture,
            validCallbacks,
            nullptr,
            (std::numeric_limits<std::size_t>::max)(),
            0,
            "oversized selected count is rejected before plan access");

        for (std::size_t badPosition = 0;
             badPosition < validPlan.size();
             ++badPosition)
        {
            std::array<std::size_t, 3> outOfRange = validPlan;
            outOfRange[badPosition] = kEntryCapacity;
            CheckRejectedWithoutCallbacks(
                kind,
                &fixture,
                validCallbacks,
                outOfRange.data(),
                outOfRange.size(),
                kEntryCapacity,
                "out-of-range plan index fails before callbacks");
        }

        constexpr std::array<std::array<std::size_t, 3>, 3>
            duplicatePlans{{
                {{0, 0, 2}},
                {{1, 3, 1}},
                {{4, 2, 2}},
            }};
        for (const auto &duplicatePlan : duplicatePlans)
        {
            CheckRejectedWithoutCallbacks(
                kind,
                &fixture,
                validCallbacks,
                duplicatePlan.data(),
                duplicatePlan.size(),
                kEntryCapacity,
                "duplicate plan indices fail before callbacks");
        }

        fixture.ResetObservations();
        const Status nullOutputStatus = RunBatch(
            kind,
            validCallbacks,
            validPlan.data(),
            validPlan.size(),
            kEntryCapacity,
            nullptr);
        Check(nullOutputStatus == Status::UnsafeFailure,
              "null completed-count output fails closed");
        Check(fixture.traceCount == 0,
              "null completed-count output invokes no callback");
        Check(MutationMatchesPrefix(fixture, kind, nullptr, 0),
              "null completed-count output mutates no entry");
    }
}

void TestSuccessfulTraceOrder()
{
    constexpr std::array<std::size_t, 4> plan{6, 1, 4, 0};
    for (const BatchKind kind :
         {BatchKind::Retirement, BatchKind::Reconstruction})
    {
        Fixture fixture{};
        const auto planBefore = plan;
        std::size_t completedCount = 0;
        const Status status = RunBatch(
            kind,
            CallbacksFor(&fixture),
            plan.data(),
            plan.size(),
            kEntryCapacity,
            &completedCount);
        Check(status == Status::Success,
              "successful batch returns Success");
        Check(completedCount == plan.size(),
              "successful batch reports the complete prefix");
        Check(TraceMatches(
                  fixture, kind, plan.data(), plan.size(), plan.size()),
              "successful batch runs all preflights before plan-order commits");
        Check(MutationMatchesPrefix(
                  fixture, kind, plan.data(), plan.size()),
              "successful batch mutates exactly the selected entries");
        Check(plan == planBefore,
              "batch controller does not modify caller plan storage");
    }
}

void TestEveryPreflightResultAtEveryPosition()
{
    constexpr std::array<std::size_t, 4> plan{5, 2, 7, 1};
    constexpr Status invalidStatus = static_cast<Status>(0xFFu);
    constexpr std::array<Status, 3> failuresToInject{
        Status::RecoverableFailure,
        Status::UnsafeFailure,
        invalidStatus,
    };

    for (const BatchKind kind :
         {BatchKind::Retirement, BatchKind::Reconstruction})
    {
        const Operation preflight = PreflightOperation(kind);
        for (std::size_t failurePosition = 0;
             failurePosition < plan.size();
             ++failurePosition)
        {
            for (const Status injected : failuresToInject)
            {
                Fixture fixture{};
                fixture.results[OperationIndex(preflight)]
                               [plan[failurePosition]] = injected;
                std::size_t completedCount = 77;
                const Status status = RunBatch(
                    kind,
                    CallbacksFor(&fixture),
                    plan.data(),
                    plan.size(),
                    kEntryCapacity,
                    &completedCount);
                const Status expected = injected == invalidStatus
                    ? Status::UnsafeFailure
                    : injected;
                Check(status == expected,
                      "preflight result is propagated or fails closed");
                Check(completedCount == 0,
                      "preflight failure reports no completed commits");
                Check(TraceMatches(
                          fixture,
                          kind,
                          plan.data(),
                          failurePosition + 1,
                          0),
                      "preflight failure stops before every commit callback");
                Check(MutationMatchesPrefix(
                          fixture, kind, plan.data(), 0),
                      "late preflight failure preserves the no-mutation guarantee");
            }
        }
    }
}

void TestEveryCommitResultAtEveryPosition()
{
    constexpr std::array<std::size_t, 4> plan{3, 0, 6, 2};
    constexpr Status invalidStatus = static_cast<Status>(0xA5u);
    constexpr std::array<Status, 3> failuresToInject{
        Status::RecoverableFailure,
        Status::UnsafeFailure,
        invalidStatus,
    };

    for (const BatchKind kind :
         {BatchKind::Retirement, BatchKind::Reconstruction})
    {
        const Operation commit = CommitOperation(kind);
        for (std::size_t failurePosition = 0;
             failurePosition < plan.size();
             ++failurePosition)
        {
            for (const Status injected : failuresToInject)
            {
                Fixture fixture{};
                fixture.results[OperationIndex(commit)]
                               [plan[failurePosition]] = injected;
                std::size_t completedCount = 99;
                const Status status = RunBatch(
                    kind,
                    CallbacksFor(&fixture),
                    plan.data(),
                    plan.size(),
                    kEntryCapacity,
                    &completedCount);
                const Status expected = injected == invalidStatus
                    ? Status::UnsafeFailure
                    : injected;
                Check(status == expected,
                      "commit result is propagated or fails closed");
                Check(completedCount == failurePosition,
                      "commit failure preserves the exact successful prefix");
                Check(TraceMatches(
                          fixture,
                          kind,
                          plan.data(),
                          plan.size(),
                          failurePosition + 1),
                      "commit failure follows complete preflight and stops in plan order");
                Check(MutationMatchesPrefix(
                          fixture,
                          kind,
                          plan.data(),
                          failurePosition),
                      "commit failure mutates only callbacks that returned Success");
            }
        }
    }
}

void TestControllerReuse()
{
    constexpr std::array<std::size_t, 3> firstPlan{7, 3, 5};
    constexpr std::array<std::size_t, 2> secondPlan{1, 6};
    Fixture fixture{};
    const archive::ArchivePhysicsBatchCallbacks callbacks =
        CallbacksFor(&fixture);

    fixture.results[OperationIndex(Operation::Retire)][3] =
        Status::RecoverableFailure;
    std::size_t completedCount = 100;
    Status status = archive::RunArchivePhysicsRetirementBatch(
        callbacks,
        firstPlan.data(),
        firstPlan.size(),
        kEntryCapacity,
        &completedCount);
    Check(status == Status::RecoverableFailure && completedCount == 1,
          "first reused call exposes its completed retirement prefix");
    Check(TraceMatches(
              fixture,
              BatchKind::Retirement,
              firstPlan.data(),
              firstPlan.size(),
              2),
          "first reused call has the expected failure trace");

    fixture.Reset();
    completedCount = 101;
    status = archive::RunArchivePhysicsReconstructionBatch(
        callbacks,
        secondPlan.data(),
        secondPlan.size(),
        kEntryCapacity,
        &completedCount);
    Check(status == Status::Success
              && completedCount == secondPlan.size(),
          "controller state does not leak into a different reused batch");
    Check(TraceMatches(
              fixture,
              BatchKind::Reconstruction,
              secondPlan.data(),
              secondPlan.size(),
              secondPlan.size()),
          "reused reconstruction retains exact two-pass order");
    Check(MutationMatchesPrefix(
              fixture,
              BatchKind::Reconstruction,
              secondPlan.data(),
              secondPlan.size()),
          "reused reconstruction mutates only its own selection");

    fixture.Reset();
    completedCount = 102;
    status = archive::RunArchivePhysicsRetirementBatch(
        callbacks, nullptr, 0, kEntryCapacity, &completedCount);
    Check(status == Status::Success && completedCount == 0
              && fixture.traceCount == 0,
          "controller can be reused for an empty terminal batch");
}
} // namespace

int main()
{
    TestEmptySelections();
    TestInvalidArguments();
    TestSuccessfulTraceOrder();
    TestEveryPreflightResultAtEveryPosition();
    TestEveryCommitResultAtEveryPosition();
    TestControllerReuse();

    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "%d FX archive physics batch controller test(s) failed\n",
            failures);
        return 1;
    }

    std::puts("FX archive physics batch controller tests passed");
    return 0;
}

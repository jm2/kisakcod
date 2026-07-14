#include <EffectsCore/fx_archive_restore_control.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace restore = fx::archive;

static_assert(sizeof(restore::RestoreControlOperation) == 1);
static_assert(sizeof(restore::RestoreControlOperationStatus) == 1);
static_assert(sizeof(restore::RestoreControlOutcome) == 1);
static_assert(std::is_standard_layout_v<restore::RestoreControlCallbacks>);
static_assert(std::is_trivially_copyable_v<
              restore::RestoreControlCallbacks>);
static_assert(noexcept(restore::RunRestoreControl(
    std::declval<const restore::RestoreControlCallbacks &>())));

namespace
{
using Operation = restore::RestoreControlOperation;
using Status = restore::RestoreControlOperationStatus;
using Outcome = restore::RestoreControlOutcome;

constexpr std::array PrepareOperations = {
    Operation::CaptureOriginal,
    Operation::PlanRetirement,
    Operation::RetireOriginal,
    Operation::PreparePhysicsReplacement,
    Operation::CreateDesiredPhysics,
    Operation::ValidateDesiredPhysics,
    Operation::PublishPhysicsReplacement,
};

constexpr std::array DesiredPublicationOperations = {
    Operation::PublishDesiredGraph,
    Operation::ValidateDesiredState,
};

constexpr std::array CommitOperations = {
    Operation::ValidateDiscardedOriginalPhysics,
    Operation::DrainNonLivePhysics,
};

constexpr std::array LiveGraphRecoveryOperations = {
    Operation::DrainNonLivePhysics,
    Operation::ValidateOriginalTokensInLiveGraph,
    Operation::ReconstructRetiredOriginalPhysics,
    Operation::PatchOriginalTokensInLiveGraph,
    Operation::ValidateOriginalGraph,
    Operation::ValidateOriginalPhysics,
};

constexpr std::array SnapshotRecoveryOperations = {
    Operation::RollbackPhysicsReplacement,
    Operation::DrainNonLivePhysics,
    Operation::ValidateOriginalTokensInSnapshot,
    Operation::ReconstructRetiredOriginalPhysics,
    Operation::PatchOriginalTokensInSnapshot,
    Operation::ValidateOriginalPhysics,
    Operation::PublishOriginalGraph,
    Operation::ValidateOriginalGraph,
};

constexpr Status InvalidStatus = static_cast<Status>(0xFFu);
constexpr std::array NonSuccessStatuses = {
    Status::RecoverableFailure,
    Status::UnsafeFailure,
    InvalidStatus,
};

struct Injection
{
    std::size_t ordinal = 0;
    Status status = Status::Success;
};

struct FakeBackend
{
    std::array<Operation, 64> trace{};
    std::size_t traceCount = 0;
    std::array<Injection, 4> injections{};
    std::size_t injectionCount = 0;
    bool overflow = false;
};

struct ExpectedTrace
{
    std::array<Operation, 64> operations{};
    std::size_t count = 0;

    void Add(const Operation operation) noexcept
    {
        if (count < operations.size())
            operations[count++] = operation;
    }

    template <std::size_t Count>
    void Append(const std::array<Operation, Count> &source) noexcept
    {
        AppendPrefix(source, Count);
    }

    template <std::size_t Count>
    void AppendPrefix(
        const std::array<Operation, Count> &source,
        const std::size_t prefixCount) noexcept
    {
        for (std::size_t index = 0;
             index < prefixCount && index < Count;
             ++index)
        {
            Add(source[index]);
        }
    }
};

int failures = 0;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

Status Perform(
    void *const opaque,
    const Operation operation) noexcept
{
    auto *const backend = static_cast<FakeBackend *>(opaque);
    const std::size_t ordinal = backend->traceCount;
    if (backend->traceCount == backend->trace.size())
    {
        backend->overflow = true;
        return Status::UnsafeFailure;
    }
    backend->trace[backend->traceCount++] = operation;

    for (std::size_t index = 0;
         index < backend->injectionCount;
         ++index)
    {
        if (backend->injections[index].ordinal == ordinal)
            return backend->injections[index].status;
    }
    return Status::Success;
}

Outcome Run(FakeBackend *const backend) noexcept
{
    restore::RestoreControlCallbacks callbacks{};
    callbacks.context = backend;
    callbacks.perform = Perform;
    return restore::RunRestoreControl(callbacks);
}

void Inject(
    FakeBackend *const backend,
    const std::size_t ordinal,
    const Status status) noexcept
{
    if (backend->injectionCount == backend->injections.size())
    {
        backend->overflow = true;
        return;
    }
    backend->injections[backend->injectionCount++] = {ordinal, status};
}

void ExpectRun(
    FakeBackend *const backend,
    const Outcome expectedOutcome,
    const ExpectedTrace &expectedTrace,
    const char *const message)
{
    const Outcome actualOutcome = Run(backend);
    bool matches = actualOutcome == expectedOutcome
        && !backend->overflow
        && backend->traceCount == expectedTrace.count;
    for (std::size_t index = 0;
         matches && index < expectedTrace.count;
         ++index)
    {
        matches = backend->trace[index]
            == expectedTrace.operations[index];
    }
    if (!matches)
    {
        std::fprintf(
            stderr,
            "FAIL: %s (outcome %u/%u, trace %zu/%zu)\n",
            message,
            static_cast<unsigned>(actualOutcome),
            static_cast<unsigned>(expectedOutcome),
            backend->traceCount,
            expectedTrace.count);
        ++failures;
    }
}

ExpectedTrace SuccessTrace() noexcept
{
    ExpectedTrace trace{};
    trace.Append(PrepareOperations);
    trace.Append(DesiredPublicationOperations);
    trace.Append(CommitOperations);
    return trace;
}

ExpectedTrace PrepareFailurePrefix(
    const std::size_t failedIndex) noexcept
{
    ExpectedTrace trace{};
    trace.AppendPrefix(PrepareOperations, failedIndex + 1);
    return trace;
}

ExpectedTrace DesiredFailurePrefix(
    const std::size_t failedIndex) noexcept
{
    ExpectedTrace trace{};
    trace.Append(PrepareOperations);
    trace.AppendPrefix(
        DesiredPublicationOperations,
        failedIndex + 1);
    return trace;
}

ExpectedTrace CommitFailurePrefix(
    const std::size_t failedIndex) noexcept
{
    ExpectedTrace trace{};
    trace.Append(PrepareOperations);
    trace.Append(DesiredPublicationOperations);
    trace.AppendPrefix(CommitOperations, failedIndex + 1);
    return trace;
}

void TestInvalidCallbacks()
{
    restore::RestoreControlCallbacks callbacks{};
    Expect(
        restore::RunRestoreControl(callbacks)
            == Outcome::UnsafeFailure,
        "empty callbacks fail closed");

    callbacks.perform = Perform;
    Expect(
        restore::RunRestoreControl(callbacks)
            == Outcome::UnsafeFailure,
        "null callback context fails closed");

    FakeBackend backend{};
    callbacks.context = &backend;
    callbacks.perform = nullptr;
    Expect(
        restore::RunRestoreControl(callbacks)
            == Outcome::UnsafeFailure,
        "null perform callback fails closed");
    Expect(backend.traceCount == 0, "invalid callbacks perform no work");
}

void TestSuccessPath()
{
    FakeBackend backend{};
    ExpectRun(
        &backend,
        Outcome::DesiredPublished,
        SuccessTrace(),
        "all-success path publishes desired state");
}

void TestPrimaryPathInjection()
{
    for (std::size_t failedIndex = 0;
         failedIndex < PrepareOperations.size();
         ++failedIndex)
    {
        for (const Status status : NonSuccessStatuses)
        {
            FakeBackend backend{};
            Inject(&backend, failedIndex, status);
            ExpectedTrace expected = PrepareFailurePrefix(failedIndex);
            Outcome outcome = Outcome::UnsafeFailure;
            if (status == Status::RecoverableFailure)
            {
                expected.Append(LiveGraphRecoveryOperations);
                outcome = Outcome::OriginalRestored;
            }
            ExpectRun(
                &backend,
                outcome,
                expected,
                "prepare operation injection follows live recovery table");
        }
    }

    for (std::size_t failedIndex = 0;
         failedIndex < DesiredPublicationOperations.size();
         ++failedIndex)
    {
        const std::size_t ordinal =
            PrepareOperations.size() + failedIndex;
        for (const Status status : NonSuccessStatuses)
        {
            FakeBackend backend{};
            Inject(&backend, ordinal, status);
            ExpectedTrace expected = DesiredFailurePrefix(failedIndex);
            Outcome outcome = Outcome::UnsafeFailure;
            if (status == Status::RecoverableFailure)
            {
                expected.Append(SnapshotRecoveryOperations);
                outcome = Outcome::OriginalRestored;
            }
            ExpectRun(
                &backend,
                outcome,
                expected,
                "publication operation injection follows snapshot recovery table");
        }
    }

    for (std::size_t failedIndex = 0;
         failedIndex < CommitOperations.size();
         ++failedIndex)
    {
        const std::size_t ordinal = PrepareOperations.size()
            + DesiredPublicationOperations.size() + failedIndex;
        for (const Status status : NonSuccessStatuses)
        {
            FakeBackend backend{};
            Inject(&backend, ordinal, status);
            ExpectedTrace expected = CommitFailurePrefix(failedIndex);
            Outcome outcome = Outcome::UnsafeFailure;
            if (status == Status::RecoverableFailure)
            {
                expected.Add(Operation::PublishSafeEmpty);
                outcome = Outcome::SafeEmptyPublished;
            }
            ExpectRun(
                &backend,
                outcome,
                expected,
                "commit operation injection follows safe-empty table");
        }
    }
}

void TestLiveRecoveryInjection()
{
    for (std::size_t failedIndex = 0;
         failedIndex < LiveGraphRecoveryOperations.size();
         ++failedIndex)
    {
        for (const Status status : NonSuccessStatuses)
        {
            FakeBackend backend{};
            Inject(&backend, 0, Status::RecoverableFailure);
            Inject(&backend, failedIndex + 1, status);

            ExpectedTrace expected{};
            expected.Add(Operation::CaptureOriginal);
            expected.AppendPrefix(
                LiveGraphRecoveryOperations,
                failedIndex + 1);
            Outcome outcome = Outcome::UnsafeFailure;
            if (status == Status::RecoverableFailure)
            {
                expected.Add(Operation::PublishSafeEmpty);
                outcome = Outcome::SafeEmptyPublished;
            }
            ExpectRun(
                &backend,
                outcome,
                expected,
                "live recovery injection reaches one safe terminal state");
        }
    }
}

void TestSnapshotRecoveryInjection()
{
    const std::size_t publicationFailureOrdinal =
        PrepareOperations.size();
    const std::size_t recoveryStartOrdinal =
        publicationFailureOrdinal + 1;
    for (std::size_t failedIndex = 0;
         failedIndex < SnapshotRecoveryOperations.size();
         ++failedIndex)
    {
        for (const Status status : NonSuccessStatuses)
        {
            FakeBackend backend{};
            Inject(
                &backend,
                publicationFailureOrdinal,
                Status::RecoverableFailure);
            Inject(
                &backend,
                recoveryStartOrdinal + failedIndex,
                status);

            ExpectedTrace expected = DesiredFailurePrefix(0);
            expected.AppendPrefix(
                SnapshotRecoveryOperations,
                failedIndex + 1);
            Outcome outcome = Outcome::UnsafeFailure;
            if (status == Status::RecoverableFailure)
            {
                expected.Add(Operation::PublishSafeEmpty);
                outcome = Outcome::SafeEmptyPublished;
            }
            ExpectRun(
                &backend,
                outcome,
                expected,
                "snapshot recovery injection reaches one safe terminal state");
        }
    }
}

void TestSafeEmptyInjection()
{
    constexpr std::array SafeEmptyStatuses = {
        Status::RecoverableFailure,
        Status::UnsafeFailure,
        InvalidStatus,
    };

    for (const Status safeEmptyStatus : SafeEmptyStatuses)
    {
        {
            FakeBackend backend{};
            Inject(&backend, 0, Status::RecoverableFailure);
            Inject(&backend, 1, Status::RecoverableFailure);
            Inject(&backend, 2, safeEmptyStatus);

            ExpectedTrace expected{};
            expected.Add(Operation::CaptureOriginal);
            expected.Add(Operation::DrainNonLivePhysics);
            expected.Add(Operation::PublishSafeEmpty);
            ExpectRun(
                &backend,
                Outcome::UnsafeFailure,
                expected,
                "live-recovery safe-empty failure is unsafe");
        }

        {
            FakeBackend backend{};
            const std::size_t publicationFailureOrdinal =
                PrepareOperations.size();
            Inject(
                &backend,
                publicationFailureOrdinal,
                Status::RecoverableFailure);
            Inject(
                &backend,
                publicationFailureOrdinal + 1,
                Status::RecoverableFailure);
            Inject(
                &backend,
                publicationFailureOrdinal + 2,
                safeEmptyStatus);

            ExpectedTrace expected = DesiredFailurePrefix(0);
            expected.Add(Operation::RollbackPhysicsReplacement);
            expected.Add(Operation::PublishSafeEmpty);
            ExpectRun(
                &backend,
                Outcome::UnsafeFailure,
                expected,
                "snapshot-recovery safe-empty failure is unsafe");
        }

        {
            FakeBackend backend{};
            const std::size_t commitFailureOrdinal =
                PrepareOperations.size()
                + DesiredPublicationOperations.size();
            Inject(
                &backend,
                commitFailureOrdinal,
                Status::RecoverableFailure);
            Inject(
                &backend,
                commitFailureOrdinal + 1,
                safeEmptyStatus);

            ExpectedTrace expected = CommitFailurePrefix(0);
            expected.Add(Operation::PublishSafeEmpty);
            ExpectRun(
                &backend,
                Outcome::UnsafeFailure,
                expected,
                "commit safe-empty failure is unsafe");
        }
    }
}
} // namespace

int main()
{
    TestInvalidCallbacks();
    TestSuccessPath();
    TestPrimaryPathInjection();
    TestLiveRecoveryInjection();
    TestSnapshotRecoveryInjection();
    TestSafeEmptyInjection();

    if (failures != 0)
    {
        std::fprintf(
            stderr,
            "%d FX archive restore-control test(s) failed\n",
            failures);
        return 1;
    }
    std::puts("FX archive restore-control tests passed");
    return 0;
}

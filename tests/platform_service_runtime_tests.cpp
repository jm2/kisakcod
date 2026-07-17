#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

#include <database/db_script_string_transaction.h>
#include <qcommon/sys_event.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_thread.h>
#include <qcommon/sys_time.h>
#include <qcommon/sys_worker_gate.h>

// The standalone service target intentionally does not link the engine's
// assert/reporting graph.  Preserve debug assertion behavior with a fatal test
// handler so Debug MSVC configurations remain independently linkable.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

bool RunDatabaseScriptStringAdapterTests();

namespace
{
bool TestScriptStringTransactionSerializer()
{
    namespace transaction = db::script_string_transaction;
    using transaction::ScriptStringTransactionStatus;
    using transaction::ScriptStringTransactionToken;

    static_assert(!std::is_copy_constructible_v<ScriptStringTransactionToken>);
    static_assert(!std::is_move_constructible_v<ScriptStringTransactionToken>);
    static_assert(sizeof(ScriptStringTransactionToken) == 8);

    if (transaction::TryBeginScriptStringTransaction(nullptr)
            != ScriptStringTransactionStatus::InvalidArgument
        || transaction::FinishScriptStringTransaction(nullptr)
            != ScriptStringTransactionStatus::InvalidArgument)
    {
        std::fputs("script-string transaction accepted a null token\n", stderr);
        return false;
    }

    ScriptStringTransactionToken owner;
    if (transaction::TryBeginScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success
        || !owner.active() || owner.serial() == 0
        || !transaction::OwnsScriptStringTransaction(owner))
    {
        std::fputs("script-string transaction did not publish its owner\n", stderr);
        return false;
    }
    const std::uint32_t firstSerial = owner.serial();

    ScriptStringTransactionToken nested;
    if (transaction::TryBeginScriptStringTransaction(&nested)
            != ScriptStringTransactionStatus::Busy
        || nested.active() || nested.serial() != 0)
    {
        std::fputs("recursive script-string transaction was not rejected\n", stderr);
        return false;
    }
    if (transaction::FinishScriptStringTransaction(&nested)
            != ScriptStringTransactionStatus::InvalidToken
        || !transaction::OwnsScriptStringTransaction(owner))
    {
        std::fputs("invalid finish released the active transaction\n", stderr);
        return false;
    }

    std::atomic<bool> contenderReady{false};
    std::atomic<bool> contenderAcquired{false};
    std::atomic<int> contenderBeginStatus{
        static_cast<int>(ScriptStringTransactionStatus::InvalidArgument)};
    std::atomic<int> contenderFinishStatus{
        static_cast<int>(ScriptStringTransactionStatus::InvalidArgument)};
    std::thread contender([&]() {
        ScriptStringTransactionToken token;
        contenderReady.store(true, std::memory_order_release);
        const ScriptStringTransactionStatus beginStatus =
            transaction::TryBeginScriptStringTransaction(&token);
        contenderBeginStatus.store(
            static_cast<int>(beginStatus), std::memory_order_release);
        if (beginStatus != ScriptStringTransactionStatus::Success)
            return;
        contenderAcquired.store(true, std::memory_order_release);
        contenderFinishStatus.store(
            static_cast<int>(
                transaction::FinishScriptStringTransaction(&token)),
            std::memory_order_release);
    });

    while (!contenderReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (contenderAcquired.load(std::memory_order_acquire))
    {
        std::fputs("concurrent script-string transaction overlapped\n", stderr);
        (void)transaction::FinishScriptStringTransaction(&owner);
        contender.join();
        return false;
    }

    if (transaction::FinishScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success)
    {
        std::fputs("script-string transaction finish failed\n", stderr);
        contender.join();
        return false;
    }
    contender.join();
    if (owner.active() || owner.serial() != 0
        || contenderBeginStatus.load(std::memory_order_acquire)
            != static_cast<int>(ScriptStringTransactionStatus::Success)
        || contenderFinishStatus.load(std::memory_order_acquire)
            != static_cast<int>(ScriptStringTransactionStatus::Success))
    {
        std::fputs("serialized contender did not complete cleanly\n", stderr);
        return false;
    }

    if (transaction::TryBeginScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success)
    {
        std::fputs("script-string foreign-owner test could not begin\n", stderr);
        return false;
    }
    std::atomic<bool> foreignReady{false};
    std::atomic<bool> foreignCompleted{false};
    std::atomic<bool> foreignOwns{true};
    std::atomic<int> foreignFinishStatus{
        static_cast<int>(ScriptStringTransactionStatus::Success)};
    std::thread foreign([&]() {
        foreignReady.store(true, std::memory_order_release);
        foreignOwns.store(
            transaction::OwnsScriptStringTransaction(owner),
            std::memory_order_release);
        foreignFinishStatus.store(
            static_cast<int>(
                transaction::FinishScriptStringTransaction(&owner)),
            std::memory_order_release);
        foreignCompleted.store(true, std::memory_order_release);
    });
    while (!foreignReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (foreignCompleted.load(std::memory_order_acquire))
    {
        std::fputs(
            "foreign script-string owner bypassed the serializer\n",
            stderr);
        (void)transaction::FinishScriptStringTransaction(&owner);
        foreign.join();
        return false;
    }
    if (transaction::FinishScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success)
    {
        std::fputs("foreign-owner test could not finish transaction\n", stderr);
        foreign.join();
        return false;
    }
    foreign.join();
    if (foreignOwns.load(std::memory_order_acquire)
        || foreignFinishStatus.load(std::memory_order_acquire)
            != static_cast<int>(ScriptStringTransactionStatus::InvalidToken)
        || !foreignCompleted.load(std::memory_order_acquire))
    {
        std::fputs(
            "foreign script-string token authentication did not fail closed\n",
            stderr);
        return false;
    }

    if (transaction::TryBeginScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success
        || owner.serial() == 0 || owner.serial() == firstSerial
        || transaction::FinishScriptStringTransaction(&owner)
            != ScriptStringTransactionStatus::Success)
    {
        std::fputs("script-string transaction token could not be reused\n", stderr);
        return false;
    }
    return true;
}

bool IsForwardOrEqual(const std::uint32_t before, const std::uint32_t after)
{
    return after - before <= static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max());
}

bool RunEventWaiterRound(
    SysEventHandle *const event,
    const std::uint32_t expectedSignaledWaiters,
    const char *const eventKind)
{
    constexpr std::uint32_t waiterCount = 4;
    constexpr std::uint32_t waiterTimeoutMilliseconds = 250;

    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> signaledCount{0};
    std::atomic<std::uint32_t> completedCount{0};

    std::vector<std::thread> waiters;
    waiters.reserve(waiterCount);
    for (std::uint32_t waiter = 0; waiter < waiterCount; ++waiter)
    {
        waiters.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            if (Sys_WaitForSingleObjectTimeout(
                    event,
                    waiterTimeoutMilliseconds))
            {
                signaledCount.fetch_add(1);
            }
            completedCount.fetch_add(1);
        });
    }

    while (readyCount.load(std::memory_order_acquire) != waiterCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    Sys_SetEvent(event);

    for (std::thread &waiter : waiters)
        waiter.join();

    if (completedCount.load() != waiterCount
        || signaledCount.load() != expectedSignaledWaiters)
    {
        std::fprintf(
            stderr,
            "%s event woke %u of %u waiters; expected %u\n",
            eventKind,
            signaledCount.load(),
            completedCount.load(),
            expectedSignaledWaiters);
        return false;
    }

    return true;
}

bool TestAutoResetEvents()
{
    SysEventHandle event = nullptr;
    Sys_CreateEvent(false, false, &event);
    if (!event)
    {
        std::fputs("auto-reset event creation returned null\n", stderr);
        return false;
    }

    bool valid = true;
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("initially-false auto-reset event was signaled\n", stderr);
        valid = false;
    }

    std::atomic<bool> infiniteWaitReady{false};
    std::atomic<bool> infiniteWaitCompleted{false};
    std::thread infiniteWaiter([&]() {
        infiniteWaitReady.store(true, std::memory_order_release);
        Sys_WaitForSingleObject(&event);
        infiniteWaitCompleted.store(true, std::memory_order_release);
    });
    while (!infiniteWaitReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    Sys_SetEvent(&event);
    infiniteWaiter.join();
    if (!infiniteWaitCompleted.load(std::memory_order_acquire))
    {
        std::fputs("infinite event wait did not consume its signal\n", stderr);
        valid = false;
    }

    if (!RunEventWaiterRound(&event, 1, "auto-reset")
        || !RunEventWaiterRound(&event, 1, "auto-reset"))
    {
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("auto-reset event destroy did not null its handle\n", stderr);
        valid = false;
    }

    Sys_CreateEvent(false, true, &event);
    if (!event)
    {
        std::fputs("initially-true auto-reset event creation returned null\n", stderr);
        return false;
    }
    if (!Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("initial auto-reset signal was not consumed exactly once\n", stderr);
        valid = false;
    }

    Sys_SetEvent(&event);
    if (!Sys_WaitForSingleObjectTimeout(&event, 50)
        || Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("set auto-reset signal was not consumed exactly once\n", stderr);
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("initially-true auto-reset destroy did not null its handle\n", stderr);
        valid = false;
    }

    return valid;
}

bool TestManualResetEvents()
{
    SysEventHandle event = nullptr;
    Sys_CreateEvent(true, false, &event);
    if (!event)
    {
        std::fputs("manual-reset event creation returned null\n", stderr);
        return false;
    }

    bool valid = true;
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("initially-false manual-reset event was signaled\n", stderr);
        valid = false;
    }

    if (!RunEventWaiterRound(&event, 4, "manual-reset"))
        valid = false;

    if (!Sys_WaitForSingleObjectTimeout(&event, 0)
        || !Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("manual-reset event did not retain its signaled state\n", stderr);
        valid = false;
    }

    Sys_ResetEvent(&event);
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("manual-reset event remained signaled after reset\n", stderr);
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("manual-reset event destroy did not null its handle\n", stderr);
        valid = false;
    }

    return valid;
}

struct BlockingThreadState
{
    SysThreadHandle *self;
    SysThreadHandle parent;
    SysEventHandle entered;
    SysEventHandle release;
    std::atomic<bool> selfIdentity{false};
    std::atomic<bool> parentIdentity{false};
    std::uint32_t visibleValue{0};
};

void KISAK_CDECL BlockingThreadEntry(void *const userData)
{
    BlockingThreadState *const state =
        static_cast<BlockingThreadState *>(userData);
    state->selfIdentity.store(
        Sys_ThreadIsCurrent(*state->self),
        std::memory_order_release);
    state->parentIdentity.store(
        Sys_ThreadIsCurrent(state->parent),
        std::memory_order_release);
    state->visibleValue = 1;
    Sys_SetEvent(&state->entered);
    Sys_WaitForSingleObject(&state->release);
    state->visibleValue = 2;
}

bool TestThreadCaptureAndLifecycle()
{
    SysThreadHandle parent = nullptr;
    if (!Sys_ThreadCaptureCurrent("platform-test-main", &parent)
        || !parent
        || !Sys_ThreadIsCurrent(parent))
    {
        std::fputs("current-thread capture or identity failed\n", stderr);
        return false;
    }

    SysThreadHandle child = nullptr;
    BlockingThreadState state{&child, parent, nullptr, nullptr};
    Sys_CreateEvent(true, false, &state.entered);
    Sys_CreateEvent(true, false, &state.release);
    if (!Sys_ThreadCreateSuspended(
            BlockingThreadEntry,
            &state,
            "platform-test-blocked",
            &child)
        || !child)
    {
        std::fputs("suspended thread creation failed\n", stderr);
        return false;
    }

    if (Sys_WaitForSingleObjectTimeout(&state.entered, 0)
        || Sys_ThreadIsCurrent(child))
    {
        std::fputs("suspended thread ran before start\n", stderr);
        return false;
    }

    Sys_ThreadStart(child);
    if (!Sys_WaitForSingleObjectTimeout(&state.entered, 2'000))
    {
        std::fputs("started thread did not enter its callback\n", stderr);
        return false;
    }
    if (!state.selfIdentity.load(std::memory_order_acquire)
        || state.parentIdentity.load(std::memory_order_acquire)
        || Sys_ThreadIsCurrent(child))
    {
        std::fputs("parent/child thread identity was not isolated\n", stderr);
        return false;
    }
    if (Sys_ThreadJoinTimeout(child, 0)
        || Sys_ThreadJoinTimeout(child, 5))
    {
        std::fputs("blocked thread joined before its release event\n", stderr);
        return false;
    }

    Sys_SetEvent(&state.release);
    if (!Sys_ThreadJoinTimeout(child, 2'000))
    {
        std::fputs("released thread did not join within the bound\n", stderr);
        return false;
    }
    if (state.visibleValue != 2)
    {
        std::fputs("thread completion did not publish callback writes\n", stderr);
        return false;
    }

    Sys_ThreadDestroy(&child);
    if (child)
    {
        std::fputs("joined thread destroy did not null its handle\n", stderr);
        return false;
    }
    Sys_DestroyEvent(&state.entered);
    Sys_DestroyEvent(&state.release);

    Sys_ThreadDestroy(&parent);
    if (parent)
    {
        std::fputs("captured thread destroy did not null its handle\n", stderr);
        return false;
    }

    return true;
}

bool IsDefinedThreadPolicyStatus(const SysThreadPolicyStatus status)
{
    switch (status)
    {
    case SysThreadPolicyStatus::Applied:
    case SysThreadPolicyStatus::Unsupported:
    case SysThreadPolicyStatus::PermissionDenied:
    case SysThreadPolicyStatus::Unavailable:
        return true;
    }

    return false;
}

bool TestThreadPolicyServices()
{
    SysThreadHandle current = nullptr;
    if (!Sys_ThreadCaptureCurrent("platform-test-thread-policy", &current)
        || !current)
    {
        std::fputs("thread-policy current-thread capture failed\n", stderr);
        return false;
    }

    bool valid = true;
    const std::uint32_t eligibleProcessorCount =
        Sys_ThreadGetEligibleProcessorCount();
    if (eligibleProcessorCount < 1)
    {
        std::fputs("eligible processor count was zero\n", stderr);
        valid = false;
    }

    const SysThreadPolicyStatus normalStatus = Sys_ThreadSetPriority(
        current,
        SysThreadPriority::Normal);
    if (normalStatus != SysThreadPolicyStatus::Applied)
    {
        std::fprintf(
            stderr,
            "normal thread priority was not applied (status %u)\n",
            static_cast<unsigned int>(normalStatus));
        valid = false;
    }

    // Above-normal may legitimately require privileges. Restore Normal after
    // probing both hints so later tests do not inherit a scheduling change.
    const SysThreadPolicyStatus aboveStatus = Sys_ThreadSetPriority(
        current,
        SysThreadPriority::AboveNormal);
    const SysThreadPolicyStatus belowStatus = Sys_ThreadSetPriority(
        current,
        SysThreadPriority::BelowNormal);
    if (!IsDefinedThreadPolicyStatus(aboveStatus)
        || !IsDefinedThreadPolicyStatus(belowStatus))
    {
        std::fprintf(
            stderr,
            "thread priority returned an undefined status (above %u, below %u)\n",
            static_cast<unsigned int>(aboveStatus),
            static_cast<unsigned int>(belowStatus));
        valid = false;
    }
    if (Sys_ThreadSetPriority(current, SysThreadPriority::Normal)
        != SysThreadPolicyStatus::Applied)
    {
        std::fputs("normal thread priority could not be restored\n", stderr);
        valid = false;
    }

    const SysThreadPolicyStatus outOfRangePinStatus =
        Sys_ThreadPinToEligibleProcessor(current, eligibleProcessorCount);
    if (outOfRangePinStatus != SysThreadPolicyStatus::Unavailable)
    {
        std::fprintf(
            stderr,
            "out-of-range processor ordinal was not unavailable (status %u)\n",
            static_cast<unsigned int>(outOfRangePinStatus));
        valid = false;
    }

    const SysThreadPolicyStatus firstProcessorPinStatus =
        Sys_ThreadPinToEligibleProcessor(current, 0);
#if defined(__APPLE__)
    if (firstProcessorPinStatus != SysThreadPolicyStatus::Unsupported)
    {
        std::fprintf(
            stderr,
            "macOS processor pinning was not reported unsupported (status %u)\n",
            static_cast<unsigned int>(firstProcessorPinStatus));
        valid = false;
    }
#elif defined(_WIN32) || defined(__linux__)
    if (firstProcessorPinStatus != SysThreadPolicyStatus::Applied)
    {
        std::fprintf(
            stderr,
            "processor ordinal zero was not applied (status %u)\n",
            static_cast<unsigned int>(firstProcessorPinStatus));
        valid = false;
    }
#else
#error "Thread policy runtime contract requires Windows, Linux, or macOS"
#endif

    const SysThreadPolicyStatus clearAffinityStatus =
        Sys_ThreadClearAffinity(current);
    if (clearAffinityStatus != SysThreadPolicyStatus::Applied
        && clearAffinityStatus != SysThreadPolicyStatus::Unsupported)
    {
        std::fprintf(
            stderr,
            "clearing thread affinity returned status %u\n",
            static_cast<unsigned int>(clearAffinityStatus));
        valid = false;
    }

    // The only crash-freeze call made by this test targets the caller. It must
    // report that identity instead of ever suspending the live test thread.
    const SysThreadCrashFreezeStatus crashFreezeStatus =
        Sys_ThreadForceSuspendForCrash(current);
    if (crashFreezeStatus != SysThreadCrashFreezeStatus::CurrentThread)
    {
        std::fprintf(
            stderr,
            "current-thread crash freeze was not rejected safely (status %u)\n",
            static_cast<unsigned int>(crashFreezeStatus));
        valid = false;
    }

    Sys_ThreadDestroy(&current);
    if (current)
    {
        std::fputs("thread-policy capture destroy retained its handle\n", stderr);
        valid = false;
    }

    return valid;
}

struct CompletingThreadState
{
    SysEventHandle completed;
    std::uint32_t visibleValue;
};

void KISAK_CDECL CompletingThreadEntry(void *const userData)
{
    CompletingThreadState *const state =
        static_cast<CompletingThreadState *>(userData);
    state->visibleValue = 0xC0DEF00Du;
    Sys_SetEvent(&state->completed);
}

bool TestKnownCompleteInfiniteJoin()
{
    CompletingThreadState state{nullptr, 0};
    Sys_CreateEvent(true, false, &state.completed);

    SysThreadHandle thread = nullptr;
    if (!Sys_ThreadCreateSuspended(
            CompletingThreadEntry,
            &state,
            "platform-test-complete",
            &thread))
    {
        std::fputs("known-complete thread creation failed\n", stderr);
        return false;
    }

    Sys_ThreadStart(thread);
    if (!Sys_WaitForSingleObjectTimeout(&state.completed, 2'000))
    {
        std::fputs("known-complete thread did not signal completion\n", stderr);
        return false;
    }
    Sys_ThreadJoin(thread);
    if (state.visibleValue != 0xC0DEF00Du)
    {
        std::fputs("infinite join did not publish callback writes\n", stderr);
        return false;
    }

    Sys_ThreadDestroy(&thread);
    Sys_DestroyEvent(&state.completed);
    if (thread || state.completed)
    {
        std::fputs("known-complete lifecycle did not null its handles\n", stderr);
        return false;
    }
    return true;
}

constexpr std::uint32_t multipleThreadCount = 4;

struct MultipleThreadGroup;
struct MultipleThreadState
{
    MultipleThreadGroup *group;
    std::uint32_t index;
};

struct MultipleThreadGroup
{
    std::array<SysThreadHandle, multipleThreadCount> handles{};
    std::array<MultipleThreadState, multipleThreadCount> states{};
    SysEventHandle allEntered{nullptr};
    SysEventHandle release{nullptr};
    std::atomic<std::uint32_t> enteredCount{0};
    std::atomic<std::uint32_t> identityFailures{0};
};

void KISAK_CDECL MultipleThreadEntry(void *const userData)
{
    MultipleThreadState *const state =
        static_cast<MultipleThreadState *>(userData);
    MultipleThreadGroup *const group = state->group;

    for (std::uint32_t index = 0; index < multipleThreadCount; ++index)
    {
        const bool expectedCurrent = index == state->index;
        if (Sys_ThreadIsCurrent(group->handles[index]) != expectedCurrent)
            group->identityFailures.fetch_add(1);
    }

    if (group->enteredCount.fetch_add(1) + 1 == multipleThreadCount)
        Sys_SetEvent(&group->allEntered);
    Sys_WaitForSingleObject(&group->release);
}

bool TestMultipleThreadIdentity()
{
    MultipleThreadGroup group;
    Sys_CreateEvent(true, false, &group.allEntered);
    Sys_CreateEvent(true, false, &group.release);

    for (std::uint32_t index = 0; index < multipleThreadCount; ++index)
    {
        group.states[index] = MultipleThreadState{&group, index};
        if (!Sys_ThreadCreateSuspended(
                MultipleThreadEntry,
                &group.states[index],
                "platform-test-multiple",
                &group.handles[index]))
        {
            std::fputs("multiple-thread creation failed\n", stderr);
            return false;
        }
        if (Sys_ThreadIsCurrent(group.handles[index]))
        {
            std::fputs("child handle matched the creating thread\n", stderr);
            return false;
        }
        for (std::uint32_t prior = 0; prior < index; ++prior)
        {
            if (group.handles[prior] == group.handles[index])
            {
                std::fputs("simultaneous threads shared an opaque handle\n", stderr);
                return false;
            }
        }
    }

    for (SysThreadHandle thread : group.handles)
        Sys_ThreadStart(thread);

    const bool allEntered =
        Sys_WaitForSingleObjectTimeout(&group.allEntered, 2'000);
    Sys_SetEvent(&group.release);

    bool allJoined = true;
    for (SysThreadHandle thread : group.handles)
    {
        if (!Sys_ThreadJoinTimeout(thread, 2'000))
            allJoined = false;
    }
    if (!allEntered
        || !allJoined
        || group.enteredCount.load() != multipleThreadCount
        || group.identityFailures.load() != 0)
    {
        std::fputs("multiple-thread identity/lifecycle contract failed\n", stderr);
        return false;
    }

    for (SysThreadHandle &thread : group.handles)
        Sys_ThreadDestroy(&thread);
    Sys_DestroyEvent(&group.allEntered);
    Sys_DestroyEvent(&group.release);
    for (SysThreadHandle thread : group.handles)
    {
        if (thread)
        {
            std::fputs("multiple-thread destroy did not null every handle\n", stderr);
            return false;
        }
    }
    return true;
}

void KISAK_CDECL RepeatedThreadEntry(void *const userData)
{
    std::atomic<std::uint32_t> *const completionCount =
        static_cast<std::atomic<std::uint32_t> *>(userData);
    completionCount->fetch_add(1);
}

bool TestRepeatedThreadLifecycle()
{
    constexpr std::uint32_t lifecycleCount = 32;
    std::atomic<std::uint32_t> completionCount{0};

    for (std::uint32_t iteration = 0; iteration < lifecycleCount; ++iteration)
    {
        SysThreadHandle thread = nullptr;
        if (!Sys_ThreadCreateSuspended(
                RepeatedThreadEntry,
                &completionCount,
                "platform-test-repeat",
                &thread))
        {
            std::fputs("repeated thread creation failed\n", stderr);
            return false;
        }
        Sys_ThreadStart(thread);
        if (!Sys_ThreadJoinTimeout(thread, 2'000))
        {
            std::fputs("repeated thread join exceeded its bound\n", stderr);
            return false;
        }
        Sys_ThreadDestroy(&thread);
        if (thread)
        {
            std::fputs("repeated thread destroy retained its handle\n", stderr);
            return false;
        }
    }

    if (completionCount.load() != lifecycleCount)
    {
        std::fputs("repeated thread callbacks did not run exactly once\n", stderr);
        return false;
    }
    return true;
}

struct WorkerGateThreadState
{
    SysWorkerGateHandle gate{nullptr};
    SysThreadHandle thread{nullptr};
    SysEventHandle wakeEvent{nullptr};
    SysEventHandle enteredEvent{nullptr};
    SysEventHandle taskStartedEvent{nullptr};
    SysEventHandle taskReleaseEvent{nullptr};
    SysEventHandle taskCompletedEvent{nullptr};
    std::atomic<bool> stop{false};
    std::atomic<bool> taskPending{false};
    std::atomic<bool> blockTask{false};
    std::atomic<std::uint32_t> completedTasks{0};
};

void KISAK_CDECL WorkerGateThreadEntry(void *const userData)
{
    WorkerGateThreadState *const state =
        static_cast<WorkerGateThreadState *>(userData);
    Sys_SetEvent(&state->enteredEvent);

    for (;;)
    {
        Sys_WorkerGatePausePoint(state->gate);
        Sys_WaitForSingleObject(&state->wakeEvent);
        Sys_WorkerGatePausePoint(state->gate);

        if (state->stop.load(std::memory_order_acquire))
            return;
        if (!state->taskPending.exchange(false, std::memory_order_acq_rel))
            continue;

        Sys_SetEvent(&state->taskStartedEvent);
        if (state->blockTask.load(std::memory_order_acquire))
            Sys_WaitForSingleObject(&state->taskReleaseEvent);

        state->completedTasks.fetch_add(1, std::memory_order_release);
        Sys_SetEvent(&state->taskCompletedEvent);
    }
}

bool CreateWorkerGateThread(
    WorkerGateThreadState *const state,
    const char *const name)
{
    Sys_WorkerGateCreate(&state->gate);
    Sys_CreateEvent(false, false, &state->wakeEvent);
    Sys_CreateEvent(true, false, &state->enteredEvent);
    Sys_CreateEvent(false, false, &state->taskStartedEvent);
    Sys_CreateEvent(false, false, &state->taskReleaseEvent);
    Sys_CreateEvent(false, false, &state->taskCompletedEvent);
    return Sys_ThreadCreateSuspended(
        WorkerGateThreadEntry,
        state,
        name,
        &state->thread);
}

bool StartWorkerGateThread(WorkerGateThreadState *const state)
{
    if (!Sys_WorkerGateActivate(state->gate))
        return false;
    Sys_ThreadStart(state->thread);
    return Sys_WaitForSingleObjectTimeout(&state->enteredEvent, 2'000);
}

bool StopWorkerGateThread(WorkerGateThreadState *const state)
{
    state->stop.store(true, std::memory_order_release);
    if (Sys_WorkerGateActivate(state->gate))
        return false;
    Sys_SetEvent(&state->wakeEvent);
    if (!Sys_ThreadJoinTimeout(state->thread, 2'000))
        return false;

    Sys_ThreadDestroy(&state->thread);
    Sys_WorkerGateDestroy(&state->gate);
    Sys_DestroyEvent(&state->wakeEvent);
    Sys_DestroyEvent(&state->enteredEvent);
    Sys_DestroyEvent(&state->taskStartedEvent);
    Sys_DestroyEvent(&state->taskReleaseEvent);
    Sys_DestroyEvent(&state->taskCompletedEvent);
    return !state->thread
        && !state->gate
        && !state->wakeEvent
        && !state->enteredEvent
        && !state->taskStartedEvent
        && !state->taskReleaseEvent
        && !state->taskCompletedEvent;
}

bool PauseWorkerGateThread(WorkerGateThreadState *const state)
{
    if (!Sys_WorkerGateRequestPause(state->gate))
        return false;
    Sys_SetEvent(&state->wakeEvent);
    Sys_WorkerGateWaitPaused(state->gate);
    return !Sys_WorkerGateIsActive(state->gate);
}

bool QueueWorkerGateTask(WorkerGateThreadState *const state)
{
    state->taskPending.store(true, std::memory_order_release);
    Sys_SetEvent(&state->wakeEvent);
    return Sys_WaitForSingleObjectTimeout(&state->taskStartedEvent, 2'000)
        && Sys_WaitForSingleObjectTimeout(&state->taskCompletedEvent, 2'000);
}

bool TestCooperativeWorkerGate()
{
    WorkerGateThreadState first;
    if (!CreateWorkerGateThread(&first, "platform-test-worker-gate"))
    {
        std::fputs("worker-gate thread creation failed\n", stderr);
        return false;
    }

    if (Sys_WorkerGateIsActive(first.gate)
        || Sys_WorkerGateRequestPause(first.gate))
    {
        std::fputs("new worker gate did not start inactive\n", stderr);
        return false;
    }
    Sys_WorkerGateWaitPaused(first.gate);
    if (!StartWorkerGateThread(&first)
        || Sys_WorkerGateActivate(first.gate))
    {
        std::fputs("worker gate did not perform a one-shot initial start\n", stderr);
        return false;
    }

    if (!PauseWorkerGateThread(&first)
        || Sys_WorkerGateRequestPause(first.gate))
    {
        std::fputs("waiting worker did not acknowledge its pause\n", stderr);
        return false;
    }
    Sys_WorkerGateWaitPaused(first.gate);
    const std::uint32_t parkedCount = first.completedTasks.load();

    first.taskPending.store(true, std::memory_order_release);
    Sys_SetEvent(&first.wakeEvent);
    if (Sys_WaitForSingleObjectTimeout(&first.taskStartedEvent, 5)
        || first.completedTasks.load() != parkedCount)
    {
        std::fputs("parked worker consumed queued work\n", stderr);
        return false;
    }
    if (Sys_WorkerGateActivate(first.gate)
        || !Sys_WaitForSingleObjectTimeout(&first.taskStartedEvent, 2'000)
        || !Sys_WaitForSingleObjectTimeout(&first.taskCompletedEvent, 2'000)
        || first.completedTasks.load() != parkedCount + 1)
    {
        std::fputs("resumed worker did not consume queued work\n", stderr);
        return false;
    }

    first.blockTask.store(true, std::memory_order_release);
    first.taskPending.store(true, std::memory_order_release);
    Sys_SetEvent(&first.wakeEvent);
    if (!Sys_WaitForSingleObjectTimeout(&first.taskStartedEvent, 2'000)
        || !Sys_WorkerGateRequestPause(first.gate))
    {
        std::fputs("in-flight worker task did not start pause protocol\n", stderr);
        return false;
    }
    Sys_SetEvent(&first.wakeEvent);

    SysEventHandle pauseReturnedEvent = nullptr;
    Sys_CreateEvent(true, false, &pauseReturnedEvent);
    std::thread pauseWaiter([&]() {
        Sys_WorkerGateWaitPaused(first.gate);
        Sys_SetEvent(&pauseReturnedEvent);
    });
    const bool returnedBeforeSafePoint =
        Sys_WaitForSingleObjectTimeout(&pauseReturnedEvent, 5);
    Sys_SetEvent(&first.taskReleaseEvent);
    const bool returnedAfterSafePoint =
        Sys_WaitForSingleObjectTimeout(&pauseReturnedEvent, 2'000);
    pauseWaiter.join();
    Sys_DestroyEvent(&pauseReturnedEvent);
    first.blockTask.store(false, std::memory_order_release);
    if (returnedBeforeSafePoint
        || !returnedAfterSafePoint
        || !Sys_WaitForSingleObjectTimeout(&first.taskCompletedEvent, 2'000))
    {
        std::fputs("worker pause did not wait for an in-flight safe point\n", stderr);
        return false;
    }

    constexpr std::uint32_t rapidPauseCycles = 128;
    for (std::uint32_t cycle = 0; cycle < rapidPauseCycles; ++cycle)
    {
        if (Sys_WorkerGateActivate(first.gate)
            || !Sys_WorkerGateRequestPause(first.gate))
        {
            std::fputs("rapid worker-gate transition failed\n", stderr);
            return false;
        }
        Sys_SetEvent(&first.wakeEvent);
        Sys_WorkerGateWaitPaused(first.gate);
        if (Sys_WorkerGateIsActive(first.gate)
            || Sys_WorkerGateRequestPause(first.gate))
        {
            std::fputs("rapid worker-gate pause was not stable\n", stderr);
            return false;
        }
    }

    WorkerGateThreadState second;
    if (!CreateWorkerGateThread(&second, "platform-test-worker-gate-2")
        || !StartWorkerGateThread(&second)
        || !QueueWorkerGateTask(&second)
        || second.completedTasks.load() != 1
        || Sys_WorkerGateIsActive(first.gate))
    {
        std::fputs("worker gates did not operate independently\n", stderr);
        return false;
    }

    if (!StopWorkerGateThread(&second)
        || !StopWorkerGateThread(&first))
    {
        std::fputs("worker-gate lifecycle cleanup failed\n", stderr);
        return false;
    }
    return true;
}

bool TestConcurrentInitialization()
{
    constexpr std::uint32_t threadCount = 8;
    constexpr std::uint32_t initializationWindowMilliseconds = 5'000;
    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> failureCount{0};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::uint32_t worker = 0; worker < threadCount; ++worker)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            const std::uint32_t before = Sys_Milliseconds();
            Sys_EnterCriticalSection(CRITSECT_CONSOLE);
            Sys_LeaveCriticalSection(CRITSECT_CONSOLE);
            const std::uint32_t after = Sys_Milliseconds();
            if (before > initializationWindowMilliseconds
                || !IsForwardOrEqual(before, after))
                failureCount.fetch_add(1);
        });
    }

    while (readyCount.load(std::memory_order_acquire) != threadCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    if (failureCount.load() != 0)
    {
        std::fputs("concurrent platform-service initialization failed\n", stderr);
        return false;
    }
    return true;
}

bool TestTimeServices()
{
    const std::uint32_t rawBeforeYield = Sys_MillisecondsRaw();
    const std::uint32_t relativeBeforeYield = Sys_Milliseconds();
    Sys_Sleep(0);
    const std::uint32_t rawAfterYield = Sys_MillisecondsRaw();
    const std::uint32_t relativeAfterYield = Sys_Milliseconds();

    if (!IsForwardOrEqual(rawBeforeYield, rawAfterYield)
        || !IsForwardOrEqual(relativeBeforeYield, relativeAfterYield))
    {
        std::fputs("platform time moved backwards across Sys_Sleep(0)\n", stderr);
        return false;
    }

    constexpr std::uint32_t shortSleepMilliseconds = 15;
    const auto steadyBeforeSleep = std::chrono::steady_clock::now();
    const std::uint32_t rawBeforeSleep = Sys_MillisecondsRaw();
    const std::uint32_t relativeBeforeSleep = Sys_Milliseconds();
    Sys_Sleep(shortSleepMilliseconds);
    const std::uint32_t rawAfterSleep = Sys_MillisecondsRaw();
    const std::uint32_t relativeAfterSleep = Sys_Milliseconds();
    const auto steadyAfterSleep = std::chrono::steady_clock::now();

    if (!IsForwardOrEqual(rawBeforeSleep, rawAfterSleep)
        || !IsForwardOrEqual(relativeBeforeSleep, relativeAfterSleep))
    {
        std::fputs("platform time moved backwards across a short sleep\n", stderr);
        return false;
    }
    if (rawAfterSleep - rawBeforeSleep == 0
        || relativeAfterSleep - relativeBeforeSleep == 0)
    {
        std::fputs("platform clocks did not advance across a short sleep\n", stderr);
        return false;
    }

    const auto steadyElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        steadyAfterSleep - steadyBeforeSleep);
    if (steadyElapsed.count() < 1)
    {
        std::fputs("Sys_Sleep returned without an observable short delay\n", stderr);
        return false;
    }

    return true;
}

template <typename LockFunction, typename UnlockFunction>
bool TestContendedExclusion(
    const char *const serviceName,
    LockFunction lock,
    UnlockFunction unlock)
{
    constexpr std::uint32_t threadCount = 4;
    constexpr std::uint32_t iterationsPerThread = 256;

    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> insideCount{0};
    std::atomic<std::uint32_t> completedCount{0};
    std::atomic<std::uint32_t> violationCount{0};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::uint32_t worker = 0; worker < threadCount; ++worker)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < iterationsPerThread;
                 ++iteration)
            {
                lock();
                if (insideCount.fetch_add(1) != 0)
                    violationCount.fetch_add(1);

                if ((iteration & 31U) == 0)
                    std::this_thread::yield();

                completedCount.fetch_add(1);
                if (insideCount.fetch_sub(1) != 1)
                    violationCount.fetch_add(1);
                unlock();
            }
        });
    }

    while (readyCount.load(std::memory_order_acquire) != threadCount)
        Sys_Sleep(0);
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    const std::uint32_t expectedCount = threadCount * iterationsPerThread;
    if (completedCount.load() != expectedCount
        || insideCount.load() != 0
        || violationCount.load() != 0)
    {
        std::fprintf(stderr, "%s did not provide mutual exclusion\n", serviceName);
        return false;
    }

    return true;
}

bool TestCriticalSections()
{
    Sys_InitializeCriticalSections();
    Sys_InitializeCriticalSections();

    Sys_EnterCriticalSection(CRITSECT_CONSOLE);
    Sys_EnterCriticalSection(CRITSECT_CONSOLE);
    Sys_LeaveCriticalSection(CRITSECT_CONSOLE);
    Sys_LeaveCriticalSection(CRITSECT_CONSOLE);

    return TestContendedExclusion(
        "recursive critical section",
        []() { Sys_EnterCriticalSection(CRITSECT_CONSOLE); },
        []() { Sys_LeaveCriticalSection(CRITSECT_CONSOLE); });
}

bool TestFastCriticalSection()
{
    FastCriticalSection criticalSection{};
    if (Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection started write-locked\n", stderr);
        return false;
    }

    Sys_LockWrite(&criticalSection);
    if (!Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection did not publish its writer state\n", stderr);
        return false;
    }
    Sys_UnlockWrite(&criticalSection);
    if (Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection retained its writer state after unlock\n", stderr);
        return false;
    }

    const bool excluded = TestContendedExclusion(
        "FastCriticalSection",
        [&criticalSection]() { Sys_LockWrite(&criticalSection); },
        [&criticalSection]() { Sys_UnlockWrite(&criticalSection); });

    if (criticalSection.readCount != 0 || criticalSection.writeCount != 0)
    {
        std::fputs("FastCriticalSection counters were not balanced\n", stderr);
        return false;
    }

    return excluded;
}

bool TestFastCriticalSectionReadersAndWriters()
{
    constexpr std::uint32_t readerCount = 4;
    constexpr std::uint32_t writerCount = 2;
    constexpr std::uint32_t readerIterations = 512;
    constexpr std::uint32_t writerIterations = 256;
    constexpr std::uint64_t payloadMask = 0xA5A55A5AF0F00F0FULL;

    struct SharedPayload
    {
        std::uint64_t generation;
        std::uint64_t check;
    };

    FastCriticalSection criticalSection{};
    SharedPayload payload{0, payloadMask};
    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> firstReadersInside{0};
    std::atomic<std::uint32_t> activeReaders{0};
    std::atomic<std::uint32_t> activeWriters{0};
    std::atomic<std::uint32_t> readerOverlapViolations{0};
    std::atomic<std::uint32_t> writerOverlapViolations{0};
    std::atomic<std::uint32_t> writerStateViolations{0};
    std::atomic<std::uint32_t> payloadViolations{0};

    std::vector<std::thread> workers;
    workers.reserve(readerCount + writerCount);

    for (std::uint32_t reader = 0; reader < readerCount; ++reader)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < readerIterations;
                 ++iteration)
            {
                Sys_LockRead(&criticalSection);
                activeReaders.fetch_add(1);
                if (activeWriters.load() != 0)
                    readerOverlapViolations.fetch_add(1);

                if (iteration == 0)
                {
                    firstReadersInside.fetch_add(1, std::memory_order_release);
                    while (firstReadersInside.load(std::memory_order_acquire) != readerCount)
                        std::this_thread::yield();
                }

                const std::uint64_t generation = payload.generation;
                std::this_thread::yield();
                if (payload.check != (generation ^ payloadMask))
                    payloadViolations.fetch_add(1);

                activeReaders.fetch_sub(1);
                Sys_UnlockRead(&criticalSection);
            }
        });
    }

    for (std::uint32_t writer = 0; writer < writerCount; ++writer)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < writerIterations;
                 ++iteration)
            {
                Sys_LockWrite(&criticalSection);
                if (!Sys_IsWriteLocked(&criticalSection))
                    writerStateViolations.fetch_add(1);
                if (activeWriters.fetch_add(1) != 0 || activeReaders.load() != 0)
                    writerOverlapViolations.fetch_add(1);

                const std::uint64_t nextGeneration = payload.generation + 1;
                payload.generation = nextGeneration;
                std::this_thread::yield();
                payload.check = nextGeneration ^ payloadMask;

                if (activeWriters.fetch_sub(1) != 1)
                    writerOverlapViolations.fetch_add(1);
                Sys_UnlockWrite(&criticalSection);
            }
        });
    }

    while (readyCount.load(std::memory_order_acquire) != readerCount + writerCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    const std::uint64_t expectedGeneration = writerCount * writerIterations;
    if (firstReadersInside.load() != readerCount
        || activeReaders.load() != 0
        || activeWriters.load() != 0
        || readerOverlapViolations.load() != 0
        || writerOverlapViolations.load() != 0
        || writerStateViolations.load() != 0
        || payloadViolations.load() != 0
        || payload.generation != expectedGeneration
        || payload.check != (expectedGeneration ^ payloadMask)
        || criticalSection.readCount != 0
        || criticalSection.writeCount != 0
        || Sys_IsWriteLocked(&criticalSection))
    {
        std::fprintf(
            stderr,
            "FastCriticalSection reader/writer contract failed: "
            "firstReaders=%u activeReaders=%u activeWriters=%u "
            "readerOverlap=%u writerOverlap=%u writerState=%u payload=%u "
            "generation=%llu expected=%llu check=%llu readCount=%u writeCount=%u\n",
            firstReadersInside.load(),
            activeReaders.load(),
            activeWriters.load(),
            readerOverlapViolations.load(),
            writerOverlapViolations.load(),
            writerStateViolations.load(),
            payloadViolations.load(),
            static_cast<unsigned long long>(payload.generation),
            static_cast<unsigned long long>(expectedGeneration),
            static_cast<unsigned long long>(payload.check),
            criticalSection.readCount,
            criticalSection.writeCount);
        return false;
    }

    return true;
}
}

int main()
{
    if (!TestScriptStringTransactionSerializer())
        return 1;
    if (!RunDatabaseScriptStringAdapterTests())
        return 1;
    if (!TestAutoResetEvents())
        return 1;
    if (!TestManualResetEvents())
        return 1;
    if (!TestThreadCaptureAndLifecycle())
        return 1;
    if (!TestThreadPolicyServices())
        return 1;
    if (!TestKnownCompleteInfiniteJoin())
        return 1;
    if (!TestMultipleThreadIdentity())
        return 1;
    if (!TestRepeatedThreadLifecycle())
        return 1;
    if (!TestCooperativeWorkerGate())
        return 1;
    if (!TestConcurrentInitialization())
        return 1;
    if (!TestTimeServices())
        return 1;
    if (!TestCriticalSections())
        return 1;
    if (!TestFastCriticalSection())
        return 1;
    if (!TestFastCriticalSectionReadersAndWriters())
        return 1;

    std::puts("platform service runtime contracts passed");
    return 0;
}

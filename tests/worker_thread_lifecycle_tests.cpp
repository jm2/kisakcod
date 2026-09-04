// Worker-thread shutdown and safe-reinitialization lifecycle tests (M8
// embed/restart release blocker). This target links the production
// src/qcommon/threads.cpp in its headless composition and drives the real
// Sys_SpawnWorkerThread / Sys_SetWorkerThreadActive / Sys_ShutdownWorkerThread
// sequence against real native threads.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include <qcommon/threads.h>
#include <universal/q_shared.h>

// The standalone target does not link the engine's assert/reporting graph.
// Preserve debug assertion behavior with a fatal handler so Debug
// configurations remain independently linkable.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

// threads.cpp externals that live in engine translation units outside this
// target's composition. The test never calls the scaled-sleep or thread-lock
// paths, but the symbols must resolve; the dvar stub keeps the lock switch
// disabled.
void KISAK_CDECL Com_InitThreadData(int threadContext)
{
    (void)threadContext;
}

// threads.cpp bootstraps profiler state per thread; the standalone target
// compiles the non-Tracy profile.h declarations without profile.cpp.
void KISAK_CDECL Profile_InitContext(int profileContext)
{
    (void)profileContext;
}

float com_timescaleValue = 1.0f;

static dvar_s s_lockThreadsDvar{};
const dvar_t *sys_lockThreads = &s_lockThreadsDvar;

namespace
{
std::atomic<bool> g_renderThreadStop{false};
SysEventHandle g_renderThreadWakeEvent = nullptr;
SysEventHandle g_renderThreadExitedEvent = nullptr;

std::atomic<std::uint32_t> g_workerProcessedCommands[2]{};
std::atomic<bool> g_workerObservedLatch[2]{};

void KISAK_CDECL TestRenderThreadEntry(std::uint32_t threadContext)
{
    (void)threadContext;
    while (!g_renderThreadStop.load(std::memory_order_acquire))
        Sys_WaitForSingleObjectTimeout(&g_renderThreadWakeEvent, 1u);
    // Acknowledge exit so the controller never destroys the wake event while
    // this thread is still parked inside a wait on it (Sys_DestroyEvent
    // fail-fasts on active waiters).
    Sys_SetEvent(&g_renderThreadExitedEvent);
}

// Mirrors the production R_WorkerThread loop shape: the latch is the loop
// condition, each cycle parks at pause points and drains the worker command
// event. Work accounting is a counter; the shutdown join bounds the wait.
void KISAK_CDECL TestWorkerThreadEntry(std::uint32_t threadContext)
{
    const ThreadContext_t context = static_cast<ThreadContext_t>(threadContext);
    const std::size_t slot =
        static_cast<std::size_t>(context - THREAD_CONTEXT_WORKER0);

    while (!Sys_WorkerThreadShutdownRequested(context))
    {
        Sys_WorkerThreadPausePoint(context);
        {
            Sys_WaitForWorkerCmd();
            Sys_WorkerThreadPausePoint(context);
        }
        g_workerProcessedCommands[slot].fetch_add(1, std::memory_order_release);
    }
    g_workerObservedLatch[slot].store(true, std::memory_order_release);
}

bool WaitForWorkerProgress(std::atomic<std::uint32_t> &counter, std::uint32_t target)
{
    for (int wait = 0; wait < 2000; ++wait)
    {
        if (counter.load(std::memory_order_acquire) >= target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return counter.load(std::memory_order_acquire) >= target;
}

bool SpawnActivateAndDrainWorker(std::uint32_t processedTarget, const char *label)
{
    if (!Sys_SpawnWorkerThread(TestWorkerThreadEntry, 0))
    {
        std::fputs("worker slot 0 spawn failed\n", stderr);
        return false;
    }
    Sys_SetWorkerThreadActive(0, true);
    Sys_SetWorkerCmdEvent();
    if (!WaitForWorkerProgress(g_workerProcessedCommands[0], processedTarget))
    {
        std::fprintf(stderr, "%s did not drain its command event\n", label);
        return false;
    }
    return true;
}

bool ShutdownWorkerSlotAndVerifyLatch(const char *label)
{
    if (!Sys_ShutdownWorkerThread(0))
    {
        std::fprintf(stderr, "%s shutdown failed\n", label);
        return false;
    }
    if (!g_workerObservedLatch[0].load(std::memory_order_acquire))
    {
        std::fprintf(stderr, "%s exited without observing its shutdown latch\n", label);
        return false;
    }
    return true;
}

bool TestShutdownRejectsInvalidAndEmptySlots()
{
    if (Sys_ShutdownWorkerThread(2)
        || Sys_ShutdownWorkerThread(0xffffffffu))
    {
        std::fputs("shutdown accepted an invalid worker index\n", stderr);
        return false;
    }
    if (Sys_ShutdownWorkerThread(0) || Sys_ShutdownWorkerThread(1))
    {
        std::fputs("shutdown accepted an unspawned worker slot\n", stderr);
        return false;
    }
    if (Sys_WorkerThreadShutdownRequested(THREAD_CONTEXT_WORKER0))
    {
        std::fputs("unspawned worker context reported a shutdown request\n", stderr);
        return false;
    }
    if (Sys_WorkerThreadShutdownRequested(THREAD_CONTEXT_MAIN))
    {
        std::fputs("main thread context reported a worker shutdown request\n", stderr);
        return false;
    }
    return true;
}

bool TestActiveWorkerShutdownAndRespawn()
{
    // Spawn, activate, let it drain commands, then latch and join. The
    // shutdown join has no test-side bound: a worker that misses its latch
    // hangs the test and fails the ctest timeout instead.
    if (!SpawnActivateAndDrainWorker(1u, "activated worker"))
        return false;

    if (!ShutdownWorkerSlotAndVerifyLatch("active worker slot 0"))
        return false;
    if (Sys_ShutdownWorkerThread(0))
    {
        std::fputs("shutdown of a released worker slot succeeded twice\n", stderr);
        return false;
    }

    // Safe reinitialization: the emptied slot must accept a fresh spawn,
    // activate, and drain through the same lifecycle again.
    g_workerObservedLatch[0].store(false, std::memory_order_release);
    if (!SpawnActivateAndDrainWorker(2u, "respawned worker"))
        return false;
    return ShutdownWorkerSlotAndVerifyLatch("respawned worker slot 0");
}

bool TestParkedWorkerShutdown()
{
    // Activate, then park the worker through a pause request so its native
    // thread blocks inside the gate's resume wait. The shutdown must release
    // the parked worker without any controller-side activation.
    if (!Sys_SpawnWorkerThread(TestWorkerThreadEntry, 0))
    {
        std::fputs("worker slot 0 spawn failed\n", stderr);
        return false;
    }
    Sys_SetWorkerThreadActive(0, true);
    Sys_SetWorkerThreadActive(0, false);

    if (!Sys_ShutdownWorkerThread(0))
    {
        std::fputs("parked worker shutdown failed\n", stderr);
        return false;
    }
    if (!g_workerObservedLatch[0].load(std::memory_order_acquire))
    {
        std::fputs("parked worker exited without observing its latch\n", stderr);
        return false;
    }
    return true;
}

bool TestNeverActivatedWorkerShutdown()
{
    // Spawn without activating: the worker is suspended before its entry ran
    // and can never be joined, so shutdown runs the entry once under the
    // pre-latched gate (the loop condition exits before any command wait)
    // and then joins it.
    if (!Sys_SpawnWorkerThread(TestWorkerThreadEntry, 1))
    {
        std::fputs("worker slot 1 spawn failed\n", stderr);
        return false;
    }
    if (!Sys_ShutdownWorkerThread(1))
    {
        std::fputs("never-activated worker shutdown failed\n", stderr);
        return false;
    }
    if (!g_workerObservedLatch[1].load(std::memory_order_acquire))
    {
        std::fputs("never-activated worker exited without observing its latch\n", stderr);
        return false;
    }
    if (g_workerProcessedCommands[1].load(std::memory_order_acquire) != 0u)
    {
        std::fputs("never-activated worker processed commands during teardown\n", stderr);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    // Sys_InitMainThread captures the calling thread; Sys_SpawnRenderThread
    // creates the shared backend/worker command events exactly as the engine
    // startup order does before any worker is spawned.
    Sys_InitMainThread();
    Sys_CreateEvent(false, false, &g_renderThreadWakeEvent);
    Sys_CreateEvent(true, false, &g_renderThreadExitedEvent);
    if (!Sys_SpawnRenderThread(TestRenderThreadEntry))
    {
        std::fputs("render thread spawn failed\n", stderr);
        return 1;
    }

    if (!TestShutdownRejectsInvalidAndEmptySlots())
        return 1;
    if (!TestActiveWorkerShutdownAndRespawn())
        return 1;
    if (!TestParkedWorkerShutdown())
        return 1;
    if (!TestNeverActivatedWorkerShutdown())
        return 1;

    g_renderThreadStop.store(true, std::memory_order_release);
    Sys_SetEvent(&g_renderThreadWakeEvent);
    // Join the render thread by acknowledgement before tearing down its wake
    // event: destroying an event with an active waiter fail-fasts by design,
    // so shutdown ordering must quiesce the waiter first.
    if (!Sys_WaitForSingleObjectTimeout(&g_renderThreadExitedEvent, 5'000))
    {
        std::fputs("render thread did not acknowledge shutdown\n", stderr);
        return 1;
    }
    Sys_DestroyEvent(&g_renderThreadExitedEvent);
    Sys_DestroyEvent(&g_renderThreadWakeEvent);

    std::puts("worker thread lifecycle contracts passed");
    return 0;
}

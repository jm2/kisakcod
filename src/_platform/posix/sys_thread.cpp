#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <pthread.h>
#if defined(__linux__)
#include <sched.h>
#endif
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#include <qcommon/sys_thread.h>

namespace
{
enum class SysThreadState
{
    Captured,
    Suspended,
    Running,
    Joining,
    Joined,
    CrashFrozen,
};
}

struct SysThread
{
    pthread_t nativeThread{};
    SysThreadEntry entry{};
    void *userData{};
    std::string name;
    std::mutex startMutex;
    std::condition_variable startCondition;
    std::mutex completionMutex;
    std::condition_variable completionCondition;
    std::atomic<SysThreadState> state{SysThreadState::Captured};
    bool startRequested{};
    bool exited{};
};

namespace
{
#if defined(__linux__)
struct LinuxAffinityTopology
{
    std::mutex mutex;
    cpu_set_t *baseline{};
    std::size_t setSize{};
    std::vector<std::size_t> eligibleProcessors;
    bool initialized{};
    bool available{};

    ~LinuxAffinityTopology()
    {
        if (baseline)
            CPU_FREE(baseline);
    }
};

LinuxAffinityTopology s_linuxAffinityTopology;
#endif

[[noreturn]] void Sys_ThreadFailFast()
{
    std::abort();
}

void Sys_ValidateThreadOutput(SysThreadHandle *outThread)
{
    if (!outThread || *outThread)
        Sys_ThreadFailFast();
}

SysThread *Sys_ValidateThread(SysThreadHandle thread)
{
    if (!thread)
        Sys_ThreadFailFast();
    return thread;
}

SysThread *Sys_ValidatePolicyThread(SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    const SysThreadState state = thread->state.load(std::memory_order_acquire);
    if (state == SysThreadState::Joining
        || state == SysThreadState::Joined
        || state == SysThreadState::CrashFrozen)
        Sys_ThreadFailFast();
    return thread;
}

#if defined(__linux__)
bool Sys_InitializeLinuxAffinityLocked()
{
    LinuxAffinityTopology &topology = s_linuxAffinityTopology;
    if (topology.initialized)
        return topology.available;

    topology.initialized = true;

    long configuredProcessorCount = sysconf(_SC_NPROCESSORS_CONF);
    std::size_t processorCapacity = 64;
    if (configuredProcessorCount > 0)
    {
        const auto configured = static_cast<unsigned long long>(
            configuredProcessorCount);
        if (configured <= std::numeric_limits<std::size_t>::max())
            processorCapacity = static_cast<std::size_t>(configured);
    }
    if (processorCapacity < 64)
        processorCapacity = 64;

    cpu_set_t *baseline = nullptr;
    std::size_t setSize = 0;
    for (;;)
    {
        baseline = CPU_ALLOC(processorCapacity);
        if (!baseline)
            return false;

        setSize = CPU_ALLOC_SIZE(processorCapacity);
        CPU_ZERO_S(setSize, baseline);
        const int result = pthread_getaffinity_np(
            pthread_self(),
            setSize,
            baseline);
        if (result == 0)
            break;

        CPU_FREE(baseline);
        baseline = nullptr;
        if (result != EINVAL
            || processorCapacity
                > std::numeric_limits<std::size_t>::max() / 2)
        {
            return false;
        }
        processorCapacity *= 2;
    }

    try
    {
        if (setSize > std::numeric_limits<std::size_t>::max() / CHAR_BIT)
        {
            CPU_FREE(baseline);
            return false;
        }
        const std::size_t representableProcessorCount = setSize * CHAR_BIT;
        for (std::size_t processor = 0;
             processor < representableProcessorCount;
             ++processor)
        {
            if (CPU_ISSET_S(processor, setSize, baseline))
                topology.eligibleProcessors.push_back(processor);
        }
    }
    catch (...)
    {
        CPU_FREE(baseline);
        topology.eligibleProcessors.clear();
        return false;
    }

    if (topology.eligibleProcessors.empty())
    {
        CPU_FREE(baseline);
        return false;
    }

    topology.baseline = baseline;
    topology.setSize = setSize;
    topology.available = true;
    return true;
}

bool Sys_EnsureLinuxAffinity()
{
    try
    {
        std::lock_guard<std::mutex> lock(s_linuxAffinityTopology.mutex);
        return Sys_InitializeLinuxAffinityLocked();
    }
    catch (...)
    {
        return false;
    }
}

SysThreadPolicyStatus Sys_MapPthreadAffinityResult(const int result)
{
    if (result == 0)
        return SysThreadPolicyStatus::Applied;
    if (result == EPERM || result == EACCES)
        return SysThreadPolicyStatus::PermissionDenied;
#if defined(ENOSYS)
    if (result == ENOSYS)
        return SysThreadPolicyStatus::Unsupported;
#endif
    return SysThreadPolicyStatus::Unavailable;
}
#endif

#if defined(__APPLE__)
std::uint32_t Sys_GetOnlineProcessorCount()
{
    const long processorCount = sysconf(_SC_NPROCESSORS_ONLN);
    if (processorCount <= 0)
        return 1;

    const auto count = static_cast<unsigned long long>(processorCount);
    if (count > std::numeric_limits<std::uint32_t>::max())
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(count);
}
#endif

SysThread *Sys_AllocateThread(const char *name)
{
    if (!name)
        Sys_ThreadFailFast();

    SysThread *thread = nullptr;
    try
    {
        thread = new (std::nothrow) SysThread{};
        if (!thread)
            return nullptr;
        thread->name.assign(name);
        return thread;
    }
    catch (const std::bad_alloc &)
    {
        delete thread;
        return nullptr;
    }
    catch (const std::system_error &)
    {
        delete thread;
        return nullptr;
    }
    catch (...)
    {
        delete thread;
        Sys_ThreadFailFast();
    }
}

void Sys_SetCurrentThreadName(const std::string &sourceName)
{
#if defined(__linux__)
    constexpr std::size_t nameCapacity = 16;
#elif defined(__APPLE__)
    constexpr std::size_t nameCapacity = 64;
#else
#error "sys_thread.cpp: unsupported POSIX target"
#endif

    char name[nameCapacity]{};
    const std::size_t copyLength = sourceName.size() < nameCapacity - 1
        ? sourceName.size()
        : nameCapacity - 1;
    if (copyLength != 0)
        std::memcpy(name, sourceName.data(), copyLength);

#if defined(__linux__)
    (void)pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    (void)pthread_setname_np(name);
#endif
}

void *Sys_ThreadTrampoline(void *parameter)
{
    SysThread *const thread = static_cast<SysThread *>(parameter);

    try
    {
        {
            std::unique_lock<std::mutex> lock(thread->startMutex);
            thread->startCondition.wait(
                lock,
                [thread]() { return thread->startRequested; });
        }

        Sys_SetCurrentThreadName(thread->name);
        thread->entry(thread->userData);

        {
            std::lock_guard<std::mutex> lock(thread->completionMutex);
            thread->exited = true;
        }
        thread->completionCondition.notify_all();
        return nullptr;
    }
    catch (...)
    {
        Sys_ThreadFailFast();
    }
}

bool Sys_IsCreateResourceFailure(const int result)
{
    return result == EAGAIN || result == ENOMEM;
}

bool Sys_ThreadJoinInternal(
    SysThreadHandle threadHandle,
    const bool hasTimeout,
    const std::uint32_t timeoutMsec)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    if (pthread_equal(pthread_self(), thread->nativeThread) != 0)
        Sys_ThreadFailFast();

    SysThreadState expected = SysThreadState::Running;
    if (!thread->state.compare_exchange_strong(
            expected,
            SysThreadState::Joining,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        Sys_ThreadFailFast();
    }

    try
    {
        std::unique_lock<std::mutex> lock(thread->completionMutex);
        if (hasTimeout)
        {
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(timeoutMsec);
            if (!thread->completionCondition.wait_until(
                    lock,
                    deadline,
                    [thread]() { return thread->exited; }))
            {
                thread->state.store(
                    SysThreadState::Running,
                    std::memory_order_release);
                return false;
            }
        }
        else
        {
            thread->completionCondition.wait(
                lock,
                [thread]() { return thread->exited; });
        }
    }
    catch (...)
    {
        Sys_ThreadFailFast();
    }

    if (pthread_join(thread->nativeThread, nullptr) != 0)
        Sys_ThreadFailFast();

    thread->state.store(SysThreadState::Joined, std::memory_order_release);
    return true;
}
}

bool KISAK_CDECL Sys_ThreadCaptureCurrent(
    const char *name,
    SysThreadHandle *outThread)
{
    Sys_ValidateThreadOutput(outThread);

    SysThread *const thread = Sys_AllocateThread(name);
    if (!thread)
        return false;

#if defined(__linux__)
    (void)Sys_EnsureLinuxAffinity();
#endif

    thread->nativeThread = pthread_self();
    thread->state.store(SysThreadState::Captured, std::memory_order_relaxed);
    Sys_SetCurrentThreadName(thread->name);
    *outThread = thread;
    return true;
}

bool KISAK_CDECL Sys_ThreadCreateSuspended(
    const SysThreadEntry entry,
    void *const userData,
    const char *name,
    SysThreadHandle *outThread)
{
    Sys_ValidateThreadOutput(outThread);
    if (!entry)
        Sys_ThreadFailFast();

    SysThread *const thread = Sys_AllocateThread(name);
    if (!thread)
        return false;

#if defined(__linux__)
    (void)Sys_EnsureLinuxAffinity();
#endif

    thread->entry = entry;
    thread->userData = userData;
    thread->state.store(SysThreadState::Suspended, std::memory_order_relaxed);

    const int result = pthread_create(
        &thread->nativeThread,
        nullptr,
        Sys_ThreadTrampoline,
        thread);
    if (result != 0)
    {
        delete thread;
        if (Sys_IsCreateResourceFailure(result))
            return false;
        Sys_ThreadFailFast();
    }

    *outThread = thread;
    return true;
}

void KISAK_CDECL Sys_ThreadStart(SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    SysThreadState expected = SysThreadState::Suspended;
    if (!thread->state.compare_exchange_strong(
            expected,
            SysThreadState::Running,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        Sys_ThreadFailFast();
    }

    try
    {
        {
            std::lock_guard<std::mutex> lock(thread->startMutex);
            thread->startRequested = true;
        }
        thread->startCondition.notify_one();
    }
    catch (...)
    {
        Sys_ThreadFailFast();
    }
}

bool KISAK_CDECL Sys_ThreadIsCurrent(SysThreadHandle threadHandle)
{
    const SysThread *const thread = Sys_ValidateThread(threadHandle);
    return pthread_equal(pthread_self(), thread->nativeThread) != 0;
}

bool KISAK_CDECL Sys_ThreadJoinTimeout(
    SysThreadHandle thread,
    const std::uint32_t msec)
{
    if (msec == UINT32_MAX)
        Sys_ThreadFailFast();
    return Sys_ThreadJoinInternal(thread, true, msec);
}

void KISAK_CDECL Sys_ThreadJoin(SysThreadHandle thread)
{
    if (!Sys_ThreadJoinInternal(thread, false, 0))
        Sys_ThreadFailFast();
}

void KISAK_CDECL Sys_ThreadDestroy(SysThreadHandle *threadHandle)
{
    if (!threadHandle)
        Sys_ThreadFailFast();

    SysThread *const thread = Sys_ValidateThread(*threadHandle);
    const SysThreadState state = thread->state.load(std::memory_order_acquire);
    if (state != SysThreadState::Captured && state != SysThreadState::Joined)
        Sys_ThreadFailFast();

    delete thread;
    *threadHandle = nullptr;
}

std::uint32_t KISAK_CDECL Sys_ThreadGetEligibleProcessorCount()
{
#if defined(__linux__)
    try
    {
        std::lock_guard<std::mutex> lock(s_linuxAffinityTopology.mutex);
        if (!Sys_InitializeLinuxAffinityLocked())
            return 1;

        const std::size_t count =
            s_linuxAffinityTopology.eligibleProcessors.size();
        if (count > std::numeric_limits<std::uint32_t>::max())
            return std::numeric_limits<std::uint32_t>::max();
        return static_cast<std::uint32_t>(count);
    }
    catch (...)
    {
        return 1;
    }
#elif defined(__APPLE__)
    return Sys_GetOnlineProcessorCount();
#else
#error "sys_thread.cpp: unsupported POSIX target"
#endif
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadSetPriority(
    SysThreadHandle threadHandle,
    const SysThreadPriority priority)
{
    (void)Sys_ValidatePolicyThread(threadHandle);
    switch (priority)
    {
    case SysThreadPriority::BelowNormal:
    case SysThreadPriority::AboveNormal:
        return SysThreadPolicyStatus::Unsupported;
    case SysThreadPriority::Normal:
        return SysThreadPolicyStatus::Applied;
    default:
        Sys_ThreadFailFast();
    }
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadPinToEligibleProcessor(
    SysThreadHandle threadHandle,
    const std::uint32_t ordinal)
{
    SysThread *const thread = Sys_ValidatePolicyThread(threadHandle);

#if defined(__linux__)
    try
    {
        std::lock_guard<std::mutex> lock(s_linuxAffinityTopology.mutex);
        LinuxAffinityTopology &topology = s_linuxAffinityTopology;
        if (!Sys_InitializeLinuxAffinityLocked()
            || ordinal >= topology.eligibleProcessors.size())
        {
            return SysThreadPolicyStatus::Unavailable;
        }

        const std::size_t representableProcessorCount =
            topology.setSize * CHAR_BIT;
        cpu_set_t *const selectedSet = CPU_ALLOC(representableProcessorCount);
        if (!selectedSet)
            return SysThreadPolicyStatus::Unavailable;

        CPU_ZERO_S(topology.setSize, selectedSet);
        CPU_SET_S(
            topology.eligibleProcessors[ordinal],
            topology.setSize,
            selectedSet);
        const int result = pthread_setaffinity_np(
            thread->nativeThread,
            topology.setSize,
            selectedSet);
        CPU_FREE(selectedSet);
        return Sys_MapPthreadAffinityResult(result);
    }
    catch (...)
    {
        return SysThreadPolicyStatus::Unavailable;
    }
#elif defined(__APPLE__)
    (void)thread;
    if (ordinal >= Sys_GetOnlineProcessorCount())
        return SysThreadPolicyStatus::Unavailable;
    return SysThreadPolicyStatus::Unsupported;
#else
#error "sys_thread.cpp: unsupported POSIX target"
#endif
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadClearAffinity(
    SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidatePolicyThread(threadHandle);

#if defined(__linux__)
    try
    {
        std::lock_guard<std::mutex> lock(s_linuxAffinityTopology.mutex);
        LinuxAffinityTopology &topology = s_linuxAffinityTopology;
        if (!Sys_InitializeLinuxAffinityLocked())
            return SysThreadPolicyStatus::Unavailable;

        return Sys_MapPthreadAffinityResult(pthread_setaffinity_np(
            thread->nativeThread,
            topology.setSize,
            topology.baseline));
    }
    catch (...)
    {
        return SysThreadPolicyStatus::Unavailable;
    }
#elif defined(__APPLE__)
    (void)thread;
    return SysThreadPolicyStatus::Applied;
#else
#error "sys_thread.cpp: unsupported POSIX target"
#endif
}

SysThreadCrashFreezeStatus KISAK_CDECL Sys_ThreadForceSuspendForCrash(
    SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    if (pthread_equal(pthread_self(), thread->nativeThread) != 0)
        return SysThreadCrashFreezeStatus::CurrentThread;

    const SysThreadState state = thread->state.load(std::memory_order_acquire);
    if (state == SysThreadState::Suspended
        || state == SysThreadState::Joining
        || state == SysThreadState::Joined
        || state == SysThreadState::CrashFrozen)
    {
        return SysThreadCrashFreezeStatus::AlreadyStopped;
    }
    return SysThreadCrashFreezeStatus::Unsupported;
}

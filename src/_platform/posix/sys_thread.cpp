#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <pthread.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <system_error>

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

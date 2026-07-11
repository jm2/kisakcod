#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <new>

#include <qcommon/sys_event.h>

struct SysEventWaiter
{
    SysEventWaiter *next;
    bool released;
};

struct SysEvent
{
    std::mutex mutex;
    std::condition_variable condition;
    SysEventWaiter *autoWaiterHead;
    SysEventWaiter *autoWaiterTail;
    std::uint64_t setGeneration;
    std::size_t activeWaiters;
    bool manualReset;
    bool signaled;
    bool destroying;
};

namespace
{
[[noreturn]] void Sys_EventFailFast()
{
    std::abort();
}

void Sys_ValidateEventOutput(SysEventHandle *event)
{
    if (!event || *event)
        Sys_EventFailFast();
}

SysEvent *Sys_GetEvent(SysEventHandle *event)
{
    if (!event || !*event)
        Sys_EventFailFast();
    return *event;
}

void Sys_ValidateUsableEvent(const SysEvent *event)
{
    if (event->destroying)
        Sys_EventFailFast();
}

void Sys_EnqueueAutoWaiter(SysEvent *event, SysEventWaiter *waiter)
{
    if (event->autoWaiterTail)
        event->autoWaiterTail->next = waiter;
    else
        event->autoWaiterHead = waiter;
    event->autoWaiterTail = waiter;
}

void Sys_RemoveAutoWaiter(SysEvent *event, SysEventWaiter *waiter)
{
    SysEventWaiter *previous = nullptr;
    SysEventWaiter *current = event->autoWaiterHead;
    while (current && current != waiter)
    {
        previous = current;
        current = current->next;
    }
    if (!current)
        Sys_EventFailFast();

    if (previous)
        previous->next = current->next;
    else
        event->autoWaiterHead = current->next;
    if (event->autoWaiterTail == current)
        event->autoWaiterTail = previous;
    current->next = nullptr;
}

void Sys_ReleaseOneAutoWaiter(SysEvent *event)
{
    SysEventWaiter *const waiter = event->autoWaiterHead;
    if (!waiter)
    {
        event->signaled = true;
        return;
    }

    event->autoWaiterHead = waiter->next;
    if (!event->autoWaiterHead)
        event->autoWaiterTail = nullptr;
    waiter->next = nullptr;
    waiter->released = true;
}

bool Sys_WaitEventInternal(
    SysEventHandle *eventHandle,
    const bool hasTimeout,
    const std::uint32_t timeoutMsec)
{
    SysEvent *const event = Sys_GetEvent(eventHandle);

    try
    {
        std::unique_lock<std::mutex> lock(event->mutex);
        Sys_ValidateUsableEvent(event);

        if (event->manualReset)
        {
            if (event->signaled)
                return true;
            if (hasTimeout && timeoutMsec == 0)
                return false;

            const std::uint64_t observedGeneration = event->setGeneration;
            ++event->activeWaiters;

            if (!hasTimeout)
            {
                while (!event->signaled && event->setGeneration == observedGeneration)
                    event->condition.wait(lock);
                --event->activeWaiters;
                return true;
            }

            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(timeoutMsec);
            while (!event->signaled && event->setGeneration == observedGeneration)
            {
                if (event->condition.wait_until(lock, deadline)
                    == std::cv_status::timeout)
                {
                    if (!event->signaled && event->setGeneration == observedGeneration)
                    {
                        --event->activeWaiters;
                        return false;
                    }
                }
            }
            --event->activeWaiters;
            return true;
        }

        if (event->signaled)
        {
            event->signaled = false;
            return true;
        }
        if (hasTimeout && timeoutMsec == 0)
            return false;

        SysEventWaiter waiter{};
        Sys_EnqueueAutoWaiter(event, &waiter);
        ++event->activeWaiters;

        if (!hasTimeout)
        {
            while (!waiter.released)
                event->condition.wait(lock);
            --event->activeWaiters;
            return true;
        }

        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMsec);
        while (!waiter.released)
        {
            if (event->condition.wait_until(lock, deadline)
                == std::cv_status::timeout)
            {
                if (!waiter.released)
                {
                    Sys_RemoveAutoWaiter(event, &waiter);
                    --event->activeWaiters;
                    return false;
                }
            }
        }
        --event->activeWaiters;
        return true;
    }
    catch (...)
    {
        Sys_EventFailFast();
    }
}
}

void KISAK_CDECL Sys_CreateEvent(
    const bool manualReset,
    const bool initialState,
    SysEventHandle *event)
{
    Sys_ValidateEventOutput(event);

    try
    {
        *event = new SysEvent{
            {},
            {},
            nullptr,
            nullptr,
            0,
            0,
            manualReset,
            initialState,
            false};
    }
    catch (...)
    {
        Sys_EventFailFast();
    }
}

void KISAK_CDECL Sys_DestroyEvent(SysEventHandle *eventHandle)
{
    SysEvent *const event = Sys_GetEvent(eventHandle);

    try
    {
        std::unique_lock<std::mutex> lock(event->mutex);
        Sys_ValidateUsableEvent(event);
        if (event->activeWaiters != 0
            || event->autoWaiterHead
            || event->autoWaiterTail)
        {
            Sys_EventFailFast();
        }
        event->destroying = true;
    }
    catch (...)
    {
        Sys_EventFailFast();
    }

    delete event;
    *eventHandle = nullptr;
}

void KISAK_CDECL Sys_SetEvent(SysEventHandle *eventHandle)
{
    SysEvent *const event = Sys_GetEvent(eventHandle);
    bool notify = false;

    try
    {
        std::lock_guard<std::mutex> lock(event->mutex);
        Sys_ValidateUsableEvent(event);
        if (event->manualReset)
        {
            ++event->setGeneration;
            event->signaled = true;
            notify = true;
        }
        else
        {
            notify = event->autoWaiterHead != nullptr;
            Sys_ReleaseOneAutoWaiter(event);
        }
    }
    catch (...)
    {
        Sys_EventFailFast();
    }

    if (notify)
        event->condition.notify_all();
}

void KISAK_CDECL Sys_ResetEvent(SysEventHandle *eventHandle)
{
    SysEvent *const event = Sys_GetEvent(eventHandle);

    try
    {
        std::lock_guard<std::mutex> lock(event->mutex);
        Sys_ValidateUsableEvent(event);
        event->signaled = false;
    }
    catch (...)
    {
        Sys_EventFailFast();
    }
}

void KISAK_CDECL Sys_WaitForSingleObject(SysEventHandle *event)
{
    if (!Sys_WaitEventInternal(event, false, 0))
        Sys_EventFailFast();
}

bool KISAK_CDECL Sys_WaitForSingleObjectTimeout(
    SysEventHandle *event,
    const std::uint32_t msec)
{
    if (msec == UINT32_MAX)
        Sys_EventFailFast();
    return Sys_WaitEventInternal(event, true, msec);
}

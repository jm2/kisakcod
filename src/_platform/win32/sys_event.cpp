#include <Windows.h>

#include <cstdlib>
#include <new>

#include <qcommon/sys_event.h>

struct SysEvent
{
    HANDLE handle;
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
    if (!event || !*event || !(*event)->handle)
        Sys_EventFailFast();
    return *event;
}
}

void KISAK_CDECL Sys_CreateEvent(
    const bool manualReset,
    const bool initialState,
    SysEventHandle *event)
{
    Sys_ValidateEventOutput(event);

    SysEvent *const createdEvent = new (std::nothrow) SysEvent{};
    if (!createdEvent)
        Sys_EventFailFast();

    createdEvent->handle = CreateEventA(
        nullptr,
        manualReset ? TRUE : FALSE,
        initialState ? TRUE : FALSE,
        nullptr);
    if (!createdEvent->handle)
        Sys_EventFailFast();

    *event = createdEvent;
}

void KISAK_CDECL Sys_DestroyEvent(SysEventHandle *event)
{
    SysEvent *const destroyedEvent = Sys_GetEvent(event);
    if (!CloseHandle(destroyedEvent->handle))
        Sys_EventFailFast();

    destroyedEvent->handle = nullptr;
    delete destroyedEvent;
    *event = nullptr;
}

void KISAK_CDECL Sys_SetEvent(SysEventHandle *event)
{
    if (!SetEvent(Sys_GetEvent(event)->handle))
        Sys_EventFailFast();
}

void KISAK_CDECL Sys_ResetEvent(SysEventHandle *event)
{
    if (!ResetEvent(Sys_GetEvent(event)->handle))
        Sys_EventFailFast();
}

void KISAK_CDECL Sys_WaitForSingleObject(SysEventHandle *event)
{
    const DWORD result = WaitForSingleObject(Sys_GetEvent(event)->handle, INFINITE);
    if (result != WAIT_OBJECT_0)
        Sys_EventFailFast();
}

bool KISAK_CDECL Sys_WaitForSingleObjectTimeout(
    SysEventHandle *event,
    const std::uint32_t msec)
{
    if (msec == UINT32_MAX)
        Sys_EventFailFast();

    const DWORD result = WaitForSingleObject(Sys_GetEvent(event)->handle, msec);
    if (result == WAIT_OBJECT_0)
        return true;
    if (result == WAIT_TIMEOUT)
        return false;
    Sys_EventFailFast();
}

#include <Windows.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <string>

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
    HANDLE handle{};
    DWORD threadId{};
    SysThreadEntry entry{};
    void *userData{};
    std::string name;
    std::atomic<SysThreadState> state{SysThreadState::Captured};
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
    if (!thread || !thread->handle)
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
    catch (...)
    {
        delete thread;
        Sys_ThreadFailFast();
    }
}

using SetThreadDescriptionFunction = HRESULT (WINAPI *)(HANDLE, PCWSTR);

SetThreadDescriptionFunction Sys_FindSetThreadDescription()
{
    const HMODULE kernel32 = GetModuleHandleW(L"Kernel32.dll");
    if (!kernel32)
        return nullptr;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4191)
#endif
    const auto function = reinterpret_cast<SetThreadDescriptionFunction>(
        GetProcAddress(kernel32, "SetThreadDescription"));
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return function;
}

void Sys_SetCurrentThreadName(const char *name)
{
    const SetThreadDescriptionFunction setThreadDescription =
        Sys_FindSetThreadDescription();
    if (!setThreadDescription || !name || !*name)
        return;

    const int length = MultiByteToWideChar(
        CP_UTF8,
        0,
        name,
        -1,
        nullptr,
        0);
    if (length <= 0)
        return;

    wchar_t *const wideName = new (std::nothrow) wchar_t[length];
    if (!wideName)
        return;

    const int convertedLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        name,
        -1,
        wideName,
        length);
    if (convertedLength > 0)
        (void)setThreadDescription(GetCurrentThread(), wideName);

    delete[] wideName;
}

DWORD WINAPI Sys_ThreadTrampoline(void *parameter)
{
    SysThread *const thread = static_cast<SysThread *>(parameter);
    Sys_SetCurrentThreadName(thread->name.c_str());

    try
    {
        thread->entry(thread->userData);
    }
    catch (...)
    {
        Sys_ThreadFailFast();
    }

    return 0;
}

bool Sys_ThreadJoinInternal(
    SysThreadHandle threadHandle,
    const DWORD timeout)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    if (GetCurrentThreadId() == thread->threadId)
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

    const DWORD result = WaitForSingleObject(thread->handle, timeout);
    if (result == WAIT_OBJECT_0)
    {
        thread->state.store(SysThreadState::Joined, std::memory_order_release);
        return true;
    }
    if (result == WAIT_TIMEOUT && timeout != INFINITE)
    {
        thread->state.store(SysThreadState::Running, std::memory_order_release);
        return false;
    }

    Sys_ThreadFailFast();
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

    HANDLE duplicatedHandle = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(),
            &duplicatedHandle,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS))
    {
        delete thread;
        return false;
    }

    thread->handle = duplicatedHandle;
    thread->threadId = GetCurrentThreadId();
    thread->state.store(SysThreadState::Captured, std::memory_order_relaxed);
    Sys_SetCurrentThreadName(thread->name.c_str());
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
    thread->handle = CreateThread(
        nullptr,
        0,
        Sys_ThreadTrampoline,
        thread,
        CREATE_SUSPENDED,
        &thread->threadId);
    if (!thread->handle)
    {
        delete thread;
        return false;
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

    const DWORD previousSuspendCount = ResumeThread(thread->handle);
    if (previousSuspendCount != 1)
        Sys_ThreadFailFast();
}

bool KISAK_CDECL Sys_ThreadIsCurrent(SysThreadHandle threadHandle)
{
    const SysThread *const thread = Sys_ValidateThread(threadHandle);
    return GetCurrentThreadId() == thread->threadId;
}

bool KISAK_CDECL Sys_ThreadJoinTimeout(
    SysThreadHandle thread,
    const std::uint32_t msec)
{
    if (msec == UINT32_MAX)
        Sys_ThreadFailFast();
    return Sys_ThreadJoinInternal(thread, static_cast<DWORD>(msec));
}

void KISAK_CDECL Sys_ThreadJoin(SysThreadHandle thread)
{
    if (!Sys_ThreadJoinInternal(thread, INFINITE))
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

    if (!CloseHandle(thread->handle))
        Sys_ThreadFailFast();

    thread->handle = nullptr;
    delete thread;
    *threadHandle = nullptr;
}

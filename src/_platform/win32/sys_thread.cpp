#include <Windows.h>

#include <atomic>
#include <climits>
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
    CrashFrozen,
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

SysThreadPolicyStatus Sys_MapWin32PolicyError(const DWORD error)
{
    if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD)
        return SysThreadPolicyStatus::PermissionDenied;
    if (error == ERROR_CALL_NOT_IMPLEMENTED || error == ERROR_NOT_SUPPORTED)
        return SysThreadPolicyStatus::Unsupported;
    return SysThreadPolicyStatus::Unavailable;
}

bool Sys_GetProcessAffinity(DWORD_PTR *processAffinity)
{
    DWORD_PTR systemAffinity = 0;
    *processAffinity = 0;
    return GetProcessAffinityMask(
               GetCurrentProcess(),
               processAffinity,
               &systemAffinity)
        && *processAffinity != 0;
}

std::uint32_t Sys_CountAffinityProcessors(const DWORD_PTR affinity)
{
    std::uint32_t count = 0;
    for (unsigned int bitIndex = 0;
         bitIndex < sizeof(DWORD_PTR) * CHAR_BIT;
         ++bitIndex)
    {
        const DWORD_PTR bit = static_cast<DWORD_PTR>(1) << bitIndex;
        if ((affinity & bit) != 0)
            ++count;
    }
    return count;
}

DWORD_PTR Sys_GetAffinityBitForOrdinal(
    const DWORD_PTR affinity,
    const std::uint32_t ordinal)
{
    std::uint32_t currentOrdinal = 0;
    for (unsigned int bitIndex = 0;
         bitIndex < sizeof(DWORD_PTR) * CHAR_BIT;
         ++bitIndex)
    {
        const DWORD_PTR bit = static_cast<DWORD_PTR>(1) << bitIndex;
        if ((affinity & bit) == 0)
            continue;
        if (currentOrdinal == ordinal)
            return bit;
        ++currentOrdinal;
    }
    return 0;
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

std::uint32_t KISAK_CDECL Sys_ThreadGetEligibleProcessorCount()
{
    DWORD_PTR processAffinity = 0;
    if (!Sys_GetProcessAffinity(&processAffinity))
        return 1;

    const std::uint32_t count = Sys_CountAffinityProcessors(processAffinity);
    return count != 0 ? count : 1;
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadSetPriority(
    SysThreadHandle threadHandle,
    const SysThreadPriority priority)
{
    SysThread *const thread = Sys_ValidatePolicyThread(threadHandle);

    int nativePriority = THREAD_PRIORITY_NORMAL;
    switch (priority)
    {
    case SysThreadPriority::BelowNormal:
        nativePriority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case SysThreadPriority::Normal:
        nativePriority = THREAD_PRIORITY_NORMAL;
        break;
    case SysThreadPriority::AboveNormal:
        nativePriority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    default:
        Sys_ThreadFailFast();
    }

    if (SetThreadPriority(thread->handle, nativePriority))
        return SysThreadPolicyStatus::Applied;
    return Sys_MapWin32PolicyError(GetLastError());
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadPinToEligibleProcessor(
    SysThreadHandle threadHandle,
    const std::uint32_t ordinal)
{
    SysThread *const thread = Sys_ValidatePolicyThread(threadHandle);

    DWORD_PTR processAffinity = 0;
    if (!Sys_GetProcessAffinity(&processAffinity))
        return SysThreadPolicyStatus::Unavailable;

    const DWORD_PTR selectedAffinity =
        Sys_GetAffinityBitForOrdinal(processAffinity, ordinal);
    if (selectedAffinity == 0)
        return SysThreadPolicyStatus::Unavailable;

    if (SetThreadAffinityMask(thread->handle, selectedAffinity) != 0)
        return SysThreadPolicyStatus::Applied;
    return Sys_MapWin32PolicyError(GetLastError());
}

SysThreadPolicyStatus KISAK_CDECL Sys_ThreadClearAffinity(
    SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidatePolicyThread(threadHandle);

    DWORD_PTR processAffinity = 0;
    if (!Sys_GetProcessAffinity(&processAffinity))
        return SysThreadPolicyStatus::Unavailable;

    if (SetThreadAffinityMask(thread->handle, processAffinity) != 0)
        return SysThreadPolicyStatus::Applied;
    return Sys_MapWin32PolicyError(GetLastError());
}

SysThreadCrashFreezeStatus KISAK_CDECL Sys_ThreadForceSuspendForCrash(
    SysThreadHandle threadHandle)
{
    SysThread *const thread = Sys_ValidateThread(threadHandle);
    if (GetCurrentThreadId() == thread->threadId)
        return SysThreadCrashFreezeStatus::CurrentThread;

    const SysThreadState state = thread->state.load(std::memory_order_acquire);
    if (state == SysThreadState::Suspended
        || state == SysThreadState::Joining
        || state == SysThreadState::Joined
        || state == SysThreadState::CrashFrozen)
    {
        return SysThreadCrashFreezeStatus::AlreadyStopped;
    }

    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeThread(thread->handle, &exitCode) && exitCode != STILL_ACTIVE)
        return SysThreadCrashFreezeStatus::AlreadyStopped;

    if (SuspendThread(thread->handle) != static_cast<DWORD>(-1))
    {
        thread->state.store(SysThreadState::CrashFrozen, std::memory_order_release);
        return SysThreadCrashFreezeStatus::Frozen;
    }

    if (GetExitCodeThread(thread->handle, &exitCode) && exitCode != STILL_ACTIVE)
        return SysThreadCrashFreezeStatus::AlreadyStopped;
    return SysThreadCrashFreezeStatus::Failed;
}

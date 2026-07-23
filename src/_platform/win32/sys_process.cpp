// SPDX-License-Identifier: GPL-3.0-only
//
// Process service implementation for Win32. Wraps CreateProcessW,
// WaitForSingleObject, and TerminateProcess in the portable API.
// Signal-park and Mach crash-freeze are intentionally unsupported on
// Win32 (no POSIX-style signal mask, no Mach ports).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <qcommon/sys_process.h>

#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
bool BuildCommandLine(const wchar_t *executable, const wchar_t *arguments,
    std::wstring *outLine)
{
    if (!executable || !*executable)
        return false;
    outLine->clear();
    outLine->reserve(std::wcslen(executable) + 4);
    outLine->push_back(L'"');
    outLine->append(executable);
    outLine->push_back(L'"');
    if (arguments && *arguments)
    {
        outLine->push_back(L' ');
        outLine->append(arguments);
    }
    return true;
}

bool ToWide(const char *input, std::wstring *output)
{
    output->clear();
    if (!input)
        return true;
    const std::size_t length = std::strlen(input);
    if (length == 0)
        return true;
    const int wideLength = MultiByteToWideChar(
        CP_UTF8, 0, input, static_cast<int>(length), nullptr, 0);
    if (wideLength <= 0)
        return false;
    std::vector<wchar_t> buffer(
        static_cast<std::size_t>(wideLength) + 1, L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8, 0, input, static_cast<int>(length),
        buffer.data(), wideLength);
    if (converted != wideLength)
        return false;
    output->assign(buffer.data(), static_cast<std::size_t>(converted));
    return true;
}
}

struct SysProcess
{
    HANDLE handle{nullptr};
    DWORD processId{0};
};

SysProcessLaunchStatus KISAK_CDECL Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle)
{
    if (!outHandle || *outHandle || !executablePath || !executablePath[0])
        return SysProcessLaunchStatus::InvalidArgument;

    std::wstring executableWide;
    if (!ToWide(executablePath, &executableWide))
        return SysProcessLaunchStatus::InvalidArgument;
    std::wstring argumentsWide;
    if (!ToWide(arguments, &argumentsWide))
        return SysProcessLaunchStatus::InvalidArgument;

    std::wstring commandLine;
    if (!BuildCommandLine(executableWide.c_str(), argumentsWide.c_str(),
        &commandLine))
        return SysProcessLaunchStatus::InvalidArgument;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    std::vector<wchar_t> mutableCommand(commandLine.begin(),
        commandLine.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup,
        &info);
    if (!created)
        return SysProcessLaunchStatus::SpawnFailed;

    CloseHandle(info.hThread);

    SysProcess *process = new SysProcess();
    process->handle = info.hProcess;
    process->processId = info.dwProcessId;
    *outHandle = process;
    return SysProcessLaunchStatus::Started;
}

SysProcessWaitStatus KISAK_CDECL Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs)
{
    if (!handle || !handle->handle)
        return SysProcessWaitStatus::InvalidHandle;

    const DWORD waitResult = WaitForSingleObject(
        handle->handle, timeoutMs);
    switch (waitResult)
    {
    case WAIT_OBJECT_0:
        return SysProcessWaitStatus::Exited;
    case WAIT_TIMEOUT:
        return SysProcessWaitStatus::TimedOut;
    case WAIT_ABANDONED:
    case WAIT_FAILED:
    default:
        return SysProcessWaitStatus::InvalidHandle;
    }
}

SysProcessTerminateStatus KISAK_CDECL Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode)
{
    if (!handle || !handle->handle)
        return SysProcessTerminateStatus::InvalidHandle;
    if (TerminateProcess(handle->handle, exitCode))
        return SysProcessTerminateStatus::Signaled;
    return SysProcessTerminateStatus::Unsupported;
}

bool KISAK_CDECL Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode)
{
    if (!handle || !exitCode || !handle->handle)
        return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(handle->handle, &code))
        return false;
    if (code == STILL_ACTIVE)
        return false;
    *exitCode = static_cast<std::uint32_t>(code);
    return true;
}

void KISAK_CDECL Sys_ProcessRelease(SysProcessHandle *handle)
{
    if (!handle || !*handle)
        return;
    if ((*handle)->handle)
    {
        CloseHandle((*handle)->handle);
        (*handle)->handle = nullptr;
    }
    delete *handle;
    *handle = nullptr;
}

SysSignalParkStatus KISAK_CDECL Sys_SignalPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    (void)signals;
    (void)signalCount;
    return SysSignalParkStatus::Unsupported;
}

SysSignalParkStatus KISAK_CDECL Sys_SignalUnPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    if (signalCount == 0 && !signals)
        return SysSignalParkStatus::Unparked;
    (void)signals;
    (void)signalCount;
    return SysSignalParkStatus::Unsupported;
}

SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept
{
    return SysProcessFreezeStatus::Unsupported;
}

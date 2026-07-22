// SPDX-License-Identifier: GPL-3.0-only
//
// Process service implementation for Win32. Wraps CreateProcessW,
// WaitForSingleObject, and TerminateProcess in the portable Sys_Process* API.
// Signal-park is intentionally unsupported on Win32 (no POSIX-style
// per-process signal mask).

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
struct CommandLine
{
    std::wstring quoted;
};

bool IsLaunchRefused(const wchar_t *executablePath)
{
    if (!executablePath)
        return true;
    static constexpr wchar_t kRefused[] = L"quit";
    std::size_t i = 0;
    for (; i < sizeof(kRefused) / sizeof(kRefused[0]) - 1; ++i)
    {
        if (!executablePath[i])
            return false;
        const wchar_t a = executablePath[i];
        const wchar_t b = kRefused[i];
        const wchar_t loA = static_cast<wchar_t>(
            a >= L'A' && a <= L'Z' ? a + 32 : a);
        const wchar_t loB = static_cast<wchar_t>(
            b >= L'A' && b <= L'Z' ? b + 32 : b);
        if (loA != loB)
            return false;
    }
    return executablePath[i] == L'\0';
}

bool BuildCommandLine(const wchar_t *executable, const wchar_t *arguments,
    CommandLine *outLine)
{
    if (!executable || !*executable)
        return false;
    outLine->quoted.clear();
    outLine->quoted.reserve(std::wcslen(executable) + 4);
    outLine->quoted.push_back(L'"');
    outLine->quoted.append(executable);
    outLine->quoted.push_back(L'"');
    if (arguments && *arguments)
    {
        outLine->quoted.push_back(L' ');
        outLine->quoted.append(arguments);
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
    std::vector<wchar_t> buffer(static_cast<std::size_t>(wideLength) + 1, L'\0');
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

SysProcessLaunchStatus Sys_ProcessLaunch(
    const char *executablePath,
    const char *arguments,
    SysProcessHandle *outHandle)
{
    if (!outHandle || *outHandle)
        return SysProcessLaunchStatus::InvalidArgument;

    std::wstring executableWide;
    if (!ToWide(executablePath, &executableWide))
        return SysProcessLaunchStatus::InvalidArgument;
    std::wstring argumentsWide;
    if (!ToWide(arguments, &argumentsWide))
        return SysProcessLaunchStatus::InvalidArgument;

    if (IsLaunchRefused(executableWide.c_str()))
        return SysProcessLaunchStatus::InvalidArgument;

    CommandLine commandLine;
    if (!BuildCommandLine(executableWide.c_str(), argumentsWide.c_str(),
        &commandLine))
        return SysProcessLaunchStatus::InvalidArgument;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};

    std::vector<wchar_t> mutableCommand(commandLine.quoted.begin(),
        commandLine.quoted.end());
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
        return SysProcessLaunchStatus::LaunchFailed;

    CloseHandle(info.hThread);

    auto *const process = new SysProcess();
    process->handle = info.hProcess;
    process->processId = info.dwProcessId;
    *outHandle = process;
    return SysProcessLaunchStatus::Started;
}

SysProcessWaitStatus Sys_ProcessWait(
    SysProcessHandle handle,
    std::uint32_t timeoutMs)
{
    if (!handle)
        return SysProcessWaitStatus::InvalidHandle;
    auto *const process = static_cast<SysProcess *>(handle);
    if (!process->handle)
        return SysProcessWaitStatus::InvalidHandle;

    DWORD waitResult;
    if (timeoutMs == 0)
    {
        waitResult = WaitForSingleObject(process->handle, 0);
    }
    else
    {
        waitResult = WaitForSingleObject(process->handle, timeoutMs);
    }

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

SysProcessTerminateStatus Sys_ProcessTerminate(
    SysProcessHandle handle,
    std::uint32_t exitCode)
{
    if (!handle)
        return SysProcessTerminateStatus::InvalidHandle;
    auto *const process = static_cast<SysProcess *>(handle);
    if (!process->handle)
        return SysProcessTerminateStatus::InvalidHandle;

    if (TerminateProcess(process->handle, exitCode))
        return SysProcessTerminateStatus::Signaled;
    return SysProcessTerminateStatus::Failed;
}

bool Sys_ProcessGetExitCode(
    SysProcessHandle handle,
    std::uint32_t *exitCode)
{
    if (!handle || !exitCode)
        return false;
    auto *const process = static_cast<SysProcess *>(handle);
    if (!process->handle)
        return false;

    DWORD code = 0;
    if (!GetExitCodeProcess(process->handle, &code))
        return false;
    if (code == STILL_ACTIVE)
        return false;
    *exitCode = code;
    return true;
}

void Sys_ProcessRelease(SysProcessHandle *handle)
{
    if (!handle || !*handle)
        return;
    auto *const process = static_cast<SysProcess *>(*handle);
    if (process->handle)
    {
        CloseHandle(process->handle);
        process->handle = nullptr;
    }
    delete process;
    *handle = nullptr;
}

SysSignalParkStatus Sys_SignalParkEnter()
{
    return SysSignalParkStatus::Unsupported;
}

SysSignalParkLeaveStatus Sys_SignalParkLeave()
{
    return SysSignalParkLeaveStatus::Unsupported;
}

SysMachCrashFreezeStatus Sys_MachCrashFreezeSuspend(SysProcessHandle)
{
    return SysMachCrashFreezeStatus::Unsupported;
}

SysMachCrashFreezeStatus Sys_MachCrashFreezeResume(SysProcessHandle)
{
    return SysMachCrashFreezeStatus::Unsupported;
}

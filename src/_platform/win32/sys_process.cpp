#include <qcommon/sys_process.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Windows.h>

namespace
{
std::wstring WideFromUtf8(const char *const utf8)
{
    if (!utf8)
        return std::wstring();

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8,
        -1,
        nullptr,
        0);
    if (wideLength <= 0)
        return std::wstring();

    std::vector<wchar_t> buffer(static_cast<std::size_t>(wideLength));
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8,
        -1,
        buffer.data(),
        wideLength);
    if (written <= 0)
        return std::wstring();
    return std::wstring(buffer.data(), static_cast<std::size_t>(written - 1));
}

std::wstring QuoteArg(const std::wstring &raw)
{
    std::wstring quoted;
    quoted.reserve(raw.size() + 2);
    quoted.push_back(L'"');
    for (wchar_t ch : raw)
    {
        if (ch == L'\\')
        {
            quoted.push_back(L'\\');
            quoted.push_back(L'\\');
        }
        else if (ch == L'"')
        {
            quoted.push_back(L'\\');
            quoted.push_back(L'"');
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildCommandLine(char *const *argv)
{
    if (!argv)
        return std::wstring();
    std::wstring commandLine;
    bool first = true;
    for (std::size_t index = 0; argv[index] != nullptr; ++index)
    {
        std::wstring piece = WideFromUtf8(argv[index]);
        if (piece.empty())
            piece = std::wstring(L"\"\"");
        else
            piece = QuoteArg(piece);
        if (!first)
            commandLine.push_back(L' ');
        commandLine.append(piece);
        first = false;
    }
    return commandLine;
}

std::wstring BuildEnvironmentBlock(char *const *environIn)
{
    if (!environIn)
        return std::wstring();

    std::wstring block;
    for (std::size_t index = 0; environIn[index] != nullptr; ++index)
    {
        const std::wstring pair = WideFromUtf8(environIn[index]);
        if (pair.empty())
            continue;
        if (pair.find(L'=') == std::wstring::npos)
            continue;
        block.append(pair);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

DWORD WaitForProcess(
    HANDLE process,
    std::uint32_t msec)
{
    if (msec == UINT32_MAX)
        msec = INFINITE;

    const DWORD waitResult = WaitForSingleObject(process, msec);
    if (waitResult == WAIT_OBJECT_0)
        return 0;
    if (waitResult == WAIT_TIMEOUT)
        return WAIT_TIMEOUT;
    return GetLastError();
}
}

SysProcessLaunchResult KISAK_CDECL Sys_ProcessLaunchAndWait(
    const char *utf8Path,
    char *const *argv,
    char *const *environIn,
    std::uint32_t msec) noexcept
{
    SysProcessLaunchResult result{};
    if (!utf8Path || !utf8Path[0] || !argv || !argv[0])
    {
        result.status = SysProcessLaunchStatus::InvalidArgument;
        return result;
    }
    if (msec == UINT32_MAX)
    {
        result.status = SysProcessLaunchStatus::InvalidArgument;
        return result;
    }

    const std::wstring widePath = WideFromUtf8(utf8Path);
    const std::wstring commandLine = BuildCommandLine(argv);
    const std::wstring environment = BuildEnvironmentBlock(environIn);
    if (widePath.empty() || commandLine.empty())
    {
        result.status = SysProcessLaunchStatus::InvalidArgument;
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = environment.empty()
        ? 0
        : CREATE_UNICODE_ENVIRONMENT;

    const BOOL created = CreateProcessW(
        widePath.c_str(),
        const_cast<wchar_t *>(commandLine.c_str()),
        nullptr,
        nullptr,
        FALSE,
        flags,
        environment.empty() ? nullptr : (LPVOID)(environment.c_str()),
        nullptr,
        &startup,
        &process);
    if (!created)
    {
        result.status = SysProcessLaunchStatus::SpawnFailed;
        return result;
    }

    const DWORD waitResult = WaitForProcess(process.hProcess, msec);
    if (waitResult == WAIT_TIMEOUT)
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.status = SysProcessLaunchStatus::TimedOut;
        return result;
    }
    if (waitResult != 0)
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.status = SysProcessLaunchStatus::WaitFailed;
        return result;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.status = SysProcessLaunchStatus::WaitFailed;
        return result;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    result.status = SysProcessLaunchStatus::Completed;
    result.exitCode = static_cast<int>(exitCode);
    return result;
}

SysSignalParkStatus KISAK_CDECL Sys_SignalPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    // Signals are not a first-class Win32 concept; the engine routes SIGINT
    // and SIGTERM through SetConsoleCtrlHandler + GenerateConsoleCtrlEvent
    // and never needs a per-thread block. Return Unsupported so callers can
    // skip the park branch on Win32 without touching the per-thread freeze
    // path.
    (void)signals;
    (void)signalCount;
    return SysSignalParkStatus::Unsupported;
}

SysSignalParkStatus KISAK_CDECL Sys_SignalUnPark(
    const int *signals,
    std::size_t signalCount) noexcept
{
    (void)signals;
    (void)signalCount;
    return SysSignalParkStatus::Unsupported;
}

SysProcessFreezeStatus KISAK_CDECL Sys_ProcessFreezeForCrash() noexcept
{
    // Whole-process freeze on Win32 walks the engine-owned SysThread
    // registry and calls SuspendThread on every captured handle. That
    // primitive already lives in sys_thread.cpp; the freeze entry point
    // here returns Unsupported so the post-mortem handler can fan out
    // across captured handles instead of claiming a single freeze
    // primitive that cannot reach external threads.
    return SysProcessFreezeStatus::Unsupported;
}
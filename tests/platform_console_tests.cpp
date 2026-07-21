#include <qcommon/sys_console.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace
{
const char *checkStage = "startup";

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        return false;
    }
    return true;
}

bool ExpectRead(
    const char *stage,
    SysConsoleReadStatus expectedStatus,
    std::string_view expected = {},
    std::size_t capacity = SYS_CONSOLE_MAX_LINE_LENGTH + 1);

#if defined(_WIN32)
DWORD OutputIdentifier(const SysConsoleOutputStream stream)
{
    return stream == SysConsoleOutputStream::StandardOutput
        ? STD_OUTPUT_HANDLE
        : STD_ERROR_HANDLE;
}

class OutputCapture
{
public:
    explicit OutputCapture(const SysConsoleOutputStream stream)
        : identifier_(OutputIdentifier(stream)), saved_(GetStdHandle(identifier_))
    {
        if (CreatePipe(&read_, &write_, nullptr, 0)
            && SetStdHandle(identifier_, write_))
        {
            active_ = true;
        }
    }

    OutputCapture(const OutputCapture &) = delete;
    OutputCapture &operator=(const OutputCapture &) = delete;

    ~OutputCapture()
    {
        Restore();
        if (read_)
            CloseHandle(read_);
        if (write_)
            CloseHandle(write_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

    bool Finish(std::string *const captured)
    {
        if (!active_ || !captured)
            return false;
        Restore();
        captured->clear();
        std::array<char, 256> chunk{};
        for (;;)
        {
            DWORD readCount = 0;
            if (ReadFile(
                    read_,
                    chunk.data(),
                    static_cast<DWORD>(chunk.size()),
                    &readCount,
                    nullptr))
            {
                if (readCount == 0)
                    return true;
                captured->append(chunk.data(), readCount);
                continue;
            }
            return GetLastError() == ERROR_BROKEN_PIPE;
        }
    }

private:
    void Restore()
    {
        if (!active_)
            return;
        (void)SetStdHandle(identifier_, saved_);
        CloseHandle(write_);
        write_ = nullptr;
        active_ = false;
    }

    DWORD identifier_;
    HANDLE saved_ = nullptr;
    HANDLE read_ = nullptr;
    HANDLE write_ = nullptr;
    bool active_ = false;
};

class MissingOutput
{
public:
    explicit MissingOutput(const SysConsoleOutputStream stream)
        : identifier_(OutputIdentifier(stream)), saved_(GetStdHandle(identifier_))
    {
        active_ = SetStdHandle(identifier_, nullptr) != FALSE;
    }

    ~MissingOutput()
    {
        if (active_)
            (void)SetStdHandle(identifier_, saved_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    DWORD identifier_;
    HANDLE saved_ = nullptr;
    bool active_ = false;
};

class BrokenOutput
{
public:
    explicit BrokenOutput(const SysConsoleOutputStream stream)
        : identifier_(OutputIdentifier(stream)), saved_(GetStdHandle(identifier_))
    {
        HANDLE reader = nullptr;
        if (CreatePipe(&reader, &write_, nullptr, 0))
        {
            CloseHandle(reader);
            active_ = SetStdHandle(identifier_, write_) != FALSE;
        }
    }

    BrokenOutput(const BrokenOutput &) = delete;
    BrokenOutput &operator=(const BrokenOutput &) = delete;

    ~BrokenOutput()
    {
        if (active_)
            (void)SetStdHandle(identifier_, saved_);
        if (write_)
            CloseHandle(write_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    DWORD identifier_;
    HANDLE saved_ = nullptr;
    HANDLE write_ = nullptr;
    bool active_ = false;
};

class InputFile
{
public:
    explicit InputFile(const std::string_view bytes)
        : saved_(GetStdHandle(STD_INPUT_HANDLE))
    {
        std::array<WCHAR, MAX_PATH + 1> directory{};
        std::array<WCHAR, MAX_PATH + 1> path{};
        const WCHAR prefix[] = {'k', 's', 'i', 0};
        if (bytes.size() > MAXDWORD
            || GetTempPathW(
                static_cast<DWORD>(directory.size()),
                directory.data()) == 0
            || GetTempFileNameW(
                directory.data(), prefix, 0, path.data()) == 0)
        {
            return;
        }

        file_ = CreateFileW(
            path.data(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            nullptr);
        if (file_ == INVALID_HANDLE_VALUE)
        {
            (void)DeleteFileW(path.data());
            return;
        }

        DWORD written = 0;
        LARGE_INTEGER beginning{};
        if (!WriteFile(
                file_,
                bytes.data(),
                static_cast<DWORD>(bytes.size()),
                &written,
                nullptr)
            || written != bytes.size()
            || !SetFilePointerEx(file_, beginning, nullptr, FILE_BEGIN))
        {
            return;
        }
        active_ = SetStdHandle(STD_INPUT_HANDLE, file_) != FALSE;
    }

    InputFile(const InputFile &) = delete;
    InputFile &operator=(const InputFile &) = delete;

    ~InputFile()
    {
        if (active_)
            (void)SetStdHandle(STD_INPUT_HANDLE, saved_);
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    HANDLE saved_ = nullptr;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    bool active_ = false;
};

class InputPipe
{
public:
    InputPipe() : saved_(GetStdHandle(STD_INPUT_HANDLE))
    {
        if (CreatePipe(&read_, &write_, nullptr, 0)
            && SetStdHandle(STD_INPUT_HANDLE, read_))
        {
            active_ = true;
        }
    }

    InputPipe(const InputPipe &) = delete;
    InputPipe &operator=(const InputPipe &) = delete;

    ~InputPipe()
    {
        if (active_)
            (void)SetStdHandle(STD_INPUT_HANDLE, saved_);
        if (read_)
            CloseHandle(read_);
        if (write_)
            CloseHandle(write_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

    bool Write(const std::string_view bytes)
    {
        std::size_t offset = 0;
        while (offset != bytes.size())
        {
            const std::size_t remaining = bytes.size() - offset;
            const DWORD request = remaining > MAXDWORD
                ? MAXDWORD
                : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(
                    write_,
                    bytes.data() + offset,
                    request,
                    &written,
                    nullptr)
                || written == 0)
            {
                return false;
            }
            offset += written;
        }
        return true;
    }

    void CloseWriter()
    {
        if (write_)
        {
            CloseHandle(write_);
            write_ = nullptr;
        }
    }

private:
    HANDLE saved_ = nullptr;
    HANDLE read_ = nullptr;
    HANDLE write_ = nullptr;
    bool active_ = false;
};

class ConsoleInput
{
public:
    ConsoleInput() : saved_(GetStdHandle(STD_INPUT_HANDLE))
    {
        // Tests launch with or without an inherited console. If one is not
        // already attached, allocate one so the backend sees FILE_TYPE_CHAR.
        // AllocConsole fails harmlessly when a console is already present;
        // either way, the resulting STD_INPUT_HANDLE is the console input.
        const bool allocated = AllocConsole() != FALSE;
        owned_ = allocated;
        const HANDLE current = GetStdHandle(STD_INPUT_HANDLE);
        if (current == nullptr || current == INVALID_HANDLE_VALUE)
            return;
        if (GetFileType(current) != FILE_TYPE_CHAR)
            return;
        active_ = SetStdHandle(STD_INPUT_HANDLE, current) != FALSE
            && FlushConsoleInputBuffer(current) != FALSE;
        if (!active_)
            return;
        input_ = current;
    }

    ConsoleInput(const ConsoleInput &) = delete;
    ConsoleInput &operator=(const ConsoleInput &) = delete;

    ~ConsoleInput()
    {
        if (active_)
            (void)SetStdHandle(STD_INPUT_HANDLE, saved_);
        if (owned_)
            FreeConsole();
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_ && input_ != nullptr && input_ != INVALID_HANDLE_VALUE;
    }

    bool WriteEvents(const INPUT_RECORD *events, const DWORD count)
    {
        if (!IsReady() || events == nullptr || count == 0)
            return false;
        DWORD written = 0;
        return WriteConsoleInput(input_, events, count, &written) != FALSE
            && written == count;
    }

private:
    HANDLE saved_ = nullptr;
    HANDLE input_ = nullptr;
    bool owned_ = false;
    bool active_ = false;
};

bool TestValidFlush()
{
    std::array<WCHAR, MAX_PATH + 1> directory{};
    std::array<WCHAR, MAX_PATH + 1> path{};
    const WCHAR prefix[] = {'k', 's', 'c', 0};
    if (GetTempPathW(
            static_cast<DWORD>(directory.size()),
            directory.data()) == 0
        || GetTempFileNameW(
            directory.data(), prefix, 0, path.data()) == 0)
    {
        return Check(false, "create flush fixture");
    }

    const HANDLE file = CreateFileW(
        path.data(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        (void)DeleteFileW(path.data());
        return Check(false, "open flush fixture");
    }

    const HANDLE saved = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected = SetStdHandle(STD_OUTPUT_HANDLE, file) != FALSE;
    const char bytes[] = "flush";
    const bool passed = redirected
        && Sys_ConsoleIsRedirected(SysConsoleOutputStream::StandardOutput)
        && Sys_ConsoleWrite(
            SysConsoleOutputStream::StandardOutput,
            bytes,
            sizeof(bytes) - 1) == SysConsoleIoStatus::Complete
        && Sys_ConsoleFlush(SysConsoleOutputStream::StandardOutput)
            == SysConsoleIoStatus::Complete;
    if (redirected)
        (void)SetStdHandle(STD_OUTPUT_HANDLE, saved);
    CloseHandle(file);
    (void)DeleteFileW(path.data());
    return Check(passed, "valid disk flush");
}

bool TestMessageModePipe()
{
    std::array<char, 160> pipeName{};
    const int formatted = std::snprintf(
            pipeName.data(),
            pipeName.size(),
            "\\\\.\\pipe\\kisakcod-console-%lu-%llu",
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long long>(GetTickCount64()));
    if (formatted < 0
        || static_cast<std::size_t>(formatted) >= pipeName.size())
    {
        return Check(false, "format message pipe name");
    }

    const HANDLE server = CreateNamedPipeA(
        pipeName.data(),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        256,
        256,
        0,
        nullptr);
    if (server == INVALID_HANDLE_VALUE)
        return Check(false, "create message pipe");

    const HANDLE writer = CreateFileA(
        pipeName.data(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (writer == INVALID_HANDLE_VALUE)
    {
        CloseHandle(server);
        return Check(false, "open message pipe writer");
    }
    const bool connected = ConnectNamedPipe(server, nullptr) != FALSE
        || GetLastError() == ERROR_PIPE_CONNECTED;
    const char message[] = "message\n";
    DWORD written = 0;
    const bool clientPassed = connected
        && WriteFile(
            writer,
            message,
            static_cast<DWORD>(sizeof(message) - 1),
            &written,
            nullptr) != FALSE
        && written == sizeof(message) - 1;
    if (!clientPassed)
    {
        CloseHandle(writer);
        CloseHandle(server);
        return Check(false, "write message pipe");
    }

    const HANDLE saved = GetStdHandle(STD_INPUT_HANDLE);
    const bool redirected = SetStdHandle(STD_INPUT_HANDLE, server) != FALSE;
    const bool passed = redirected
        && ExpectRead(
            "message pipe preserves ERROR_MORE_DATA bytes",
            SysConsoleReadStatus::LineReady,
            "message");
    if (redirected)
        (void)SetStdHandle(STD_INPUT_HANDLE, saved);
    CloseHandle(writer);
    CloseHandle(server);
    return passed;
}

bool TestConsoleInput()
{
    ConsoleInput input;
    if (!Check(input.IsReady(), "create console input"))
        return false;

    // An empty console input queue is nonblocking, like the pipe/disk paths.
    if (!ExpectRead("empty console is nonblocking", SysConsoleReadStatus::NoData))
        return false;

    auto KeyEvent = [](const bool keyDown, const char ascii) {
        INPUT_RECORD record{};
        record.EventType = KEY_EVENT;
        record.Event.KeyEvent.bKeyDown = keyDown;
        record.Event.KeyEvent.uChar.AsciiChar = ascii;
        return record;
    };

    auto ResizeEvent = []() {
        INPUT_RECORD record{};
        record.EventType = WINDOW_BUFFER_SIZE_EVENT;
        record.Event.WindowBufferSizeEvent.dwSize = {1, 1};
        return record;
    };

    auto CtrlCEvent = []() {
        INPUT_RECORD record{};
        record.EventType = CTRL_C_EVENT;
        return record;
    };

    // Single printable byte, with KEY_UP drained first and a non-key event
    // interleaved to prove the drain order.
    {
        const INPUT_RECORD events[] = {
            ResizeEvent(),
            KeyEvent(false, 'a'),
            KeyEvent(true, 'h'),
            CtrlCEvent(),
            KeyEvent(true, 'i'),
        };
        if (!Check(
                input.WriteEvents(events, sizeof(events) / sizeof(events[0])),
                "write printable console events")
            || !ExpectRead(
                "console KEY_UP and non-key records drain first",
                SysConsoleReadStatus::LineReady,
                "hi"))
        {
            return false;
        }
    }

    // CRLF line; the cooked console will not insert the LF if ENABLE_PROCESSED_INPUT
    // does not have ENABLE_LINE_INPUT, but our boundary is bytewise so the caller
    // writes both bytes itself.
    {
        const INPUT_RECORD events[] = {
            KeyEvent(true, 'o'),
            KeyEvent(true, 'k'),
            KeyEvent(true, '\r'),
            KeyEvent(true, '\n'),
        };
        if (!Check(
                input.WriteEvents(events, sizeof(events) / sizeof(events[0])),
                "write complete console line")
            || !ExpectRead(
                "console CRLF line",
                SysConsoleReadStatus::LineReady,
                "ok"))
        {
            return false;
        }
    }

    // Zero-AsciiChar keys (arrow keys, function keys in cooked mode) must be
    // drained without producing a byte.
    {
        const INPUT_RECORD events[] = {
            KeyEvent(true, 0),
            KeyEvent(true, 'x'),
            KeyEvent(true, 0),
            KeyEvent(true, 'y'),
            KeyEvent(true, '\n'),
        };
        if (!Check(
                input.WriteEvents(events, sizeof(events) / sizeof(events[0])),
                "write zero-ascii console events")
            || !ExpectRead(
                "zero-ascii console keys are drained",
                SysConsoleReadStatus::LineReady,
                "xy"))
        {
            return false;
        }
    }

    return true;
}
#else
int OutputDescriptor(const SysConsoleOutputStream stream)
{
    return stream == SysConsoleOutputStream::StandardOutput
        ? STDOUT_FILENO
        : STDERR_FILENO;
}

class OutputCapture
{
public:
    explicit OutputCapture(const SysConsoleOutputStream stream)
        : descriptor_(OutputDescriptor(stream)), saved_(dup(descriptor_))
    {
        int descriptors[2] = {-1, -1};
        if (saved_ >= 0 && pipe(descriptors) == 0)
        {
            read_ = descriptors[0];
            if (dup2(descriptors[1], descriptor_) >= 0)
                active_ = true;
            close(descriptors[1]);
        }
    }

    OutputCapture(const OutputCapture &) = delete;
    OutputCapture &operator=(const OutputCapture &) = delete;

    ~OutputCapture()
    {
        Restore();
        if (saved_ >= 0)
            close(saved_);
        if (read_ >= 0)
            close(read_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

    bool Finish(std::string *const captured)
    {
        if (!active_ || !captured)
            return false;
        Restore();
        captured->clear();
        std::array<char, 256> chunk{};
        for (;;)
        {
            const ssize_t readCount = read(read_, chunk.data(), chunk.size());
            if (readCount > 0)
            {
                captured->append(
                    chunk.data(), static_cast<std::size_t>(readCount));
                continue;
            }
            if (readCount < 0 && errno == EINTR)
                continue;
            return readCount == 0;
        }
    }

private:
    void Restore()
    {
        if (!active_)
            return;
        (void)dup2(saved_, descriptor_);
        close(saved_);
        saved_ = -1;
        active_ = false;
    }

    int descriptor_ = -1;
    int saved_ = -1;
    int read_ = -1;
    bool active_ = false;
};

class MissingOutput
{
public:
    explicit MissingOutput(const SysConsoleOutputStream stream)
        : descriptor_(OutputDescriptor(stream)), saved_(dup(descriptor_))
    {
        if (saved_ >= 0 && close(descriptor_) == 0)
            active_ = true;
    }

    ~MissingOutput()
    {
        if (active_)
            (void)dup2(saved_, descriptor_);
        if (saved_ >= 0)
            close(saved_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    int descriptor_ = -1;
    int saved_ = -1;
    bool active_ = false;
};

class BrokenOutput
{
public:
    explicit BrokenOutput(const SysConsoleOutputStream stream)
        : descriptor_(OutputDescriptor(stream)), saved_(dup(descriptor_))
    {
        int descriptors[2] = {-1, -1};
        if (saved_ >= 0 && pipe(descriptors) == 0)
        {
            close(descriptors[0]);
            if (dup2(descriptors[1], descriptor_) >= 0)
                active_ = true;
            close(descriptors[1]);
        }
    }

    BrokenOutput(const BrokenOutput &) = delete;
    BrokenOutput &operator=(const BrokenOutput &) = delete;

    ~BrokenOutput()
    {
        if (active_)
            (void)dup2(saved_, descriptor_);
        if (saved_ >= 0)
            close(saved_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    int descriptor_ = -1;
    int saved_ = -1;
    bool active_ = false;
};

class InputFile
{
public:
    explicit InputFile(const std::string_view bytes)
        : saved_(dup(STDIN_FILENO)), file_(std::tmpfile())
    {
        if (saved_ < 0 || !file_)
            return;
        if (std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size()
            || std::fflush(file_) != 0
            || std::fseek(file_, 0, SEEK_SET) != 0)
        {
            return;
        }
        active_ = dup2(fileno(file_), STDIN_FILENO) >= 0;
    }

    InputFile(const InputFile &) = delete;
    InputFile &operator=(const InputFile &) = delete;

    ~InputFile()
    {
        if (active_)
            (void)dup2(saved_, STDIN_FILENO);
        if (saved_ >= 0)
            close(saved_);
        if (file_)
            std::fclose(file_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

private:
    int saved_ = -1;
    std::FILE *file_ = nullptr;
    bool active_ = false;
};

class InputPipe
{
public:
    InputPipe() : saved_(dup(STDIN_FILENO))
    {
        int descriptors[2] = {-1, -1};
        if (saved_ >= 0 && pipe(descriptors) == 0)
        {
            read_ = descriptors[0];
            write_ = descriptors[1];
            if (dup2(read_, STDIN_FILENO) >= 0)
                active_ = true;
        }
    }

    InputPipe(const InputPipe &) = delete;
    InputPipe &operator=(const InputPipe &) = delete;

    ~InputPipe()
    {
        if (active_)
            (void)dup2(saved_, STDIN_FILENO);
        if (saved_ >= 0)
            close(saved_);
        if (read_ >= 0)
            close(read_);
        if (write_ >= 0)
            close(write_);
    }

    [[nodiscard]] bool IsReady() const
    {
        return active_;
    }

    bool Write(const std::string_view bytes)
    {
        std::size_t offset = 0;
        while (offset != bytes.size())
        {
            const ssize_t written = write(
                write_, bytes.data() + offset, bytes.size() - offset);
            if (written > 0)
            {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

    void CloseWriter()
    {
        if (write_ >= 0)
        {
            close(write_);
            write_ = -1;
        }
    }

private:
    int saved_ = -1;
    int read_ = -1;
    int write_ = -1;
    bool active_ = false;
};

bool TestValidFlush()
{
    OutputCapture capture(SysConsoleOutputStream::StandardOutput);
    if (!Check(capture.IsReady(), "create flush capture"))
        return false;
    const bool flushed = Sys_ConsoleFlush(
        SysConsoleOutputStream::StandardOutput) == SysConsoleIoStatus::Complete;
    std::string ignored;
    const bool finished = capture.Finish(&ignored);
    return Check(flushed && finished, "valid descriptor flush");
}
#endif

bool TestOutput(const SysConsoleOutputStream stream)
{
    OutputCapture capture(stream);
    if (!Check(capture.IsReady(), "create output capture"))
        return false;

    const char bytes[] = {'A', '\0', 'B', '\n'};
    const bool redirected = Sys_ConsoleIsRedirected(stream);
    const bool written = Sys_ConsoleWrite(stream, bytes, sizeof(bytes))
        == SysConsoleIoStatus::Complete;
    const bool flushed = Sys_ConsoleFlush(stream)
        == SysConsoleIoStatus::Complete;
    std::string captured;
    const bool finished = capture.Finish(&captured);
    return Check(
        redirected
            && written
            && flushed
            && finished
            && captured == std::string(bytes, sizeof(bytes)),
        stream == SysConsoleOutputStream::StandardOutput
            ? "stdout exact byte write"
            : "stderr exact byte write");
}

bool TestBrokenOutputWrite()
{
    BrokenOutput output(SysConsoleOutputStream::StandardOutput);
    return Check(
        output.IsReady()
            && Sys_ConsoleWrite(
                SysConsoleOutputStream::StandardOutput, "broken", 6)
                == SysConsoleIoStatus::IoError,
        "broken pipe returns I/O error");
}

bool TestUnavailableOutput()
{
    const SysConsoleOutputStream stream =
        SysConsoleOutputStream::StandardOutput;
    MissingOutput missing(stream);
    if (!Check(missing.IsReady(), "remove stdout"))
        return false;
    return Check(
        !Sys_ConsoleIsRedirected(stream)
            && Sys_ConsoleWrite(stream, "x", 1)
                == SysConsoleIoStatus::Unavailable
            && Sys_ConsoleFlush(stream) == SysConsoleIoStatus::Unavailable,
        "unavailable stdout status");
}

bool ExpectRead(
    const char *const stage,
    const SysConsoleReadStatus expectedStatus,
    const std::string_view expected,
    const std::size_t capacity)
{
    std::array<char, SYS_CONSOLE_MAX_LINE_LENGTH + 1> output{};
    output.fill('x');
    const SysConsoleReadResult result =
        Sys_ConsoleTryReadLine(output.data(), capacity);
    if (result.status != expectedStatus)
        return Check(false, stage);
    if (expectedStatus == SysConsoleReadStatus::LineReady)
    {
        return Check(
            result.length == expected.size()
                && std::memcmp(output.data(), expected.data(), expected.size()) == 0
                && output[result.length] == '\0',
            stage);
    }
    return Check(result.length == 0 && output[0] == '\0', stage);
}

bool TestReadBudget()
{
    std::string rejected(4097, 'q');
    rejected += "\nnext\n";
    InputFile input(rejected);
    return Check(input.IsReady(), "create budget input file")
        && ExpectRead(
            "rejected line stops at read budget",
            SysConsoleReadStatus::NoData)
        && ExpectRead(
            "rejected line resumes to terminator",
            SysConsoleReadStatus::Truncated)
        && ExpectRead(
            "line after budgeted rejection",
            SysConsoleReadStatus::LineReady,
            "next");
}

bool TestInvalidAtEndOfFile()
{
    const char bytes[] = {'b', 'a', 'd', '\0', 't', 'a', 'i', 'l'};
    InputFile input(std::string_view(bytes, sizeof(bytes)));
    return Check(input.IsReady(), "create invalid EOF input file")
        && ExpectRead(
            "EOF publishes invalid partial line",
            SysConsoleReadStatus::InvalidData)
        && ExpectRead("stable invalid EOF", SysConsoleReadStatus::EndOfFile);
}

#if !defined(_WIN32)
bool TestSigpipeDisposition(
    void (*const disposition)(int),
    const bool verifyPendingPreservation)
{
    sigset_t pipeSignal{};
    sigset_t savedMask{};
    struct sigaction savedAction{};
    struct sigaction testAction{};
    if (sigemptyset(&pipeSignal) != 0
        || sigaddset(&pipeSignal, SIGPIPE) != 0
        || pthread_sigmask(SIG_SETMASK, nullptr, &savedMask) != 0
        || sigaction(SIGPIPE, nullptr, &savedAction) != 0)
    {
        return Check(false, "capture SIGPIPE state");
    }

    testAction.sa_handler = disposition;
    (void)sigemptyset(&testAction.sa_mask);
    bool passed = sigaction(SIGPIPE, &testAction, nullptr) == 0
        && pthread_sigmask(SIG_UNBLOCK, &pipeSignal, nullptr) == 0
        && TestBrokenOutputWrite();

    sigset_t currentMask{};
    sigset_t pending{};
    passed = passed
        && pthread_sigmask(SIG_SETMASK, nullptr, &currentMask) == 0
        && sigismember(&currentMask, SIGPIPE) == 0
        && sigpending(&pending) == 0
        && sigismember(&pending, SIGPIPE) == 0;

    if (passed && verifyPendingPreservation)
    {
        BrokenOutput output(SysConsoleOutputStream::StandardOutput);
        passed = output.IsReady()
            && pthread_sigmask(SIG_BLOCK, &pipeSignal, nullptr) == 0
            && raise(SIGPIPE) == 0
            && sigpending(&pending) == 0
            && sigismember(&pending, SIGPIPE) == 1
            && Sys_ConsoleWrite(
                SysConsoleOutputStream::StandardOutput, "again", 5)
                == SysConsoleIoStatus::IoError
            && sigpending(&pending) == 0
            && sigismember(&pending, SIGPIPE) == 1;
        int received = 0;
        if (passed)
            passed = sigwait(&pipeSignal, &received) == 0
                && received == SIGPIPE;
    }

    sigset_t cleanupPending{};
    if (sigpending(&cleanupPending) == 0
        && sigismember(&cleanupPending, SIGPIPE) == 1)
    {
        int received = 0;
        passed = pthread_sigmask(SIG_BLOCK, &pipeSignal, nullptr) == 0
            && sigwait(&pipeSignal, &received) == 0
            && received == SIGPIPE
            && passed;
    }

    const bool actionRestored = sigaction(SIGPIPE, &savedAction, nullptr) == 0;
    const bool maskRestored =
        pthread_sigmask(SIG_SETMASK, &savedMask, nullptr) == 0;
    return Check(
        passed && actionRestored && maskRestored,
        disposition == SIG_DFL
            ? "default SIGPIPE containment"
            : "ignored SIGPIPE containment");
}
#endif

bool TestInput()
{
    InputPipe input;
    if (!Check(input.IsReady(), "create input pipe"))
        return false;
    if (!ExpectRead("empty pipe is nonblocking", SysConsoleReadStatus::NoData))
        return false;
    if (!Check(input.Write("hel"), "write partial line")
        || !ExpectRead("partial line is nonblocking", SysConsoleReadStatus::NoData))
    {
        return false;
    }
    if (!Check(input.Write("lo\r\n\nsecond\n"), "write complete lines")
        || !ExpectRead("CRLF line", SysConsoleReadStatus::LineReady, "hello")
        || !ExpectRead("empty line", SysConsoleReadStatus::LineReady, "")
        || !ExpectRead("buffered second line", SysConsoleReadStatus::LineReady, "second"))
    {
        return false;
    }
    if (!Check(input.Write("wide\nok\n"), "write output-truncation lines")
        || !ExpectRead(
            "small destination rejects complete line",
            SysConsoleReadStatus::Truncated,
            {},
            4)
        || !ExpectRead("line after small destination", SysConsoleReadStatus::LineReady, "ok"))
    {
        return false;
    }

    std::string overlong(SYS_CONSOLE_MAX_LINE_LENGTH + 1, 'x');
    overlong += "\nnext\n";
    if (!Check(input.Write(overlong), "write overlong line")
        || !ExpectRead("overlong line drained", SysConsoleReadStatus::Truncated)
        || !ExpectRead("line after overlong input", SysConsoleReadStatus::LineReady, "next"))
    {
        return false;
    }

    const char invalidBytes[] = {'b', 'a', 'd', '\0', 't', 'a', 'i', 'l', '\n'};
    if (!Check(
            input.Write(std::string_view(invalidBytes, sizeof(invalidBytes)))
                && input.Write("safe\n"),
            "write embedded NUL line")
        || !ExpectRead("embedded NUL line drained", SysConsoleReadStatus::InvalidData)
        || !ExpectRead("line after embedded NUL", SysConsoleReadStatus::LineReady, "safe"))
    {
        return false;
    }

    if (!Check(input.Write("tail"), "write EOF fragment"))
        return false;
    input.CloseWriter();
    return ExpectRead(
               "EOF publishes final partial line",
               SysConsoleReadStatus::LineReady,
               "tail")
        && ExpectRead("stable EOF", SysConsoleReadStatus::EndOfFile);
}
} // namespace

int main(const int argc, char **const argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--invalid-eof") == 0)
        return TestInvalidAtEndOfFile() ? 0 : 1;
#if !defined(_WIN32)
    if (argc == 2 && std::strcmp(argv[1], "--sigpipe-default") == 0)
        return TestSigpipeDisposition(SIG_DFL, true) ? 0 : 1;
    if (argc == 2 && std::strcmp(argv[1], "--sigpipe-ignore") == 0)
        return TestSigpipeDisposition(SIG_IGN, false) ? 0 : 1;
#endif
    if (argc != 1)
    {
        std::fprintf(stderr, "FAIL: unknown platform console test mode\n");
        return 2;
    }

    const SysConsoleOutputStream invalidStream =
        static_cast<SysConsoleOutputStream>(0xff);
    char output = 'x';
    if (!Check(
            Sys_ConsoleWrite(invalidStream, "x", 1)
                    == SysConsoleIoStatus::InvalidArgument
                && Sys_ConsoleWrite(
                    SysConsoleOutputStream::StandardOutput, nullptr, 1)
                    == SysConsoleIoStatus::InvalidArgument
                && Sys_ConsoleWrite(
                    SysConsoleOutputStream::StandardOutput, nullptr, 0)
                    == SysConsoleIoStatus::Complete
                && Sys_ConsoleFlush(invalidStream)
                    == SysConsoleIoStatus::InvalidArgument
                && !Sys_ConsoleIsRedirected(invalidStream)
                && Sys_ConsoleTryReadLine(nullptr, 1).status
                    == SysConsoleReadStatus::InvalidArgument
                && Sys_ConsoleTryReadLine(&output, 0).status
                    == SysConsoleReadStatus::InvalidArgument,
            "invalid API arguments")
        || !TestOutput(SysConsoleOutputStream::StandardOutput)
        || !TestOutput(SysConsoleOutputStream::StandardError)
        || !TestValidFlush()
        || !TestUnavailableOutput()
#if defined(_WIN32)
        || !TestBrokenOutputWrite()
        || !TestMessageModePipe()
        || !TestConsoleInput()
#endif
        || !TestReadBudget()
        || !TestInput())
    {
        std::fprintf(stderr, "FAIL: platform console stage: %s\n", checkStage);
        return 1;
    }
    return 0;
}

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
    std::string captured;
    const bool finished = capture.Finish(&captured);
    return Check(
        redirected
            && written
            && finished
            && captured == std::string(bytes, sizeof(bytes)),
        stream == SysConsoleOutputStream::StandardOutput
            ? "stdout exact byte write"
            : "stderr exact byte write");
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
    const std::string_view expected = {},
    const std::size_t capacity = SYS_CONSOLE_MAX_LINE_LENGTH + 1)
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

int main()
{
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
        || !TestInput())
    {
        std::fprintf(stderr, "FAIL: platform console stage: %s\n", checkStage);
        return 1;
    }
    return 0;
}

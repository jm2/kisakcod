// Read test for the portable async-read file service (sys_file): open,
// read-at-offset, wait and close over a real fixture. The fast-file loader
// stages its ring through exactly these calls, so the contract that matters is
// byte-exact transfers, short reads at end of file, and the issue-time EOF
// answer on both platforms.

#include <qcommon/sys_file.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
std::string gPath;
SysFileHandle gFile = nullptr;

int Fail(const char *const message)
{
    std::fprintf(stderr, "sys_file test failed: %s\n", message);
    if (gFile)
        Sys_FileClose(&gFile);
    if (!gPath.empty())
        std::remove(gPath.c_str());
    return 1;
}

bool WriteFixture(const std::vector<std::uint8_t> &bytes)
{
    FILE *const file = std::fopen(gPath.c_str(), "wb");
    if (!file)
        return false;
    const bool written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && written;
}

// Issues one request and harvests its outcome. A backend may answer at issue
// time (POSIX pread, Win32 refusal at end of file) or leave the request
// pending for the wait; both shapes are legal and must report the same bytes.
SysFileReadResult ReadAt(
    const std::uint64_t offset,
    void *buffer,
    const std::uint32_t bytes)
{
    const SysFileReadResult issue = Sys_FileReadBegin(gFile, offset, buffer, bytes);
    return issue.status == SysFileReadStatus::Pending
        ? Sys_FileReadWait(gFile, 5000u)
        : issue;
}

bool SameBytes(const std::uint8_t *a, const std::uint8_t *b, const std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}
}

int main()
{
    std::vector<std::uint8_t> fixture(1000);
    for (std::size_t i = 0; i < fixture.size(); ++i)
        fixture[i] = static_cast<std::uint8_t>((i * 7u + 13u) & 0xFFu);

    gPath = "kisakcod-sys-file-test-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    if (!WriteFixture(fixture))
        return Fail("could not write the fixture file");

    if (Sys_FileOpenRead((gPath + ".missing").c_str()))
        return Fail("open succeeded for a missing file");

    gFile = Sys_FileOpenRead(gPath.c_str());
    if (!gFile)
        return Fail("open failed for the fixture file");

    std::uint64_t size = 0;
    if (!Sys_FileGetSize(gFile, &size) || size != fixture.size())
        return Fail("reported file size does not match the fixture");

    // A full request in the middle of the file transfers exactly.
    {
        std::uint8_t buffer[128] = {};
        const SysFileReadResult result = ReadAt(100, buffer, 128);
        if (result.status != SysFileReadStatus::Complete
            || result.bytes != 128
            || result.error != 0
            || !SameBytes(buffer, fixture.data() + 100, 128))
        {
            return Fail("mid-file read did not transfer the requested bytes");
        }
    }

    // A request crossing end of file transfers the tail and reports Eof.
    {
        std::uint8_t buffer[128] = {};
        const SysFileReadResult result = ReadAt(940, buffer, 128);
        if (result.status != SysFileReadStatus::Eof
            || result.bytes != 60
            || !SameBytes(buffer, fixture.data() + 940, 60))
        {
            return Fail("short read at end of file misbehaved");
        }
    }

    // A request at or past end of file transfers nothing and reports Eof.
    {
        std::uint8_t buffer[64] = {};
        SysFileReadResult result = ReadAt(fixture.size(), buffer, 64);
        if (result.status != SysFileReadStatus::Eof || result.bytes != 0)
            return Fail("read at end of file did not report Eof");
        result = ReadAt(fixture.size() + 4096, buffer, 64);
        if (result.status != SysFileReadStatus::Eof || result.bytes != 0)
            return Fail("read past end of file did not report Eof");
    }

    // Sequential staging reads from the start reproduce the whole file. Every
    // request here is fully satisfiable, so each reports Complete; the end of
    // the stream is discovered by the follow-up read (above).
    {
        std::uint8_t buffer[256] = {};
        std::size_t offset = 0;
        while (offset < fixture.size())
        {
            const std::uint32_t request = static_cast<std::uint32_t>(
                fixture.size() - offset < sizeof(buffer)
                    ? fixture.size() - offset
                    : sizeof(buffer));
            const SysFileReadResult result = ReadAt(offset, buffer, request);
            if (result.status != SysFileReadStatus::Complete
                || result.bytes != request
                || !SameBytes(buffer, fixture.data() + offset, request))
            {
                return Fail("sequential staging read misbehaved");
            }
            offset += request;
        }
    }

    // Argument and request-state contracts.
    {
        std::uint8_t buffer[16] = {};
        if (Sys_FileReadBegin(gFile, 0, buffer, 0).status != SysFileReadStatus::Invalid)
            return Fail("zero-size request was accepted");
        if (Sys_FileReadBegin(gFile, 0, nullptr, 16).status != SysFileReadStatus::Invalid)
            return Fail("null-buffer request was accepted");
        if (Sys_FileReadBegin(gFile, 0, buffer, 16).status != SysFileReadStatus::Pending)
            return Fail("first request was not accepted");
        if (Sys_FileReadBegin(gFile, 64, buffer, 16).status != SysFileReadStatus::Invalid)
            return Fail("second concurrent request was accepted");
        const SysFileReadResult result = Sys_FileReadWait(gFile, 5000u);
        if (result.status != SysFileReadStatus::Complete || result.bytes != 16)
            return Fail("outstanding request did not complete");
        if (Sys_FileReadWait(gFile, 5000u).status != SysFileReadStatus::Invalid)
            return Fail("wait without an outstanding request was accepted");
    }

    Sys_FileClose(&gFile);
    if (gFile)
        return Fail("close did not reset the caller's handle");
    std::remove(gPath.c_str());
    gPath.clear();
    return 0;
}

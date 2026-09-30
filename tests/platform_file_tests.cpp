// Read test for the portable async-read file service (sys_file): open,
// read-at-offset, wait and close over a real fixture, checking byte-exact
// transfers, short reads at EOF and the issue-time EOF answer.

#include <qcommon/sys_file.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    // std::ofstream, not fopen: MSVC's C4996 deprecation is an error in CI.
    std::ofstream file(gPath, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
}

// Issues one request and harvests its outcome. A backend may answer at issue
// time or leave the request pending for the wait; both shapes are legal.
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

    std::uint8_t buffer[256] = {};

    // A full request in the middle of the file transfers exactly.
    SysFileReadResult r = ReadAt(100, buffer, 128);
    if (r.status != SysFileReadStatus::Complete || r.bytes != 128 || r.error != 0
        || std::memcmp(buffer, fixture.data() + 100, 128) != 0)
        return Fail("mid-file read did not transfer the requested bytes");

    // A request crossing end of file transfers the tail and reports Eof.
    r = ReadAt(940, buffer, 128);
    if (r.status != SysFileReadStatus::Eof || r.bytes != 60
        || std::memcmp(buffer, fixture.data() + 940, 60) != 0)
        return Fail("short read at end of file misbehaved");

    // A request at or past end of file transfers nothing and reports Eof.
    r = ReadAt(fixture.size(), buffer, 64);
    if (r.status != SysFileReadStatus::Eof || r.bytes != 0)
        return Fail("read at end of file did not report Eof");
    r = ReadAt(fixture.size() + 4096, buffer, 64);
    if (r.status != SysFileReadStatus::Eof || r.bytes != 0)
        return Fail("read past end of file did not report Eof");

    // Sequential staging reads reproduce the whole file.
    for (std::size_t offset = 0; offset < fixture.size();)
    {
        const std::uint32_t request = static_cast<std::uint32_t>(
            fixture.size() - offset < sizeof(buffer)
                ? fixture.size() - offset
                : sizeof(buffer));
        r = ReadAt(offset, buffer, request);
        if (r.status != SysFileReadStatus::Complete || r.bytes != request
            || std::memcmp(buffer, fixture.data() + offset, request) != 0)
            return Fail("sequential staging read misbehaved");
        offset += request;
    }

    // Argument and request-state contracts.
    if (Sys_FileReadBegin(gFile, 0, buffer, 0).status != SysFileReadStatus::Invalid)
        return Fail("zero-size request was accepted");
    if (Sys_FileReadBegin(gFile, 0, nullptr, 16).status != SysFileReadStatus::Invalid)
        return Fail("null-buffer request was accepted");
    if (Sys_FileReadBegin(gFile, 0, buffer, 16).status != SysFileReadStatus::Pending)
        return Fail("first request was not accepted");
    if (Sys_FileReadBegin(gFile, 64, buffer, 16).status != SysFileReadStatus::Invalid)
        return Fail("second concurrent request was accepted");
    r = Sys_FileReadWait(gFile, 5000u);
    if (r.status != SysFileReadStatus::Complete || r.bytes != 16)
        return Fail("outstanding request did not complete");
    if (Sys_FileReadWait(gFile, 5000u).status != SysFileReadStatus::Invalid)
        return Fail("wait without an outstanding request was accepted");

    Sys_FileClose(&gFile);
    if (gFile)
        return Fail("close did not reset the caller's handle");

    // Paths are UTF-8 (the sys_file contract): U+00E9 is two bytes that an
    // ANSI-code-page open on Windows would misread.
    const std::string utf8Name = gPath + "-\xC3\xA9";
    const std::filesystem::path utf8Path(
        std::u8string(reinterpret_cast<const char8_t *>(utf8Name.data()), utf8Name.size()));
    {
        std::ofstream named(utf8Path, std::ios::binary | std::ios::trunc);
        named.write(reinterpret_cast<const char *>(fixture.data()), 16);
    }
    SysFileHandle namedFile = Sys_FileOpenRead(utf8Name.c_str());
    std::uint64_t namedSize = 0;
    const bool namedOpened = namedFile && Sys_FileGetSize(namedFile, &namedSize) && namedSize == 16;
    Sys_FileClose(&namedFile);
    std::error_code ignored;
    std::filesystem::remove(utf8Path, ignored);
    if (!namedOpened)
        return Fail("open failed for a UTF-8 file name");

    std::remove(gPath.c_str());
    gPath.clear();
    return 0;
}

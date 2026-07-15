#include <universal/memfile.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
int failures;
int unexpectedReports;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void StoreLittleEndianU32(
    std::vector<std::uint8_t> &buffer,
    const std::size_t offset,
    const std::uint32_t value)
{
    CHECK(offset <= buffer.size());
    CHECK(buffer.size() - offset >= sizeof(value));
    if (offset > buffer.size() || buffer.size() - offset < sizeof(value))
        return;
    buffer[offset + 0] = static_cast<std::uint8_t>(value);
    buffer[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    buffer[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    buffer[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> MakeRawSegment(
    const std::vector<std::uint8_t> &encoded)
{
    std::vector<std::uint8_t> archive(sizeof(std::uint32_t) + encoded.size());
    CHECK(archive.size() <= UINT32_MAX);
    StoreLittleEndianU32(
        archive, 0, static_cast<std::uint32_t>(archive.size()));
    if (!encoded.empty())
    {
        std::memcpy(
            archive.data() + sizeof(std::uint32_t),
            encoded.data(),
            encoded.size());
    }
    return archive;
}

void CloseReader(MemoryFile &reader)
{
    if (!reader.memoryOverflow && reader.segmentIndex >= 0)
        MemFile_MoveToSegment(&reader, -1);
    MemFile_Shutdown(&reader);
}

std::vector<std::uint8_t> WriteArchive(
    const std::vector<std::uint8_t> &payload,
    const bool compress)
{
    const std::size_t capacity = payload.size() * 3 + 4096;
    CHECK(capacity <= static_cast<std::size_t>(INT32_MAX));
    std::vector<std::uint8_t> archive(capacity);
    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archive.size()),
        archive.data(),
        false,
        compress);
    MemFile_WriteData(
        &writer,
        static_cast<int>(payload.size()),
        payload.data());
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    CHECK(writer.bufferSize >= 0);
    archive.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    return archive;
}

MemFileReadStatus ReadArchiveInChunks(
    std::vector<std::uint8_t> &archive,
    const bool compress,
    std::vector<std::uint8_t> &output)
{
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        compress);
    constexpr std::array<int, 7> chunkSizes{{1, 7, 64, 3, 129, 2, 511}};
    std::size_t offset = 0;
    std::size_t chunkIndex = 0;
    MemFileReadStatus status = MemFileReadStatus::Success;
    while (offset < output.size())
    {
        const std::size_t remaining = output.size() - offset;
        const std::size_t requested = static_cast<std::size_t>(
            chunkSizes[chunkIndex % chunkSizes.size()]);
        const std::size_t count = requested < remaining ? requested : remaining;
        status = MemFile_TryReadDataNoReport(
            &reader,
            static_cast<int>(count),
            output.data() + offset);
        if (status != MemFileReadStatus::Success)
            break;
        offset += count;
        ++chunkIndex;
    }
    CloseReader(reader);
    return status;
}

void TestHandcraftedRawRle()
{
    const std::vector<std::uint8_t> encoded{
        0x02, 'A', 'B', 'C',
        0x81, 'D', 'E',
        0x40, 'F',
    };
    std::vector<std::uint8_t> archive = MakeRawSegment(encoded);
    const std::array<std::uint8_t, 9> expected{
        'A', 'B', 'C', 'D', 'E', 0, 0, 'F', 0,
    };

    MemoryFile wholeReader{};
    MemFile_InitForReading(
        &wholeReader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<std::uint8_t, 11> whole{};
    whole.front() = 0xA5;
    whole.back() = 0x5A;
    CHECK(MemFile_TryReadDataNoReport(
              &wholeReader,
              static_cast<int>(expected.size()),
              whole.data() + 1)
        == MemFileReadStatus::Success);
    CHECK(std::memcmp(whole.data() + 1, expected.data(), expected.size()) == 0);
    CHECK(whole.front() == 0xA5);
    CHECK(whole.back() == 0x5A);
    CloseReader(wholeReader);

    MemoryFile byteReader{};
    MemFile_InitForReading(
        &byteReader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<std::uint8_t, 9> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        CHECK(MemFile_TryReadDataNoReport(&byteReader, 1, &bytes[index])
            == MemFileReadStatus::Success);
    }
    CHECK(bytes == expected);
    CloseReader(byteReader);
}

void TestSegmentBoundaryAndPartialFailure()
{
    std::vector<std::uint8_t> archive{
        6, 0, 0, 0, 0x00, 'A',
        6, 0, 0, 0, 0x00, 'B',
    };
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<std::uint8_t, 4> output{{0xC1, 0xC2, 0xC3, 0xC4}};
    CHECK(MemFile_TryReadDataNoReport(&reader, 2, output.data() + 1)
        == MemFileReadStatus::Overflow);
    CHECK(output[0] == 0xC1);
    CHECK(output[1] == 'A');
    CHECK(output[2] == 0xC3);
    CHECK(output[3] == 0xC4);
    CHECK(reader.memoryOverflow);
    CHECK(MemFile_TryReadDataNoReport(&reader, 0, nullptr)
        == MemFileReadStatus::Success);
    CHECK(MemFile_TryReadDataNoReport(&reader, 1, output.data())
        == MemFileReadStatus::Overflow);
    CloseReader(reader);

    archive = MakeRawSegment({0x01, 'Q'});
    MemoryFile partialReader{};
    MemFile_InitForReading(
        &partialReader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<std::uint8_t, 4> partial{{0xD1, 0xD2, 0xD3, 0xD4}};
    CHECK(MemFile_TryReadDataNoReport(&partialReader, 2, partial.data() + 1)
        == MemFileReadStatus::Overflow);
    CHECK(partial[0] == 0xD1);
    CHECK(partial[1] == 'Q');
    CHECK(partial[2] == 0);
    CHECK(partial[3] == 0xD4);
    CloseReader(partialReader);
}

std::vector<std::uint8_t> MakeMixedPayload(const std::size_t size)
{
    std::vector<std::uint8_t> payload(size);
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
        if ((index >= 31 && index < 101) || index % 97 == 0)
            payload[index] = 0;
        else
            payload[index] = static_cast<std::uint8_t>((index * 37 + 11) & 0xFF);
    }
    return payload;
}

void TestRawAndCompressedRoundTrips()
{
    for (const bool compress : {false, true})
    {
        const std::vector<std::uint8_t> payload =
            MakeMixedPayload(compress ? 12289u : 1025u);
        std::vector<std::uint8_t> archive = WriteArchive(payload, compress);
        std::vector<std::uint8_t> restored(payload.size(), 0xEE);
        CHECK(ReadArchiveInChunks(archive, compress, restored)
            == MemFileReadStatus::Success);
        CHECK(restored == payload);
    }
}

void TestCompressedFailureResetsTheSingleton()
{
    const std::vector<std::uint8_t> payload = MakeMixedPayload(4097);
    std::vector<std::uint8_t> validArchive = WriteArchive(payload, true);
    CHECK(validArchive.size() > sizeof(std::uint32_t));

    std::vector<std::uint8_t> corruptArchive = validArchive;
    corruptArchive[sizeof(std::uint32_t)] ^= 0xFF;
    MemoryFile corruptReader{};
    MemFile_InitForReading(
        &corruptReader,
        static_cast<int>(corruptArchive.size()),
        corruptArchive.data(),
        true);
    std::array<std::uint8_t, 16> scratch{};
    CHECK(MemFile_TryReadDataNoReport(
              &corruptReader,
              static_cast<int>(scratch.size()),
              scratch.data())
        == MemFileReadStatus::Overflow);
    CHECK(corruptReader.memoryOverflow);
    CloseReader(corruptReader);

    std::vector<std::uint8_t> restored(payload.size());
    CHECK(ReadArchiveInChunks(validArchive, true, restored)
        == MemFileReadStatus::Success);
    CHECK(restored == payload);

    std::vector<std::uint8_t> truncatedArchive = validArchive;
    truncatedArchive.resize(truncatedArchive.size() - 1);
    StoreLittleEndianU32(
        truncatedArchive,
        0,
        static_cast<std::uint32_t>(truncatedArchive.size()));
    MemoryFile truncatedReader{};
    MemFile_InitForReading(
        &truncatedReader,
        static_cast<int>(truncatedArchive.size()),
        truncatedArchive.data(),
        true);
    // Ask for one byte beyond the logical payload so inflate must consume and
    // validate the truncated stream trailer rather than returning after its
    // last successfully produced output byte.
    std::vector<std::uint8_t> truncatedOutput(payload.size() + 1);
    CHECK(MemFile_TryReadDataNoReport(
              &truncatedReader,
              static_cast<int>(truncatedOutput.size()),
              truncatedOutput.data())
        == MemFileReadStatus::Overflow);
    CHECK(truncatedReader.memoryOverflow);
    CloseReader(truncatedReader);
}

void TestErrorAbandonmentReleasesTheSingleton()
{
    const std::vector<std::uint8_t> payload = MakeMixedPayload(257);
    MemFile_AbandonCurrentThreadForError();
    MemFile_AbandonCurrentThreadForError();

    for (const bool compress : {false, true})
    {
        std::vector<std::uint8_t> abandonedArchive = WriteArchive(payload, compress);
        MemoryFile abandonedReader{};
        MemFile_InitForReading(
            &abandonedReader,
            static_cast<int>(abandonedArchive.size()),
            abandonedArchive.data(),
            compress);
        std::array<std::uint8_t, 17> prefix{};
        CHECK(MemFile_TryReadDataNoReport(
                  &abandonedReader,
                  static_cast<int>(prefix.size()),
                  prefix.data())
            == MemFileReadStatus::Success);
        CHECK(std::equal(prefix.begin(), prefix.end(), payload.begin()));

        std::thread foreignAbandoner([] {
            MemFile_AbandonCurrentThreadForError();
        });
        foreignAbandoner.join();
        std::uint8_t next = 0;
        CHECK(MemFile_TryReadDataNoReport(&abandonedReader, 1, &next)
            == MemFileReadStatus::Success);
        CHECK(next == payload[prefix.size()]);

        // Model Com_Error's longjmp boundary: the active MemoryFile does not
        // receive normal segment cleanup before its stack frame disappears.
        MemFile_AbandonCurrentThreadForError();
        MemFile_AbandonCurrentThreadForError();

        std::vector<std::uint8_t> reusableArchive = WriteArchive(payload, compress);
        std::vector<std::uint8_t> restored(payload.size());
        CHECK(ReadArchiveInChunks(reusableArchive, compress, restored)
            == MemFileReadStatus::Success);
        CHECK(restored == payload);
        MemFile_Shutdown(&abandonedReader);
    }

    std::vector<std::uint8_t> abandonedStorage(4096);
    MemoryFile abandonedWriter{};
    MemFile_InitForWriting(
        &abandonedWriter,
        static_cast<int>(abandonedStorage.size()),
        abandonedStorage.data(),
        false,
        true);
    MemFile_WriteData(
        &abandonedWriter,
        static_cast<int>(payload.size()),
        payload.data());
    MemFile_AbandonCurrentThreadForError();
    MemFile_AbandonCurrentThreadForError();

    std::vector<std::uint8_t> reusableArchive = WriteArchive(payload, true);
    std::vector<std::uint8_t> restored(payload.size());
    CHECK(ReadArchiveInChunks(reusableArchive, true, restored)
        == MemFileReadStatus::Success);
    CHECK(restored == payload);
    MemFile_Shutdown(&abandonedWriter);
}

MemFileReadStatus ReadCString(
    const std::vector<std::uint8_t> &payload,
    char *const output,
    const std::size_t outputSize,
    std::size_t *const outputLength,
    bool *const overflow)
{
    std::vector<std::uint8_t> archive = WriteArchive(payload, false);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    const MemFileReadStatus status = MemFile_TryReadCStringNoReport(
        &reader, output, outputSize, outputLength);
    *overflow = reader.memoryOverflow;
    CloseReader(reader);
    return status;
}

void TestBoundedCStrings()
{
    std::array<char, 66> output{};
    output.front() = 'L';
    output.back() = 'R';
    std::size_t length = 99;
    bool overflow = true;
    CHECK(ReadCString(
              {0}, output.data() + 1, 64, &length, &overflow)
        == MemFileReadStatus::Success);
    CHECK(length == 0);
    CHECK(output[1] == '\0');
    CHECK(!overflow);
    CHECK(output.front() == 'L' && output.back() == 'R');

    std::vector<std::uint8_t> exact(64, 'x');
    exact.back() = 0;
    output.fill('?');
    output.front() = 'L';
    output.back() = 'R';
    length = 0;
    CHECK(ReadCString(
              exact, output.data() + 1, 64, &length, &overflow)
        == MemFileReadStatus::Success);
    CHECK(length == 63);
    CHECK(output[64] == '\0');
    CHECK(output.front() == 'L' && output.back() == 'R');

    std::vector<std::uint8_t> tooLong(65, 'y');
    tooLong.back() = 0;
    std::vector<std::uint8_t> archive = WriteArchive(tooLong, false);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    output.fill('?');
    output.front() = 'L';
    output.back() = 'R';
    length = 0;
    CHECK(MemFile_TryReadCStringNoReport(
              &reader, output.data() + 1, 64, &length)
        == MemFileReadStatus::OutputTooSmall);
    CHECK(length == 63);
    CHECK(output[64] == '\0');
    CHECK(!reader.memoryOverflow);
    std::array<char, 1> terminator{{'!'}};
    std::size_t terminatorLength = 7;
    CHECK(MemFile_TryReadCStringNoReport(
              &reader, terminator.data(), terminator.size(), &terminatorLength)
        == MemFileReadStatus::Success);
    CHECK(terminatorLength == 0 && terminator[0] == '\0');
    CloseReader(reader);

    archive = WriteArchive({'z', 0}, false);
    MemoryFile narrowReader{};
    MemFile_InitForReading(
        &narrowReader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<char, 1> narrow{{'!'}};
    length = 42;
    CHECK(MemFile_TryReadCStringNoReport(
              &narrowReader, narrow.data(), narrow.size(), &length)
        == MemFileReadStatus::OutputTooSmall);
    CHECK(length == 0 && narrow[0] == '\0');
    CHECK(!narrowReader.memoryOverflow);
    CHECK(MemFile_TryReadCStringNoReport(
              &narrowReader, narrow.data(), narrow.size(), &length)
        == MemFileReadStatus::Success);
    CloseReader(narrowReader);

    archive = WriteArchive({'a', 'b'}, false);
    MemoryFile truncatedReader{};
    MemFile_InitForReading(
        &truncatedReader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    std::array<char, 8> partial{};
    length = 77;
    CHECK(MemFile_TryReadCStringNoReport(
              &truncatedReader, partial.data(), partial.size(), &length)
        == MemFileReadStatus::Overflow);
    CHECK(length == 2);
    CHECK(std::strcmp(partial.data(), "ab") == 0);
    CHECK(truncatedReader.memoryOverflow);
    partial.fill('p');
    length = 88;
    CHECK(MemFile_TryReadCStringNoReport(
              &truncatedReader, partial.data(), partial.size(), &length)
        == MemFileReadStatus::Overflow);
    CHECK(partial[0] == '\0' && length == 0);
    CloseReader(truncatedReader);
}

void TestInvalidArgumentsDoNotMutate()
{
    MemoryFile invalid{};
    std::array<char, 4> output{{'a', 'b', 'c', '\0'}};
    std::size_t length = 17;
    CHECK(MemFile_TryReadCStringNoReport(
              &invalid, output.data(), output.size(), &length)
        == MemFileReadStatus::InvalidState);
    CHECK(output[0] == 'a' && length == 17);
    CHECK(MemFile_TryReadCStringNoReport(
              &invalid, output.data(), 0, &length)
        == MemFileReadStatus::InvalidArgument);
    CHECK(output[0] == 'a' && length == 17);
    CHECK(MemFile_TryReadDataNoReport(nullptr, 1, nullptr)
        == MemFileReadStatus::InvalidArgument);
    CHECK(MemFile_TryReadDataNoReport(&invalid, -1, nullptr)
        == MemFileReadStatus::InvalidArgument);

    std::vector<std::uint8_t> archive = WriteArchive({'k', 0}, false);
    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archive.size()),
        archive.data(),
        false);
    output = {{'a', 'b', 'c', '\0'}};
    length = 23;
    CHECK(MemFile_TryReadCStringNoReport(
              &reader, output.data(), 0, &length)
        == MemFileReadStatus::InvalidArgument);
    CHECK(output[0] == 'a' && length == 23);
    CHECK(MemFile_TryReadCStringNoReport(
              &reader, output.data(), output.size(), nullptr)
        == MemFileReadStatus::InvalidArgument);
    CHECK(output[0] == 'a' && length == 23);
    CHECK(MemFile_TryReadCStringNoReport(
              &reader, output.data(), output.size(), &length)
        == MemFileReadStatus::Success);
    CHECK(length == 1 && std::strcmp(output.data(), "k") == 0);
    CloseReader(reader);
}
} // namespace

void MyAssertHandler(
    const char *filename,
    int line,
    int type,
    const char *format,
    ...)
{
    (void)filename;
    (void)line;
    (void)type;
    (void)format;
    ++unexpectedReports;
}

void QDECL Com_Printf(const int channel, const char *format, ...)
{
    (void)channel;
    (void)format;
    ++unexpectedReports;
}

void QDECL Com_Error(const errorParm_t code, const char *format, ...)
{
    (void)code;
    (void)format;
    ++unexpectedReports;
}

char *QDECL va(const char *format, ...)
{
    static char result[1]{};
    (void)format;
    return result;
}

bool __cdecl Sys_IsMainThread()
{
    return true;
}

bool __cdecl Sys_IsRenderThread()
{
    return false;
}

bool __cdecl Sys_IsDatabaseThread()
{
    return false;
}

int main()
{
    TestHandcraftedRawRle();
    TestSegmentBoundaryAndPartialFailure();
    TestRawAndCompressedRoundTrips();
    TestCompressedFailureResetsTheSingleton();
    TestErrorAbandonmentReleasesTheSingleton();
    TestBoundedCStrings();
    TestInvalidArgumentsDoNotMutate();
    CHECK(unexpectedReports == 0);

    if (failures)
    {
        std::fprintf(stderr, "%d MemoryFile test(s) failed\n", failures);
        return 1;
    }
    std::puts("MemoryFile bounded-read tests passed");
    return 0;
}

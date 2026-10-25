// SPDX-License-Identifier: GPL-3.0-only
//
// unzip_skip_tests.cpp -- the IWD reader (qcommon/unzip.cpp) over the vendored
// zlib. FS_Seek moves through a compressed IWD entry by reading into a NULL
// buffer: unzReadCurrentFile(file, 0, n) skips n bytes, and a backwards seek
// reopens the entry (unzSetCurrentFileInfoPosition, unzOpenCurrentFile) and
// skips from the start. The test writes a zip with one deflated entry and
// checks every FS_Seek pattern: the skip count, unztell, and the bytes read
// after it.

#include <qcommon/com_fileaccess.h>
#include <qcommon/unzip.h>
#include <zlib/zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The engine's file wrappers unzip.cpp reads through, over stdio. Origins are
// FileWrapper_Seek's: 0 current, 1 end, 2 set.
FILE *__cdecl FS_FileOpenReadBinary(const char *filename) { return std::fopen(filename, "rb"); }
void __cdecl FS_FileClose(FILE *stream) { std::fclose(stream); }
uint32_t __cdecl FS_FileRead(void *ptr, uint32_t len, FILE *stream)
{
    return static_cast<uint32_t>(std::fread(ptr, 1, len, stream));
}
int __cdecl FS_FileSeek(FILE *file, int offset, int whence)
{
    static const int origins[] = {SEEK_CUR, SEEK_END, SEEK_SET};
    return std::fseek(file, offset, origins[whence]);
}
int __cdecl FS_FileGetFileSize(FILE *file)
{
    const long here = std::ftell(file);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, here, SEEK_SET);
    return static_cast<int>(size);
}

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "unzip-skip: line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

constexpr unsigned PayloadSize = 100000;
const char EntryName[] = "sound/skip.bin";

std::vector<unsigned char> MakePayload()
{
    // Compressible but not trivial: repeating phrases with a counter mixed in.
    std::vector<unsigned char> payload(PayloadSize);
    std::uint32_t state = 12345;
    for (unsigned i = 0; i < PayloadSize; ++i)
    {
        state = state * 1103515245u + 12345u;
        payload[i] = static_cast<unsigned char>((i % 97 < 60) ? "kisakcod iwd entry "[i % 19] : (state >> 16));
    }
    return payload;
}

std::vector<unsigned char> RawDeflate(const std::vector<unsigned char> &in)
{
    z_stream stream{};
    CHECK(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<unsigned char> out(deflateBound(&stream, static_cast<uLong>(in.size())));
    stream.next_in = const_cast<Bytef *>(in.data());
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    CHECK(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

void Put16(std::vector<unsigned char> &v, unsigned x) { v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF); }
void Put32(std::vector<unsigned char> &v, std::uint32_t x) { Put16(v, x & 0xFFFF); Put16(v, x >> 16); }

// One deflated entry: local header, data, central directory, end record.
std::vector<unsigned char> MakeZip(const std::vector<unsigned char> &payload)
{
    const std::vector<unsigned char> packed = RawDeflate(payload);
    const std::uint32_t crc = crc32(0, payload.data(), static_cast<uInt>(payload.size()));
    const unsigned nameLength = sizeof(EntryName) - 1;
    std::vector<unsigned char> zip;
    auto common = [&](std::vector<unsigned char> &v) {
        Put16(v, 20); Put16(v, 0); Put16(v, 8); Put16(v, 0); Put16(v, 0x21);
        Put32(v, crc); Put32(v, static_cast<std::uint32_t>(packed.size())); Put32(v, PayloadSize);
        Put16(v, nameLength); Put16(v, 0);
    };
    Put32(zip, 0x04034b50);
    common(zip);
    zip.insert(zip.end(), EntryName, EntryName + nameLength);
    zip.insert(zip.end(), packed.begin(), packed.end());
    const std::uint32_t centralOffset = static_cast<std::uint32_t>(zip.size());
    Put32(zip, 0x02014b50);
    Put16(zip, 20);
    common(zip);
    Put16(zip, 0); Put16(zip, 0); Put16(zip, 0); Put32(zip, 0); Put32(zip, 0);
    zip.insert(zip.end(), EntryName, EntryName + nameLength);
    const std::uint32_t centralSize = static_cast<std::uint32_t>(zip.size()) - centralOffset;
    Put32(zip, 0x06054b50);
    Put16(zip, 0); Put16(zip, 0); Put16(zip, 1); Put16(zip, 1);
    Put32(zip, centralSize); Put32(zip, centralOffset); Put16(zip, 0);
    return zip;
}

// Reads `count` bytes at the entry's current position and compares them.
void ExpectBytes(unzFile zip, const std::vector<unsigned char> &payload, unsigned at, unsigned count)
{
    std::vector<unsigned char> got(count);
    CHECK(unzReadCurrentFile(zip, got.data(), count) == static_cast<int>(count));
    CHECK(std::memcmp(got.data(), payload.data() + at, count) == 0);
}
} // namespace

int main()
{
    const std::vector<unsigned char> payload = MakePayload();
    const std::vector<unsigned char> zipBytes = MakeZip(payload);
    const std::string path = "kisakcod-unzip-skip-test.iwd";
    FILE *out = std::fopen(path.c_str(), "wb");
    if (!out || std::fwrite(zipBytes.data(), 1, zipBytes.size(), out) != zipBytes.size())
    {
        std::fprintf(stderr, "unzip-skip: cannot write %s\n", path.c_str());
        return EXIT_FAILURE;
    }
    std::fclose(out);

    unzFile zip = unzOpen(path.c_str());
    CHECK(zip != nullptr);
    if (!zip)
        return EXIT_FAILURE;
    unsigned long entryPos = 0;
    CHECK(unzGoToFirstFile(zip) == UNZ_OK);
    CHECK(unzGetCurrentFileInfoPosition(zip, &entryPos) == UNZ_OK);
    CHECK(unzOpenCurrentFile(zip) == UNZ_OK);

    // FS_Seek forward from the start: skip, then read what follows.
    CHECK(unzReadCurrentFile(zip, nullptr, 70000) == 70000);
    CHECK(unztell(zip) == 70000);
    ExpectBytes(zip, payload, 70000, 100);

    // A relative skip from the middle, across several scratch-sized chunks.
    CHECK(unzReadCurrentFile(zip, nullptr, 9000) == 9000);
    CHECK(unztell(zip) == 79100);
    ExpectBytes(zip, payload, 79100, 64);

    // FS_Seek backwards: reopen the entry and skip from its start.
    CHECK(unzSetCurrentFileInfoPosition(zip, entryPos) == UNZ_OK);
    CHECK(unzOpenCurrentFile(zip) == UNZ_OK);
    CHECK(unzReadCurrentFile(zip, nullptr, 12345) == 12345);
    ExpectBytes(zip, payload, 12345, 50);

    // A skip past the end stops at the end.
    CHECK(unzReadCurrentFile(zip, nullptr, PayloadSize) == static_cast<int>(PayloadSize - 12395));
    unsigned char tail = 0;
    CHECK(unzReadCurrentFile(zip, &tail, 1) == 0);

    CHECK(unzCloseCurrentFile(zip) == UNZ_OK);
    CHECK(unzClose(zip) == UNZ_OK);
    std::remove(path.c_str());

    if (g_failures)
        return EXIT_FAILURE;
    std::printf("unzip-skip: all checks passed\n");
    return EXIT_SUCCESS;
}

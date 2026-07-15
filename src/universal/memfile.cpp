#include "memfile.h"

#include <qcommon/qcommon.h>

#include <qcommon/threads.h>
#include <zlib/zlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

static int g_cacheSize;
static int g_nonZeroCount;
static int g_zeroCount;
static int g_cacheBufferLen;

static bool g_compress;

#define CODE_LEN_MASK 63
#define SAVE_SEGMENT_COUNT 8

static byte g_cacheBuffer[CODE_LEN_MASK + 2];
static byte g_saveBuffer[8192];

static int streamModeThread;
static MemFileMode streamMode;

static z_stream_s stream;

// The legacy codec is process-global.  Keep its owner and the selected read
// segment out of MemoryFile so the disk/runtime structure remains ABI-neutral.
static MemoryFile* g_streamOwner;
static thread_local MemoryFile* g_currentThreadStreamOwner;
static int g_activeSegmentIndex = -1;
static size_t g_activeSegmentDataStart;
static size_t g_activeSegmentEnd;

static const char* MemFileModeNames[] = // idb
{
    "default",
    "inflating",
    "deflating"
};

static const char* MemFileThreadNames[] = // idb
{
    "unknown",
    "main",
    "debugService",
    "server",
    "backend",
    "database",
    "stream",
    "sndStreamPacketCallback"
};

static int GetThreadID();

/*

===== MEMFILE DATA FORMAT =====

Memfiles come in two flavors: Compressed and uncompressed.

In either case, there is a simple run-length encoding scheme that has two possible cases, indicated by a "header" and continuation bytes:

The header byte is encoded as follows:

MAYBE_NZ_COUNT: 2
AUX: 6

if MAYBE_NZ_COUNT is zero, the header is followed by a raw run of
(AUX + 1) bytes. Otherwise it describes MAYBE_NZ_COUNT raw bytes followed by
(AUX + 1) zero bytes.

This continues until there is no data left in the file.

*/

namespace
{

uint32_t MemFile_ReadLittleEndianU32(const uint8_t* input) noexcept
{
    return static_cast<uint32_t>(input[0])
        | (static_cast<uint32_t>(input[1]) << 8)
        | (static_cast<uint32_t>(input[2]) << 16)
        | (static_cast<uint32_t>(input[3]) << 24);
}

void MemFile_WriteLittleEndianU32(
    uint8_t* const output,
    const uint32_t value) noexcept
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

bool MemFile_TryLocateSegmentNoReport(
    const MemoryFile* memFile,
    uint32_t index,
    size_t* segmentStart,
    size_t* segmentEnd) noexcept
{
    if (!memFile || !segmentStart || !segmentEnd || !memFile->buffer
        || memFile->bufferSize < 0 || index >= SAVE_SEGMENT_COUNT)
    {
        return false;
    }

    const size_t bufferSize = static_cast<size_t>(memFile->bufferSize);
    size_t offset = 0;
    for (uint32_t current = 0; current <= index; ++current)
    {
        if (offset > bufferSize || bufferSize - offset < sizeof(uint32_t))
            return false;

        const uint32_t encodedLength = MemFile_ReadLittleEndianU32(memFile->buffer + offset);
        const size_t length = static_cast<size_t>(encodedLength);
        if (length < sizeof(uint32_t) || length > bufferSize - offset)
            return false;

        if (current == index)
        {
            *segmentStart = offset;
            *segmentEnd = offset + length;
            return true;
        }
        offset += length;
    }

    return false;
}

void MemFile_ClearStreamSidecar() noexcept
{
    g_streamOwner = nullptr;
    g_currentThreadStreamOwner = nullptr;
    g_activeSegmentIndex = -1;
    g_activeSegmentDataStart = 0;
    g_activeSegmentEnd = 0;
}

void MemFile_ResetOwnedStreamNoReport(MemoryFile* memFile) noexcept
{
    if (g_streamOwner != memFile)
    {
        if (g_currentThreadStreamOwner == memFile)
            g_currentThreadStreamOwner = nullptr;
        return;
    }

    if (g_compress && stream.state)
    {
        if (streamMode == MEM_FILE_MODE_INFLATE)
            (void)inflateEnd(&stream);
        else if (streamMode == MEM_FILE_MODE_DEFLATE)
            (void)deflateEnd(&stream);
    }

    std::memset(&stream, 0, sizeof(stream));
    streamMode = MEM_FILE_MODE_DEFAULT;
    streamModeThread = 0;
    g_compress = false;
    MemFile_ClearStreamSidecar();
    g_nonZeroCount = 0;
    g_zeroCount = 0;
    g_cacheSize = 0;
    g_cacheBufferLen = 0;
}

bool MemFile_HasValidReadObject(const MemoryFile* memFile) noexcept
{
    return memFile && memFile->archiveProc == MemFile_ReadData && memFile->buffer
        && memFile->bufferSize >= 0 && memFile->bytesUsed >= 0
        && memFile->bytesUsed <= memFile->bufferSize;
}

bool MemFile_HasValidReadDecoder(const MemoryFile* memFile) noexcept
{
    if (!MemFile_HasValidReadObject(memFile) || memFile->segmentIndex < 0
        || memFile->segmentIndex >= SAVE_SEGMENT_COUNT || g_streamOwner != memFile
        || g_activeSegmentIndex != memFile->segmentIndex
        || streamMode != MEM_FILE_MODE_INFLATE || streamModeThread != GetThreadID()
        || g_currentThreadStreamOwner != memFile
        || g_compress != memFile->compress || g_nonZeroCount < 0 || g_nonZeroCount > 64
        || g_zeroCount < 0 || g_zeroCount > 64
        || (g_zeroCount > 0 && g_nonZeroCount > 3))
    {
        return false;
    }

    const size_t bufferSize = static_cast<size_t>(memFile->bufferSize);
    const size_t bytesUsed = static_cast<size_t>(memFile->bytesUsed);
    if (g_activeSegmentDataStart < sizeof(uint32_t)
        || g_activeSegmentDataStart > g_activeSegmentEnd
        || g_activeSegmentEnd > bufferSize || bytesUsed < g_activeSegmentDataStart
        || bytesUsed > g_activeSegmentEnd)
    {
        return false;
    }

    if (!memFile->compress)
        return true;

    if (!stream.state || stream.next_in != memFile->buffer + bytesUsed)
        return false;

    const size_t remaining = g_activeSegmentEnd - bytesUsed;
    return remaining <= (std::numeric_limits<uInt>::max)()
        && stream.avail_in == static_cast<uInt>(remaining);
}

MemFileReadStatus MemFile_ReadEncodedByteNoReport(
    MemoryFile* memFile,
    uint8_t* output) noexcept
{
    *output = 0;
    if (!memFile->compress)
    {
        const size_t bytesUsed = static_cast<size_t>(memFile->bytesUsed);
        if (bytesUsed < g_activeSegmentEnd)
        {
            *output = memFile->buffer[bytesUsed];
            ++memFile->bytesUsed;
            return MemFileReadStatus::Success;
        }
    }
    else
    {
        stream.next_out = output;
        stream.avail_out = 1;

        const int err = inflate(&stream, Z_SYNC_FLUSH);
        const uintptr_t bufferAddress = reinterpret_cast<uintptr_t>(memFile->buffer);
        const uintptr_t nextAddress = reinterpret_cast<uintptr_t>(stream.next_in);
        const size_t bufferSize = static_cast<size_t>(memFile->bufferSize);
        bool positionIsValid = bufferAddress <= (std::numeric_limits<uintptr_t>::max)() - bufferSize;
        if (positionIsValid)
        {
            const uintptr_t bufferEndAddress = bufferAddress + bufferSize;
            positionIsValid = nextAddress >= bufferAddress && nextAddress <= bufferEndAddress;
        }

        if (positionIsValid)
        {
            const size_t nextOffset = static_cast<size_t>(nextAddress - bufferAddress);
            positionIsValid = nextOffset >= g_activeSegmentDataStart
                && nextOffset <= g_activeSegmentEnd
                && stream.avail_in <= g_activeSegmentEnd - nextOffset
                && nextOffset + stream.avail_in == g_activeSegmentEnd;
            if (positionIsValid)
                memFile->bytesUsed = static_cast<int>(nextOffset);
        }

        if (positionIsValid && (err == Z_OK || err == Z_STREAM_END) && stream.avail_out == 0)
            return MemFileReadStatus::Success;
    }

    memFile->memoryOverflow = true;
    MemFile_ResetOwnedStreamNoReport(memFile);
    return MemFileReadStatus::Overflow;
}

} // namespace

static void AssertStreamMode(MemFileMode mode)
{
    if (streamMode != mode)
    {
        const char *fmt = va(
            "Memfile routine expected streamMode \"%s\", but is \"%s\" instead.  Possible race with thread \"%s\".",
            MemFileModeNames[mode],
            MemFileModeNames[streamMode],
            MemFileThreadNames[streamModeThread]);
        MyAssertHandler(".\\universal\\memfile.cpp", 177, 0, fmt);
    }
}

static int GetThreadID()
{
    if (Sys_IsMainThread())
        return 1;
    if (Sys_IsRenderThread())
        return 4;
    if (Sys_IsDatabaseThread())
        return 5;
    return 0;
}

void SetStreamMode(MemFileMode mode)
{
    streamMode = mode;
    streamModeThread = GetThreadID();
}

void MemFile_ArchiveData(MemoryFile* memFile, int bytes, void* data)
{
    iassert(memFile);
    iassert(memFile->archiveProc);

    memFile->archiveProc(memFile, bytes, (byte *)data);
}

static void MemFile_WriteDataForArchive(MemoryFile* memFile, int bytes, byte* data)
{
    MemFile_WriteData(memFile, bytes, data);
}

void MemFile_CommonInit(MemoryFile* memFile, int size, byte* buffer, bool errorOnOverflow, bool compress)
{
    iassert(memFile);
    iassert(buffer);
    vassert(size > 0, "(size = %d)", size);

    if (g_streamOwner == memFile)
        MemFile_ResetOwnedStreamNoReport(memFile);

    memFile->buffer = buffer;
    memFile->bufferSize = size;
    memFile->bytesUsed = 0;
    memFile->errorOnOverflow = errorOnOverflow;
    memFile->memoryOverflow = 0;
    memFile->segmentIndex = -1;
    memFile->segmentStart = 0;
    memFile->compress = compress;
}

void MemFile_InitForReading(MemoryFile* memFile, int size, byte* buffer, bool compress)
{
    MemFile_CommonInit(memFile, size, buffer, 1, compress);
    memFile->archiveProc = MemFile_ReadData;
    MemFile_MoveToSegment(memFile, 0);
}

void MemFile_InitForWriting(MemoryFile* memFile, int size, byte* buffer,
    bool errorOnOverflow, bool compress)
{
    MemFile_CommonInit(memFile, size, buffer, errorOnOverflow, compress);
    memFile->archiveProc = MemFile_WriteDataForArchive;
    MemFile_StartSegment(memFile, 0);
}

bool MemFile_IsReading(MemoryFile* memFile)
{
    iassert(memFile);
    return memFile->archiveProc == MemFile_ReadData;
}

bool MemFile_IsWriting(MemoryFile* memFile)
{
    iassert(memFile);
    return memFile->archiveProc == MemFile_WriteDataForArchive;
}

double MemFile_ReadFloat(MemoryFile* memFile)
{
    float value = NAN;
    MemFile_ReadData(memFile, 4, (byte *)&value);
    iassert(!isnan(value));
    return value;
}

// KISAKTODO cleaning this up is going to be such a headache

void MemFile_StartSegment(MemoryFile* memFile, int index)
{
    vassert(((index >= -1) && (index < SAVE_SEGMENT_COUNT)), "(index = %d)", index);

    // bail if we've already overflowed
    if (memFile->memoryOverflow)
        return;

    int lastSegmentIndex = memFile->segmentIndex;

    // if we already have a segment, end it and check for overflow again
    if (memFile->segmentIndex >= 0)
    {
        MemFile_EndSegment(memFile);

        if (memFile->memoryOverflow)
            return;
    }

    if (index >= 0 && (streamMode != MEM_FILE_MODE_DEFAULT || g_streamOwner))
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            185,
            0,
            "%s",
            "no other memfile owns the global stream");
        return;
    }

    // start new segment, for real this time.
    memFile->segmentIndex = index;
    if (index >= 0)
    {
        iassert(index == lastSegmentIndex + 1);

        memFile->segmentStart = memFile->bytesUsed;
        if (memFile->bytesUsed + 69 <= memFile->bufferSize)
        {
            memFile->bytesUsed += 4;
            iassert(!memFile->memoryOverflow);

            MemFile_deflateInit(
                &memFile->buffer[memFile->bytesUsed],
                memFile->bufferSize - memFile->bytesUsed,
                memFile->compress);

            g_streamOwner = memFile;
            g_currentThreadStreamOwner = memFile;
            g_activeSegmentIndex = index;
            g_activeSegmentDataStart = 0;
            g_activeSegmentEnd = 0;

            g_cacheSize = 1;
            g_nonZeroCount = 0;
            g_zeroCount = 0;
            g_cacheBufferLen = -1;
        }
        else
        {
            // WHOOPS!

            if (memFile->errorOnOverflow)
                Com_Error(ERR_DROP, "MemFile_StartSegment: Out of memory");

            Com_Printf(10, "MemFile_StartSegment: Out of memory\n");
            memFile->memoryOverflow = 1;
        }
    }
    else
    {
        // NOTE: -1 is a dummy used to indicate "end of file"
        // such that the logic needed to ensure flushed memfiles is completed
        // ... this is in the "start segment" function, for some reason? whatever
        memFile->bufferSize = memFile->bytesUsed;
    }
}

void __cdecl MemFile_deflateInit(uint8_t* next_out, uint32_t avail_out, bool compress)
{
    AssertStreamMode(MEM_FILE_MODE_DEFAULT);
    if (compress)
    {
        memset((uint8_t*)&stream, 0, sizeof(stream));
        stream.next_out = next_out;
        stream.avail_out = avail_out;
        if (deflateInit_(&stream, 1, ZLIB_VERSION, static_cast<int>(sizeof(stream))))
            MyAssertHandler(".\\universal\\memfile.cpp", 224, 0, "%s", "err == Z_OK");
    }
    SetStreamMode(MEM_FILE_MODE_DEFLATE);
    g_compress = compress;
}

void __cdecl MemFile_EndSegment(MemoryFile* memFile)
{
    uint32_t err; // [esp+0h] [ebp-Ch]
    uint32_t index; // [esp+8h] [ebp-4h]

    if (memFile->memoryOverflow)
        MyAssertHandler(".\\universal\\memfile.cpp", 310, 0, "%s", "!memFile->memoryOverflow");
    if (!memFile->memoryOverflow)
    {
        index = memFile->segmentIndex;
        if (index >= 8)
            MyAssertHandler(
                ".\\universal\\memfile.cpp",
                317,
                0,
                "%s\n\t(index) = %i",
                "((index >= 0) && (index < SAVE_SEGMENT_COUNT))",
                index);
        if (g_cacheSize > 1)
        {
            if (g_cacheBufferLen < 0)
                MyAssertHandler(".\\universal\\memfile.cpp", 321, 0, "%s", "g_cacheBufferLen >= 0");
            if (!MemFile_WriteDataInternal(memFile, g_cacheSize, g_nonZeroCount, g_cacheBufferLen, 0))
            {
                MemFile_WriteError(memFile);
                return;
            }
            g_cacheSize = 0;
        }
        AssertStreamMode(MEM_FILE_MODE_DEFLATE);
        if (!memFile->compress)
            goto LABEL_30;
        stream.next_in = g_saveBuffer;
        if (&memFile->buffer[memFile->bytesUsed] != stream.next_out)
            MyAssertHandler(
                ".\\universal\\memfile.cpp",
                337,
                0,
                "%s",
                "memFile->buffer + memFile->bytesUsed == stream.next_out");
        err = deflate(&stream, 4u);
        if (err > 1)
            MyAssertHandler(".\\universal\\memfile.cpp", 340, 0, "%s\n\t(err) = %i", "((err == 0) || (err == 1))", err);
        memFile->bytesUsed = stream.next_out - memFile->buffer;
        if (memFile->bytesUsed > memFile->bufferSize)
            MyAssertHandler(".\\universal\\memfile.cpp", 343, 0, "%s", "memFile->bytesUsed <= memFile->bufferSize");
        if (err == 1)
        {
        LABEL_30:
            if (MemFile_deflateEnd(memFile->compress))
                MyAssertHandler(".\\universal\\memfile.cpp", 362, 0, "%s", "err == Z_OK");
            memFile->segmentIndex = -1;
            MemFile_WriteLittleEndianU32(
                &memFile->buffer[memFile->segmentStart],
                static_cast<uint32_t>(
                    memFile->bytesUsed - memFile->segmentStart));
        }
        else
        {
            if (stream.avail_out)
                MyAssertHandler(
                    ".\\universal\\memfile.cpp",
                    347,
                    0,
                    "%s\n\t(stream.avail_out) = %i",
                    "(!stream.avail_out)",
                    stream.avail_out);
            MemFile_deflateEnd(memFile->compress);
            if (memFile->errorOnOverflow)
                Com_Error(ERR_DROP, "MemFile_EndSegment: Out of memory");
            Com_Printf(10, "MemFile_EndSegment: Out of memory");
            memFile->memoryOverflow = 1;
        }
    }
}

uint32_t __cdecl MemFile_deflateEnd(bool compress)
{
    uint32_t err; // [esp+0h] [ebp-4h]

    AssertStreamMode(MEM_FILE_MODE_DEFLATE);
    if (compress)
        err = deflateEnd(&stream);
    else
        err = 0;
    SetStreamMode(MEM_FILE_MODE_DEFAULT);
    g_compress = false;
    MemFile_ClearStreamSidecar();
    return err;
}

void __cdecl MemFile_MoveToSegment(MemoryFile* memFile, int index)
{
    if (!memFile)
    {
        MyAssertHandler(".\\universal\\memfile.cpp", 445, 0, "%s", "memFile");
        return;
    }
    if (index < -1 || index >= 8)
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            446,
            0,
            "%s\n\t(index) = %i",
            "((index >= -1) && (index < SAVE_SEGMENT_COUNT))",
            index);
        return;
    }
    if (!memFile->memoryOverflow)
    {
        if (memFile->segmentIndex >= 0)
        {
            if (g_streamOwner != memFile || streamMode != MEM_FILE_MODE_INFLATE
                || g_compress != memFile->compress)
            {
                MyAssertHandler(
                    ".\\universal\\memfile.cpp",
                    454,
                    0,
                    "%s",
                    "memFile owns the active inflate stream");
                return;
            }
            if (MemFile_inflateEnd(memFile->compress))
                MyAssertHandler(".\\universal\\memfile.cpp", 454, 0, "%s", "err == Z_OK");
        }
        memFile->segmentIndex = index;
        if (index >= 0)
        {
            if (streamMode != MEM_FILE_MODE_DEFAULT || g_streamOwner)
            {
                MyAssertHandler(
                    ".\\universal\\memfile.cpp",
                    459,
                    0,
                    "%s",
                    "no other memfile owns the global stream");
                memFile->segmentIndex = -1;
                return;
            }

            size_t segmentStart = 0;
            size_t segmentEnd = 0;
            if (!MemFile_TryLocateSegmentNoReport(
                    memFile,
                    static_cast<uint32_t>(index),
                    &segmentStart,
                    &segmentEnd))
            {
                MyAssertHandler(
                    ".\\universal\\memfile.cpp",
                    464,
                    0,
                    "%s",
                    "segment header and length are within the memfile buffer");
                memFile->segmentIndex = -1;
                memFile->memoryOverflow = true;
                g_nonZeroCount = 0;
                g_zeroCount = 0;
                return;
            }

            const size_t dataStart = segmentStart + sizeof(uint32_t);
            const size_t dataLength = segmentEnd - dataStart;
            memFile->bytesUsed = static_cast<int>(dataStart);
            MemFile_inflateInit(
                &memFile->buffer[dataStart],
                static_cast<uint32_t>(dataLength),
                memFile->compress);
            g_streamOwner = memFile;
            g_currentThreadStreamOwner = memFile;
            g_activeSegmentIndex = index;
            g_activeSegmentDataStart = dataStart;
            g_activeSegmentEnd = segmentEnd;
            g_nonZeroCount = 0;
            g_zeroCount = 0;
        }
    }
}

void __cdecl MemFile_inflateInit(uint8_t* next_in, uint32_t len, bool compress)
{
    AssertStreamMode(MEM_FILE_MODE_DEFAULT);
    if (compress)
    {
        memset((uint8_t*)&stream, 0, sizeof(stream));
        stream.next_in = next_in;
        stream.avail_in = len;
        if (inflateInit_(&stream, ZLIB_VERSION, static_cast<int>(sizeof(stream))))
            MyAssertHandler(".\\universal\\memfile.cpp", 387, 0, "%s", "err == Z_OK");
    }
    SetStreamMode(MEM_FILE_MODE_INFLATE);
    g_compress = compress;
}

int __cdecl MemFile_inflateEnd(bool compress)
{
    int err; // [esp+0h] [ebp-4h]

    AssertStreamMode(MEM_FILE_MODE_INFLATE);
    if (compress)
        err = inflateEnd(&stream);
    else
        err = 0;
    SetStreamMode(MEM_FILE_MODE_DEFAULT);
    g_compress = false;
    MemFile_ClearStreamSidecar();
    return err;
}

uint8_t* __cdecl MemFile_GetSegmentAddess(MemoryFile* memFile, uint32_t index)
{
    if (index >= 8)
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            418,
            0,
            "%s\n\t(index) = %i",
            "((index >= 0) && (index < SAVE_SEGMENT_COUNT))",
            index);
        return nullptr;
    }
    if (!memFile)
    {
        MyAssertHandler(".\\universal\\memfile.cpp", 419, 0, "%s", "memFile");
        return nullptr;
    }
    if (memFile->memoryOverflow)
    {
        MyAssertHandler(".\\universal\\memfile.cpp", 419, 0, "%s", "!memFile->memoryOverflow");
        return nullptr;
    }

    size_t segmentStart = 0;
    size_t segmentEnd = 0;
    if (!MemFile_TryLocateSegmentNoReport(memFile, index, &segmentStart, &segmentEnd))
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            429,
            0,
            "%s",
            "segment header and length are within the memfile buffer");
        return nullptr;
    }
    return &memFile->buffer[segmentStart];
}

void __cdecl MemFile_WriteError(MemoryFile* memFile)
{
    if (memFile->memoryOverflow)
        MyAssertHandler(".\\universal\\memfile.cpp", 523, 0, "%s", "!memFile->memoryOverflow");
    MemFile_deflateEnd(memFile->compress);
    if (memFile->errorOnOverflow)
        Com_Error(ERR_DROP, "MemFile_EndSegment: Out of memory");
    Com_Printf(10, "MemFile_EndSegment: Out of memory");
    memFile->memoryOverflow = 1;
}

int __cdecl MemFile_WriteDataInternal(
    MemoryFile* memFile,
    int bytes,
    char nonZeroCount,
    char cacheBufferLen,
    uint8_t nextByte)
{
    signed int sourceLen; // [esp+0h] [ebp-10h]
    uint8_t* data; // [esp+8h] [ebp-8h]
    int len; // [esp+Ch] [ebp-4h]

    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 544, 0, "%s", "memFile");
    if (!MemFile_IsWriting(memFile))
        MyAssertHandler(".\\universal\\memfile.cpp", 545, 0, "%s", "MemFile_IsWriting( memFile )");
    if (!memFile->buffer)
        MyAssertHandler(".\\universal\\memfile.cpp", 546, 0, "%s", "memFile->buffer");
    if (memFile->bytesUsed < 0 || memFile->bytesUsed > memFile->bufferSize)
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            547,
            0,
            "memFile->bytesUsed not in [0, memFile->bufferSize]\n\t%i not in [%i, %i]",
            memFile->bytesUsed,
            0,
            memFile->bufferSize);
    if (bytes <= 0)
        MyAssertHandler(".\\universal\\memfile.cpp", 548, 0, "%s\n\t(bytes) = %i", "(bytes > 0)", bytes);
    AssertStreamMode(MEM_FILE_MODE_DEFLATE);
    if (memFile->memoryOverflow)
        MyAssertHandler(".\\universal\\memfile.cpp", 550, 0, "%s", "!memFile->memoryOverflow");
    if (memFile->compress)
    {
        data = g_cacheBuffer;
        g_cacheBuffer[0] = cacheBufferLen + (nonZeroCount << 6);
        sourceLen = bytes + stream.avail_in;
        if (!(bytes + stream.avail_in))
            MyAssertHandler(".\\universal\\memfile.cpp", 559, 0, "%s", "sourceLen");
        while (sourceLen >= 0x2000)
        {
            if (stream.avail_in >= 0x2000)
                MyAssertHandler(".\\universal\\memfile.cpp", 569, 0, "%s", "stream.avail_in < TEMP_SAVE_BUFFER_SIZE");
            len = 0x2000 - stream.avail_in;
            memcpy(&g_saveBuffer[stream.avail_in], data, 0x2000 - stream.avail_in);
            stream.avail_in = 0x2000;
            sourceLen -= 0x2000;
            data += len;
            stream.next_in = g_saveBuffer;
            if (&memFile->buffer[memFile->bytesUsed] != stream.next_out)
                MyAssertHandler(
                    ".\\universal\\memfile.cpp",
                    578,
                    0,
                    "%s",
                    "memFile->buffer + memFile->bytesUsed == stream.next_out");
            if (deflate(&stream, 2u))
                MyAssertHandler(".\\universal\\memfile.cpp", 581, 0, "%s", "err == Z_OK");
            memFile->bytesUsed = stream.next_out - memFile->buffer;
            if (memFile->bytesUsed > memFile->bufferSize)
                MyAssertHandler(".\\universal\\memfile.cpp", 584, 0, "%s", "memFile->bytesUsed <= memFile->bufferSize");
            if (!stream.avail_out)
                return 0;
            if (!sourceLen)
                goto LABEL_30;
        }
        memcpy(&g_saveBuffer[stream.avail_in], data, sourceLen - stream.avail_in);
        stream.avail_in = sourceLen;
    LABEL_30:
        g_cacheBuffer[1] = nextByte;
        return 1;
    }
    else
    {
        memFile->buffer[memFile->bytesUsed] = cacheBufferLen + (nonZeroCount << 6);
        memFile->bytesUsed += bytes;
        if (memFile->bytesUsed + 65 > memFile->bufferSize)
        {
            return 0;
        }
        else
        {
            memFile->buffer[memFile->bytesUsed + 1] = nextByte;
            return 1;
        }
    }
}

int __cdecl MemFile_GetUsedSize(MemoryFile* memFile)
{
    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 612, 0, "%s", "memFile");
    return memFile->bytesUsed;
}

void __cdecl MemFile_WriteData(MemoryFile* memFile, int byteCount, const void* dat)
{
    uint32_t moveByte; // [esp+0h] [ebp-20h]
    uint32_t nextByte; // [esp+8h] [ebp-18h]
    int nonZeroCount; // [esp+Ch] [ebp-14h]
    int cacheBufferLen; // [esp+10h] [ebp-10h]
    int zeroCount; // [esp+18h] [ebp-8h]
    int cacheSize; // [esp+1Ch] [ebp-4h]
    int cacheSizea; // [esp+1Ch] [ebp-4h]
    int i;

    const byte* p = (const byte *)dat;

    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 643, 0, "%s", "memFile");
    if (!MemFile_IsWriting(memFile))
        MyAssertHandler(".\\universal\\memfile.cpp", 644, 0, "%s", "MemFile_IsWriting( memFile )");
    if (!memFile->buffer)
        MyAssertHandler(".\\universal\\memfile.cpp", 645, 0, "%s", "memFile->buffer");
    if (memFile->bytesUsed < 0 || memFile->bytesUsed > memFile->bufferSize)
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            646,
            0,
            "memFile->bytesUsed not in [0, memFile->bufferSize]\n\t%i not in [%i, %i]",
            memFile->bytesUsed,
            0,
            memFile->bufferSize);
    if (byteCount < 0)
        MyAssertHandler(".\\universal\\memfile.cpp", 647, 0, "%s\n\t(byteCount) = %i", "(byteCount >= 0)", byteCount);
    if (memFile->memoryOverflow)
        return;
    if (!p)
        MyAssertHandler(".\\universal\\memfile.cpp", 652, 0, "%s", "p");

    cacheSize = g_cacheSize;
    zeroCount = g_zeroCount;
    nonZeroCount = g_nonZeroCount;
    cacheBufferLen = g_cacheBufferLen;

#if 0
#define TRY_WRITE(bytes, nonZeroCount, cacheBufferLen, nextByte) \
    if (!MemFile_WriteDataInternal(memFile, bytes, nonZeroCount, cacheBufferLen, nextByte)) { MemFile_WriteError(memFile); return; }

#define NEXT_BYTE() \
    if (++i >= byteCount) goto DONE;

#define BOUNDS_CHECK() \
    vassert(i < byteCount, "(i = %d, byteCount = %d)", i, byteCount);

    // MAIN LOOP
    for (int i = 0; i < byteCount; )
    {
        // if we were previously writing nonzero bytes
        if (nonZeroCount)
        {
            vassert(i < byteCount, "(i = %d, byteCount = %d)", i, byteCount);
            vassert(nonZeroCount < 4, "(nonZeroCount = %d)", nonZeroCount);

            // hit some zero bytes. increment either until we're done or we overflowed
            bool overflow = false;
            while (!p[i])
            {
                // whoops, we'd overflow if we tried to write this zero byte.
                if (cacheBufferLen == CODE_LEN_MASK)
                {
                    overflow = true;
                    break;
                }

                ++cacheBufferLen;
                
                // continue on to the next byte
                NEXT_BYTE();
            }

            zeroCount = overflow ? 1 : 0;
            TRY_WRITE(cacheSize, nonZeroCount, cacheBufferLen, p[i]);

            cacheBufferLen = 0;
            nonZeroCount = 0;
            cacheSize = 2;
        }

        BOUNDS_CHECK();

        // flush cache if we'd overflow by writing the next byte, zero or not.
        if (cacheBufferLen == CODE_LEN_MASK)
        {
            // setting nonZeroCount to 0 implies we're writing a "stream" of N bytes, where N is CODE_LEN_MASK + 1.
            TRY_WRITE(cacheSize, 0, CODE_LEN_MASK, nextByte);

            cacheBufferLen = 0;
            nonZeroCount = 0;
            cacheSize = 2;
            zeroCount = (nextByte == 0) ? 1 : 0;

            NEXT_BYTE();
            nextByte = p[i];
        }

        if (p[i])
        {
            // nonzero byte, reset zero count. 
            zeroCount = 0;

        }
        else
        {
            // if we hit a zero byte, increment zero count and ??????

        }
    }

    DONE:

    // write-back data back to globals
    g_cacheSize = cacheSize;
    g_zeroCount = zeroCount;
    g_nonZeroCount = nonZeroCount;
    g_cacheBufferLen = cacheBufferLen;

    // append to cache, flush if needed
#endif

    // ORIGINAL BELOW

    for (i = 0; ; ++i)
    {
    LABEL_16:
        if (i >= byteCount)
        {
            g_cacheSize = cacheSize;
            g_zeroCount = zeroCount;
            g_nonZeroCount = nonZeroCount;
            g_cacheBufferLen = cacheBufferLen;
            return;
        }
        if (!nonZeroCount)
            break;

        vassert(i < byteCount, "(i = %d, byteCount = %d)", i, byteCount);

        while (!p[i])
        {
            ++zeroCount;
            if (cacheBufferLen == 63)
            {
                zeroCount = 1;
                goto LABEL_25;
            }
            ++cacheBufferLen;
            if (++i >= byteCount)
                goto LABEL_16;
        }
        zeroCount = 0;
    LABEL_25:
        if (!MemFile_WriteDataInternal(memFile, cacheSize, nonZeroCount, cacheBufferLen, p[i]))
            goto LABEL_56;
        cacheBufferLen = 0;
        nonZeroCount = 0;
        cacheSize = 2;
    }
    vassert(i < byteCount, "(i = %d, byteCount = %d)", i, byteCount);

    while (1)
    {
        nextByte = (uint8_t)p[i];
        if (cacheBufferLen == 63)
        {
            if (!MemFile_WriteDataInternal(memFile, cacheSize, 0, 63, nextByte))
                goto LABEL_56;
            cacheBufferLen = 0;
            nonZeroCount = 0;
            cacheSize = 2;
            zeroCount = nextByte == 0;
            ++i;
            goto LABEL_79;
        }
        if (!p[i])
            break;
        zeroCount = 0;
    LABEL_71:
        ++cacheBufferLen;
        
        vassert(cacheSize > 0, "(cacheSize = %d)", cacheSize);
        vassert(cacheSize < CODE_LEN_MASK + 2, "(cacheSize = %d)", cacheSize);
        
        if (memFile->compress)
            g_cacheBuffer[cacheSize] = nextByte;
        else
            memFile->buffer[cacheSize + memFile->bytesUsed] = nextByte;
        
        ++cacheSize;
        ++i;
    LABEL_79:
        if (i >= byteCount)
            goto LABEL_16;
    }
    ++zeroCount;
    if (cacheBufferLen <= 2)
    {
        if (cacheBufferLen >= 0)
        {
            nonZeroCount = cacheBufferLen + 1;
            cacheBufferLen = 0;
            ++i;
            goto LABEL_16;
        }
        goto LABEL_71;
    }
    if (cacheSize <= 2)
        MyAssertHandler(".\\universal\\memfile.cpp", 728, 0, "%s", "cacheSize > 2");
    if (zeroCount < 3)
    {
        if (memFile->compress)
        {
            iassert(g_cacheBuffer[cacheSize - 2] != 0 || g_cacheBuffer[cacheSize - 1] != 0);
           //if (!*(&g_cacheSize + cacheSize + 2) && !*(&g_cacheSize + cacheSize + 3))
           //    MyAssertHandler(
           //        ".\\universal\\memfile.cpp",
           //        765,
           //        0,
           //        "%s",
           //        "g_cacheBuffer[cacheSize - 2] != 0 || g_cacheBuffer[cacheSize - 1] != 0");
        }
        else if (!memFile->buffer[cacheSize - 2 + memFile->bytesUsed]
            && !memFile->buffer[cacheSize - 1 + memFile->bytesUsed])
        {
            MyAssertHandler(
                ".\\universal\\memfile.cpp",
                768,
                0,
                "%s",
                "memFile->buffer[memFile->bytesUsed + cacheSize - 2] != 0 || memFile->buffer[memFile->bytesUsed + cacheSize - 1] != 0");
        }
        goto LABEL_71;
    }
    if (zeroCount != 3)
        MyAssertHandler(".\\universal\\memfile.cpp", 731, 0, "%s", "zeroCount == 3");
    if (cacheSize <= 4)
        MyAssertHandler(".\\universal\\memfile.cpp", 732, 0, "%s\n\t(cacheSize) = %i", "(cacheSize > 4)", cacheSize);
    cacheSizea = cacheSize - 3;

    if (memFile->compress)
    {
        vassert(g_cacheBuffer[cacheSize - 2] == 0 && g_cacheBuffer[cacheSize - 1] == 0,
            "g_cacheBuffer[cacheSize - 2] == %d, g_cacheBuffer[cacheSize - 1] == %d",
            g_cacheBuffer[cacheSize - 2], g_cacheBuffer[cacheSize - 1]);
        moveByte = g_cacheBuffer[cacheSizea];
    }
    else
    {
        vassert(memFile->buffer[memFile->bytesUsed + cacheSize - 2] == 0 && memFile->buffer[memFile->bytesUsed + cacheSize - 1] == 0,
            "memFile->buffer[memFile->bytesUsed + cacheSize - 2] == %d, memFile->buffer[memFile->bytesUsed + cacheSize - 1] == %d",
            memFile->buffer[memFile->bytesUsed + cacheSize - 2], memFile->buffer[memFile->bytesUsed + cacheSize - 1]);

        moveByte = memFile->buffer[cacheSizea + memFile->bytesUsed];
    }
    if (!moveByte)
        MyAssertHandler(".\\universal\\memfile.cpp", 749, 0, "%s", "moveByte");
    if (MemFile_WriteDataInternal(memFile, cacheSizea, 0, cacheBufferLen - 3, moveByte))
    {
        cacheBufferLen = 2;
        nonZeroCount = 1;
        cacheSize = 2;
        zeroCount = 0;
        ++i;
        goto LABEL_16;
    }

LABEL_56:
    MemFile_WriteError(memFile);
}

void __cdecl MemFile_WriteCString(MemoryFile* memFile, const char* string)
{
    if (!string)
        MyAssertHandler(".\\universal\\memfile.cpp", 813, 0, "%s", "string");
    MemFile_WriteData(memFile, strlen(string) + 1, string);
}

const char* __cdecl MemFile_ReadCString(MemoryFile* memFile)
{
    uint8_t* string; // [esp+0h] [ebp-4h]

    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 824, 0, "%s", "memFile");
    if (!memFile->buffer)
        MyAssertHandler(".\\universal\\memfile.cpp", 825, 0, "%s", "memFile->buffer");
    if (memFile->bytesUsed < 0 || memFile->bytesUsed > memFile->bufferSize)
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            826,
            0,
            "memFile->bytesUsed not in [0, memFile->bufferSize]\n\t%i not in [%i, %i]",
            memFile->bytesUsed,
            0,
            memFile->bufferSize);

    string = g_saveBuffer;
    while (1)
    {
        MemFile_ReadData(memFile, 1, string);
        if (memFile->memoryOverflow)
            return "";
        if (!*string)
            break;
        if (++string >= &g_saveBuffer[8191])
            Com_Error(ERR_DROP, "Trying to read corrupted file");
    }

    return (const char*)g_saveBuffer;
}

void __cdecl MemFile_ReadData(MemoryFile* memFile, int byteCount, uint8_t* p)
{
    uint8_t* data; // [esp+0h] [ebp-8h]
    uint8_t code; // [esp+7h] [ebp-1h]

    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 900, 0, "%s", "memFile");
    if (!MemFile_IsReading(memFile))
        MyAssertHandler(".\\universal\\memfile.cpp", 901, 0, "%s", "MemFile_IsReading( memFile )");
    if (!memFile->buffer)
        MyAssertHandler(".\\universal\\memfile.cpp", 902, 0, "%s", "memFile->buffer");
    if (memFile->bytesUsed < 0 || memFile->bytesUsed > memFile->bufferSize)
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            903,
            0,
            "memFile->bytesUsed not in [0, memFile->bufferSize]\n\t%i not in [%i, %i]",
            memFile->bytesUsed,
            0,
            memFile->bufferSize);
    if (byteCount < 0)
        MyAssertHandler(".\\universal\\memfile.cpp", 904, 0, "%s\n\t(byteCount) = %i", "(byteCount >= 0)", byteCount);
    if (byteCount && !memFile->memoryOverflow)
    {
        if (!p)
            MyAssertHandler(".\\universal\\memfile.cpp", 912, 0, "%s", "p");
        data = p;
        while (1)
        {
            while (g_nonZeroCount)
            {
                --g_nonZeroCount;
                --byteCount;
                *data++ = MemFile_ReadByteInternal(memFile);
                if (memFile->memoryOverflow || !byteCount)
                    return;
            }
            while (g_zeroCount)
            {
                --g_zeroCount;
                --byteCount;
                *data++ = 0;
                if (!byteCount)
                    return;
            }
            code = MemFile_ReadByteInternal(memFile);
            if (memFile->memoryOverflow)
                break;
            if ((code & 0xC0) != 0)
            {
                g_nonZeroCount = (int)code >> 6;
                g_zeroCount = (code & 0x3F) + 1;
            }
            else
            {
                g_nonZeroCount = (code & 0x3F) + 1;
                g_zeroCount = 0;
            }
        }
    }
}

MemFileReadStatus MemFile_TryReadDataNoReport(
    MemoryFile* memFile,
    int byteCount,
    uint8_t* output) noexcept
{
    if (!memFile || byteCount < 0 || (byteCount > 0 && !output))
        return MemFileReadStatus::InvalidArgument;
    if (!MemFile_HasValidReadObject(memFile))
        return MemFileReadStatus::InvalidState;
    if (byteCount == 0)
        return MemFileReadStatus::Success;
    if (memFile->memoryOverflow)
        return MemFileReadStatus::Overflow;
    if (!MemFile_HasValidReadDecoder(memFile))
        return MemFileReadStatus::InvalidState;

    int remaining = byteCount;
    uint8_t* destination = output;
    while (true)
    {
        while (g_nonZeroCount)
        {
            --g_nonZeroCount;
            --remaining;

            uint8_t value = 0;
            const MemFileReadStatus status = MemFile_ReadEncodedByteNoReport(memFile, &value);
            *destination++ = value;
            if (status != MemFileReadStatus::Success)
                return status;
            if (!remaining)
                return MemFileReadStatus::Success;
        }

        while (g_zeroCount)
        {
            --g_zeroCount;
            --remaining;
            *destination++ = 0;
            if (!remaining)
                return MemFileReadStatus::Success;
        }

        uint8_t code = 0;
        const MemFileReadStatus status = MemFile_ReadEncodedByteNoReport(memFile, &code);
        if (status != MemFileReadStatus::Success)
            return status;

        if ((code & 0xC0) != 0)
        {
            g_nonZeroCount = static_cast<int>(code >> 6);
            g_zeroCount = static_cast<int>(code & CODE_LEN_MASK) + 1;
        }
        else
        {
            g_nonZeroCount = static_cast<int>(code & CODE_LEN_MASK) + 1;
            g_zeroCount = 0;
        }
    }
}

MemFileReadStatus MemFile_TryReadCStringNoReport(
    MemoryFile* memFile,
    char* output,
    size_t outputSize,
    size_t* outputLength) noexcept
{
    if (!memFile || !output || !outputLength || outputSize == 0)
        return MemFileReadStatus::InvalidArgument;
    if (!MemFile_HasValidReadObject(memFile))
        return MemFileReadStatus::InvalidState;
    if (!memFile->memoryOverflow && !MemFile_HasValidReadDecoder(memFile))
        return MemFileReadStatus::InvalidState;

    output[0] = '\0';
    *outputLength = 0;
    if (memFile->memoryOverflow)
        return MemFileReadStatus::Overflow;

    size_t retainedLength = 0;
    for (size_t consumed = 0; consumed < outputSize; ++consumed)
    {
        uint8_t value = 0;
        const MemFileReadStatus status = MemFile_TryReadDataNoReport(memFile, 1, &value);
        if (status != MemFileReadStatus::Success)
        {
            output[retainedLength] = '\0';
            *outputLength = retainedLength;
            return status;
        }

        if (!value)
        {
            output[retainedLength] = '\0';
            *outputLength = retainedLength;
            return MemFileReadStatus::Success;
        }

        if (consumed + 1 < outputSize)
        {
            output[retainedLength++] = static_cast<char>(value);
            continue;
        }

        output[retainedLength] = '\0';
        *outputLength = retainedLength;
        return MemFileReadStatus::OutputTooSmall;
    }

    return MemFileReadStatus::OutputTooSmall;
}

void MemFile_AbandonCurrentThreadForError() noexcept
{
    MemoryFile* const owner = g_currentThreadStreamOwner;
    if (owner)
        MemFile_ResetOwnedStreamNoReport(owner);
}

uint8_t __cdecl MemFile_ReadByteInternal(MemoryFile* memFile)
{
    uint32_t err; // [esp+0h] [ebp-8h]
    uint8_t result; // [esp+7h] [ebp-1h] BYREF

    if (!memFile)
        MyAssertHandler(".\\universal\\memfile.cpp", 851, 0, "%s", "memFile");
    if (!MemFile_IsReading(memFile))
        MyAssertHandler(".\\universal\\memfile.cpp", 852, 0, "%s", "MemFile_IsReading( memFile )");
    if (!memFile->buffer)
        MyAssertHandler(".\\universal\\memfile.cpp", 853, 0, "%s", "memFile->buffer");
    if (memFile->bytesUsed < 0 || memFile->bytesUsed > memFile->bufferSize)
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            854,
            0,
            "memFile->bytesUsed not in [0, memFile->bufferSize]\n\t%i not in [%i, %i]",
            memFile->bytesUsed,
            0,
            memFile->bufferSize);
    if (memFile->memoryOverflow)
        MyAssertHandler(".\\universal\\memfile.cpp", 855, 0, "%s", "!memFile->memoryOverflow");
    AssertStreamMode(MEM_FILE_MODE_INFLATE);
    if (!MemFile_HasValidReadDecoder(memFile))
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            857,
            0,
            "%s",
            "memFile owns a valid active read segment");
    }
    else if (memFile->compress)
    {
        stream.next_out = &result;
        stream.avail_out = 1;
        if (&memFile->buffer[memFile->bytesUsed] != stream.next_in)
            MyAssertHandler(
                ".\\universal\\memfile.cpp",
                865,
                0,
                "%s",
                "memFile->buffer + memFile->bytesUsed == stream.next_in");
        err = inflate(&stream, 2);
        if (err > 1)
            MyAssertHandler(".\\universal\\memfile.cpp", 868, 0, "%s\n\t(err) = %i", "((err == 0) || (err == 1))", err);
        memFile->bytesUsed = stream.next_in - memFile->buffer;
        if (!stream.avail_out)
            return result;
    }
    else if (static_cast<size_t>(memFile->bytesUsed) < g_activeSegmentEnd)
    {
        return memFile->buffer[memFile->bytesUsed++];
    }
    if (memFile->errorOnOverflow)
        Com_Error(ERR_DROP, "Trying to read corrupted file");
    Com_Printf(10, "Trying to read corrupted file");
    memFile->memoryOverflow = 1;
    return 0;
}

void MemFile_Shutdown(MemoryFile *memFile)
{
    iassert(memFile);

    if (g_streamOwner == memFile)
        MemFile_ResetOwnedStreamNoReport(memFile);

    memFile->buffer = 0;
}

uint8_t *MemFile_CopySegments(MemoryFile *memFile, int index, void *buf)
{
    iassert(!memFile->memoryOverflow);

    if (index < 0)
    {
        MyAssertHandler(
            ".\\universal\\memfile.cpp",
            960,
            0,
            "%s\n\t(index) = %i",
            "((index >= 0) && (index < SAVE_SEGMENT_COUNT))",
            index);
        return nullptr;
    }

    const uint8_t* segmentAddress = MemFile_GetSegmentAddess(memFile, static_cast<uint32_t>(index));
    if (!segmentAddress)
        return nullptr;

    const size_t segmentOffset = static_cast<size_t>(segmentAddress - memFile->buffer);
    const size_t copySize = static_cast<size_t>(memFile->bufferSize) - segmentOffset;
    if (buf)
        memcpy(buf, segmentAddress, copySize);
    return reinterpret_cast<uint8_t*>(copySize);
}

// SPDX-License-Identifier: GPL-3.0
//
// Production-path coverage for the tagInfo save record. The subject is the
// real src/game/taginfo_save.cpp — the record module g_save.cpp's
// WriteField2 / ReadField SF_TYPE_TAG_INFO branches delegate to — compiled
// as a test subject and driven through the real universal/memfile.cpp stream
// primitives with a fake entity arena and string table.
//
// Regression context (operator rework 2026-09-04): the earlier
// implementation routed tagInfo_s::parent/next through the legacy
// WriteField1 SF_ENTITY walker, which classifies and subtracts through the
// low 32 bits of the pointer-sized field and overwrites only 4 bytes of
// each 64-bit pointer before the converter read the value back. The
// header-only converter tests could not see that production path. These
// tests drive the real record module end to end: full-pointer index
// derivation, parity with the pinned Disk32 converter contract, load-side
// validation, and pointer restoration.
//
// Note on stream framing: MemFile segments carry their own header and RLE
// encoding, so assertions are behavioral (what the record module writes is
// what a reader decodes into) rather than raw byte offsets.

#include <game/taginfo_save.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int failures = 0;
int unexpectedReports = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

namespace taginfo = taginfo_save;

// Fake entity arena. The stride deliberately differs from the 4-byte index
// encoding so that deriving an index from any truncated pointer value cannot
// accidentally succeed — only genuine full-pointer arithmetic against the
// arena base produces the correct index.
struct FakeEntity
{
    std::uint32_t marker;
    std::uint8_t padding[28];
};
constexpr std::uint32_t kEntityCount = 8;
FakeEntity tagArena[kEntityCount];

taginfo::TagInfoEntityMap MakeEntityMap()
{
    return taginfo::TagInfoEntityMap{tagArena, sizeof(FakeEntity), kEntityCount};
}

// The record module reports defects before touching the stream; the fail
// callback unwinds through a dedicated exception type so the validation paths
// are observable. Deliberately NOT setjmp/longjmp: the interaction between
// _setjmp and C++ object destruction is non-portable and MSVC /W4 /WX
// promotes the C4611 diagnostic to an error anywhere a jump frame is in
// scope (including inlined callees). Throwing from the module's own fail
// callback is plain C++ on every supported compiler and observes exactly the
// same reporting contract. The type carries no members: the reported text is
// delivered through the failMessage witness below, so there is nothing to
// copy or drop on the unwind path.
struct RecordDefect
{
};

const char *failMessage = nullptr;

void RecordFail(const char *message)
{
    failMessage = message;
    throw RecordDefect{};
}

// Fake scr string table: handle N maps to knownNames[N - 1].
const char *const knownNames[4] = {"alphonso", "bravo", "chair", "delta"};
constexpr std::uint16_t kNameHandleChair = 3;

const char *FakeStringFromHandle(const std::uint16_t handle)
{
    if (handle == 0 || handle > 4)
        return nullptr;
    return knownNames[handle - 1];
}

std::uint16_t FakeHandleFromString(const char *const text)
{
    for (std::uint16_t i = 0; i < 4; ++i)
    {
        if (std::strcmp(knownNames[i], text) == 0)
            return static_cast<std::uint16_t>(i + 1);
    }
    return 0;
}

// Drives WriteHostRecord expecting a defect report through RecordFail.
// The catch frame carries no resource ownership: callers keep ownership of
// every handle, and the module's guarantee is that a defect is reported
// before the stream is touched, so the writer needs no repair here. Returns
// true when the module reported a defect (RecordFail threw).
bool WriteExpectingDefect(
    MemoryFile *writer,
    const taginfo::TagInfoEntityMap &map,
    const taginfo::TagInfoLiveRecord &live)
{
    try
    {
        taginfo::WriteHostRecord(
            writer, map, RecordFail, FakeStringFromHandle, live);
    }
    catch (const RecordDefect &)
    {
        return true;
    }
    return false;
}

// Read-side counterpart of WriteExpectingDefect.
bool ReadExpectingDefect(
    MemoryFile *reader,
    const taginfo::TagInfoEntityMap &map)
{
    taginfo::TagInfoRestoredRecord restored{};
    try
    {
        taginfo::ReadHostRecord(
            reader, map, RecordFail, FakeHandleFromString, restored);
    }
    catch (const RecordDefect &)
    {
        return true;
    }
    return false;
}

constexpr std::size_t kStorageBytes = 4096;

void BeginWriter(MemoryFile &file, std::vector<std::uint8_t> &storage)
{
    storage.assign(kStorageBytes, 0);
    file = MemoryFile{};
    MemFile_InitForWriting(
        &file,
        static_cast<int>(storage.size()),
        storage.data(),
        false,
        false);
}

// Finalize the writer the way the memfile segment protocol requires and hand
// the payload buffer back for reading.
std::vector<std::uint8_t> FinishWriter(
    MemoryFile &file,
    std::vector<std::uint8_t> &storage)
{
    MemFile_StartSegment(&file, -1);
    CHECK(!file.memoryOverflow);
    CHECK(file.bufferSize >= 0);
    storage.resize(static_cast<std::size_t>(file.bufferSize));
    MemFile_Shutdown(&file);
    return storage;
}

// The reader is handed back through a reference, never by value: MemFile
// stream ownership is keyed on object identity (g_streamOwner holds the
// address MemFile_InitForReading registered), so a copied or relocated
// MemoryFile desyncs the owner check and every subsequent read is rejected
// as decoder-invalid. A named-local return here compiled to an actual copy
// on MSVC (GCC's NRVO hid it), which is exactly the Windows-only round-trip
// failure the CI legs observed.
void BeginReader(const std::vector<std::uint8_t> &payload, MemoryFile &reader)
{
    reader = MemoryFile{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(payload.size()),
        const_cast<std::uint8_t *>(payload.data()),
        false);
}

void CloseReader(MemoryFile &reader)
{
    if (!reader.memoryOverflow && reader.segmentIndex >= 0)
        MemFile_MoveToSegment(&reader, -1);
    MemFile_Shutdown(&reader);
}

bool FloatArraysEqual(const float a[4][3], const float b[4][3])
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            if (a[r][c] != b[r][c])
                return false;
        }
    }
    return true;
}

void FillAxes(taginfo::TagInfoLiveRecord &live)
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            live.axis[r][c] = static_cast<float>(r * 3 + c);
            live.parentInvAxis[r][c] = static_cast<float>(-(r * 3 + c));
        }
    }
}

taginfo::TagInfoLiveRecord MakeNamedLiveRecord()
{
    taginfo::TagInfoLiveRecord live{};
    live.parent = &tagArena[kEntityCount - 1]; // maximum valid index
    live.next = &tagArena[0];                  // index 1 (array base)
    live.name = kNameHandleChair;
    live.index = 42;
    FillAxes(live);
    return live;
}

void CheckRestoredMatches(
    const taginfo::TagInfoRestoredRecord &restored,
    const void *const expectedParent,
    const void *const expectedNext,
    const std::uint16_t expectedName,
    const std::int32_t expectedIndex,
    const taginfo::TagInfoLiveRecord &live,
    const int line)
{
    // The load path must rebuild the exact FULL native pointers.
    Check(restored.parent == expectedParent, "restored.parent", line);
    Check(restored.next == expectedNext, "restored.next", line);
    Check(restored.name == expectedName, "restored.name", line);
    Check(restored.index == expectedIndex, "restored.index", line);
    Check(FloatArraysEqual(restored.axis, live.axis), "restored.axis", line);
    Check(FloatArraysEqual(restored.parentInvAxis, live.parentInvAxis),
        "restored.parentInvAxis", line);
}

void TestNamedRecordRoundTrip()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live = MakeNamedLiveRecord();

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);

    taginfo::WriteHostRecord(
        &writer, map, RecordFail, FakeStringFromHandle, live);

    const std::vector<std::uint8_t> payload = FinishWriter(writer, storage);
    MemoryFile reader{};
    BeginReader(payload, reader);

    taginfo::TagInfoRestoredRecord restored{};
    taginfo::ReadHostRecord(
        &reader, map, RecordFail, FakeHandleFromString, restored);

    // Handle 3 only maps back from the exact text "chair", so a restored
    // handle of 3 proves the CString carried the live record's name.
    CheckRestoredMatches(restored, &tagArena[kEntityCount - 1], &tagArena[0],
        kNameHandleChair, 42, live, __LINE__);

    CloseReader(reader);
}

// Assemble the same logical record through the pinned Disk32 converter
// contract and emit it as a raw record stream (fixed 0x70 image + name
// CString), bypassing the record module.
std::vector<std::uint8_t> WriteRawConverterStream(
    const taginfo::TagInfoLiveRecord &live)
{
    taginfo::tagInfoHostView expectedView{};
    expectedView.parent = kEntityCount; // &tagArena[kEntityCount - 1]
    expectedView.next = 1;              // &tagArena[0]
    expectedView.name = 1;              // retail mark, never the live handle
    expectedView.index = live.index;
    std::memcpy(expectedView.axis, live.axis, sizeof(expectedView.axis));
    std::memcpy(expectedView.parentInvAxis, live.parentInvAxis,
        sizeof(expectedView.parentInvAxis));
    const taginfo::tagInfoDisk32_s expectedDisk =
        taginfo::TagInfoToDisk32(expectedView);

    MemoryFile rawWriter{};
    std::vector<std::uint8_t> rawStorage;
    BeginWriter(rawWriter, rawStorage);
    MemFile_WriteData(
        &rawWriter,
        static_cast<int>(taginfo::kTagInfoDisk32Bytes),
        &expectedDisk);
    MemFile_WriteCString(&rawWriter, FakeStringFromHandle(live.name));
    return FinishWriter(rawWriter, rawStorage);
}

// Run one written payload through the real load path.
taginfo::TagInfoRestoredRecord DecodeRecordStream(
    const std::vector<std::uint8_t> &payload,
    const taginfo::TagInfoEntityMap &map)
{
    taginfo::TagInfoRestoredRecord restored{};
    MemoryFile reader{};
    BeginReader(payload, reader);
    taginfo::ReadHostRecord(
        &reader, map, RecordFail, FakeHandleFromString, restored);
    CloseReader(reader);
    return restored;
}

void TestWireParityWithPinnedConverter()
{
    // Whatever WriteHostRecord emits must decode to exactly the record the
    // pinned Disk32 converter produces for the same logical values: write
    // one stream via the record module and one via the raw converter image,
    // then require identical decodes.
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live = MakeNamedLiveRecord();

    MemoryFile moduleWriter{};
    std::vector<std::uint8_t> moduleStorage;
    BeginWriter(moduleWriter, moduleStorage);
    taginfo::WriteHostRecord(
        &moduleWriter, map, RecordFail, FakeStringFromHandle, live);
    const std::vector<std::uint8_t> modulePayload =
        FinishWriter(moduleWriter, moduleStorage);
    const std::vector<std::uint8_t> rawPayload = WriteRawConverterStream(live);

    const taginfo::TagInfoRestoredRecord fromModule =
        DecodeRecordStream(modulePayload, map);
    const taginfo::TagInfoRestoredRecord fromRaw =
        DecodeRecordStream(rawPayload, map);

    CHECK(fromRaw.parent == fromModule.parent);
    CHECK(fromRaw.next == fromModule.next);
    CHECK(fromRaw.name == fromModule.name);
    CHECK(fromRaw.index == fromModule.index);
    CHECK(FloatArraysEqual(fromRaw.axis, fromModule.axis));
    CHECK(FloatArraysEqual(fromRaw.parentInvAxis, fromModule.parentInvAxis));

    // And both must land on the exact full native pointers with the retail
    // mark restored through the string table.
    CHECK(fromModule.parent == &tagArena[kEntityCount - 1]);
    CHECK(fromModule.next == &tagArena[0]);
    CHECK(fromModule.name == kNameHandleChair);
}

void TestUnnamedNullRecord()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live{};
    live.parent = nullptr;
    live.next = nullptr;
    live.name = 0;
    live.index = -7;
    FillAxes(live);

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);

    taginfo::WriteHostRecord(
        &writer, map, RecordFail, FakeStringFromHandle, live);
    // The zero mark must make the record module append nothing; prove it by
    // following the record with an unrelated CString that must survive the
    // record load without being swallowed.
    MemFile_WriteCString(&writer, "after");

    const std::vector<std::uint8_t> payload = FinishWriter(writer, storage);
    MemoryFile reader{};
    BeginReader(payload, reader);

    taginfo::TagInfoRestoredRecord restored{};
    taginfo::ReadHostRecord(
        &reader, map, RecordFail, FakeHandleFromString, restored);

    CHECK(restored.parent == nullptr);
    CHECK(restored.next == nullptr);
    CHECK(restored.name == 0);
    CHECK(restored.index == -7);
    CHECK(FloatArraysEqual(restored.axis, live.axis));
    CHECK(FloatArraysEqual(restored.parentInvAxis, live.parentInvAxis));

    // The CString following the record is exactly what remains.
    CHECK(std::strcmp(MemFile_ReadCString(&reader), "after") == 0);

    CloseReader(reader);
}

void TestWriteRejectsMisalignedPointer()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live = MakeNamedLiveRecord();
    // Interior pointer: not on an entity boundary.
    live.parent = static_cast<const std::uint8_t *>(
                      static_cast<const void *>(&tagArena[1]))
        + 4;

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);
    const int usedBefore = MemFile_GetUsedSize(&writer);

    if (!WriteExpectingDefect(&writer, map, live))
    {
        Check(false, "misaligned parent pointer must be rejected", __LINE__);
    }
    else
    {
        CHECK(failMessage != nullptr);
        CHECK(std::strstr(failMessage, "boundary") != nullptr);
        // The stream must be untouched: validation precedes any write.
        CHECK(MemFile_GetUsedSize(&writer) == usedBefore);
    }

    MemFile_Shutdown(&writer);
}

void TestWriteRejectsOutOfRangePointer()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live = MakeNamedLiveRecord();
    // One slot past the end of the entity array.
    live.next = &tagArena[kEntityCount];

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);
    const int usedBefore = MemFile_GetUsedSize(&writer);

    if (!WriteExpectingDefect(&writer, map, live))
    {
        Check(false, "out-of-range next pointer must be rejected", __LINE__);
    }
    else
    {
        CHECK(failMessage != nullptr);
        CHECK(std::strstr(failMessage, "out of range") != nullptr);
        CHECK(MemFile_GetUsedSize(&writer) == usedBefore);
    }

    MemFile_Shutdown(&writer);
}

void TestWriteRejectsPointerBelowBase()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();
    taginfo::TagInfoLiveRecord live = MakeNamedLiveRecord();
    // One entity slot below the arena base. The displacement is laundered
    // through a volatile read so the optimizer cannot fold the deliberately
    // out-of-bounds pointer into a compile-time array-bounds diagnostic.
    const std::uintptr_t arenaBase = reinterpret_cast<std::uintptr_t>(tagArena);
    const volatile std::uintptr_t displacement = sizeof(FakeEntity);
    live.parent = reinterpret_cast<const void *>(arenaBase - displacement);

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);
    const int usedBefore = MemFile_GetUsedSize(&writer);

    if (!WriteExpectingDefect(&writer, map, live))
    {
        Check(false, "pointer below the entity base must be rejected", __LINE__);
    }
    else
    {
        CHECK(failMessage != nullptr);
        CHECK(std::strstr(failMessage, "precedes") != nullptr);
        CHECK(MemFile_GetUsedSize(&writer) == usedBefore);
    }

    MemFile_Shutdown(&writer);
}

void TestReadRejectsOutOfRangeIndex()
{
    const taginfo::TagInfoEntityMap map = MakeEntityMap();

    // Craft a record whose parent index exceeds the entity bound and feed it
    // through the real load path.
    taginfo::tagInfoHostView hostile{};
    hostile.parent = kEntityCount + 1u;
    hostile.next = 0;
    hostile.name = 0;
    hostile.index = 1;
    const taginfo::tagInfoDisk32_s hostileDisk =
        taginfo::TagInfoToDisk32(hostile);

    MemoryFile writer{};
    std::vector<std::uint8_t> storage;
    BeginWriter(writer, storage);
    MemFile_WriteData(
        &writer,
        static_cast<int>(taginfo::kTagInfoDisk32Bytes),
        &hostileDisk);
    const std::vector<std::uint8_t> payload = FinishWriter(writer, storage);

    MemoryFile reader{};
    BeginReader(payload, reader);

    if (!ReadExpectingDefect(&reader, map))
    {
        Check(false, "out-of-range record index must be rejected", __LINE__);
    }
    else
    {
        CHECK(failMessage != nullptr);
        CHECK(std::strstr(failMessage, "out of range") != nullptr);
    }

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

namespace
{
// Backing store for the va() shim below. Namespace scope rather than a
// function-local static: none of the tested record paths format through
// va(), so the shim only has to hand back a valid empty C string.
char g_vaStub[1] = {};
} // namespace

char *QDECL va(const char *format, ...)
{
    (void)format;
    return g_vaStub;
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
    TestNamedRecordRoundTrip();
    TestWireParityWithPinnedConverter();
    TestUnnamedNullRecord();
    TestWriteRejectsMisalignedPointer();
    TestWriteRejectsOutOfRangePointer();
    TestWriteRejectsPointerBelowBase();
    TestReadRejectsOutOfRangeIndex();

    CHECK(unexpectedReports == 0);

    if (failures != 0)
    {
        std::fprintf(stderr,
            "save_taginfo_production_test: %d failure(s)\n",
            failures);
        return 1;
    }
    std::puts("save_taginfo_production_test passed");
    return 0;
}

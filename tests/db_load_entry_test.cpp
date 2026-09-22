// db_load_entry_test: drives the REAL production database admission
// and fast-file stream relocation entry points (ki-458h / #125).
//
// Enrollment (all production code, no synthetic parser sketches):
//   - DB_Init / DB_AddXAsset / DB_FindXAssetHeader /
//     DB_FindXAssetEntry / DB_EnumXAssets / DB_RemoveXAsset from
//     src/database/db_registry.cpp - the production hash-table
//     admission, cloning, lookup and removal chain over the
//     production per-type pools.
//   - Load_Stream / Load_StreamArray / Load_DelayStream /
//     DB_ConvertOffsetToPointer / DB_ConvertOffsetToAlias /
//     DB_ConvertOffsetToCString / DB_ConvertOffsetToTempString from
//     src/database/db_stream_load.cpp over the production stream
//     block machinery (db_stream.cpp), relocation resolver
//     (db_relocation.cpp) and temp-string intern bridge
//     (db_load_legacy_bridge.cpp).
//   - The production zone-runtime table substrate and sync
//     primitives (db_zone_runtime_table.cpp cohort, sys_sync.cpp,
//     physicalmemory).
//
// The harness services (db_load_entry_harness.hpp) supply the
// controlled providers: an in-memory fast-file byte source behind
// DB_LoadXFileData, real stream blocks backed by fixture buffers,
// and single-threaded database-thread coordination semantics. The
// fail-closed protocol matches the production guards: a rejected
// operation RECORDS its error and returns without mutating its
// output field, and every rejection case asserts both the recorded
// error and the untouched-output invariant. The registry cases close
// with a resilience pass proving the registry admits and finds fresh
// assets after every recorded rejection above.
//
// Win32-x86 only: the enrolled production TUs compile in this test
// binary exactly as the game targets build them — the decompiled
// MSVC dialect and the DirectX/Miles/ODE header web that pulls in do
// not compile on the portable 64-bit legs; tests/CMakeLists.txt
// gates this target to the Windows x86 CI leg, where every enrolled
// TU is already built by an engine target.

#include "db_load_entry_harness.hpp"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// The useFastFile dvar the production IsFastFileLoad check reads:
// fast-file mode enabled.
const dvar_t *useFastFile = &db_load_entry_harness::UseFastFileDvar();

// ---------------------------------------------------------------------------
// printf-family, FS and process-kill definitions (bodies live in a
// .cpp: CWE-134 policy for printf wrappers; the FS endpoints and
// process-kill helpers stay beside them so the header stays free of
// definitions the registry TUs would collide with).
// ---------------------------------------------------------------------------
void Com_Error(errorParm_t code, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    db_load_entry_harness::RecordedError record;
    record.channel = static_cast<int>(code);
    record.text = buffer;
    db_load_entry_harness::State().errors.push_back(record);
    // Visible triage: every recorded drop names itself on stdout.
    std::printf("err: %s\n", buffer);

    if (code == ERR_FATAL)
    {
        std::printf("FATAL: %s\n", buffer);
        std::_Exit(3);
    }
    // ERR_DROP: production longjmps to the zone-load frame. When the
    // case armed a jump target the path is one production relies on
    // unwinding through (no return guard after the drop), so honor
    // it after the error is recorded.
    if (db_load_entry_harness::ErrDrop().armed)
        std::longjmp(db_load_entry_harness::ErrDrop().target, 1);
}

void Com_PrintError(int channel, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    db_load_entry_harness::RecordedError record;
    record.channel = channel;
    record.text = buffer;
    db_load_entry_harness::State().printErrors.push_back(record);
    std::printf("printError(%d): %s\n", channel, buffer);
}

void Com_Printf(int channel, const char *fmt, ...)
{
    (void)channel;
    (void)fmt;
}

void MyAssertHandler(const char *filename, int line, int type,
                     const char *fmt, ...)
{
    (void)filename;
    (void)line;
    (void)type;
    (void)fmt;
    std::printf("PRODUCTION ASSERT - terminating\n");
    std::_Exit(3);
}

// The physicalmemory / zone-runtime substrate's out-of-memory
// endpoint: no allocation the admission fixtures make can fail, so
// reaching it is a hard error.
void Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    (void)filename;
    (void)line;
    std::printf("OUT OF MEMORY - terminating\n");
    std::_Exit(3);
}

double ConvertToMB(const int bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

// FS no-op file endpoints: DB_LogMissingAsset opens missingasset.csv
// through these; the harness reports no file so the write is skipped.
int FS_FOpenTextFileWrite(const char *filename)
{
    (void)filename;
    return 0;
}

int FS_FOpenFileAppend(const char *filename)
{
    (void)filename;
    return 0;
}

void FS_FCloseFile(int handle)
{
    (void)handle;
}

uint32_t FS_Write(const char *buffer, uint32_t len, int handle)
{
    (void)buffer;
    (void)len;
    (void)handle;
    return 0;
}

// The controlled fast-file byte provider: DB_LoadXFileData is the
// production seam where the database loader reads from the fast-file
// image; the harness feeds sequential bytes from the fixture source.
void __cdecl DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    ++db_load_entry_harness::State().loadXFileDataCalls;
    if (!pos)
    {
        Com_Error(ERR_DROP, "Harness fixture: null stream read target");
        return;
    }
    db_load_entry_harness::HarnessState &state =
        db_load_entry_harness::State();
    const uint32_t remaining =
        static_cast<uint32_t>(state.fileBytes.size()) - state.fileCursor;
    if (size > remaining)
    {
        Com_Error(ERR_DROP,
                  "Harness fixture: stream read of %u bytes exhausts the "
                  "fast-file image (%u remaining)",
                  size, remaining);
        return;
    }
    std::memcpy(pos, state.fileBytes.data() + state.fileCursor, size);
    state.fileCursor += size;
    state.servedBytes += size;
}

// ---------------------------------------------------------------------------
// The cases.
// ---------------------------------------------------------------------------
namespace
{
uint32_t g_failures = 0;
uint32_t g_checks = 0;

void ExpectTrue(bool condition, const char *what, int line)
{
    ++g_checks;
    if (!condition)
    {
        std::printf("FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

void ExpectU32(uint32_t actual, uint32_t expected, const char *what,
               int line)
{
    ++g_checks;
    if (actual != expected)
    {
        std::printf("FAIL line %d: %s (actual 0x%08X expected 0x%08X)\n",
                    line, what, actual, expected);
        ++g_failures;
    }
}

void ClearErrors()
{
    db_load_entry_harness::State().errors.clear();
}
} // namespace

#define EXPECT(cond) ExpectTrue((cond), #cond, __LINE__)
#define EXPECT_U32(actual, expected) \
    ExpectU32((actual), (expected), #actual " == " #expected, __LINE__)

namespace
{
using namespace db_load_entry_harness;

// The shared fixture payloads: the registry clones
// DB_GetXAssetTypeSize(ASSET_TYPE_RAWFILE) bytes out of the caller's
// header, so the payloads must outlive every admission case.
unsigned char g_payloadBytes[64];
RawFilePayload g_payloadA;
RawFilePayload g_payloadB;

void InitPayloads()
{
    for (unsigned int i = 0; i < sizeof(g_payloadBytes); ++i)
        g_payloadBytes[i] = static_cast<unsigned char>(i * 7u);

    g_payloadA.name = "loadentry/payload_a";
    g_payloadA.len = 32;
    g_payloadA.buffer = reinterpret_cast<const char *>(g_payloadBytes);
    g_payloadB.name = "loadentry/payload_b";
    g_payloadB.len = 64;
    g_payloadB.buffer = reinterpret_cast<const char *>(g_payloadBytes);
}

XAssetHeader PayloadHeader(RawFilePayload *payload)
{
    XAssetHeader header;
    header.data = payload;
    return header;
}

bool HeaderBytesEqual(XAssetHeader header, const void *rawFileBytes,
                      uint32_t len)
{
    const RawFile *rawFile =
        reinterpret_cast<const RawFile *>(header.data);
    if (!rawFile || !rawFile->buffer)
        return false;
    if (rawFile->len != static_cast<int>(len))
        return false;
    return std::memcmp(rawFile->buffer, rawFileBytes, len) == 0;
}

// C1: production admission, lookup, duplicate-add and enum.
void CaseRegistryAdmissionRoundtrip()
{
    std::printf("case: registry admission roundtrip\n");
    ClearErrors();

    XAssetHeader added =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&g_payloadA));
    EXPECT(added.data != nullptr);

    // The production lookup returns the POOL CLONE, not the caller's
    // payload: name, length and byte content must match.
    XAssetHeader found = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                                             "loadentry/payload_a");
    EXPECT(found.data != nullptr);
    EXPECT(found.data != static_cast<void *>(&g_payloadA));
    EXPECT(HeaderBytesEqual(found, g_payloadBytes, 32));

    // Production lookup is case-insensitive (I_stricmp chain walk).
    XAssetHeader foundUpper = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                                                  "LOADENTRY/PAYLOAD_A");
    EXPECT(foundUpper.data == found.data);

    // Duplicate admission returns the SAME pooled header: the
    // registry hashes and dedupes instead of double-admitting.
    XAssetHeader duplicate =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&g_payloadA));
    EXPECT(duplicate.data != nullptr);
    EXPECT(duplicate.data == found.data);
    EXPECT(Errors().empty());

    // Production enum visits the admitted asset.
    uint32_t visited = 0;
    DB_EnumXAssets(
        ASSET_TYPE_RAWFILE,
        [](XAssetHeader header, void *data)
        {
            (void)header;
            ++*static_cast<uint32_t *>(data);
        },
        &visited,
        1);
    EXPECT(visited >= 1);
}

// C2: the production removal dispatch. DB_RemoveXAsset routes the
// asset through the per-type DB_RemoveXAssetHandler table (teardown
// of type-owned resources); the HASH ENTRY's lifetime is owned by the
// zone-unload path, not by this entry point. ASSET_TYPE_RAWFILE has
// no teardown handler, so removal is a no-op dispatch: the entry
// stays linked and a re-admission dedupes to the same pool slot.
void CaseRegistryRemoveReadmit()
{
    std::printf("case: registry remove and re-admit\n");
    ClearErrors();

    XAssetHeader added =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&g_payloadB));
    EXPECT(added.data != nullptr);
    XAssetHeader found = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                                             "loadentry/payload_b");
    EXPECT(found.data == added.data);

    // Production removal through the entry the registry returned.
    XAssetEntryPoolEntry *entry =
        DB_FindXAssetEntry(ASSET_TYPE_RAWFILE, "loadentry/payload_b");
    EXPECT(entry != nullptr);
    if (!entry)
        return;
    EXPECT(entry->entry.asset.header.data == added.data);
    DB_RemoveXAsset(&entry->entry.asset);
    EXPECT(Errors().empty());

    // The handler-less type keeps its hash entry: the lookup still
    // resolves to the same pool slot after the dispatch.
    XAssetEntryPoolEntry *stillLinked =
        DB_FindXAssetEntry(ASSET_TYPE_RAWFILE, "loadentry/payload_b");
    EXPECT(stillLinked == entry);

    // Re-admission dedupes: the registry hashes and returns the same
    // pooled header instead of double-admitting.
    ClearErrors();
    XAssetHeader readmit =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&g_payloadB));
    EXPECT(readmit.data != nullptr);
    EXPECT(readmit.data == added.data);
    EXPECT(DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                               "loadentry/payload_b")
               .data
           == added.data);
    EXPECT(Errors().empty());
}

// C3: stub-asset (',name') default-entry route on a type with no
// default asset. Production DB_LinkXAssetEntry routes comma-prefixed
// names into DB_CreateDefaultEntry; DB_FindXAssetDefaultHeaderInternal
// finds no default rawfile and drops ("Could not load default
// asset"). Production unwinds that drop via longjmp - the call site
// has no return guard - so the case arms the harness jump target and
// asserts both the recorded error and the unwinding.
void CaseRegistryStubAsset()
{
    std::printf("case: registry stub asset default-entry drop\n");
    ClearErrors();

    static RawFilePayload stubPayload;
    stubPayload.name = ",loadentry/stub_default";
    stubPayload.len = 4;
    stubPayload.buffer = "STUB";

    volatile bool unwound = false;
    jmp_buf &target = db_load_entry_harness::ArmErrDrop();
    if (setjmp(target) == 1)
    {
        unwound = true;
    }
    else
    {
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&stubPayload));
    }
    db_load_entry_harness::DisarmErrDrop();

    EXPECT(unwound);
    EXPECT(HasErrorContaining("Could not load default asset"));

    // The unwound drop skipped the hash critical section's unlock
    // (production aborts the whole zone load here). Re-initialize
    // exactly like the production "next zone load" sequence: reset
    // the exported fast critical section and re-run the production
    // registry boot, then prove ordinary admission still succeeds.
    db_hashCritSect.readCount = 0;
    db_hashCritSect.writeCount = 0;
    ClearErrors();
    DB_Init();
    XAssetHeader next =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&g_payloadA));
    EXPECT(next.data != nullptr);
    EXPECT(DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                               "loadentry/payload_a")
               .data
           == next.data);
    EXPECT(Errors().empty());
}

// C4: stream init + valid pointer relocation (block 1 zero-fill
// materialization).
void CaseStreamPointerRelocation()
{
    std::printf("case: stream pointer relocation valid\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    DB_PushStreamPos(1);
    EXPECT(DB_GetStreamPos() == fixture.zoneMemory.blocks[1].data);
    // Load_Stream at stream start materializes the range zero-filled.
    Load_Stream(true, DB_GetStreamPos(), 64);
    for (unsigned int i = 0; i < 64; ++i)
    {
        if (fixture.data[1][i] != 0)
        {
            EXPECT(false);
            break;
        }
    }
    DB_PopStreamPos();

    // The token (block 1, offset 0) resolves to the block base and
    // the production writer narrows it into the caller's field.
    uint32_t field = OffsetToken(1, 0);
    DB_ConvertOffsetToPointer(&field, 64, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field,
               static_cast<uint32_t>(
                   reinterpret_cast<uintptr_t>(
                       fixture.zoneMemory.blocks[1].data)));

    // A second range further in resolves to base + offset.
    DB_PushStreamPos(1);
    Load_Stream(true, DB_GetStreamPos(), 128);
    DB_PopStreamPos();
    uint32_t field2 = OffsetToken(1, 64);
    DB_ConvertOffsetToPointer(&field2, 64, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field2,
               static_cast<uint32_t>(
                   reinterpret_cast<uintptr_t>(
                       fixture.zoneMemory.blocks[1].data + 64)));
    EXPECT(Errors().empty());
}

// C5: pointer relocation rejections - every malformed shape must
// record an error and leave the output field untouched.
void CaseStreamPointerRelocationMalformed()
{
    std::printf("case: stream pointer relocation malformed\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);
    DB_PushStreamPos(1);
    Load_Stream(true, DB_GetStreamPos(), 64);
    DB_PopStreamPos();

    // Null token: the writer rejects it and does not touch the field.
    uint32_t field = 0x12345678;
    ClearErrors();
    DB_ConvertOffsetToPointer(&field, 64, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field, 0x12345678u);
    EXPECT(HasErrorContaining("offset"));

    // Inline token (0xFFFFFFFF) is not an offset token.
    ClearErrors();
    field = 0xFFFFFFFFu;
    DB_ConvertOffsetToPointer(&field, 64, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field, 0xFFFFFFFFu);
    EXPECT(HasErrorContaining("offset"));

    // Wrong-block requirement: block 2's range is not materialized in
    // block 1, and the mask allows only block 1.
    ClearErrors();
    field = OffsetToken(2, 0);
    DB_ConvertOffsetToPointer(&field, 64, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field, OffsetToken(2, 0));
    EXPECT(HasErrorContaining("offset"));

    // Wrong block mask: the range is materialized in block 1 only.
    ClearErrors();
    field = OffsetToken(1, 0);
    DB_ConvertOffsetToPointer(&field, 64, 4, db::relocation::BlockBit(2));
    EXPECT_U32(field, OffsetToken(1, 0));
    EXPECT(HasErrorContaining("offset"));

    // Range beyond the materialized extent: offset 0 with 128
    // required bytes exceeds the 64 materialized.
    ClearErrors();
    field = OffsetToken(1, 0);
    DB_ConvertOffsetToPointer(&field, 128, 4, db::relocation::BlockBit(1));
    EXPECT_U32(field, OffsetToken(1, 0));
    EXPECT(HasErrorContaining("offset"));

    // Misaligned requirement: the range start is not 16-byte aligned.
    ClearErrors();
    field = OffsetToken(1, 4);
    DB_ConvertOffsetToPointer(&field, 16, 16, db::relocation::BlockBit(1));
    EXPECT_U32(field, OffsetToken(1, 4));
    EXPECT(HasErrorContaining("offset"));

    // The stream context itself is untouched by every rejection.
    EXPECT(DB_GetStreamPos()
           == fixture.zoneMemory.blocks[0].data);
}

// C6: alias registration + kind/metadata enforcement.
void CaseStreamAliasRelocation()
{
    std::printf("case: stream alias relocation\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    // DB_InsertPointer allocates the 4-byte slot in block 4 and
    // registers it at offset 0.
    DBAliasHandle handle = DB_InsertPointer(DBAliasKind::RawFile);
    EXPECT(static_cast<bool>(handle));

    static unsigned char aliasPayload[16];
    for (unsigned int i = 0; i < sizeof(aliasPayload); ++i)
        aliasPayload[i] = static_cast<unsigned char>(i * 3u);
    DB_SetInsertedPointer(handle, DBAliasKind::RawFile, aliasPayload, 0);

    // The production writer resolves the token to the registered
    // native payload and narrows it into the field.
    uint32_t field = AliasToken(0);
    DB_ConvertOffsetToAlias(&field, DBAliasKind::RawFile, 0);
    EXPECT_U32(field,
               static_cast<uint32_t>(
                   reinterpret_cast<uintptr_t>(aliasPayload)));

    // Kind mismatch: the field is untouched and the error recorded.
    ClearErrors();
    field = AliasToken(0);
    DB_ConvertOffsetToAlias(&field, DBAliasKind::XModel, 0);
    EXPECT_U32(field, AliasToken(0));
    EXPECT(HasErrorContaining("alias"));

    // Metadata mismatch: same enforcement.
    ClearErrors();
    field = AliasToken(0);
    DB_ConvertOffsetToAlias(&field, DBAliasKind::RawFile, 7);
    EXPECT_U32(field, AliasToken(0));
    EXPECT(HasErrorContaining("alias"));

    // Unregistered slot offset: rejected.
    ClearErrors();
    field = AliasToken(4);
    DB_ConvertOffsetToAlias(&field, DBAliasKind::RawFile, 0);
    EXPECT_U32(field, AliasToken(4));
    EXPECT(HasErrorContaining("alias"));

    // Metadata flows: a slot published with metadata 7 resolves only
    // under the matching expectation.
    DBAliasHandle handle7 = DB_InsertPointer(DBAliasKind::XModel);
    EXPECT(static_cast<bool>(handle7));
    static unsigned char aliasPayload7[32];
    DB_SetInsertedPointer(handle7, DBAliasKind::XModel, aliasPayload7, 7);
    field = AliasToken(4);
    DB_ConvertOffsetToAlias(&field, DBAliasKind::XModel, 7);
    EXPECT_U32(field,
               static_cast<uint32_t>(
                   reinterpret_cast<uintptr_t>(aliasPayload7)));
}

// C7: the production C-string pipeline. The production producers are
// Load_XStringCustom (the inline -1 token route: AllocLoad_raw_byte
// storage, provider-streamed bytes, DB_RegisterStreamCString
// registration) and DB_ConvertOffsetToCString / DB_ConvertOffsetToTempString
// (the block-token route over registered extents). The case drives
// all three plus the unregistered-extent rejection.
void CaseStreamCStringRelocation()
{
    std::printf("case: stream cstring / tempstring relocation\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    // (a) A block token over an extent that was never registered:
    // the production writer rejects it and leaves the field.
    DB_PushStreamPos(1);
    Load_Stream(true, DB_GetStreamPos(), 16);
    uint32_t unregistered = OffsetToken(1, 0);
    DB_ConvertOffsetToCString(&unregistered, db::relocation::BlockBit(1));
    EXPECT_U32(unregistered, OffsetToken(1, 0));
    EXPECT(HasErrorContaining("Invalid fast-file string offset"));
    ClearErrors();

    // (b) The production inline-string route. AllocLoad_raw_byte is
    // DB_AllocStreamPos(0): the string storage comes from the stream
    // position. The storage range is materialized by Load_Stream
    // (zero-filled and registered), then Load_XStringCustom streams
    // the NUL-terminated bytes from the controlled provider and
    // registers the extent.
    DB_PushStreamPos(1);
    char *storage = reinterpret_cast<char *>(DB_AllocStreamPos(0));
    EXPECT(storage != nullptr);
    EXPECT(storage == reinterpret_cast<char *>(DB_GetStreamPos()));
    Load_Stream(true, reinterpret_cast<uint8_t *>(storage), 64);

    const uint32_t cursor = ServedBytes();
    if (static_cast<size_t>(cursor) + 13 > State().fileBytes.size())
    {
        State().fileBytes.resize(cursor + 13, 0);
    }
    std::memcpy(State().fileBytes.data() + cursor, "inlinestring", 13);

    char *field = storage;
    Load_XStringCustom(&field);
    EXPECT(field == storage);
    EXPECT(std::strcmp(storage, "inlinestring") == 0);
    EXPECT(ServedBytes() == cursor + 13);

    // The production registration is queryable: the extent resolves
    // with its exact byte count (12 chars + terminator).
    uint32_t registeredBytes = 0;
    EXPECT(DB_ValidateStreamCString(storage, &registeredBytes)
           == db::relocation::Status::Ok);
    EXPECT_U32(registeredBytes, 13);
    EXPECT(Errors().empty());

    // (c) The block-token route over the SAME registered extent: the
    // production writer resolves the token to the storage address.
    const uint32_t storageOffset = static_cast<uint32_t>(
        reinterpret_cast<uint8_t *>(storage)
        - fixture.zoneMemory.blocks[1].data);
    uint32_t stringField = OffsetToken(1, storageOffset);
    DB_ConvertOffsetToCString(&stringField, db::relocation::BlockBit(1));
    EXPECT_U32(stringField,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(storage)));
    EXPECT(Errors().empty());

    // (d) The production temp-string route. The user-4 stream intern
    // is receipt-lifecycle-bound: the standalone registry-ownership
    // admission validates the zone-runtime table composition, and a
    // table without a facade-bound stream generation (the
    // validateSharedComposition Bound branch) refuses the intern.
    // Driven outside that lifecycle the production writer fails
    // closed: the recorded drop names the intern and the field keeps
    // the source token exactly.
    DB_PopStreamPos();
    DB_PopStreamPos();
    ClearErrors();
    uint32_t tempField = OffsetToken(1, storageOffset);
    DB_ConvertOffsetToTempString(&tempField, db::relocation::BlockBit(1));
    EXPECT_U32(tempField, OffsetToken(1, storageOffset));
    EXPECT(HasErrorContaining("Database user-4 stream intern failed"));

    // (e) Offset-token rejection stays exact after the positive
    // paths: an extent that was never materialized is not a
    // registered string.
    ClearErrors();
    uint32_t foreign = OffsetToken(1, 200);
    DB_ConvertOffsetToCString(&foreign, db::relocation::BlockBit(1));
    EXPECT_U32(foreign, OffsetToken(1, 200));
    EXPECT(HasErrorContaining("Invalid fast-file string offset"));
}

// C8: delayed stream admission (blocks 2/3) drains through the
// controlled provider.
void CaseStreamDelayedLoad()
{
    std::printf("case: stream delayed load\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    // Provider bytes for the delayed read: they land in the block
    // when Load_DelayStream drains the queue. The provider serves
    // sequential bytes, so the image is padded with everything the
    // earlier cases already consumed and the payload sits exactly at
    // the current cursor.
    const uint32_t servedBeforeImage = ServedBytes();
    ByteWriter image;
    image.Zeros(servedBeforeImage);
    for (uint32_t i = 0; i < 32; ++i)
        image.PushU8(static_cast<unsigned char>(0xA0u + i));
    SetFastFileBytes(image.bytes);

    DB_PushStreamPos(2);
    Load_Stream(true, DB_GetStreamPos(), 32);
    // The delay queue holds the read; the block bytes are untouched
    // until Load_DelayStream drains it.
    EXPECT(State().loadXFileDataCalls == 0);
    for (unsigned int i = 0; i < 32; ++i)
    {
        if (fixture.data[2][i] != 0)
        {
            EXPECT(false);
            break;
        }
    }
    DB_PopStreamPos();

    Load_DelayStream();
    EXPECT(State().loadXFileDataCalls == 1);
    EXPECT(ServedBytes() == servedBeforeImage + 32);
    EXPECT(std::memcmp(fixture.zoneMemory.blocks[2].data,
                       image.bytes.data() + servedBeforeImage, 32)
           == 0);
    EXPECT(Errors().empty());
}

// C9: provider-block reads bypass the delay queue and stream
// directly from the fixture image.
void CaseStreamProviderBlocks()
{
    std::printf("case: stream provider blocks\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    ByteWriter image;
    for (uint32_t i = 0; i < 48; ++i)
        image.PushU8(static_cast<unsigned char>(i + 1));
    SetFastFileBytes(image.bytes);

    // Block 5 is a provider block: Load_Stream pulls DB_LoadXFileData
    // immediately.
    DB_PushStreamPos(5);
    Load_Stream(true, DB_GetStreamPos(), 48);
    DB_PopStreamPos();
    EXPECT(State().loadXFileDataCalls == 1);
    EXPECT(State().fileCursor == 48);
    EXPECT(std::memcmp(fixture.zoneMemory.blocks[5].data,
                       image.bytes.data(), 48)
           == 0);
    EXPECT(Errors().empty());
}

// C10: stream-array overflow enforcement.
void CaseStreamArrayOverflow()
{
    std::printf("case: stream array overflow\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    DB_PushStreamPos(1);
    // count * stride overflows the 32-bit checked product: the
    // production CheckedArrayBytes guard rejects before any read.
    Load_StreamArray(true, DB_GetStreamPos(), 0x40000000u, 8u);
    EXPECT(HasErrorContaining("Invalid fast-file array size"));
    DB_PopStreamPos();
}

// C11: registry resilience - after every recorded rejection above the
// registry still admits and finds fresh assets (fail-closed, not
// corrupted).
void CaseRegistryResilienceAfterRejections()
{
    std::printf("case: registry resilience after rejections\n");
    ClearErrors();

    static RawFilePayload latePayload;
    latePayload.name = "loadentry/late_payload";
    latePayload.len = 8;
    latePayload.buffer = "LATEBYTE";
    XAssetHeader added =
        DB_AddXAsset(ASSET_TYPE_RAWFILE, PayloadHeader(&latePayload));
    EXPECT(added.data != nullptr);
    XAssetHeader found = DB_FindXAssetHeader(ASSET_TYPE_RAWFILE,
                                             "loadentry/late_payload");
    EXPECT(found.data != nullptr);
    EXPECT(HeaderBytesEqual(found,
                            reinterpret_cast<const unsigned char *>(
                                "LATEBYTE"),
                            8));
    EXPECT(Errors().empty());
}
} // namespace

int main()
{
    std::printf("db_load_entry_test: production registry + stream-load "
                "enrollment\n");

    ResetHarness();
    InitPayloads();
    DB_Init();

    CaseRegistryAdmissionRoundtrip();
    CaseRegistryRemoveReadmit();
    CaseRegistryStubAsset();
    CaseStreamPointerRelocation();
    CaseStreamPointerRelocationMalformed();
    CaseStreamAliasRelocation();
    CaseStreamCStringRelocation();
    CaseStreamDelayedLoad();
    CaseStreamProviderBlocks();
    CaseStreamArrayOverflow();
    CaseRegistryResilienceAfterRejections();

    std::printf("checks=%u failures=%u\n", g_checks, g_failures);
    if (g_failures != 0)
        return 1;
    std::printf("db_load_entry_test: ALL PASS\n");
    return 0;
}

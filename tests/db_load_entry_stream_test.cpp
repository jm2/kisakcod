// db_load_entry_stream_test: the fast-file stream-relocation cases of
// the db_load_entry production enrollment (ki-458h / #125).
//
// Split out of db_load_entry_test.cpp (behavior-preserving extraction,
// mirroring the fuzz-fastfile gate split): this TU owns the
// Load_Stream / Load_StreamArray / Load_DelayStream and
// DB_ConvertOffsetTo{Pointer,Alias,CString,TempString} cases against
// the real production stream machinery; db_load_entry_test.cpp owns
// the registry cases, the engine-service definitions and main().
// Both TUs report through the shared db_load_entry_checks.hpp
// framework so the binary-wide check/failure tally is unchanged, and
// every assertion, case order and printed case banner is identical.

#include "db_load_entry_harness.hpp"
#include "db_load_entry_checks.hpp"

#include <algorithm>
#include <cstring>

namespace
{
using namespace db_load_entry_harness;
using namespace db_load_entry_checks;

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

// One pointer-relocation rejection: the production writer records the
// error and leaves the caller's field byte-identical. The label and
// line reproduce the exact failure text the inline EXPECT_U32 spelled
// before the extraction.
void ExpectPointerRelocationRejected(uint32_t initialField, uint32_t size,
                                     uint32_t alignment, uint32_t block,
                                     const char *fieldLabel, int line)
{
    uint32_t field = initialField;
    db_load_entry_checks::ClearErrors();
    DB_ConvertOffsetToPointer(&field, size, alignment,
                              db::relocation::BlockBit(block));
    db_load_entry_checks::ExpectU32(field, initialField, fieldLabel, line);
    db_load_entry_checks::ExpectTrue(
        HasErrorContaining("offset"), "HasErrorContaining(\"offset\")",
        line);
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
    ExpectPointerRelocationRejected(0x12345678u, 64, 4, 1,
                                    "field == 0x12345678u", __LINE__);

    // Inline token (0xFFFFFFFF) is not an offset token.
    ExpectPointerRelocationRejected(0xFFFFFFFFu, 64, 4, 1,
                                    "field == 0xFFFFFFFFu", __LINE__);

    // Wrong-block requirement: block 2's range is not materialized in
    // block 1, and the mask allows only block 1.
    ExpectPointerRelocationRejected(OffsetToken(2, 0), 64, 4, 1,
                                    "field == OffsetToken(2, 0)", __LINE__);

    // Wrong block mask: the range is materialized in block 1 only.
    ExpectPointerRelocationRejected(OffsetToken(1, 0), 64, 4, 2,
                                    "field == OffsetToken(1, 0)", __LINE__);

    // Range beyond the materialized extent: offset 0 with 128
    // required bytes exceeds the 64 materialized.
    ExpectPointerRelocationRejected(OffsetToken(1, 0), 128, 4, 1,
                                    "field == OffsetToken(1, 0)", __LINE__);

    // Misaligned requirement: the range start is not 16-byte aligned.
    ExpectPointerRelocationRejected(OffsetToken(1, 4), 16, 16, 1,
                                    "field == OffsetToken(1, 4)", __LINE__);

    // The stream context itself is untouched by every rejection.
    EXPECT(DB_GetStreamPos()
           == fixture.zoneMemory.blocks[0].data);
}

// One alias-relocation rejection: same untouched-field contract as
// the pointer rejections, asserted with the original failure text.
void ExpectAliasRelocationRejected(uint32_t slotOffset, DBAliasKind kind,
                                   uint32_t metadata,
                                   const char *fieldLabel, int line)
{
    uint32_t field = AliasToken(slotOffset);
    db_load_entry_checks::ClearErrors();
    DB_ConvertOffsetToAlias(&field, kind, metadata);
    db_load_entry_checks::ExpectU32(field, AliasToken(slotOffset),
                                    fieldLabel, line);
    db_load_entry_checks::ExpectTrue(
        HasErrorContaining("alias"), "HasErrorContaining(\"alias\")",
        line);
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
    ExpectAliasRelocationRejected(0, DBAliasKind::XModel, 0,
                                  "field == AliasToken(0)", __LINE__);

    // Metadata mismatch: same enforcement.
    ExpectAliasRelocationRejected(0, DBAliasKind::RawFile, 7,
                                  "field == AliasToken(0)", __LINE__);

    // Unregistered slot offset: rejected.
    ExpectAliasRelocationRejected(4, DBAliasKind::RawFile, 0,
                                  "field == AliasToken(4)", __LINE__);

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

// (a) A block token over an extent that was never registered: the
// production writer rejects it and leaves the field.
void CaseCStringUnregisteredExtent()
{
    DB_PushStreamPos(1);
    Load_Stream(true, DB_GetStreamPos(), 16);
    uint32_t unregistered = OffsetToken(1, 0);
    DB_ConvertOffsetToCString(&unregistered, db::relocation::BlockBit(1));
    EXPECT_U32(unregistered, OffsetToken(1, 0));
    EXPECT(HasErrorContaining("Invalid fast-file string offset"));
    db_load_entry_checks::ClearErrors();
}

// (b) The production inline-string route: AllocLoad_raw_byte is
// DB_AllocStreamPos(0), the string storage comes from the stream
// position, the storage range is materialized by Load_Stream
// (zero-filled and registered), and Load_XStringCustom streams the
// NUL-terminated bytes from the controlled provider and registers the
// extent. (c) The block-token route over the SAME registered extent:
// the production writer resolves the token to the storage address.
// Returns the storage offset inside block 1 for the temp-string case.
uint32_t CaseCStringInlineAndBlockRoutes(const StreamBlockFixture &fixture)
{
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
    // Iterator-bounded fill of the 13 provider bytes ("inlinestring"
    // plus its terminator) at the exact cursor the next read serves.
    std::copy_n("inlinestring", 13, State().fileBytes.begin() + cursor);

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

    const uint32_t storageOffset = static_cast<uint32_t>(
        reinterpret_cast<uint8_t *>(storage)
        - fixture.zoneMemory.blocks[1].data);
    uint32_t stringField = OffsetToken(1, storageOffset);
    DB_ConvertOffsetToCString(&stringField, db::relocation::BlockBit(1));
    EXPECT_U32(stringField,
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(storage)));
    EXPECT(Errors().empty());
    return storageOffset;
}

// (d) The production temp-string route. The user-4 stream intern is
// receipt-lifecycle-bound: the standalone registry-ownership
// admission validates the zone-runtime table composition, and a
// table without a facade-bound stream generation (the
// validateSharedComposition Bound branch) refuses the intern. Driven
// outside that lifecycle the production writer fails closed: the
// recorded drop names the intern and the field keeps the source
// token exactly.
void CaseCStringTempStringRoute(uint32_t storageOffset)
{
    DB_PopStreamPos();
    DB_PopStreamPos();
    db_load_entry_checks::ClearErrors();
    uint32_t tempField = OffsetToken(1, storageOffset);
    DB_ConvertOffsetToTempString(&tempField, db::relocation::BlockBit(1));
    EXPECT_U32(tempField, OffsetToken(1, storageOffset));
    EXPECT(HasErrorContaining("Database user-4 stream intern failed"));
}

// (e) Offset-token rejection stays exact after the positive paths: an
// extent that was never materialized is not a registered string.
void CaseCStringForeignExtentRejection()
{
    db_load_entry_checks::ClearErrors();
    uint32_t foreign = OffsetToken(1, 200);
    DB_ConvertOffsetToCString(&foreign, db::relocation::BlockBit(1));
    EXPECT_U32(foreign, OffsetToken(1, 200));
    EXPECT(HasErrorContaining("Invalid fast-file string offset"));
}

// C7: the production C-string pipeline. The production producers are
// Load_XStringCustom (the inline -1 token route) and
// DB_ConvertOffsetToCString / DB_ConvertOffsetToTempString (the
// block-token route over registered extents). The case drives all
// three plus the unregistered-extent rejection, in the original
// phase order.
void CaseStreamCStringRelocation()
{
    std::printf("case: stream cstring / tempstring relocation\n");
    ClearErrors();
    ResetHarness();

    StreamBlockFixture fixture;
    fixture.Configure();
    DB_InitStreams(&fixture.zoneMemory);

    CaseCStringUnregisteredExtent();
    const uint32_t storageOffset =
        CaseCStringInlineAndBlockRoutes(fixture);
    CaseCStringTempStringRoute(storageOffset);
    CaseCStringForeignExtentRejection();
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
} // namespace

// The stream-relocation case runner, called by main() in
// db_load_entry_test.cpp between the registry cases in the original
// case order.
void RunStreamLoadCases()
{
    CaseStreamPointerRelocation();
    CaseStreamPointerRelocationMalformed();
    CaseStreamAliasRelocation();
    CaseStreamCStringRelocation();
    CaseStreamDelayedLoad();
    CaseStreamProviderBlocks();
    CaseStreamArrayOverflow();
}

// fx_restore_entry_test: production FX archive save / restore
// entry-point contracts (ki-458h / #125).
//
// The roundtrip core is fully production: FX_Save captures the
// effect-definition table from the REAL database registry
// (DB_EnumXAssets over admitted ASSET_TYPE_FX assets), serializes the
// harness FxSystem / FxSystemBuffers through the Disk32 writer into a
// production MemoryFile, and FX_Restore parses, validates, stages,
// snapshots and publishes through the production reader, candidate
// builder and restore control. Malformed and truncated images are
// byte-level mutations of that production output, and every failure
// case asserts the fail-closed contract: recorded drop with the
// production message, live system untouched, and no wedged archive
// ownership (a subsequent valid restore still publishes).
//
// Win32-x86 only: the enrolled production TUs compile in this test
// binary exactly as the game targets build them — the decompiled
// MSVC dialect and the DirectX/Miles/ODE header web do not compile
// on the portable 64-bit legs; tests/CMakeLists.txt gates this
// target to the Windows x86 CI leg, where every enrolled TU is
// already built by an engine target.

#include "fx_restore_entry_harness.hpp"

#include <EffectsCore/fx_effect_def.h>

#include <database/database.h>

#include <universal/msvc_printf_shim.h>

#include <algorithm>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

// The production printf/error family. Definitions live in this test
// TU (see the harness header for the CWE-134 rationale).

void Com_PrintError(int channel, const char *fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);

    fx_restore_entry_harness::RecordedError record;
    record.text = buffer;
    fx_restore_entry_harness::State().printErrors.push_back(record);
    std::printf("print-err[%d]: %s\n", channel, buffer);
}

// Com_sprintf comes from the enrolled production q_shared.cpp.

void Com_Error(errorParm_t code, const char *fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);

    fx_restore_entry_harness::RecordedError record;
    record.text = buffer;
    fx_restore_entry_harness::State().errors.push_back(record);
    // Visible triage: every recorded drop names itself on stdout.
    std::printf("err: %s\n", buffer);

    if (fx_restore_entry_harness::ErrDrop().armed)
    {
        jmp_buf target;
        const unsigned char *const sourceBytes =
            static_cast<const unsigned char *>(
                static_cast<const void *>(
                    fx_restore_entry_harness::ErrDrop().target));
        std::copy(sourceBytes,
                  sourceBytes + sizeof(target),
                  static_cast<unsigned char *>(static_cast<void *>(target)));
        fx_restore_entry_harness::DisarmErrDrop();
        longjmp(target, 1);
    }
    if (code == ERR_FATAL || code == ERR_SERVERDISCONNECT)
    {
        std::fflush(stdout);
        std::_Exit(3);
    }
}

// Production assert endpoints: MyAssertHandler and the physicalmemory
// substrate's out-of-memory endpoint are hard failures (the fixtures
// cannot legitimately trigger either). Sys_Error lives in the shared
// db_load_entry_stubs translation unit.

void MyAssertHandler(const char *filename, int line, int type,
                     const char *fmt, ...)
{
    (void)type;
    // The variable message is formatted once into a fixed buffer and
    // printed as a single string: the format string never reaches a
    // variadic printf pass-through unanalyzed.
    char message[4096];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(message, sizeof(message), fmt, args);
    message[sizeof(message) - 1] = '\0';
    va_end(args);
    std::printf("PRODUCTION ASSERT - terminating: %s:%d: %s\n", filename,
                line, message);
    std::fflush(stdout);
    std::_Exit(3);
}

void Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    (void)filename;
    (void)line;
    std::printf("OUT OF MEMORY - terminating\n");
    std::fflush(stdout);
    std::_Exit(3);
}


// The production useFastFile dvar instance IsFastFileLoad() reads
// (q_shared.h). Fast-file mode is the archive save/restore mode.
dvar_t g_useFastFileDvar;
const dvar_t *useFastFile = &g_useFastFileDvar;

// Com_Printf: production printf - recorded and printed verbatim.
void Com_Printf(int channel, const char *fmt, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);
    std::printf("print[%d]: %s\n", channel, buffer);
}

// ConvertToMB: production physicalmemory size formatting helper.
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

// Fail-closed fast-file stream provider: the FX archive pipeline is a
// zone-memory pipeline and must never read the fast-file image; if a
// production path reaches the stream, the recorded drop surfaces in
// the test verdict instead of silently serving bytes.
void __cdecl DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    (void)pos;
    Com_Error(ERR_DROP,
              "Harness fixture: unexpected fast-file stream read of %u "
              "bytes on the FX archive path",
              size);
}

namespace
{
std::uint32_t g_checks = 0;
std::uint32_t g_failures = 0;

#define EXPECT(condition)                                                    \
    do                                                                       \
    {                                                                        \
        ++g_checks;                                                          \
        if (!(condition))                                                    \
        {                                                                    \
            ++g_failures;                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);         \
        }                                                                    \
    } while (false)

#define EXPECT_U32(actual, expected)                                         \
    do                                                                       \
    {                                                                        \
        ++g_checks;                                                          \
        if ((actual) != (expected))                                          \
        {                                                                    \
            ++g_failures;                                                    \
            std::printf("FAIL line %d: %s == %s (%u vs %u)\n",               \
                        __LINE__,                                            \
                        #actual,                                             \
                        #expected,                                           \
                        static_cast<unsigned>(actual),                       \
                        static_cast<unsigned>(expected));                    \
        }                                                                    \
    } while (false)

void ClearErrors()
{
    fx_restore_entry_harness::State().errors.clear();
    fx_restore_entry_harness::State().printErrors.clear();
}

// Byte-exact pristine-system snapshot: std::copy over unsigned-char
// views (the bounded char-view form the CWE-120 gate requires; the
// copied bytes and size are identical to the memcpy it replaced).
void SnapshotPristineSystem(FxSystem &destination)
{
    const FxSystem &source = fx_restore_entry_harness::State().system;
    const auto *const sourceBytes =
        static_cast<const unsigned char *>(
            static_cast<const void *>(&source));
    std::copy(sourceBytes,
              sourceBytes + sizeof(FxSystem),
              static_cast<unsigned char *>(static_cast<void *>(&destination)));
}

// The minimal valid effect-definition payload admitted as a real
// ASSET_TYPE_FX asset: zero effects is a valid table for the archive
// capture, and the name participates in the production registration
// lookup during restore. The fixture is a full FxEffectDef because
// the production DB admission copies DB_GetXAssetTypeSize(
// ASSET_TYPE_FX) bytes from the header's fx pointer; a smaller
// fixture would make that copy read past the object.
FxEffectDef g_fxDef{"restore_entry/roundtrip_fx",
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    nullptr};
bool g_fxDefAdmitted = false;

bool AdmitFxAsset()
{
    if (g_fxDefAdmitted)
        return true;
    XAssetHeader header{};
    header.fx = &g_fxDef;
    const XAssetHeader added =
        DB_AddXAsset(ASSET_TYPE_FX, header);
    g_fxDefAdmitted = added.fx != nullptr;
    return g_fxDefAdmitted;
}

// Saves the pristine harness system through the production writer and
// returns the produced image bytes (excluding the trailing unused
// buffer region).
bool SaveArchive(fx_restore_entry_harness::ArchiveImage &image)
{
    image.InitForSave();
    FX_Save(0, &image.memFile);
    if (!fx_restore_entry_harness::Errors().empty())
        return false;
    // The production save-game writer owns the memfile segment opened by
    // MemFile_InitForWriting: FX_Save archives into it and the enclosing
    // writer closes it. MemFile_EndSegment flushes the deflate stream,
    // patches the segment-length header at segmentStart, and releases the
    // global stream owner so the next save/restore can start cleanly.
    MemFile_EndSegment(&image.memFile);
    if (image.memFile.memoryOverflow)
        return false;
    const int used = image.memFile.bytesUsed;
    return used > 0;
}

// Builds the truncated variant of a saved archive image: the payload
// is halved (aligned down to the 4-byte segment header) and the
// 4-byte memfile segment length header is rewritten to match, so the
// memfile boundary accepts the image (the encoded segment length lies
// within the buffer, exactly as for a legitimately produced image)
// and the production FX reader owns failing closed on the short
// stream. Cutting the buffer alone without the header rewrite cannot
// reach the reader: the segment locator rejects a length that
// overruns the buffer at the memfile assert boundary, before any FX
// code runs. Returns an empty vector when even the segment header
// would not fit (the caller records the failure).
std::vector<unsigned char> BuildTruncatedArchiveBytes(
    const fx_restore_entry_harness::ArchiveImage &image)
{
    const int truncatedSize = (image.memFile.bytesUsed / 2) & ~3;
    if (truncatedSize < 4)
        return {};
    std::vector<unsigned char> truncatedBytes(
        image.bytes.begin(),
        image.bytes.begin() + truncatedSize);
    const std::uint32_t segmentLength =
        static_cast<std::uint32_t>(truncatedSize);
    truncatedBytes[0] = static_cast<unsigned char>(segmentLength);
    truncatedBytes[1] = static_cast<unsigned char>(segmentLength >> 8);
    truncatedBytes[2] = static_cast<unsigned char>(segmentLength >> 16);
    truncatedBytes[3] = static_cast<unsigned char>(segmentLength >> 24);
    return truncatedBytes;
}

// Drives one FX_Restore through readImage expected to fail through
// the production Com_Error longjmp unwind (the reader/candidate
// failure reporter releases the lease and staging, then longjmps),
// then asserts the fail-closed contract: the drop was recorded with
// a production message, the live system is byte-identical to the
// pristine snapshot (no partial publication, no wedged gate), and
// the archive gate is open again.
void ExpectRestoreDropLeavesPristineSystem(
    fx_restore_entry_harness::ArchiveImage &readImage,
    const FxSystem &pristineSystem)
{
    volatile bool unwound = false;
    jmp_buf &target = fx_restore_entry_harness::ArmErrDrop();
    if (setjmp(target) == 1)
    {
        unwound = true;
    }
    else
    {
        FX_Restore(0, &readImage.memFile);
    }
    fx_restore_entry_harness::DisarmErrDrop();
    MemFile_MoveToSegment(&readImage.memFile, -1);
    EXPECT(unwound);
    EXPECT(fx_restore_entry_harness::Errors().size() >= 1);
    EXPECT(
        fx_restore_entry_harness::HasErrorContaining("Invalid FX")
        || fx_restore_entry_harness::HasErrorContaining("FX archive"));
    EXPECT(std::memcmp(&fx_restore_entry_harness::State().system,
                       &pristineSystem,
                       sizeof(FxSystem))
           == 0);
    EXPECT(fx_restore_entry_harness::State().archiveGate
           == static_cast<std::int32_t>(
               fx::archive::ArchiveGateValue::Open));
}
} // namespace

// C1: the restore request guards.
void CaseRestoreRequestGuards()
{
    std::printf("case: restore request guards\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();

    fx_restore_entry_harness::ArchiveImage image;
    image.InitForSave();
    MemFile_ArchiveData(&image.memFile, 4, const_cast<char *>("abc\0"));
    MemFile_EndSegment(&image.memFile);

    // Null archive image: the production writer rejects before any
    // engine access.
    FxSystem pristineSystem;
    SnapshotPristineSystem(pristineSystem);
    FX_Restore(0, nullptr);
    EXPECT(fx_restore_entry_harness::HasErrorContaining(
        "Invalid FX archive restore request"));
    EXPECT(std::memcmp(&fx_restore_entry_harness::State().system,
                       &pristineSystem,
                       sizeof(FxSystem))
           == 0);
    ClearErrors();

    // Non-zero client index: the single-client runtime refuses.
    FX_Restore(1, &image.memFile);
    EXPECT(fx_restore_entry_harness::HasErrorContaining(
        "Invalid FX archive restore request"));
}

// C2: the production save path produces a non-empty archive image of
// the pristine system with no recorded errors.
void CaseSavePristineArchive()
{
    std::printf("case: save pristine archive\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));
    EXPECT(image.memFile.bytesUsed > 0);
    EXPECT(fx_restore_entry_harness::State()
               .beginArchiveSuccesses
           == 1);
    EXPECT(fx_restore_entry_harness::State().endArchiveCalls >= 1);
    EXPECT(fx_restore_entry_harness::Errors().empty());
}

// C3: the production restore roundtrip - save, restore, and the
// published state matches the saved pristine image with the archive
// cleanly closed.
void CaseRestoreRoundtrip()
{
    std::printf("case: restore roundtrip\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));

    const int bytesUsed = image.memFile.bytesUsed;
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage readImage;
    readImage.InitForRead(image.bytes.data(), bytesUsed);
    MemFile_MoveToSegment(&readImage.memFile, 0);
    FX_Restore(0, &readImage.memFile);
    MemFile_MoveToSegment(&readImage.memFile, -1);
    EXPECT(fx_restore_entry_harness::Errors().empty());
    // The production restore closes the archive transaction: the gate
    // is released and the archiving flag cleared.
    EXPECT(fx_restore_entry_harness::State().archiveGate
           == static_cast<std::int32_t>(
               fx::archive::ArchiveGateValue::Open));
    EXPECT(fx_restore_entry_harness::State().system.isArchiving == 0);
    // The published system keeps the production invariants.
    EXPECT(fx_restore_entry_harness::State().system.iteratorCount
           == -1);
    EXPECT(fx_restore_entry_harness::State().system.effects
           == fx_restore_entry_harness::State().buffers.effects);
}

// C4: a corrupted table region fails closed with the production
// message, leaving the live system untouched.
void CaseRestoreCorruptTable()
{
    std::printf("case: restore corrupt table\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));
    const int bytesUsed = image.memFile.bytesUsed;

    // Corrupt the leading FX table bytes of the segment payload. The
    // image opens with the 4-byte memfile segment length header, which
    // must stay intact for the memfile boundary to even locate the
    // segment (a broken header there is a memfile-level assert, not an
    // FX-level drop); the production FX reader owns validation of the
    // table bytes that follow it.
    image.bytes[4] ^= 0xFF;
    image.bytes[5] ^= 0xFF;
    image.bytes[6] ^= 0xFF;
    image.bytes[7] ^= 0xFF;

    FxSystem pristineSystem;
    SnapshotPristineSystem(pristineSystem);

    fx_restore_entry_harness::ArchiveImage readImage;
    readImage.InitForRead(image.bytes.data(), bytesUsed);
    MemFile_MoveToSegment(&readImage.memFile, 0);

    // The table failure path is a production ERR_DROP unwind
    // (FX_ReportEffectTableRestoreFailure releases ownership and then
    // relies on the Com_Error longjmp; the abort behind it is the
    // unreachable backstop): arm the harness jump target around the
    // call, exactly like the production zone-load unwinding.
    ExpectRestoreDropLeavesPristineSystem(readImage, pristineSystem);
}

// C5: a truncated archive fails closed at the reader with the live
// system untouched.
void CaseRestoreTruncatedArchive()
{
    std::printf("case: restore truncated archive\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));
    const int fullSize = image.memFile.bytesUsed;
    EXPECT(fullSize > 64);

    FxSystem pristineSystem;
    SnapshotPristineSystem(pristineSystem);

    const std::vector<unsigned char> truncatedBytes =
        BuildTruncatedArchiveBytes(image);
    EXPECT(!truncatedBytes.empty());
    if (truncatedBytes.empty())
        return;

    fx_restore_entry_harness::ArchiveImage readImage;
    readImage.InitForRead(truncatedBytes.data(),
                          static_cast<int>(truncatedBytes.size()));
    MemFile_MoveToSegment(&readImage.memFile, 0);

    // Same production-unwound failure family as the corrupt-table
    // case: the reader/candidate failure reporter releases the lease
    // and staging, then relies on the Com_Error longjmp.
    ExpectRestoreDropLeavesPristineSystem(readImage, pristineSystem);
}

// C6: after a failed restore the production entry point is reusable -
// a subsequent valid restore publishes without wedged ownership.
void CaseRestoreRetryAfterFailure()
{
    std::printf("case: restore retry after failure\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));
    const int bytesUsed = image.memFile.bytesUsed;

    // Drive one failing restore first (null image).
    FX_Restore(0, nullptr);
    EXPECT(fx_restore_entry_harness::HasErrorContaining(
        "Invalid FX archive restore request"));
    ClearErrors();

    // The retry through the valid production image publishes cleanly.
    fx_restore_entry_harness::ArchiveImage readImage;
    readImage.InitForRead(image.bytes.data(), bytesUsed);
    MemFile_MoveToSegment(&readImage.memFile, 0);
    FX_Restore(0, &readImage.memFile);
    MemFile_MoveToSegment(&readImage.memFile, -1);
    EXPECT(fx_restore_entry_harness::Errors().empty());
    EXPECT(fx_restore_entry_harness::State().archiveGate
           == static_cast<std::int32_t>(
               fx::archive::ArchiveGateValue::Open));
    EXPECT(fx_restore_entry_harness::State().system.isArchiving == 0);
}

// C7: the exclusive-ownership refusal path - a refused FX_BeginArchive
// during restore fails closed with the production message and an open
// gate.
void CaseRestoreOwnershipRefusal()
{
    std::printf("case: restore ownership refusal\n");
    ClearErrors();
    fx_restore_entry_harness::ResetHarness();
    EXPECT(AdmitFxAsset());

    fx_restore_entry_harness::ArchiveImage image;
    EXPECT(SaveArchive(image));
    const int bytesUsed = image.memFile.bytesUsed;

    FxSystem pristineSystem;
    SnapshotPristineSystem(pristineSystem);

    fx_restore_entry_harness::State().failNextBeginArchive = true;
    fx_restore_entry_harness::ArchiveImage readImage;
    readImage.InitForRead(image.bytes.data(), bytesUsed);
    MemFile_MoveToSegment(&readImage.memFile, 0);
    FX_Restore(0, &readImage.memFile);
    MemFile_MoveToSegment(&readImage.memFile, -1);
    EXPECT(fx_restore_entry_harness::HasErrorContaining(
        "could not acquire exclusive ownership"));
    EXPECT(std::memcmp(&fx_restore_entry_harness::State().system,
                       &pristineSystem,
                       sizeof(FxSystem))
           == 0);
    EXPECT(fx_restore_entry_harness::State().archiveGate
           == static_cast<std::int32_t>(
               fx::archive::ArchiveGateValue::Open));
}

int main()
{
    g_useFastFileDvar.name = "useFastFile";
    g_useFastFileDvar.current.enabled = true;

    DB_Init();
    std::printf("fx_restore_entry_test: production archive save/restore enrollment\n");

    CaseRestoreRequestGuards();
    CaseSavePristineArchive();
    CaseRestoreRoundtrip();
    CaseRestoreCorruptTable();
    CaseRestoreTruncatedArchive();
    CaseRestoreRetryAfterFailure();
    CaseRestoreOwnershipRefusal();

    std::printf("checks=%u failures=%u\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("fx_restore_entry_test: ALL PASS\n");
    return g_failures == 0 ? 0 : 1;
}

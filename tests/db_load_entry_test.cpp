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
#include "db_load_entry_checks.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include <universal/msvc_printf_shim.h>

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
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);

    db_load_entry_harness::RecordedError record;
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
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    buffer[sizeof(buffer) - 1] = '\0';
    va_end(args);

    db_load_entry_harness::RecordedError record;
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
    // Iterator-bounded copy over the checked [cursor, cursor + size)
    // range the guard above validated: the range check stays
    // structural (and MSVC debug iterators harden it) instead of a
    // raw pointer-arithmetic memcpy.
    std::copy(state.fileBytes.begin() + state.fileCursor,
              state.fileBytes.begin() + state.fileCursor + size,
              pos);
    state.fileCursor += size;
    state.servedBytes += size;
}

// ---------------------------------------------------------------------------
// The cases.
// ---------------------------------------------------------------------------
// The shared EXPECT / check-tally framework lives in
// db_load_entry_checks.hpp; the stream-relocation cases live in
// db_load_entry_stream_test.cpp and report through the same
// binary-wide counters.
void RunStreamLoadCases();

namespace
{
using namespace db_load_entry_harness;
using namespace db_load_entry_checks;

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

// C4-C10, the stream init / pointer, alias and C-string relocation,
// delayed-load, provider-block and array-overflow cases, live in
// db_load_entry_stream_test.cpp (RunStreamLoadCases, called from
// main() below in the original case order).

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
    RunStreamLoadCases();
    CaseRegistryResilienceAfterRejections();

    std::printf("checks=%u failures=%u\n",
                db_load_entry_checks::g_checks,
                db_load_entry_checks::g_failures);
    if (db_load_entry_checks::g_failures != 0)
        return 1;
    std::printf("db_load_entry_test: ALL PASS\n");
    return 0;
}

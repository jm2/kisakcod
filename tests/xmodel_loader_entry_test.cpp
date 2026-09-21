// xmodel_loader_entry_test: production-loader entry-point contracts for
// the scoped nested cursor ownership and the checked second-pass seek
// (ki-okmr / #124).
//
// Unlike the portable suites (xmodel_nested_cursor_test.cpp), which
// walk the buf_cursor entry points in the loaders' exact call sequence,
// this binary links and executes the REAL production loader entry
// point — XModelLoadFile from src/xanim/xmodel_load_obj.cpp, with its
// real config/collision/LOD parsing, the real production
// XModelPartsLoadFile nested parts parse (xanim_load_obj.cpp is
// enrolled for its real ConsumeQuatNoSwap), real nested surfs cursor
// windows, and its real checked material second pass (SeekTo against a
// cursor-owned Checkpoint). Controlled fixtures are served through the
// harness file system; see xmodel_loader_entry_harness.hpp for the
// service stubs.
//
// Win32-x86 only: the loader TU's DirectX/Miles/ODE header web and
// MSVC decompiled dialect do not compile on the portable 64-bit legs;
// tests/CMakeLists.txt gates this target to the Windows x86 CI leg.

#include <xanim/xmodel.h>
// Full XSurface definition for the surface-content assertions below:
// xmodel.h only forward-declares `struct XSurface *surfs`, and the
// production definer is the same xanim.h the loader TU includes.
#include <xanim/xanim.h>
#include <xanim/buf_cursor.hpp>

#include <universal/msvc_printf_shim.h>

#include "xmodel_loader_entry_harness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// The production entry under test. No engine header declares
// XModelLoadFile — like the retail layout it is defined at global
// scope in src/xanim/xmodel_load_obj.cpp — so this TU carries the
// exact production signature and links the real definition from the
// enrolled loader TU. The tests assert against the returned XModel
// records, never against a stand-in.
XModel *__cdecl XModelLoadFile(char *name,
                               void *(__cdecl *Alloc)(int),
                               void *(__cdecl *AllocColl)(int));

// ---------------------------------------------------------------------------
// Harness definitions. The printf-family wrappers (Com_PrintError,
// Com_sprintf, Com_Error) and the production assert handler
// (MyAssertHandler) are the only functions DEFINED here: their bodies
// necessarily call a printf function with a caller-supplied format
// string, and every production printf wrapper in this repository
// lives in a .cpp — common.cpp, r_warn.cpp — so these do too (a
// header body re-triggers the CWE-134 lexical pattern). The remaining
// engine-service endpoints are defined inline in the harness header
// at global scope, matching the production signatures the loader TU
// resolves against. The harness state itself is namespace-scope
// storage in this TU.
// ---------------------------------------------------------------------------
namespace xmodel_loader_entry_harness
{
namespace
{
HarnessState g_harnessState;
}  // namespace

HarnessState &State()
{
    return g_harnessState;
}

void ResetHarness()
{
    HarnessState &s = State();
    s.files.clear();
    s.hunkData.clear();
    s.errors.clear();
    s.materialRegistrations.clear();
    s.fsReads = 0;
    s.fsFrees = 0;
    s.partsFileReads = 0;
    s.physPresetCalls = 0;
    s.collMapCalls = 0;
    // Restart the va() rotation at its first buffer so every test
    // starts from the same phase as a fresh production process.
    s.vaIndex = 0;
}
}  // namespace xmodel_loader_entry_harness

void Com_PrintError(int channel, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    xmodel_loader_entry_harness::RecordedError error;
    error.channel = channel;
    error.text = buffer;
    xmodel_loader_entry_harness::State().errors.push_back(error);
}

int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // _vsnprintf carries the MSVC truncation contract on every host:
    // the native CRT spelling on the win32-x86 leg, KISAK_vsnprintf_trunc
    // through universal/msvc_printf_shim.h on POSIX hosts.
    const int written = _vsnprintf(dest, size, fmt, args);
    va_end(args);
    return written;
}

void Com_Error(errorParm_t code, const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    xmodel_loader_entry_harness::RecordedError error;
    error.channel = static_cast<int>(code);
    error.text = buffer;
    xmodel_loader_entry_harness::State().errors.push_back(error);
    // ERR_FATAL terminates the process in production; reaching it
    // during an entry-point contract is a defect, so fail the test
    // process loudly with the recorded message. _Exit rather than
    // abort: the MSVC Debug CRT renders abort() as a modal dialog a
    // headless runner never answers, stalling the suite to its ctest
    // timeout.
    std::fprintf(stderr, "xmodel_loader_entry: Com_Error(%d): %s\n",
                 static_cast<int>(code), buffer);
    std::fflush(stderr);
    std::_Exit(3);
}

// Production assert handler (declared in universal/assertive.h),
// defined here with the printf-family wrappers above: same message
// shape and same _Exit(3) failure contract the header body carried,
// with the format primitive spelled like the wrappers (MSVC
// truncation contract via _CRT_SECURE_NO_WARNINGS) plus the explicit
// terminator the va() contract keeps.
void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...)
{
    (void)type;
    char message[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    message[sizeof(message) - 1] = '\0';
    // Any production assert firing during an entry-point contract is a
    // defect: fail the test process loudly instead of continuing. The
    // message carries the assert site so a failing run points straight
    // at the violated invariant.
    std::fprintf(stderr, "xmodel_loader_entry: production assert fired at %s:%d: %s\n",
                 filename, line, message);
    std::fflush(stderr);
    // _Exit instead of abort: the MSVC Debug CRT turns abort() into a
    // modal report dialog that a headless CI runner never answers — the
    // process sat through the full ctest timeout (1500 s) with the
    // failure invisible. _Exit terminates deterministically on every
    // configuration, keeping the nonzero exit code and flushed output.
    std::_Exit(3);
}

namespace xmodel_loader_entry_test
{
using namespace xmodel_loader_entry_harness;

namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "xmodel_loader_entry_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

// After any load attempt, the cursor must be fully deactivated and the
// file-system reads must balance with frees — no early-exit path may
// leak the model buffer or leave a cursor scope installed.
bool ExpectCleanTeardown(int expectedReads)
{
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
    CHECK(State().fsReads == expectedReads);
    CHECK(State().fsFrees == State().fsReads);
    return true;
}

// Cold load of a valid controlled model: the production parser, the
// nested parts/surfs windows and the material second pass all run
// through the real entry point.
bool TestValidColdLoad()
{
    ResetHarness();
    RegisterValidModel("coldmodel");

    XModel *model = XModelLoadFile(const_cast<char *>("coldmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(model != nullptr);
    CHECK(model->numLods == 2);
    CHECK(model->numsurfs == 3);
    CHECK(model->numBones == 2);
    CHECK(model->numRootBones == 1);
    CHECK(model->lodInfo[0].numsurfs == 2);
    CHECK(model->lodInfo[1].numsurfs == 1);
    CHECK(model->lodInfo[0].dist == 1000000.0f);
    CHECK(model->lodInfo[1].dist == 150.0f);
    CHECK(model->lodInfo[0].partBits[0] == static_cast<int>(0x80000000u));
    CHECK(model->surfs[0].vertCount == 3);
    // The file declares triCount 1; the retail surface reader pads an
    // odd triangle count to an even slot count with a duplicated
    // sentinel index and reports the padded count, so the loaded
    // surface carries 2.
    CHECK(model->surfs[0].triCount == 2);
    CHECK(model->radius == 1.0f);
    CHECK(model->collLod == 0);
    CHECK(State().physPresetCalls == 1);
    CHECK(State().collMapCalls == 1);
    // The real XModelPartsLoadFile ran exactly once, observed at the
    // file-system boundary (one xmodelparts read).
    CHECK(State().partsFileReads == 1);

    // The material second pass re-read all three surface names at the
    // rewound LOD-table position, in first-pass order.
    CHECK(State().materialRegistrations.size() == 3);
    if (State().materialRegistrations.size() == 3)
    {
        CHECK(State().materialRegistrations[0] == "mc/mat_first_a");
        CHECK(State().materialRegistrations[1] == "mc/mat_first_b");
        CHECK(State().materialRegistrations[2] == "mc/mat_first_c");
    }
    CHECK(State().errors.empty());
    return ExpectCleanTeardown(4);
}

// Warm reload: the second XModelLoadFile for the same name re-reads
// only the model file — the parts and surfs come from the hunk cache
// without a nested cursor activation — and the second pass still
// registers all materials in order.
bool TestValidWarmReload()
{
    ResetHarness();
    RegisterValidModel("warmmodel");

    XModel *first = XModelLoadFile(const_cast<char *>("warmmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(first != nullptr);
    const int coldReads = State().fsReads;
    CHECK(coldReads == 4);
    CHECK(State().partsFileReads == 1);

    XModel *second = XModelLoadFile(const_cast<char *>("warmmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(second != nullptr);
    CHECK(State().fsReads == coldReads + 1);
    CHECK(State().partsFileReads == 1);  // warm hunk cache: no second parts read
    CHECK(State().materialRegistrations.size() == 6);
    CHECK(State().materialRegistrations[3] == "mc/mat_first_a");
    CHECK(State().materialRegistrations[4] == "mc/mat_first_b");
    CHECK(State().materialRegistrations[5] == "mc/mat_first_c");
    CHECK(State().errors.empty());
    return ExpectCleanTeardown(coldReads + 1);
}

// Missing, empty and legacy-prefixed names take the loader's early
// rejection paths without ever installing a cursor scope.
bool TestEarlyRejections()
{
    ResetHarness();
    XModel *missing = XModelLoadFile(const_cast<char *>("nosuchmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(missing == nullptr);
    CHECK(ErrorCount() == 1);
    CHECK(ErrorsContain(19, "not found"));
    return ExpectCleanTeardown(0);
}

bool TestEmptyAndLegacyNameRejections()
{
    ResetHarness();
    State().files["xmodel/emptyparts"] = std::vector<unsigned char>();
    XModel *empty = XModelLoadFile(const_cast<char *>("emptyparts"), HarnessAlloc, HarnessAllocColl);
    CHECK(empty == nullptr);
    CHECK(ErrorCount() == 1);
    CHECK(ErrorsContain(19, "0 length"));
    CHECK(ExpectCleanTeardown(1));

    ResetHarness();
    XModel *legacy = XModelLoadFile(const_cast<char *>("xmodel/legacy"), HarnessAlloc, HarnessAllocColl);
    CHECK(legacy == nullptr);
    CHECK(ErrorCount() == 1);
    CHECK(ErrorsContain(19, "Remove xmodel prefix"));
    return ExpectCleanTeardown(0);
}

// A truncated model file (cut inside the first-pass LOD surface names)
// is rejected through the malformed-surface-name early exit.
bool TestTruncatedModelFile()
{
    ResetHarness();
    RegisterValidModel("cutmodel");
    ByteWriterFixture full = BuildEntryModelFile();
    std::vector<unsigned char> truncated(full.bytes.begin(), full.bytes.begin() + 80);
    State().files["xmodel/cutmodel"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("cutmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(ErrorCount() == 1);
    CHECK(ErrorsContain(19, "malformed surface name"));
    return ExpectCleanTeardown(1);
}

// Truncating AFTER the first pass but before the bone infos exercises
// the checked second pass against a latched cursor: the parts parse
// succeeds (one xmodelparts read), the bone-info reads overrun, Failed()
// latches, and SeekTo(v36) must fail closed so the loader rejects
// through the ordinary cleanup instead of re-parsing at the wrong
// position.
bool TestSecondPassFailsClosedWhenLatched()
{
    ResetHarness();
    RegisterValidModel("latchmodel");
    ByteWriterFixture full = BuildEntryModelFile();
    std::vector<unsigned char> truncated(full.bytes.begin(), full.bytes.begin() + 112);
    State().files["xmodel/latchmodel"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("latchmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(State().partsFileReads == 1);
    // The latched second pass rejects silently through the ordinary
    // cleanup path — no error print is issued on it.
    CHECK(State().errors.empty());
    return ExpectCleanTeardown(2);
}

// A malformed nested parts file must fail closed inside its own window
// and reject the model, with the parent cursor restored and the whole
// scope tree deactivated afterwards. The truncated parts file is
// served at "xmodelparts/lod_a" — the name the production loader
// resolves through config.entries[0].filename. The 6-byte cut keeps
// exactly the version/counts block, so the production parser runs its
// child-bone walk against an exhausted cursor: the parent weight
// degrades to 0 (which passes the index < i assert), the name read
// fails, and the real loader prints "malformed bone name" followed by
// the precache's "Cannot find xmodelparts 'lod_a'" — two recorded
// errors total.
bool TestTruncatedNestedParts()
{
    ResetHarness();
    RegisterValidModel("badparts");
    ByteWriterFixture full = BuildEntryPartsFile();
    std::vector<unsigned char> truncated(full.bytes.begin(),
                                         full.bytes.begin() + 6);
    State().files["xmodelparts/lod_a"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("badparts"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(State().partsFileReads == 1);
    CHECK(ErrorCount() == 2);
    CHECK(ErrorsContain(19, "malformed bone name"));
    CHECK(ErrorsContain(19, "Cannot find xmodelparts"));
    return ExpectCleanTeardown(2);
}

// A truncated nested surfs file is rejected by the production
// R_XModelSurfsLoadFile fail-closed path and rejects the whole model.
// The 2-byte cut keeps exactly the version field and starves the
// numsurfs read: the EOF read latches the cursor and reads 0, which
// mismatches modelNumsurfs, so the file is rejected through retail's
// surface-count mismatch path before any surface body parses. The cut
// must land before the surface bodies: a parse starved inside a
// surface reads zero-filled vertices whose Debug-only
// Vec3PackUnitVec degenerate-input assert fires ahead of the
// file-level rejection (and triCount==0 shapes trip their own live
// asserts), so earlier in-surface cuts are not Debug-clean.
bool TestTruncatedNestedSurfs()
{
    ResetHarness();
    RegisterValidModel("badsurfs");
    ByteWriterFixture full = BuildEntrySurfsFile(2);
    std::vector<unsigned char> truncated(full.bytes.begin(),
                                         full.bytes.begin() + 2);
    State().files["xmodelsurfs/lod_a"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("badsurfs"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(State().partsFileReads == 1);
    CHECK(ErrorCount() == 2);
    CHECK(ErrorsContain(19, "File conflict"));
    CHECK(ErrorsContain(19, "Cannot find 'xmodelsurfs"));
    return ExpectCleanTeardown(3);
}

// A stale config version is rejected before any cursor work matters.
bool TestBadConfigVersion()
{
    ResetHarness();
    RegisterValidModel("oldmodel");
    ByteWriterFixture full = BuildEntryModelFile();
    full.bytes[0] = 24;
    full.bytes[1] = 0;
    State().files["xmodel/oldmodel"] = full.bytes;

    XModel *model = XModelLoadFile(const_cast<char *>("oldmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(ErrorCount() == 1);
    CHECK(ErrorsContain(19, "out of date"));
    return ExpectCleanTeardown(1);
}

int RunAll()
{
    CHECK(TestValidColdLoad());
    CHECK(TestValidWarmReload());
    CHECK(TestEarlyRejections());
    CHECK(TestEmptyAndLegacyNameRejections());
    CHECK(TestTruncatedModelFile());
    CHECK(TestSecondPassFailsClosedWhenLatched());
    CHECK(TestTruncatedNestedParts());
    CHECK(TestTruncatedNestedSurfs());
    CHECK(TestBadConfigVersion());

    std::fprintf(stderr, "xmodel_loader_entry_test: %d/%d passed\n", g_runs - g_failures, g_runs);
    return g_failures == 0 ? 0 : 1;
}
}  // namespace xmodel_loader_entry_test

int main()
{
    return xmodel_loader_entry_test::RunAll();
}

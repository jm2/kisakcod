// xmodel_loader_entry_test: production-loader entry-point contracts for
// the scoped nested cursor ownership and the checked second-pass seek
// (ki-okmr / #124).
//
// Unlike the portable suites (xmodel_nested_cursor_test.cpp), which
// walk the buf_cursor entry points in the loaders' exact call sequence,
// this binary links and executes the REAL production loader entry
// point — XModelLoadFile from src/xanim/xmodel_load_obj.cpp, with its
// real config/collision/LOD parsing, real nested parts/surfs cursor
// windows, and its real checked material second pass (SeekTo against a
// cursor-owned Checkpoint). Controlled fixtures are served through the
// harness file system; see xmodel_loader_entry_harness.hpp for the
// service stubs and the XModelPartsLoadFile contract note.
//
// Win32-x86 only: the loader TU's DirectX/Miles/ODE header web and
// MSVC decompiled dialect do not compile on the portable 64-bit legs;
// tests/CMakeLists.txt gates this target to the Windows x86 CI leg.

#include <xanim/xmodel.h>
#include <xanim/buf_cursor.h>

#include "xmodel_loader_entry_harness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
    CHECK(model->surfs[0].triCount == 1);
    CHECK(model->radius == 1.0f);
    CHECK(model->collLod == 0);
    CHECK(State().physPresetCalls == 1);
    CHECK(State().collMapCalls == 1);
    CHECK(State().nestedPartsActivations == 1);

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
    CHECK(State().nestedPartsActivations == 1);

    XModel *second = XModelLoadFile(const_cast<char *>("warmmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(second != nullptr);
    CHECK(State().fsReads == coldReads + 1);
    CHECK(State().nestedPartsActivations == 1);  // warm cache: no nested load
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
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("not found") != std::string::npos);
    return ExpectCleanTeardown(0);
}

bool TestEmptyAndLegacyNameRejections()
{
    ResetHarness();
    State().files["xmodel/emptyparts"] = std::vector<unsigned char>();
    XModel *empty = XModelLoadFile(const_cast<char *>("emptyparts"), HarnessAlloc, HarnessAllocColl);
    CHECK(empty == nullptr);
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("0 length") != std::string::npos);
    CHECK(ExpectCleanTeardown(1));

    ResetHarness();
    XModel *legacy = XModelLoadFile(const_cast<char *>("xmodel/legacy"), HarnessAlloc, HarnessAllocColl);
    CHECK(legacy == nullptr);
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("Remove xmodel prefix") != std::string::npos);
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
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("malformed surface name") != std::string::npos);
    return ExpectCleanTeardown(1);
}

// Truncating AFTER the first pass but before the bone infos exercises
// the checked second pass against a latched cursor: the bone-info reads
// overrun, Failed() latches, and SeekTo(v36) must fail closed so the
// loader rejects through the ordinary cleanup instead of re-parsing at
// the wrong position.
bool TestSecondPassFailsClosedWhenLatched()
{
    ResetHarness();
    RegisterValidModel("latchmodel");
    ByteWriterFixture full = BuildEntryModelFile();
    std::vector<unsigned char> truncated(full.bytes.begin(), full.bytes.begin() + 112);
    State().files["xmodel/latchmodel"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("latchmodel"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    // The latched second pass rejects silently through the ordinary
    // cleanup path — no error print is issued on it.
    CHECK(State().errors.empty());
    return ExpectCleanTeardown(1);
}

// A malformed nested parts file must fail closed inside its own window
// and reject the model, with the parent cursor restored and the whole
// scope tree deactivated afterwards.
bool TestTruncatedNestedParts()
{
    ResetHarness();
    RegisterValidModel("badparts");
    std::vector<unsigned char> truncated(BuildEntryPartsFile().bytes.begin(),
                                         BuildEntryPartsFile().bytes.begin() + 8);
    State().files["xmodelparts/badparts"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("badparts"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(State().nestedPartsActivations == 1);
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("Cannot find xmodelparts") != std::string::npos);
    return ExpectCleanTeardown(2);
}

// A truncated nested surfs file is rejected by the production
// R_XModelSurfsLoadFile fail-closed path and rejects the whole model.
bool TestTruncatedNestedSurfs()
{
    ResetHarness();
    RegisterValidModel("badsurfs");
    std::vector<unsigned char> truncated(BuildEntrySurfsFile(2).bytes.begin(),
                                         BuildEntrySurfsFile(2).bytes.begin() + 12);
    State().files["xmodelsurfs/lod_a"] = truncated;

    XModel *model = XModelLoadFile(const_cast<char *>("badsurfs"), HarnessAlloc, HarnessAllocColl);
    CHECK(model == nullptr);
    CHECK(State().nestedPartsActivations == 1);
    CHECK(!State().errors.empty());
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
    CHECK(State().errors.size() == 1);
    CHECK(State().errors[0].text.find("out of date") != std::string::npos);
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

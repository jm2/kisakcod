#include <database/db_asset_layout.h>

#include <qcommon/com_error.h>

#include <initializer_list>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Exercises the real DB_AdmitAssetFamilyLoad gate from
// src/database/db_asset_layout.cpp (compiled into this target) against a
// recording Com_Error seam, so the test observes the actual ERR_DROP raise a
// 64-bit fast-file would hit rather than a re-statement of the policy.

namespace
{
int g_failures;
int g_comErrorCalls;
errorParm_t g_lastCode;
// The caller's format text and its named string argument, recorded as DATA.
// The seam never renders the caller's format as a format string, which is the
// Codacy CWE-134 shape (dispositioned like tests/net_chan_process_test_stubs.cpp
// and tests/script_runtime_pointer_test.cpp).
char g_lastFormat[512];
char g_lastNamed[256];

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

void ResetError()
{
    g_comErrorCalls = 0;
    g_lastCode = ERR_FATAL;
    g_lastFormat[0] = '\0';
    g_lastNamed[0] = '\0';
}

bool FormatMentions(const char *const needle)
{
    return std::strstr(g_lastFormat, needle) != nullptr;
}

bool NamedMentions(const char *const needle)
{
    return std::strstr(g_lastNamed, needle) != nullptr;
}

// XAssetType values (xanim/xanim.h) used as representative families.
constexpr std::int32_t kXModelPieces = 0x0;
constexpr std::int32_t kXAnimParts = 0x2;
constexpr std::int32_t kXModel = 0x3;
constexpr std::int32_t kWeapon = 0x17;
constexpr std::int32_t kRawFile = 0x1F;
constexpr std::int32_t kStringTable = 0x20;

// Families with no disk32 mirror yet.
constexpr std::int32_t kUnconverted[] = {
    kXModelPieces, kXAnimParts, kXModel, kWeapon, kRawFile, kStringTable,
};

// --- The conversion table records which families own a layout pair. ---
void TestConversionTable()
{
    using db::asset_layout::Conversion;
    using db::asset_layout::ConversionForAssetType;
    using db::asset_layout::IsFamilyConverted;

    Expect(
        ConversionForAssetType(db::asset_layout::kFx) == Conversion::OndiskRuntimePair,
        "the FX family has an ONDISK mirror plus a RUNTIME_SIZE runtime struct");
    Expect(
        ConversionForAssetType(db::asset_layout::kImpactFx) == Conversion::OndiskRuntimePair,
        "the impact-FX family has an ONDISK mirror plus a RUNTIME_SIZE runtime struct");
    Expect(IsFamilyConverted(db::asset_layout::kFx), "FX must report converted");
    Expect(IsFamilyConverted(db::asset_layout::kImpactFx), "impact FX must report converted");
    Expect(
        db::asset_layout::IsFxConversionFamily(db::asset_layout::kFx),
        "FX is applied through the FX zone adapter");
    Expect(
        db::asset_layout::IsFxConversionFamily(db::asset_layout::kImpactFx),
        "impact FX is applied through the FX zone adapter");
    Expect(
        !db::asset_layout::IsFxConversionFamily(kXAnimParts),
        "xanim parts is not an FX-zone-adapter family");

    // Every family without a mirror is still unconverted, including ones whose
    // runtime struct already carries a RUNTIME_SIZE half of the pair.
    Expect(!IsFamilyConverted(kXAnimParts), "xanim parts has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kXModel), "xmodel has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kWeapon), "weapon has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kRawFile), "rawfile has no disk32 mirror yet");
    Expect(
        !IsFamilyConverted(kStringTable),
        "stringtable has a RUNTIME_SIZE half but no ONDISK mirror, so it is not converted");
}

// --- The fail-closed rule. ---
void TestFailClosedPolicy()
{
    using db::asset_layout::IsLoadPermitted;

    // 32-bit targets keep loading every family: the runtime sizeof still
    // matches the retail record, so refusing would change x86 behaviour.
    for (const std::int32_t type : kUnconverted)
    {
        Expect(
            IsLoadPermitted(type, false, false),
            "32-bit targets must keep loading unconverted families");
    }
    Expect(
        IsLoadPermitted(db::asset_layout::kFx, false, false),
        "32-bit targets must keep loading FX with no adapter (legacy walk is sound)");
    Expect(
        IsLoadPermitted(db::asset_layout::kImpactFx, false, false),
        "32-bit targets must keep loading impact FX with no adapter");

    // 64-bit targets refuse anything without a complete pair.
    for (const std::int32_t type : kUnconverted)
    {
        Expect(
            !IsLoadPermitted(type, true, true),
            "64-bit targets must refuse an unconverted family even with an adapter");
        Expect(
            !IsLoadPermitted(type, true, false),
            "64-bit targets must refuse an unconverted family without an adapter");
    }

    // FX and Impact FX hold a pair, but only the FX zone adapter can apply it.
    Expect(
        IsLoadPermitted(db::asset_layout::kFx, true, true),
        "64-bit targets must load FX while the FX zone adapter is bound");
    Expect(
        IsLoadPermitted(db::asset_layout::kImpactFx, true, true),
        "64-bit targets must load impact FX while the FX zone adapter is bound");
    Expect(
        !IsLoadPermitted(db::asset_layout::kFx, true, false),
        "64-bit targets must refuse FX with no adapter: the loader would fall back");
    Expect(
        !IsLoadPermitted(db::asset_layout::kImpactFx, true, false),
        "64-bit targets must refuse impact FX with no adapter: the loader would fall back");
}

// --- The gate itself raises ERR_DROP whenever the load is not permitted. ---
void TestGateRefusals()
{
    // An unconverted family at 64-bit must raise exactly one ERR_DROP that
    // names the family and the layout-pair reason.
    ResetError();
    const bool refused = DB_AdmitAssetFamilyLoad(kXAnimParts, "xanimparts", true, true);
    Expect(!refused, "an unconverted family at 64-bit must be refused");
    Expect(g_comErrorCalls == 1, "refusal must raise exactly one Com_Error");
    Expect(g_lastCode == ERR_DROP, "refusal must raise ERR_DROP, not another code");
    Expect(
        NamedMentions("xanimparts"),
        "the refusal must name the family it refused");
    Expect(
        FormatMentions("layout"),
        "the refusal must explain the layout-pair reason");

    // An unconverted family that would load at 32-bit must not raise at 32-bit.
    ResetError();
    const bool allowed32 = DB_AdmitAssetFamilyLoad(kXAnimParts, "xanimparts", false, false);
    Expect(allowed32, "an unconverted family at 32-bit must still load");
    Expect(g_comErrorCalls == 0, "a 32-bit load must not raise");

    // A converted family with a bound adapter loads at 64-bit and must not raise.
    ResetError();
    const bool allowedFx =
        DB_AdmitAssetFamilyLoad(db::asset_layout::kFx, "fx", true, true);
    Expect(allowedFx, "FX at 64-bit must load while the adapter is bound");
    Expect(g_comErrorCalls == 0, "a permitted 64-bit load must not raise");

    // The same family is refused at 64-bit once the adapter is gone: that is
    // the path a headless build or an unbound zone takes into the legacy
    // retail-record walk.
    ResetError();
    const bool refusedFx =
        DB_AdmitAssetFamilyLoad(db::asset_layout::kFx, "fx", true, false);
    Expect(!refusedFx, "FX at 64-bit must be refused with no adapter");
    Expect(g_comErrorCalls == 1, "an adapter-less FX refusal must raise");
    Expect(g_lastCode == ERR_DROP, "an adapter-less FX refusal must raise ERR_DROP");
    Expect(NamedMentions("fx"), "an adapter-less FX refusal must name the family");
    Expect(
        FormatMentions("adapter"),
        "an adapter-less FX refusal must name the adapter reason");

    ResetError();
    const bool refusedImpact = DB_AdmitAssetFamilyLoad(
        db::asset_layout::kImpactFx, "impactfx", true, false);
    Expect(!refusedImpact, "impact FX at 64-bit must be refused with no adapter");
    Expect(g_comErrorCalls == 1, "an adapter-less impact-FX refusal must raise");
    Expect(g_lastCode == ERR_DROP, "an adapter-less impact-FX refusal must raise ERR_DROP");
    Expect(
        NamedMentions("impactfx"),
        "an adapter-less impact-FX refusal must name the family");

    // With no adapter at 32-bit the legacy walk is still sound, so the gate
    // keeps admitting the FX families exactly as the x86 baseline did.
    ResetError();
    const bool allowedFx32 =
        DB_AdmitAssetFamilyLoad(db::asset_layout::kFx, "fx", false, false);
    Expect(allowedFx32, "FX at 32-bit must still load with no adapter");
    Expect(g_comErrorCalls == 0, "a 32-bit FX load must not raise");

    // Out-of-range types fail closed too: they cannot claim a pair they lack.
    ResetError();
    Expect(
        !DB_AdmitAssetFamilyLoad(-1, nullptr, true, true),
        "an out-of-range type at 64-bit must be refused");
    Expect(g_comErrorCalls == 1, "an out-of-range refusal must raise");
    Expect(g_lastCode == ERR_DROP, "an out-of-range refusal must raise ERR_DROP");
    Expect(
        NamedMentions("(unknown)"),
        "a nameless family must still produce a readable refusal");
}
} // namespace

void __cdecl Com_Error(errorParm_t code, const char *fmt, ...)
{
    ++g_comErrorCalls;
    g_lastCode = code;

    // Constant format spec (Codacy CWE-134): the seam never renders the
    // caller's format string as a format string. It records the format text
    // and the caller's named string argument as data, which is exactly what
    // the assertions inspect.
    va_list args;
    va_start(args, fmt);
    const char *const named = va_arg(args, const char *);
    va_end(args);
    std::snprintf(g_lastFormat, sizeof g_lastFormat, "%s", fmt != nullptr ? fmt : "");
    std::snprintf(g_lastNamed, sizeof g_lastNamed, "%s", named != nullptr ? named : "");
}

// The developer opt-in admits exactly the 25 MP server-closure families that
// have a 64-bit disk32 loader, and never the SP-only, MP-unavailable or
// listed-only types.
void TestDisk32LoaderFamilies()
{
    int admitted = 0;
    for (std::int32_t type = -1; type <= db::asset_layout::kAssetTypeCount; ++type)
        admitted += db::asset_layout::HasDisk32Loader(type) ? 1 : 0;
    Expect(admitted == 25, "25 families have a disk32 loader");
    for (const std::int32_t excluded : {0x0A, 0x0D, 0x12, 0x18, 0x1B, 0x1C, 0x1D, 0x1E, -1, db::asset_layout::kAssetTypeCount})
        Expect(!db::asset_layout::HasDisk32Loader(excluded), "excluded type has no disk32 loader");
    for (const std::int32_t included : {0x00, 0x05, 0x0B, 0x10, 0x15, 0x17, 0x19, 0x1A, 0x20})
        Expect(db::asset_layout::HasDisk32Loader(included), "server-closure type has a disk32 loader");
}

int main()
{
    TestDisk32LoaderFamilies();
    TestConversionTable();
    TestFailClosedPolicy();
    TestGateRefusals();

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}

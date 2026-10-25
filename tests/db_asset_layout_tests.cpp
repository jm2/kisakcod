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
constexpr std::int32_t kPhysPreset = 0x1;
constexpr std::int32_t kXAnimParts = 0x2;
constexpr std::int32_t kXModel = 0x3;
constexpr std::int32_t kClipMapPvs = 0xB;
constexpr std::int32_t kUiMap = 0x12;
constexpr std::int32_t kWeapon = 0x17;
constexpr std::int32_t kRawFile = 0x1F;
constexpr std::int32_t kStringTable = 0x20;

// Families 64-bit refuses: UiMap has no 64-bit loader, so it never joins K4.
constexpr std::int32_t kUnconverted[] = {
    kUiMap,
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

    // Every family without a mirror is still unconverted, including ones whose
    // runtime struct already carries a RUNTIME_SIZE half of the pair.
    Expect(IsFamilyConverted(kXAnimParts), "xanim parts counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kXModel), "xmodel counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kXModelPieces), "xmodelpieces counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kClipMapPvs), "the PVS clip map counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kWeapon), "weapon counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kRawFile), "rawfile counts toward K4, so its pair is complete");
    Expect(IsFamilyConverted(kStringTable), "stringtable counts toward K4, so its pair is complete");
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
            IsLoadPermitted(type, false),
            "32-bit targets must keep loading unconverted families");
    }
    Expect(IsLoadPermitted(db::asset_layout::kFx, false), "32-bit targets must keep loading FX");
    Expect(IsLoadPermitted(db::asset_layout::kImpactFx, false), "32-bit targets must keep loading impact FX");

    // 64-bit targets refuse anything without a complete pair.
    for (const std::int32_t type : kUnconverted)
    {
        Expect(!IsLoadPermitted(type, true), "64-bit targets must refuse an unconverted family");
    }

    // K4 counts these: their 64-bit digest on retail data is the x86 one.
    Expect(IsLoadPermitted(kRawFile, true), "64-bit targets must load raw files, which K4 counts");
    Expect(IsLoadPermitted(kStringTable, true), "64-bit targets must load string tables, which K4 counts");
    Expect(IsLoadPermitted(kPhysPreset, true), "64-bit targets must load physics presets, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kLocalizeEntry, true),
           "64-bit targets must load localized strings, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kMapEnts, true),
           "64-bit targets must load map entities, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kSoundCurve, true),
           "64-bit targets must load sound curves, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kGameWorldMp, true),
           "64-bit targets must load MP game worlds, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kLightDef, true),
           "64-bit targets must load light defs, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kFont, true),
           "64-bit targets must load fonts, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kMenuList, true),
           "64-bit targets must load menu lists, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kComWorld, true),
           "64-bit targets must load com worlds, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kSound, true),
           "64-bit targets must load sound alias lists, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kXAnimParts, true),
           "64-bit targets must load animations, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kWeapon, true),
           "64-bit targets must load weapons, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kXModel, true),
           "64-bit targets must load models, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kMenu, true),
           "64-bit targets must load menus, which K4 counts");
#if defined(KISAK_DEDI_HEADLESS)
    Expect(IsLoadPermitted(db::asset_layout::kImage, true),
           "a 64-bit headless server must load images, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kMaterial, true),
           "a 64-bit headless server must load materials, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kLoadedSound, true),
           "a 64-bit headless server must load loaded sounds, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kTechniqueSet, true),
           "a 64-bit headless server must load technique sets, which K4 counts");
#else
    Expect(!IsLoadPermitted(db::asset_layout::kImage, true),
           "a 64-bit client keeps images guarded: no client leg has compared them");
    Expect(!IsLoadPermitted(db::asset_layout::kMaterial, true),
           "a 64-bit client keeps materials guarded: no client leg has compared them");
    Expect(!IsLoadPermitted(db::asset_layout::kLoadedSound, true),
           "a 64-bit client keeps loaded sounds guarded: it keeps samples no client leg has compared");
    Expect(!IsLoadPermitted(db::asset_layout::kTechniqueSet, true),
           "a 64-bit client keeps technique sets guarded: no client leg has compared them");
#endif

    // K4 counts FX and Impact FX: their disk32 loaders need no FX zone adapter.
    Expect(IsLoadPermitted(db::asset_layout::kFx, true), "64-bit targets must load FX, which K4 counts");
    Expect(IsLoadPermitted(db::asset_layout::kImpactFx, true), "64-bit targets must load impact FX, which K4 counts");
}

// --- The gate itself raises ERR_DROP whenever the load is not permitted. ---
void TestGateRefusals()
{
    // An unconverted family at 64-bit must raise exactly one ERR_DROP that
    // names the family and the layout-pair reason.
    ResetError();
    const bool refused = DB_AdmitAssetFamilyLoad(kUiMap, "uimap", true);
    Expect(!refused, "an unconverted family at 64-bit must be refused");
    Expect(g_comErrorCalls == 1, "refusal must raise exactly one Com_Error");
    Expect(g_lastCode == ERR_DROP, "refusal must raise ERR_DROP, not another code");
    Expect(
        NamedMentions("uimap"),
        "the refusal must name the family it refused");
    Expect(
        FormatMentions("layout"),
        "the refusal must explain the layout-pair reason");

    // An unconverted family that would load at 32-bit must not raise at 32-bit.
    ResetError();
    const bool allowed32 = DB_AdmitAssetFamilyLoad(kUiMap, "uimap", false);
    Expect(allowed32, "an unconverted family at 32-bit must still load");
    Expect(g_comErrorCalls == 0, "a 32-bit load must not raise");

    // The K4-verified FX families load at 64-bit and raise nothing.
    ResetError();
    const bool allowedFx = DB_AdmitAssetFamilyLoad(db::asset_layout::kFx, "fx", true);
    const bool allowedImpact = DB_AdmitAssetFamilyLoad(db::asset_layout::kImpactFx, "impactfx", true);
    Expect(allowedFx && allowedImpact, "FX and impact FX at 64-bit must load");
    Expect(g_comErrorCalls == 0, "a permitted 64-bit load must not raise");

    // Out-of-range types fail closed too: they cannot claim a pair they lack.
    ResetError();
    Expect(
        !DB_AdmitAssetFamilyLoad(-1, nullptr, true),
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

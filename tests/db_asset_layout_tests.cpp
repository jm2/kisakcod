#include <database/db_asset_layout.h>

#include <qcommon/com_error.h>

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
char g_lastMessage[512];

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
    g_lastMessage[0] = '\0';
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
} // namespace

void __cdecl Com_Error(errorParm_t code, const char *fmt, ...)
{
    ++g_comErrorCalls;
    g_lastCode = code;

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_lastMessage, sizeof g_lastMessage, fmt, args);
    va_end(args);
}

int main()
{
    using db::asset_layout::Conversion;
    using db::asset_layout::ConversionForAssetType;
    using db::asset_layout::IsFamilyConverted;
    using db::asset_layout::IsLoadPermitted;

    // --- The conversion table records which families own a layout pair. ---
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
    Expect(!IsFamilyConverted(kXAnimParts), "xanim parts has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kXModel), "xmodel has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kWeapon), "weapon has no disk32 mirror yet");
    Expect(!IsFamilyConverted(kRawFile), "rawfile has no disk32 mirror yet");
    Expect(
        !IsFamilyConverted(kStringTable),
        "stringtable has a RUNTIME_SIZE half but no ONDISK mirror, so it is not converted");

    // --- The fail-closed rule. ---
    // 32-bit targets keep loading every family: the runtime sizeof still
    // matches the retail record, so refusing would change x86 behaviour.
    for (const std::int32_t type : kUnconverted)
    {
        Expect(
            IsLoadPermitted(type, false),
            "32-bit targets must keep loading unconverted families");
    }
    Expect(
        IsLoadPermitted(db::asset_layout::kFx, false),
        "32-bit targets must keep loading converted families");

    // 64-bit targets refuse anything without a complete pair.
    for (const std::int32_t type : kUnconverted)
    {
        Expect(
            !IsLoadPermitted(type, true),
            "64-bit targets must refuse an unconverted family");
    }
    Expect(
        IsLoadPermitted(db::asset_layout::kFx, true),
        "64-bit targets must load a converted family");
    Expect(
        IsLoadPermitted(db::asset_layout::kImpactFx, true),
        "64-bit targets must load a converted family");

    // --- The gate itself raises ERR_DROP for an unconverted 64-bit load. ---
    ResetError();
    const bool refused = DB_AdmitAssetFamilyLoad(kXAnimParts, "xanimparts", true);
    Expect(!refused, "an unconverted family at 64-bit must be refused");
    Expect(g_comErrorCalls == 1, "refusal must raise exactly one Com_Error");
    Expect(g_lastCode == ERR_DROP, "refusal must raise ERR_DROP, not another code");
    Expect(
        std::strstr(g_lastMessage, "xanimparts") != nullptr,
        "the refusal must name the family it refused");
    Expect(
        std::strstr(g_lastMessage, "layout") != nullptr,
        "the refusal must explain the layout-pair reason");

    // An unconverted family that would load at 32-bit must not raise at 32-bit.
    ResetError();
    const bool allowed32 = DB_AdmitAssetFamilyLoad(kXAnimParts, "xanimparts", false);
    Expect(allowed32, "an unconverted family at 32-bit must still load");
    Expect(g_comErrorCalls == 0, "a 32-bit load must not raise");

    // A converted family loads at 64-bit and must not raise.
    ResetError();
    const bool allowedFx = DB_AdmitAssetFamilyLoad(db::asset_layout::kFx, "fx", true);
    Expect(allowedFx, "a converted family at 64-bit must load");
    Expect(g_comErrorCalls == 0, "a converted 64-bit load must not raise");

    // Out-of-range types fail closed too: they cannot claim a pair they lack.
    ResetError();
    Expect(
        !DB_AdmitAssetFamilyLoad(-1, nullptr, true),
        "an out-of-range type at 64-bit must be refused");
    Expect(g_comErrorCalls == 1, "an out-of-range refusal must raise");
    Expect(g_lastCode == ERR_DROP, "an out-of-range refusal must raise ERR_DROP");
    Expect(
        std::strstr(g_lastMessage, "(unknown)") != nullptr,
        "a nameless family must still produce a readable refusal");

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}

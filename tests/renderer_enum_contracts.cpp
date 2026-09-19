// Declarations, bodies and selector expressions come from production files.
// Renderer/OS services are doubles; this is not a GPU or retail parity test.
#include <array>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

#ifndef _MSC_VER
#define __cdecl
#define __int32 std::int32_t
#endif

namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "renderer enum contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#define iassert(value) CHECK(value)

#include "aspect_enum.inc"
#include "light_enum.inc"
#include "depth_enum.inc"
#include "stencil_op_enum.inc"
#include "stencil_func_enum.inc"
#include "depth_selectors.inc"

static_assert(std::is_same_v<std::underlying_type_t<GfxAspectRatio>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GfxLightType>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GfxDepthRangeType>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GfxStencilOp>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<GfxStencilFunc>, std::int32_t>);
static_assert(GFX_ASPECT_RATIO_AUTO == 0 && GFX_ASPECT_RATIO_STANDARD == 1
    && GFX_ASPECT_RATIO_WIDE_16_10 == 2 && GFX_ASPECT_RATIO_WIDE_16_9 == 3
    && GFX_ASPECT_RATIO_COUNT == 4);
static_assert(GFX_LIGHT_TYPE_NONE == 0 && GFX_LIGHT_TYPE_DIR == 1
    && GFX_LIGHT_TYPE_SPOT == 2 && GFX_LIGHT_TYPE_OMNI == 3
    && GFX_LIGHT_TYPE_COUNT == 4 && GFX_LIGHT_TYPE_DIR_SHADOWMAP == 4
    && GFX_LIGHT_TYPE_SPOT_SHADOWMAP == 5 && GFX_LIGHT_TYPE_OMNI_SHADOWMAP == 6
    && GFX_LIGHT_TYPE_COUNT_WITH_SHADOWMAP_VERSIONS == 7);
static_assert(GFX_DEPTH_RANGE_SCENE == 0 && GFX_DEPTH_RANGE_VIEWMODEL == 2
    && GFX_DEPTH_RANGE_FULL == -1);
static_assert(GFXS_STENCILOP_KEEP == 0 && GFXS_STENCILOP_ZERO == 1
    && GFXS_STENCILOP_REPLACE == 2 && GFXS_STENCILOP_INCRSAT == 3
    && GFXS_STENCILOP_DECRSAT == 4 && GFXS_STENCILOP_INVERT == 5
    && GFXS_STENCILOP_INCR == 6 && GFXS_STENCILOP_DECR == 7
    && GFXS_STENCILOP_COUNT == 8);
static_assert(GFXS_STENCILFUNC_NEVER == 0 && GFXS_STENCILFUNC_LESS == 1
    && GFXS_STENCILFUNC_EQUAL == 2 && GFXS_STENCILFUNC_LESSEQUAL == 3
    && GFXS_STENCILFUNC_GREATER == 4 && GFXS_STENCILFUNC_NOTEQUAL == 5
    && GFXS_STENCILFUNC_GREATEREQUAL == 6 && GFXS_STENCILFUNC_ALWAYS == 7
    && GFXS_STENCILFUNC_COUNT == 8);

const char *va(const char *format, ...)
{
    static char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}
void MyAssertHandler(const char *, int, int, const char *, ...) { CHECK(false); }

namespace aspect {
struct HWND__;
#include "window_type.inc"
struct dvar_s { struct { int integer{}; } current; };
dvar_s aspectDvar{}, wideDvar{};
const dvar_s *r_aspectRatio = &aspectDvar;
const dvar_s *com_wideScreen = &wideDvar;
constexpr bool alwaysfails = false;
struct {
    int sceneWidth{}, sceneHeight{}, displayWidth{}, displayHeight{}, displayFrequency{};
    bool isFullscreen{};
    float aspectRatioWindow{}, aspectRatioScenePixel{}, aspectRatioDisplayPixel{};
} vidConfig;
struct {
    bool adapterNativeIsValid{};
    int adapterNativeWidth{}, adapterNativeHeight{};
    int adapterFullscreenWidth{}, adapterFullscreenHeight{};
} dx;
void Dvar_SetBool(dvar_s *dvar, bool value) { dvar->current.integer = value; }
int SnapFloatToInt(float value) { return static_cast<int>(std::nearbyint(value)); }
// Only the unused decompiler local is suppressed; the body is unchanged.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4101)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "aspect_body.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif
void Run()
{
    GfxWindowParms window{};
    window.hz = 144;
    window.sceneWidth = 1280;
    window.sceneHeight = 720;
    window.displayWidth = 1920;
    window.displayHeight = 1080;
    dx.adapterFullscreenWidth = 1920;
    dx.adapterFullscreenHeight = 1080;
    constexpr float ratios[] = {1.3333334f, 1.6f, 1.7777778f};
    for (int mode = 1; mode <= 3; ++mode) {
        for (bool fullscreen : {false, true}) {
            aspectDvar.current.integer = mode;
            window.fullscreen = fullscreen;
            R_StoreWindowSettings(&window);
            CHECK(vidConfig.aspectRatioWindow == ratios[mode - 1]);
            CHECK(vidConfig.sceneWidth == 1280 && vidConfig.sceneHeight == 720);
            CHECK(vidConfig.displayWidth == 1920 && vidConfig.displayHeight == 1080);
            CHECK(vidConfig.displayFrequency == 144 && vidConfig.isFullscreen == fullscreen);
            CHECK(wideDvar.current.integer == (mode != 1));
            CHECK(std::fabs(vidConfig.aspectRatioScenePixel - ratios[mode - 1] * 0.5625f) < 0.000001f);
            CHECK(std::fabs(vidConfig.aspectRatioDisplayPixel - (fullscreen ? ratios[mode - 1] * 0.5625f : 1.0f)) < 0.000001f);
        }
    }
    aspectDvar.current.integer = 0;
    for (int mode = 0; mode < 3; ++mode) {
        constexpr int widths[] = {1600, 1920, 1920};
        constexpr int heights[] = {1200, 1200, 1080};
        for (bool fullscreen : {false, true}) {
            for (bool nativeValid : {false, true}) {
                window.fullscreen = fullscreen;
                dx.adapterNativeIsValid = nativeValid;
                const bool native = fullscreen && nativeValid;
                dx.adapterNativeWidth = native ? widths[mode] : 1920;
                dx.adapterNativeHeight = native ? heights[mode] : 1080;
                window.displayWidth = native ? 1920 : widths[mode];
                window.displayHeight = native ? 1080 : heights[mode];
                R_StoreWindowSettings(&window);
                CHECK(vidConfig.aspectRatioWindow == ratios[mode]);
                CHECK(wideDvar.current.integer == (mode != 0));
            }
        }
    }
}
} // namespace aspect

namespace lights {
// Unrelated renderer fields are deliberately absent; type remains an asset byte.
struct GfxLight { std::uint8_t type; float radius; float origin[3]; };
struct { float viewOrg[3]; } rg{};
void Vec3Sub(const float *a, const float *b, float *out)
{ for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i]; }
float Vec3LengthSq(const float *a) { return a[0]*a[0] + a[1]*a[1] + a[2]*a[2]; }
#include "light_body.inc"
void Run()
{
    const GfxLight spot{2, 1.0f, {100.0f, 0.0f, 0.0f}};
    const GfxLight omni{3, 10.0f, {1.0f, 0.0f, 0.0f}};
    CHECK(R_LightImportanceGreaterEqual(&spot, &omni));
    CHECK(!R_LightImportanceGreaterEqual(&omni, &spot));
    for (const std::uint8_t type : std::array<std::uint8_t, 2>{2, 3}) {
        const GfxLight near{type, 2.0f, {2.0f, 0.0f, 0.0f}};
        const GfxLight far{type, 2.0f, {4.0f, 0.0f, 0.0f}};
        CHECK(R_LightImportanceGreaterEqual(&near, &far));
        CHECK(!R_LightImportanceGreaterEqual(&far, &near));
        CHECK(R_LightImportanceGreaterEqual(&near, &near));
    }
}
} // namespace lights

namespace stencil {
#include "state_bits_type.inc"
#include "stencil_type.inc"
const StencilLogBits *expectedDesc;
int expectedState, expectedChanged, boolCalls, tableCalls;
void RB_LogBool(const char *, int state, int changed, int mask, const char *yes, const char *no)
{
    CHECK(state == expectedState && changed == expectedChanged);
    CHECK(mask == expectedDesc->enableMask);
    CHECK(std::strcmp(yes, "Enabled") == 0 && std::strcmp(no, "Disabled") == 0);
    ++boolCalls;
}
void RB_LogFromTable(const char *, int state, int changed, int mask, char shift,
    const StateBitsTable *table, int count)
{
    constexpr const char *funcs[] = {"Never", "Less", "Equal", "LessEqual", "Greater", "NotEqual", "GreaterEqual", "Always"};
    constexpr const char *ops[] = {"Keep", "Zero", "Replace", "IncrSat", "DecrSat", "Invert", "Incr", "Decr"};
    const int shifts[] = {expectedDesc->funcShift, expectedDesc->passShift,
        expectedDesc->failShift, expectedDesc->zfailShift};
    CHECK(tableCalls < 4 && count == 8);
    CHECK(shift == shifts[tableCalls]);
    CHECK(static_cast<std::uint32_t>(mask) == (7u << shift));
    CHECK(state == expectedState && changed == expectedChanged);
    for (int i = 0; i < 8; ++i) {
        CHECK(table[i].stateBits == i);
        CHECK(std::strcmp(table[i].name, tableCalls == 0 ? funcs[i] : ops[i]) == 0);
    }
    ++tableCalls;
}
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244) // Existing int-to-char service-call shift arguments.
#endif
#include "stencil_body.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
void Run()
{
    const StencilLogBits descriptions[] = {{"Front", 64, 8, 11, 14, 17}, {"Back", 128, 20, 23, 26, 29}};
    for (const auto &desc : descriptions) {
        expectedDesc = &desc;
        for (bool enabled : {false, true}) {
            for (std::uint32_t value = 0; value < 8; ++value) {
                const auto state = (enabled ? static_cast<std::uint32_t>(desc.enableMask) : 0u)
                    | (value << desc.funcShift) | (value << desc.passShift)
                    | (value << desc.failShift) | (value << desc.zfailShift);
                expectedState = std::bit_cast<int>(state);
                expectedChanged = std::bit_cast<int>(~state);
                boolCalls = tableCalls = 0;
                RB_LogStencilState(expectedState, expectedChanged, &desc);
                CHECK(boolCalls == 1 && tableCalls == (enabled ? 4 : 0));
            }
        }
    }
}
} // namespace stencil
} // namespace

void RunRendererEnumContracts()
{
    aspect::Run();
    lights::Run();
    stencil::Run();
    for (const auto select : depthSelectors) {
        CHECK(select(0) == -1);
        for (int camera = 1; camera < 256; ++camera) CHECK(select(camera) == 0);
        CHECK(select(-1) == 0);
        CHECK(select((std::numeric_limits<int>::min)()) == 0);
    }
}

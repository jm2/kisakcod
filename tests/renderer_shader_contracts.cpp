// Production slices exercise naming contracts; engine services are doubles.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <database/db_validation.h>
#ifndef _MSC_VER
#define __cdecl
#define __int32 int
#define __int64 long long
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
#endif
#ifdef _MSC_VER
#pragma warning(disable: 4244)
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "renderer shader contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
#include "code_constants.inc"
#include "technique_enum.inc"
#include "shader_arg_enum.inc"
#include "texture_source.inc"
#include "surface_enum.inc"
#include "draw_scene_enum.inc"
#include "draw_method_record.inc"
#include "renderer_shader_expected.inc"
#include "shader_selectors.inc"
static_assert(std::is_same_v<std::underlying_type_t<CodeConstant>, std::int32_t>);
static_assert(std::is_same_v<std::underlying_type_t<MaterialTechniqueType>, std::int32_t>);
#include "argument_code.inc"
#include "argument_union.inc"
#include "shader_argument.inc"
struct MaterialVertexDeclaration;
struct MaterialVertexShader;
struct MaterialPixelShader;
struct MaterialTechnique;
struct MaterialTextureDef;
struct GfxStateBits;
#include "material_pass.inc"
#include "material_constant.inc"
#include "draw_surf_fields.inc"
#include "draw_surf.inc"
#include "material_info.inc"
#include "technique_set.inc"
#include "material_record.inc"
#include "shader_constant_block.inc"
static_assert(std::is_same_v<decltype(MaterialShaderArgument::type), std::uint16_t>);
static_assert(std::extent_v<decltype(MaterialTechniqueSet::techniques)> == 34);
static_assert(std::extent_v<decltype(Material::stateBitsEntry)> == 34);
static_assert(sizeof(Material::stateBitsEntry) == 34);
static_assert(sizeof(MaterialConstantDef) == 32);

static_assert(sizeof(surfaceType_t) == 4);
static_assert(SF_TRIANGLES == 0 && SF_TRIANGLES_PRETESS == 1
    && SF_BEGIN_STATICMODEL == 2 && SF_STATICMODEL_RIGID == 2
    && SF_STATICMODEL_PRETESS == 3 && SF_STATICMODEL_CACHED == 4
    && SF_STATICMODEL_SKINNED == 5 && SF_END_STATICMODEL == 6 && SF_BMODEL == 6
    && SF_BEGIN_XMODEL == 7 && SF_XMODEL_RIGID == 7 && SF_XMODEL_RIGID_SKINNED == 8
    && SF_XMODEL_SKINNED == 9 && SF_END_XMODEL == 10 && SF_BEGIN_FX == 10
    && SF_CODE_MESH == 10 && SF_MARK_MESH == 11 && SF_PARTICLE_CLOUD == 12
    && SF_END_FX == 13 && SF_NUM_SURFACE_TYPES == 13 && SF_FORCE_32_BITS == -1);
static_assert(std::extent_v<decltype(GfxDrawMethod::litTechType), 0> == 13);
static_assert(std::extent_v<decltype(GfxDrawMethod::litTechType), 1> == 7);
GfxDrawMethod gfxDrawMethod;
#include "force_lit_body.inc"
void CheckSurfaceDispatch()
{
    for (int technique = 0; technique <= 36; ++technique) {
        std::memset(&gfxDrawMethod, 0, sizeof(gfxDrawMethod));
        gfxDrawMethod.drawScene = GFX_DRAW_SCENE_DEBUGSHADER;
        gfxDrawMethod.baseTechType = TECHNIQUE_DEPTH_PREPASS;
        gfxDrawMethod.emissiveTechType = TECHNIQUE_NONE;
        R_ForceLitTechType(static_cast<MaterialTechniqueType>(technique));
        for (const auto &surface : gfxDrawMethod.litTechType)
            for (const auto value : surface) CHECK(value == technique);
        CHECK(gfxDrawMethod.drawScene == GFX_DRAW_SCENE_DEBUGSHADER);
        CHECK(gfxDrawMethod.baseTechType == TECHNIQUE_DEPTH_PREPASS);
        CHECK(gfxDrawMethod.emissiveTechType == TECHNIQUE_NONE);
    }
}

struct GfxCmdBufSourceState { struct { float consts[58][4]; } input; };
int dirtyCalls;
int dirtySlot;
GfxCmdBufSourceState *dirtySource;
void R_DirtyCodeConstant(GfxCmdBufSourceState *source, CodeConstant slot)
{
    ++dirtyCalls;
    dirtySlot = slot;
    dirtySource = source;
}
#include "game_time_body.inc"
#include "technique_lookup_body.inc"
constexpr int ERR_DROP = 1;
int errors;
void Com_Error(int code, const char *, ...) { CHECK(code == ERR_DROP); ++errors; }
#define ARRAY_COUNT(value) (sizeof(value) / sizeof((value)[0]))
#include "shader_register_body.inc"
#include "pixel_literals_body.inc"

void CheckGameTime()
{
    struct Example {
        float gameTime;
        float sine;
        float cosine;
        float fraction;
    };
    constexpr Example examples[] = {
        {0, 0, 1, 0}, {0.25f, 1, 0, 0.25f}, {0.5f, 0, -1, 0.5f},
        {1.75f, -1, 0, 0.75f}, {-0.25f, -1, 0, 0.75f}
    };
    for (const auto &example : examples) {
        GfxCmdBufSourceState source{};
        for (auto &row : source.input.consts) for (auto &value : row) value = -123;
        dirtyCalls = 0;
        R_SetGameTime(&source, example.gameTime);
        CHECK(dirtyCalls == 1 && dirtySlot == 18 && dirtySource == &source);
        CHECK(std::abs(source.input.consts[18][0] - example.sine) < 0.000001f);
        CHECK(std::abs(source.input.consts[18][1] - example.cosine) < 0.000001f);
        CHECK(source.input.consts[18][2] == example.fraction);
        CHECK(source.input.consts[18][3] == example.gameTime);
        for (int slot = 0; slot < 58; ++slot)
            if (slot != 18) for (const float value : source.input.consts[slot]) CHECK(value == -123);
        dirtySource = nullptr;
    }
}
constexpr const char *techniqueNames[] = {
    "depth prepass", "build floatz", "build shadowmap depth", "build shadowmap color",
    "unlit", "emissive", "emissive shadow", "lit", "lit sun", "lit sun shadow",
    "lit spot", "lit spot shadow", "lit omni", "lit omni shadow", "lit instanced",
    "lit instanced sun", "lit instanced sun shadow", "lit instanced spot",
    "lit instanced spot shadow", "lit instanced omni", "lit instanced omni shadow",
    "light spot", "light omni", "light spot shadow", "fakelight normal", "fakelight view",
    "sunlight preview", "case texture", "solid wireframe", "shaded wireframe",
    "shadowcookie caster", "shadowcookie receiver", "debug bumpmap", "debug bumpmap instanced"
};
static_assert(ARRAY_COUNT(techniqueNames) == 34);
void CheckTechniques()
{
    for (int slot = 0; slot < 34; ++slot) {
        char quoted[64];
        std::snprintf(quoted, sizeof(quoted), "\"%s\"", techniqueNames[slot]);
        CHECK(Material_TechniqueTypeForName(quoted) == slot);
        CHECK(Material_TechniqueTypeForName(techniqueNames[slot]) == 34);
    }
    CHECK(Material_TechniqueTypeForName("") == 34);
    CHECK(Material_TechniqueTypeForName("\"unknown\"") == 34);
}
void CheckPixelLiterals()
{
    MaterialConstantDef constants[] = {{10, {}, {1, 2, 3, 4}}, {30, {}, {5, 6, 7, 8}}};
    const float literal[] = {9, 10, 11, 12};
    Material material{};
    material.info.name = "shader fixture";
    material.constantTable = constants;
    material.constantCount = 2;
    MaterialPass pass{};
    GfxShaderConstantBlock output{};
    output.count = 63;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(output.count == 0);
    // The first two arguments belong to other groups and must not be visited.
    MaterialShaderArgument args[7]{};
    args[0].type = 7;
    args[1].type = 7;
    args[2].type = 3;
    args[3].type = 5;
    args[4].type = 6; args[4].dest = 9; args[4].u.nameHash = 30;
    args[5].type = 6; args[5].dest = 2; args[5].u.nameHash = 10;
    args[6].type = 7; args[6].dest = 5; args[6].u.literalConst = literal;
    pass.args = args;
    pass.perPrimArgCount = pass.perObjArgCount = 1;
    pass.stableArgCount = 5;
    errors = 0;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(errors == 0 && output.count == 3);
    CHECK(output.dest[0] == 2 && output.value[0] == constants[0].literal);
    CHECK(output.dest[1] == 5 && output.value[1] == literal);
    CHECK(output.dest[2] == 9 && output.value[2] == constants[1].literal);
    pass.stableArgCount = 2;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(output.count == 0);
    pass.stableArgCount = 5;
    args[4].u.nameHash = 20;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(errors == 1 && output.count == 0);
    material.constantTable = nullptr;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(errors == 2 && output.count == 0);
    // A literal-only tail is valid; an unrecognized tag must stop iteration.
    pass.perPrimArgCount = pass.perObjArgCount = 0;
    pass.args = &args[6]; pass.stableArgCount = 1;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(output.count == 1 && output.value[0] == literal);
    args[6].type = 8;
    R_GetPixelLiteralConsts(&material, &pass, &output);
    CHECK(output.count == 0 && errors == 2);
}
} // namespace
void RunRendererShaderContracts()
{
    for (const auto chooseShaderKind : shaderSelectors) {
        CHECK(chooseShaderKind(0) == 3);
        CHECK(chooseShaderKind(1) == 5);
        CHECK(chooseShaderKind(-1) == 5);
        CHECK(chooseShaderKind(255) == 5);
    }
    CheckSurfaceDispatch();
    CheckGameTime();
    CheckTechniques();
    CheckPixelLiterals();
}

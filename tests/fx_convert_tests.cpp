// fx_convert_tests.cpp: the production editor-to-runtime effect conversion
// (EffectsCore/fx_convert.cpp, FX_Convert) at native width. FX_Convert sizes
// one blob for the FxEffectDef, its FxElemDefs and every sample, visual, trail
// and name payload, then writes them all into it. Every pointer it stores must
// land inside that blob and the bytes written must equal the planned size, now
// that FxEffectDef/FxElemDef widen under 64-bit pointers. ASan and UBSan see
// any write past the blob; UBSan also caught the frame-1 velocity scale read
// as velScale[0][3..5] and the 8-byte trail normal store into a 4-byte-aligned
// vertex. The engine's MyAssertHandler aborts here
// (com_math_test_stubs.cpp), so an engine assert fails the test.

#include <EffectsCore/fx_system.h>
#include <gfx_d3d/r_material.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
int g_failures = 0;
int g_printedErrors = 0;
std::vector<void *> g_hunk;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

// Two distinct addresses stand in for materials; the stub reports a 2 x 4 atlas.
alignas(8) uint8_t g_materialStorage[2];
Material *const kMaterialA = reinterpret_cast<Material *>(&g_materialStorage[0]);
Material *const kMaterialB = reinterpret_cast<Material *>(&g_materialStorage[1]);

const FxCurve *g_flat1D = nullptr;
const FxCurve *g_flat3D = nullptr;

void *AllocEffect(const uint32_t size)
{
    return std::malloc(size);
}

bool Inside(const void *const p, const FxEffectDef *const effect)
{
    const auto *const begin = reinterpret_cast<const uint8_t *>(effect);
    const auto *const at = static_cast<const uint8_t *>(p);
    return at >= begin && at < begin + effect->totalSize;
}

// An element every converter path accepts: flat curves, a one-second life.
void InitElem(FxEditorElemDef *const elem, const char *const name, const uint8_t elemType, const int32_t editorFlags)
{
    std::memset(static_cast<void *>(elem), 0, sizeof(*elem));
    std::snprintf(elem->name, sizeof(elem->name), "%s", name);
    elem->elemType = elemType;
    elem->editorFlags = editorFlags;
    elem->lifeSpanMsec.base = 1000;
    elem->spawnLooping.intervalMsec = 50;
    elem->spawnLooping.count = 4;
    elem->spawnOneShot.count.base = 1;
    elem->elasticity.base = 0.5f;
    for (auto &frame : elem->velShape)
        for (auto &dimension : frame)
            for (const FxCurve *&curve : dimension)
                curve = g_flat1D;
    for (const FxCurve *&curve : elem->rotationShape)
        curve = g_flat1D;
    for (auto &axis : elem->sizeShape)
        for (const FxCurve *&curve : axis)
            curve = g_flat1D;
    for (const FxCurve *&curve : elem->scaleShape)
        curve = g_flat1D;
    for (const FxCurve *&curve : elem->color)
        curve = g_flat3D;
    for (const FxCurve *&curve : elem->alpha)
        curve = g_flat1D;
}

void CheckElemPayload(const FxElemDef *const elemDef, const FxEffectDef *const effect, const char *const what)
{
    Check(elemDef->velIntervalCount >= 1 && Inside(elemDef->velSamples, effect)
            && Inside(&elemDef->velSamples[elemDef->velIntervalCount], effect),
        what);
    if (elemDef->visStateIntervalCount)
        Check(Inside(elemDef->visSamples, effect) && Inside(&elemDef->visSamples[elemDef->visStateIntervalCount], effect),
            what);
}

std::unique_ptr<FxEditorEffectDef> NewEditorEffect(const char *const name)
{
    auto editor = std::make_unique<FxEditorEffectDef>();
    std::memset(static_cast<void *>(editor.get()), 0, sizeof(*editor));
    std::snprintf(editor->name, sizeof(editor->name), "%s", name);
    return editor;
}

// A one-shot effect another effect emits: one sprite with two materials.
const FxEffectDef *ConvertEmitted()
{
    auto editor = NewEditorEffect("fx/test_spark");
    editor->elemCount = 1;
    InitElem(&editor->elems[0], "spark", 0, 0);
    editor->elems[0].visualCount = 2;
    editor->elems[0].visuals[0].material = kMaterialA;
    editor->elems[0].visuals[1].material = kMaterialB;
    const FxEffectDef *const effect = FX_Convert(editor.get(), AllocEffect);
    Check(effect && effect->elemDefCountOneShot == 1 && effect->elemDefCountLooping == 0, "emitted: one one-shot");
    return effect;
}

void TestConvert(const FxEffectDef *const emitted)
{
    auto editor = NewEditorEffect("fx/test_convert");
    editor->elemCount = 4;

    // 0: looping sprite with two materials (an out-of-line visual array).
    FxEditorElemDef *const sprite = &editor->elems[0];
    InitElem(sprite, "sprite", 0, 1 | 0x1000); // looping; frame 1 velocity is in world space
    sprite->velScale[1][0] = 1000.0f;
    sprite->visualCount = 2;
    sprite->visuals[0].material = kMaterialA;
    sprite->visuals[1].material = kMaterialB;
    sprite->lightingFrac = 0.5f;

    // 1: one-shot trail; three collinear vertices, two segments sharing the middle one.
    FxEditorElemDef *const trail = &editor->elems[1];
    InitElem(trail, "trail", 3, 0);
    trail->visualCount = 1;
    trail->visuals[0].material = kMaterialA;
    const float xs[] = {-1.0f, 0.0f, 1.0f};
    for (int i = 0; i < 3; ++i)
    {
        trail->trailDef.verts[i].pos[0] = xs[i];
        trail->trailDef.verts[i].texCoord = static_cast<float>(i);
    }
    trail->trailDef.vertCount = 3;
    const uint16_t inds[] = {0, 1, 1, 2};
    std::memcpy(trail->trailDef.inds, inds, sizeof(inds));
    trail->trailDef.indCount = 4;
    trail->trailSplitDist = 8;
    trail->trailRepeatDist = 16;
    trail->trailScrollTime = 0.25f;

    // 2: one-shot decal (mark visuals, two materials each).
    FxEditorElemDef *const decal = &editor->elems[2];
    InitElem(decal, "decal", 9, 0);
    decal->visualCount = 1;
    decal->markVisuals[0].materials[0] = kMaterialA;
    decal->markVisuals[0].materials[1] = kMaterialB;

    // 3: one-shot runner emitting the spark effect, whose element is copied in.
    FxEditorElemDef *const runner = &editor->elems[3];
    InitElem(runner, "runner", 10, 0x8000);
    runner->visualCount = 1; // a runner must name the effect it runs
    runner->visuals[0].effectDef.handle = emitted;
    runner->emission = emitted;

    const int errorsBefore = g_printedErrors;
    const FxEffectDef *const effect = FX_Convert(editor.get(), AllocEffect);
    Check(effect != nullptr, "convert succeeds");
    if (!effect)
        return;
    Check(g_printedErrors == errorsBefore, "no validation errors");
    Check(effect->elemDefCountLooping == 1 && effect->elemDefCountOneShot == 3 && effect->elemDefCountEmission == 1,
        "element counts");
    Check(std::strcmp(effect->name, editor->name) == 0 && Inside(effect->name, effect)
            && effect->name + std::strlen(effect->name) + 1 == reinterpret_cast<const char *>(effect) + effect->totalSize,
        "name is the blob's last bytes");
    Check(reinterpret_cast<const uint8_t *>(effect->elemDefs) == reinterpret_cast<const uint8_t *>(effect) + sizeof(FxEffectDef),
        "element defs follow the effect");
    Check((effect->flags & 1) != 0, "lighting flag from the sprite");

    const FxElemDef *const elemDefs = effect->elemDefs;
    for (int i = 0; i < 5; ++i)
        CheckElemPayload(&elemDefs[i], effect, "samples inside the blob");

    const FxElemDef &looping = elemDefs[0];
    Check(looping.elemType == 0 && looping.visualCount == 2 && Inside(looping.visuals.array, effect)
            && looping.visuals.array[0].material == kMaterialA && looping.visuals.array[1].material == kMaterialB,
        "sprite visual array");
    Check(looping.lightingFrac == 127, "sprite lighting fraction");
    Check(looping.atlas.rowIndexBits == 1 && looping.atlas.colIndexBits == 2 && looping.atlas.entryCount == 8,
        "sprite atlas from the material");
    Check(looping.spawn.looping.intervalMsec == 50 && looping.spawn.looping.count == 4, "sprite looping spawn");
    // Frame 1's scale is velScale[1], per millisecond of each sampling interval.
    const float perInterval = 1.0f / static_cast<float>(looping.velIntervalCount);
    const FxElemVelStateSample &firstVel = looping.velSamples[0];
    Check(std::fabs(firstVel.world.velocity.base[0] - perInterval) < 1e-6f && firstVel.world.velocity.base[1] == 0.0f
            && firstVel.local.velocity.base[0] == 0.0f,
        "sprite world velocity from frame 1");
    Check((looping.flags & 0x2000000) != 0 && (looping.flags & 0x1000000) == 0, "only the world velocity graph is used");

    const FxElemDef &trailDef = elemDefs[1];
    Check(trailDef.elemType == 3 && trailDef.visuals.instance.material == kMaterialA, "trail inline visual");
    const FxTrailDef *const outTrail = trailDef.trailDef;
    Check(outTrail && Inside(outTrail, effect) && Inside(outTrail->verts, effect) && Inside(outTrail->inds, effect),
        "trail payload inside the blob");
    if (outTrail)
    {
        Check(outTrail->vertCount == 3 && outTrail->indCount == 4, "trail shares its middle vertex");
        for (int i = 0; i < outTrail->indCount && i < 4; ++i)
            Check(outTrail->inds[i] == inds[i], "trail indices");
        Check(outTrail->splitDist == 8 && outTrail->repeatDist == 16 && outTrail->scrollTimeMsec == 250,
            "trail distances");
    }

    const FxElemDef &mark = elemDefs[2];
    Check(mark.elemType == 9 && Inside(mark.visuals.markArray, effect)
            && mark.visuals.markArray[0].materials[0] == kMaterialA
            && mark.visuals.markArray[0].materials[1] == kMaterialB,
        "decal mark visuals");
    Check(mark.trailDef == nullptr, "only the trail has a trail def");

    const FxElemDef &run = elemDefs[3];
    Check(run.elemType == 10 && run.effectEmitted.handle == emitted, "runner emits the spark effect");

    const FxElemDef &copied = elemDefs[4];
    const FxElemDef &source = emitted->elemDefs[0];
    Check(copied.elemType == 0 && copied.visualCount == 2 && Inside(copied.visuals.array, effect)
            && copied.visuals.array != source.visuals.array
            && copied.visuals.array[1].material == kMaterialB,
        "emitted visuals copied into the blob");
    Check(copied.velSamples != source.velSamples
            && std::memcmp(copied.velSamples, source.velSamples,
                   sizeof(FxElemVelStateSample) * (source.velIntervalCount + 1))
                == 0,
        "emitted velocity samples copied");

    std::free(const_cast<FxEffectDef *>(effect));
}

void TestRejects()
{
    auto editor = NewEditorEffect("fx/test_reject");
    editor->elemCount = 1;
    InitElem(&editor->elems[0], "trail", 3, 0);
    editor->elems[0].trailDef.vertCount = 2;
    editor->elems[0].trailDef.indCount = 3; // not index pairs
    editor->elems[0].trailSplitDist = 1;
    editor->elems[0].trailRepeatDist = 1;
    const int errorsBefore = g_printedErrors;
    Check(FX_Convert(editor.get(), AllocEffect) == nullptr && g_printedErrors == errorsBefore + 1,
        "odd trail index count is rejected");

    editor->elemCount = 33;
    Check(FX_Convert(editor.get(), AllocEffect) == nullptr, "more elements than the emit table holds");
}
} // namespace

// The hunk the curve allocator draws from.
uint8_t *__cdecl Hunk_AllocAlign(uint32_t size, int alignment, const char *, int)
{
    // aligned_alloc rejects an alignment below the platform's (macOS).
    const std::size_t align = std::max(static_cast<std::size_t>(alignment), alignof(std::max_align_t));
    void *const block = std::aligned_alloc(align, (static_cast<std::size_t>(size) + align - 1) / align * align);
    g_hunk.push_back(block);
    return static_cast<uint8_t *>(block);
}

void __cdecl Material_GetInfo(Material *handle, MaterialInfo *matInfo)
{
    if (handle != kMaterialA && handle != kMaterialB)
        std::abort();
    std::memset(static_cast<void *>(matInfo), 0, sizeof(*matInfo));
    matInfo->name = handle == kMaterialA ? "mtl_test_a" : "mtl_test_b";
    matInfo->textureAtlasRowCount = 2;
    matInfo->textureAtlasColumnCount = 4;
}

void Com_PrintError(int, const char *, ...)
{
    ++g_printedErrors;
}

// Reached only for model elements with physics; these have none.
PhysPreset *__cdecl FX_RegisterPhysPreset(const char *)
{
    std::abort();
}

int main()
{
    float flat1D[] = {0.0f, 1.0f, 1.0f, 1.0f};
    float flat3D[] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    g_flat1D = FxCurve_AllocAndCreateWithKeys(flat1D, 1, 2);
    g_flat3D = FxCurve_AllocAndCreateWithKeys(flat3D, 3, 2);

    const FxEffectDef *const emitted = ConvertEmitted();
    if (emitted)
    {
        TestConvert(emitted);
        std::free(const_cast<FxEffectDef *>(emitted));
    }
    TestRejects();

    for (void *const block : g_hunk)
        std::free(block);
    if (g_failures == 0)
        std::puts("fx convert contracts passed");
    return g_failures == 0 ? 0 : 1;
}

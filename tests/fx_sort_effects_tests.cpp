// fx_sort_effects_tests.cpp: the production effect sort (EffectsCore/fx_sort.cpp,
// FX_SortEffects) at native width. Each frame the active effect ring is
// reordered back to front from the camera, ties broken by element sort order
// for effects sharing an owner, with runners taking their parent's average.
// The sort takes the exclusive iterator through the production atomics and
// must release it. The effect-handle table and the FX lifecycle queries are
// stubbed; com_math_test_stubs.cpp's MyAssertHandler aborts on any engine
// assert.

#include <EffectsCore/fx_system.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
int g_failures = 0;
std::vector<FxEffect> g_effects;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

struct SortScene
{
    std::unique_ptr<FxSystem> system = std::make_unique<FxSystem>();

    SortScene()
    {
        system->isInitialized = true;
        system->camera.origin[0] = 5.0f;
    }

    // An effect x units in front of the camera along +x.
    void Add(const float x, const uint16_t owner, const uint32_t sortOrder, const FxEffectDef *const def = nullptr)
    {
        FxEffect effect{};
        effect.def = def;
        effect.owner = owner;
        effect.boltAndSortOrder.sortOrder = sortOrder;
        effect.frameNow.origin[0] = system->camera.origin[0] + x;
        effect.frameNow.quat[3] = 1.0f;
        const auto handle = static_cast<uint16_t>(g_effects.size());
        g_effects.push_back(effect);
        const int32_t slot = system->firstNewEffect;
        system->allEffectHandles[slot & (FX_EFFECT_LIMIT - 1)] = handle;
        system->firstNewEffect = slot + 1;
    }

    std::vector<uint16_t> Order() const
    {
        std::vector<uint16_t> order;
        for (int32_t i = system->firstActiveEffect; i != system->firstNewEffect; ++i)
            order.push_back(system->allEffectHandles[i & (FX_EFFECT_LIMIT - 1)]);
        return order;
    }
};

void TestBackToFront()
{
    g_effects.clear();
    SortScene scene;
    scene.Add(10.0f, 1, 0);
    scene.Add(40.0f, 2, 0);
    scene.Add(20.0f, 3, 0);
    scene.Add(30.0f, 4, 0);
    FX_SortEffects(scene.system.get());
    Check(scene.Order() == std::vector<uint16_t>{1, 3, 2, 0}, "effects sorted farthest first");
    Check(scene.system->iteratorCount == 0, "the exclusive iterator is released");

    FX_SortEffects(scene.system.get());
    Check(scene.Order() == std::vector<uint16_t>{1, 3, 2, 0}, "a sorted ring stays sorted");
}

void TestTiesAndWrap()
{
    // Start the ring near the top of the handle table so it wraps.
    g_effects.clear();
    SortScene scene;
    scene.system->firstActiveEffect = FX_EFFECT_LIMIT - 2;
    scene.system->firstNewEffect = FX_EFFECT_LIMIT - 2;
    scene.Add(50.0f, 7, 9); // same distance, same owner: lower sort order draws first
    scene.Add(50.0f, 7, 3);
    scene.Add(50.0f, 8, 1); // a different owner keeps its place in the tie
    scene.Add(60.0f, 9, 0);
    FX_SortEffects(scene.system.get());
    Check(scene.Order() == std::vector<uint16_t>{3, 1, 0, 2}, "ties broken by sort order within an owner");
}

void TestRunnerSortOrder()
{
    // A runner (sort order 255) takes the average of its parent's non-runner elements.
    FxElemDef elemDefs[3]{};
    elemDefs[0].sortOrder = 4;
    elemDefs[1].sortOrder = 8;
    elemDefs[2].elemType = 10; // runners do not count
    elemDefs[2].sortOrder = 200;
    FxEffectDef def{};
    def.elemDefs = elemDefs;
    def.elemDefCountOneShot = 3;

    g_effects.clear();
    SortScene scene;
    scene.Add(50.0f, 7, 7);
    scene.Add(50.0f, 7, 255, &def);
    FX_SortEffects(scene.system.get());
    Check(g_effects[1].boltAndSortOrder.sortOrder == 6, "runner sort order is its parent's average");
    Check(scene.Order() == std::vector<uint16_t>{1, 0}, "the runner sorts by its computed order");

    FxEffect lone{};
    lone.def = &def;
    def.elemDefCountOneShot = 0;
    Check(FX_CalcRunnerParentSortOrder(&lone) == 0, "no elements gives order 0");
}
} // namespace

// The FX lifecycle the sort consults: an initialized system with no archive.
FxEffect *__cdecl FX_EffectFromHandle(FxSystem *, uint16_t handle)
{
    if (handle >= g_effects.size())
        std::abort();
    return &g_effects[handle];
}

std::uint32_t __cdecl FX_GetCooperativeIteratorGeneration(const FxSystem *)
{
    return 1;
}

bool __cdecl FX_CurrentThreadOwnsCooperativeIterator(const FxSystem *)
{
    return false;
}

bool __cdecl FX_ThreadOwnsEffectKillExclusive(const FxSystem *) noexcept
{
    return false;
}

void __cdecl FX_WaitForArchiveGate(const FxSystem *)
{
}

bool __cdecl FX_ArchiveGateIsActive(const FxSystem *)
{
    return false;
}

void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error %s\n", fmt);
    std::abort();
}

int main()
{
    TestBackToFront();
    TestTiesAndWrap();
    TestRunnerSortOrder();
    if (g_failures == 0)
        std::puts("fx effect sort contracts passed");
    return g_failures == 0 ? 0 : 1;
}

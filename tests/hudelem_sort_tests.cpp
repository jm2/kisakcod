#include <bgame/bg_hudelem.h>
#include <cgame/cg_hudelem_sort.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace
{

constexpr std::size_t kProfileCapacity =
    kisak::cgame::ProfileHudElemCapacity<playerState_s_hud>();

#if defined(KISAK_MP) && !defined(KISAK_SP)
static_assert(kProfileCapacity == 62);
static_assert(std::extent_v<decltype(playerState_s_hud::current)> == 31);
static_assert(std::extent_v<decltype(playerState_s_hud::archival)> == 31);
static_assert(sizeof(hudelem_s) == 0xA0);
static_assert(offsetof(hudelem_s, sort) == 0x80);
#elif defined(KISAK_SP) && !defined(KISAK_MP)
static_assert(kProfileCapacity == 256);
static_assert(std::extent_v<decltype(playerState_s_hud::elem)> == 256);
static_assert(sizeof(hudelem_s) == 0xAC);
static_assert(offsetof(hudelem_s, sort) == 0x8C);
#else
#error "HUD element sort tests require exactly one game profile"
#endif

static_assert(sizeof(hudelem_s *) == sizeof(void *));

bool TestCapacityFailureIsAtomic()
{
    playerState_s_hud hud{};
#if defined(KISAK_MP)
    hud.current[0].type = HE_TYPE_TEXT;
#else
    hud.elem[0].type = HE_TYPE_TEXT;
#endif

    hudelem_s sentinel{};
    std::array<hudelem_s *, kProfileCapacity> output{};
    output.fill(&sentinel);
    const auto original = output;

    if (kisak::cgame::CollectActiveHudElems(
            output.data(), output.size() - 1, hud) != 0)
    {
        return false;
    }
    return output == original;
}

bool TestAppendFailureIsAtomic()
{
    std::array<hudelem_s, 2> source{};
    source[0].type = HE_TYPE_TEXT;
    source[1].type = HE_TYPE_VALUE;

    hudelem_s sentinel{};
    std::array<hudelem_s *, 3> output = {&sentinel, &sentinel, &sentinel};
    const auto original = output;
    std::size_t count = 2;
    if (kisak::cgame::AppendActiveHudElemPrefix(
            output.data(), output.size(), count, source.data(), source.size()))
    {
        return false;
    }
    return count == 2 && output == original;
}

bool TestFullProfileCapacity()
{
    playerState_s_hud hud{};
    std::array<hudelem_s *, kProfileCapacity> output{};
#if defined(KISAK_MP)
    for (hudelem_s &elem : hud.current)
        elem.type = HE_TYPE_TEXT;
    for (hudelem_s &elem : hud.archival)
        elem.type = HE_TYPE_TEXT;

    const std::size_t count = kisak::cgame::CollectActiveHudElems(
        output.data(), output.size(), hud);
    return count == output.size()
        && output.front() == &hud.current[0]
        && output[30] == &hud.current[30]
        && output[31] == &hud.archival[0]
        && output.back() == &hud.archival[30];
#else
    for (hudelem_s &elem : hud.elem)
        elem.type = HE_TYPE_TEXT;

    const std::size_t count = kisak::cgame::CollectActiveHudElems(
        output.data(), output.size(), hud);
    return count == output.size()
        && output.front() == &hud.elem[0]
        && output.back() == &hud.elem[255];
#endif
}

bool TestContiguousPrefixCollection()
{
    playerState_s_hud hud{};
    std::array<hudelem_s *, kProfileCapacity> output{};

#if defined(KISAK_MP)
    hud.current[0].type = HE_TYPE_TEXT;
    hud.current[1].type = HE_TYPE_VALUE;
    hud.current[3].type = HE_TYPE_MATERIAL;
    hud.archival[0].type = HE_TYPE_WAYPOINT;
    hud.archival[2].type = HE_TYPE_TEXT;

    const std::size_t count = kisak::cgame::CollectActiveHudElems(
        output.data(), output.size(), hud);
    return count == 3
        && output[0] == &hud.current[0]
        && output[1] == &hud.current[1]
        && output[2] == &hud.archival[0]
        && output[3] == nullptr;
#else
    hud.elem[0].type = HE_TYPE_TEXT;
    hud.elem[1].type = HE_TYPE_VALUE;
    hud.elem[3].type = HE_TYPE_MATERIAL;

    const std::size_t count = kisak::cgame::CollectActiveHudElems(
        output.data(), output.size(), hud);
    return count == 2
        && output[0] == &hud.elem[0]
        && output[1] == &hud.elem[1]
        && output[2] == nullptr;
#endif
}

bool TestNativePointerSort()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::array<hudelem_s, 5> storage{};
    storage[0].sort = nan;
    storage[1].sort = 5.0F;
    storage[2].sort = -2.0F;
    storage[3].sort = 1.0F;
    storage[4].sort = -0.0F;

    std::array<hudelem_s *, storage.size()> elems = {
        &storage[0], &storage[1], &storage[2], &storage[3], &storage[4]};
    std::array<std::uintptr_t, storage.size()> before{};
    for (std::size_t index = 0; index < elems.size(); ++index)
        before[index] = reinterpret_cast<std::uintptr_t>(elems[index]);

    if (!kisak::cgame::SortHudElems(elems.data(), elems.size()))
        return false;
    if (elems[0]->sort != -2.0F
        || elems[1]->sort != 0.0F
        || elems[2]->sort != 1.0F
        || elems[3]->sort != 5.0F
        || !std::isnan(elems[4]->sort))
    {
        return false;
    }

    std::array<std::uintptr_t, storage.size()> after{};
    for (std::size_t index = 0; index < elems.size(); ++index)
        after[index] = reinterpret_cast<std::uintptr_t>(elems[index]);
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    return before == after
        && kisak::cgame::SortHudElems<hudelem_s>(nullptr, 0)
        && !kisak::cgame::SortHudElems<hudelem_s>(nullptr, 1);
}

} // namespace

int main()
{
    if (!TestCapacityFailureIsAtomic())
    {
        std::fputs("HUD element capacity failure was not atomic\n", stderr);
        return 1;
    }
    if (!TestContiguousPrefixCollection())
    {
        std::fputs("HUD element contiguous-prefix collection failed\n", stderr);
        return 1;
    }
    if (!TestAppendFailureIsAtomic())
    {
        std::fputs("HUD element append failure was not atomic\n", stderr);
        return 1;
    }
    if (!TestFullProfileCapacity())
    {
        std::fputs("HUD element profile-capacity collection failed\n", stderr);
        return 1;
    }
    if (!TestNativePointerSort())
    {
        std::fputs("HUD element native-pointer sort failed\n", stderr);
        return 1;
    }
    return 0;
}

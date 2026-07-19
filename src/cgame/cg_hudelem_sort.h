#pragma once

#include <universal/sort_utils.h>

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace kisak::cgame
{

template <typename HudState>
inline constexpr std::size_t ProfileHudElemCapacity() noexcept
{
#if defined(KISAK_MP) && !defined(KISAK_SP)
    using Current = std::remove_reference_t<decltype(std::declval<HudState &>().current)>;
    using Archival = std::remove_reference_t<decltype(std::declval<HudState &>().archival)>;
    return std::extent_v<Current> + std::extent_v<Archival>;
#elif defined(KISAK_SP) && !defined(KISAK_MP)
    using Elements = std::remove_reference_t<decltype(std::declval<HudState &>().elem)>;
    return std::extent_v<Elements>;
#else
#error "HUD element sorting requires exactly one of KISAK_MP or KISAK_SP"
#endif
}

template <typename HudElem>
inline bool AppendActiveHudElemPrefix(
    HudElem **elems,
    std::size_t elemCapacity,
    std::size_t &elemCount,
    HudElem *source,
    std::size_t sourceCapacity) noexcept
{
    if (!elems || !source || elemCount > elemCapacity)
        return false;

    std::size_t activeCount = 0;
    while (activeCount < sourceCapacity && source[activeCount].type)
        ++activeCount;

    if (activeCount > elemCapacity - elemCount)
        return false;

    for (std::size_t index = 0; index < activeCount; ++index)
        elems[elemCount + index] = &source[index];
    elemCount += activeCount;
    return true;
}

template <typename HudElem, typename HudState>
inline std::size_t CollectActiveHudElems(
    HudElem **elems,
    std::size_t elemCapacity,
    HudState &hud) noexcept
{
    constexpr std::size_t profileCapacity = ProfileHudElemCapacity<HudState>();
    if (!elems || elemCapacity < profileCapacity)
        return 0;

    std::size_t elemCount = 0;
#if defined(KISAK_MP) && !defined(KISAK_SP)
    if (!AppendActiveHudElemPrefix(
            elems, elemCapacity, elemCount, hud.current, std::extent_v<decltype(hud.current)>)
        || !AppendActiveHudElemPrefix(
            elems, elemCapacity, elemCount, hud.archival, std::extent_v<decltype(hud.archival)>))
    {
        return 0;
    }
#elif defined(KISAK_SP) && !defined(KISAK_MP)
    if (!AppendActiveHudElemPrefix(
            elems, elemCapacity, elemCount, hud.elem, std::extent_v<decltype(hud.elem)>))
    {
        return 0;
    }
#endif
    return elemCount;
}

template <typename HudElem>
inline bool SortHudElems(HudElem **elems, std::size_t elemCount)
{
    if (!elems)
        return elemCount == 0;

    std::sort(elems, elems + elemCount, [](const HudElem *left, const HudElem *right) {
        if (left == right)
            return false;
        if (!left)
            return false;
        if (!right)
            return true;
        return kisak::sort::FloatLess(left->sort, right->sort);
    });
    return true;
}

} // namespace kisak::cgame

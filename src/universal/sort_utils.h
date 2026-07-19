#pragma once

#include <cmath>
#include <cstring>

namespace kisak::sort
{

// std::sort requires a strict weak ordering. Keep unordered floating-point
// values in one deterministic equivalence class after all ordered values.
inline int CompareFloatAscending(float left, float right) noexcept
{
    const bool leftIsNan = std::isnan(left);
    const bool rightIsNan = std::isnan(right);
    if (leftIsNan || rightIsNan)
    {
        if (leftIsNan == rightIsNan)
            return 0;
        return leftIsNan ? 1 : -1;
    }

    if (left < right)
        return -1;
    if (left > right)
        return 1;
    return 0;
}

inline bool FloatLess(float left, float right) noexcept
{
    return CompareFloatAscending(left, right) < 0;
}

// Null strings are not expected in the active sort ranges, but ordering them
// last keeps this predicate valid if a partially populated table reaches it.
inline bool CStringLess(const char *left, const char *right) noexcept
{
    if (left == right)
        return false;
    if (!left)
        return false;
    if (!right)
        return true;
    return std::strcmp(left, right) < 0;
}

} // namespace kisak::sort

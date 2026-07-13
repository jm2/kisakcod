#pragma once

#include <cstddef>

namespace physics::ode
{
constexpr std::size_t AlignUserGeomClassData(
    const std::size_t size,
    const std::size_t alignment) noexcept
{
    return (size + alignment - 1u) / alignment * alignment;
}

inline constexpr std::size_t kUserGeomClassDataAlignment =
    alignof(void *) > alignof(float) ? alignof(void *) : alignof(float);

// Brush user geoms store one native pointer-sized union followed by a
// three-float center of mass. Round the payload up to its native alignment so
// the complete BrushInfo object, including tail padding, fits on every target.
inline constexpr std::size_t kUserGeomClassDataBytes =
    AlignUserGeomClassData(
        sizeof(void *) + 3u * sizeof(float),
        kUserGeomClassDataAlignment);

template <typename T>
inline constexpr bool UserGeomClassDataMatches =
    sizeof(T) == kUserGeomClassDataBytes
    && alignof(T) == kUserGeomClassDataAlignment;
} // namespace physics::ode

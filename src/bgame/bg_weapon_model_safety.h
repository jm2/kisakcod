#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace bg::weapon_model
{
template <typename ModelPointer, std::size_t ModelCount>
[[nodiscard]] constexpr ModelPointer CheckedLookup(
    const ModelPointer (&models)[ModelCount],
    std::int32_t modelIndex) noexcept
{
    static_assert(std::is_pointer_v<ModelPointer>);
    static_assert(ModelCount > 0);

    if (modelIndex < 0
        || static_cast<std::size_t>(modelIndex) >= ModelCount)
    {
        return nullptr;
    }

    return models[static_cast<std::size_t>(modelIndex)];
}

template <typename ModelPointer, std::size_t ModelCount>
[[nodiscard]] constexpr std::uint8_t ResolveIndex(
    const ModelPointer (&models)[ModelCount],
    std::int32_t requestedIndex) noexcept
{
    static_assert(
        ModelCount
        <= static_cast<std::size_t>(
            (std::numeric_limits<std::uint8_t>::max)()) + 1);

    return CheckedLookup(models, requestedIndex)
        ? static_cast<std::uint8_t>(requestedIndex)
        : std::uint8_t{0};
}
} // namespace bg::weapon_model

#pragma once

#include "db_zone_slots.h"

#include <universal/info_string.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <type_traits>

namespace db::referenced_fastfile
{
namespace detail
{
constexpr bool TryAccumulateLength(
    const std::size_t amount,
    const std::size_t limit,
    std::size_t &total) noexcept
{
    if (total > limit || amount > limit - total)
        return false;

    total += amount;
    return true;
}
} // namespace detail

template <typename Integer>
bool FormatSignedDecimal(
    const Integer value,
    char *output,
    const std::size_t capacity) noexcept
{
    static_assert(std::is_integral_v<Integer>);
    static_assert(std::is_signed_v<Integer>);

    if (!output || capacity == 0)
        return false;

    const std::to_chars_result result =
        std::to_chars(output, output + capacity - 1, value, 10);
    if (result.ec != std::errc{})
        return false;

    *result.ptr = '\0';
    return true;
}

// Zone is intentionally structural: production supplies its native record while
// portable tests use a small fixture with only the fields exercised here.
template <typename Zone, std::size_t N, typename IsExcluded, typename Visit>
void ForEachReferencedFastFile(
    const Zone (&zones)[N],
    IsExcluded isExcluded,
    Visit visit)
{
    static_assert(N == db::zone_slots::kPhysicalZoneSlotCount);

    for (std::size_t slot = db::zone_slots::kFirstUsableZoneSlot;
        slot < N;
        ++slot)
    {
        const Zone &zone = zones[slot];
        if (zone.name[0] && !isExcluded(zone.name))
            visit(slot, zone);
    }
}

// The destination, zone names, and mod directory must not overlap. The complete
// result is sized before output is touched, so false leaves output unchanged.
template <typename Zone, std::size_t N, typename IsExcluded>
bool FormatReferencedFastFileNames(
    const Zone (&zones)[N],
    const char *modDirectory,
    IsExcluded isExcluded,
    char *output,
    const std::size_t capacity)
{
    if (!output || capacity == 0)
        return false;

    struct SelectedName
    {
        const char *name;
        std::size_t nameLength;
        bool modZone;
    };

    std::array<SelectedName, db::zone_slots::kUsableZoneSlotCount> selected{};
    std::size_t selectedCount = 0;
    std::size_t requiredLength = 0;
    const std::size_t outputLimit = capacity - 1;
    const std::size_t modDirectoryLength =
        modDirectory ? std::strlen(modDirectory) : 0;
    bool fits = true;

    ForEachReferencedFastFile(
        zones,
        isExcluded,
        [&](const std::size_t, const Zone &zone)
        {
            if (!fits)
                return;

            if (!info_string::IsSafeUnquotedPathTokenComponent(zone.name)
                || (zone.modZone
                    && (!modDirectory
                        || !*modDirectory
                        || !info_string::IsSafeUnquotedPathTokenComponent(
                            modDirectory))))
            {
                fits = false;
                return;
            }

            const std::size_t nameLength = std::strlen(zone.name);
            if (selectedCount >= selected.size()
                || (selectedCount != 0
                    && !detail::TryAccumulateLength(
                        1, outputLimit, requiredLength))
                || (zone.modZone
                    && (!detail::TryAccumulateLength(
                            modDirectoryLength, outputLimit, requiredLength)
                        || !detail::TryAccumulateLength(
                            1, outputLimit, requiredLength)))
                || !detail::TryAccumulateLength(
                    nameLength, outputLimit, requiredLength))
            {
                fits = false;
                return;
            }

            selected[selectedCount++] = {
                zone.name,
                nameLength,
                static_cast<bool>(zone.modZone),
            };
        });

    if (!fits)
        return false;

    char *cursor = output;
    const auto append = [&cursor](
        const char *const part,
        const std::size_t length) noexcept
    {
        std::memcpy(cursor, part, length);
        cursor += length;
    };
    for (std::size_t index = 0; index < selectedCount; ++index)
    {
        const SelectedName &entry = selected[index];
        if (index != 0)
            *cursor++ = ' ';
        if (entry.modZone)
        {
            append(modDirectory, modDirectoryLength);
            *cursor++ = '/';
        }
        append(entry.name, entry.nameLength);
    }
    *cursor = '\0';
    return true;
}
} // namespace db::referenced_fastfile

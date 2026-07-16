#pragma once

#include <cstddef>

namespace db::referenced_fastfile
{
inline constexpr std::size_t kDefaultZoneSlot = 0;
inline constexpr std::size_t kFirstFastFileZoneSlot = 1;
inline constexpr std::size_t kLiveFastFileZoneCount = 32;
inline constexpr std::size_t kZoneSlotCount =
    kFirstFastFileZoneSlot + kLiveFastFileZoneCount;

// Zone is intentionally structural: production supplies its native record while
// portable tests use a pointer-free fixture with only name and modZone members.
template <typename Zone, std::size_t N, typename IsExcluded, typename Visit>
void ForEachReferencedFastFile(
    const Zone (&zones)[N],
    IsExcluded isExcluded,
    Visit visit)
{
    static_assert(N == kZoneSlotCount);

    for (std::size_t slot = kFirstFastFileZoneSlot; slot < N; ++slot)
    {
        const Zone &zone = zones[slot];
        if (zone.name[0] && !isExcluded(zone.name))
            visit(slot, zone);
    }
}

// Emit each output fragment separately so production retains its bounded
// I_strncat behavior without importing registry or qcommon dependencies here.
template <typename Zone, std::size_t N, typename IsExcluded, typename Emit>
void EmitReferencedFastFileNames(
    const Zone (&zones)[N],
    const char *modDirectory,
    IsExcluded isExcluded,
    Emit emit)
{
    bool emitted = false;
    ForEachReferencedFastFile(
        zones,
        isExcluded,
        [&](const std::size_t, const Zone &zone)
        {
            if (emitted)
                emit(" ");
            if (zone.modZone)
            {
                emit(modDirectory);
                emit("/");
            }
            emit(zone.name);
            emitted = true;
        });
}
} // namespace db::referenced_fastfile

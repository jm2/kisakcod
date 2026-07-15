#include <EffectsCore/fx_archive_buffers_disk32.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fx::archive
{
namespace
{
template <std::size_t LIMIT>
bool ValidatePoolHeader(
    const std::int32_t firstFree,
    const std::int32_t activeCount) noexcept
{
    static_assert(LIMIT > 0);
    static_assert(
        LIMIT <= static_cast<std::size_t>(
                     (std::numeric_limits<std::int32_t>::max)()));

    if (activeCount < 0
        || static_cast<std::size_t>(activeCount) > LIMIT
        || (firstFree < -1)
        || (firstFree >= 0
            && static_cast<std::size_t>(firstFree) >= LIMIT))
    {
        return false;
    }

    const bool poolIsFull = static_cast<std::size_t>(activeCount) == LIMIT;
    return poolIsFull == (firstFree == -1);
}

template <typename SLOT_TYPE>
bool ReadFreeLink(
    const SLOT_TYPE &slot,
    std::int32_t *const outNextFree) noexcept
{
    static_assert(std::extent_v<decltype(SLOT_TYPE::bytes)> >= 4);
    if (!outNextFree)
        return false;

    const std::uint32_t raw =
        std::uint32_t{slot.bytes[0]}
        | (std::uint32_t{slot.bytes[1]} << 8u)
        | (std::uint32_t{slot.bytes[2]} << 16u)
        | (std::uint32_t{slot.bytes[3]} << 24u);
    if (raw == (std::numeric_limits<std::uint32_t>::max)())
    {
        *outNextFree = -1;
        return true;
    }
    if (raw > static_cast<std::uint32_t>(
                  (std::numeric_limits<std::int32_t>::max)()))
    {
        return false;
    }

    *outNextFree = static_cast<std::int32_t>(raw);
    return true;
}

template <typename SLOT_TYPE, std::size_t LIMIT>
bool RebuildPoolState(
    const SLOT_TYPE (&slots)[LIMIT],
    const std::int32_t firstFree,
    const std::int32_t activeCount,
    FxPoolAllocationState<LIMIT> *const outState) noexcept
{
    if (!outState || !ValidatePoolHeader<LIMIT>(firstFree, activeCount))
        return false;

    FxPoolAllocationState<LIMIT> rebuilt{};
    rebuilt.allocatedWords.fill(
        (std::numeric_limits<std::uint64_t>::max)());
    if constexpr (LIMIT % FxPoolAllocationState<LIMIT>::WORD_BITS != 0)
    {
        constexpr std::size_t usedBits =
            LIMIT % FxPoolAllocationState<LIMIT>::WORD_BITS;
        rebuilt.allocatedWords.back() =
            (std::uint64_t{1} << usedBits) - 1u;
    }
    rebuilt.allocatedCount = LIMIT;
    rebuilt.initialized = true;

    std::int32_t freeIndex = firstFree;
    std::size_t freeCount = 0;
    while (freeIndex != -1)
    {
        if (freeIndex < 0
            || static_cast<std::size_t>(freeIndex) >= LIMIT
            || freeCount >= LIMIT)
        {
            return false;
        }

        const std::size_t index = static_cast<std::size_t>(freeIndex);
        constexpr std::size_t wordBits =
            FxPoolAllocationState<LIMIT>::WORD_BITS;
        const std::uint64_t allocatedBit =
            std::uint64_t{1} << (index % wordBits);
        // index < LIMIT above proves the word index. Avoid std::array::operator[]
        // so hardened library assertion/report paths cannot enter this decoder.
        if ((rebuilt.allocatedWords.data()[index / wordBits]
             & allocatedBit)
            == 0)
        {
            return false;
        }

        std::int32_t nextFree = -1;
        if (!ReadFreeLink(slots[index], &nextFree)
            || nextFree == freeIndex)
        {
            return false;
        }

        FxPoolSetAllocationWordBit(
            rebuilt.allocatedWords.data(), index, false);
        --rebuilt.allocatedCount;
        ++freeCount;
        freeIndex = nextFree;
    }

    if (rebuilt.allocatedCount
        != static_cast<std::size_t>(activeCount))
    {
        return false;
    }

    *outState = rebuilt;
    return true;
}
} // namespace

bool TryRebuildFxSystemBuffersDisk32PoolStates(
    const FxSystemBuffersDisk32 &buffers,
    const FxSystemDisk32 &system,
    FxSystemBuffersDisk32PoolStates *const outStates) noexcept
{
    if (!outStates)
        return false;

    FxSystemBuffersDisk32PoolStates rebuilt{};
    if (!RebuildPoolState(
            buffers.elems,
            system.firstFreeElem,
            system.activeElemCount,
            &rebuilt.elems)
        || !RebuildPoolState(
            buffers.trails,
            system.firstFreeTrail,
            system.activeTrailCount,
            &rebuilt.trails)
        || !RebuildPoolState(
            buffers.trailElems,
            system.firstFreeTrailElem,
            system.activeTrailElemCount,
            &rebuilt.trailElems))
    {
        return false;
    }

    *outStates = rebuilt;
    return true;
}
} // namespace fx::archive

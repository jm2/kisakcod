#include <EffectsCore/fx_archive_buffers_disk32.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
namespace archive = fx::archive;

constexpr std::size_t BUFFER_BYTES = 0x47480;
constexpr std::size_t EFFECTS_OFFSET = 0x00000;
constexpr std::size_t ELEMS_OFFSET = 0x20000;
constexpr std::size_t TRAILS_OFFSET = 0x34000;
constexpr std::size_t TRAIL_ELEMS_OFFSET = 0x34400;
constexpr std::size_t VIS_STATE_OFFSET = 0x44400;
constexpr std::size_t DEFERRED_ELEMS_OFFSET = 0x46420;
constexpr std::size_t PAD_BUFFER_OFFSET = 0x47420;

constexpr std::size_t ELEM_BYTES = 0x28;
constexpr std::size_t TRAIL_BYTES = 0x08;
constexpr std::size_t TRAIL_ELEM_BYTES = 0x20;
constexpr std::size_t VIS_BLOCKER_BYTES = 0x10;
constexpr std::size_t VIS_STATE_BYTES = 0x1010;

constexpr std::size_t FIRST_FREE_ELEM_OFFSET = 0x184;
constexpr std::size_t FIRST_FREE_TRAIL_ELEM_OFFSET = 0x188;
constexpr std::size_t FIRST_FREE_TRAIL_OFFSET = 0x18C;
constexpr std::size_t ACTIVE_ELEM_COUNT_OFFSET = 0x194;
constexpr std::size_t ACTIVE_TRAIL_ELEM_COUNT_OFFSET = 0x198;
constexpr std::size_t ACTIVE_TRAIL_COUNT_OFFSET = 0x19C;

using BufferBytes = std::array<std::uint8_t, BUFFER_BYTES>;
using SystemBytes = std::array<std::uint8_t, sizeof(archive::FxSystemDisk32)>;
using PoolStates = archive::FxSystemBuffersDisk32PoolStates;

enum class PoolKind : std::uint8_t
{
    Elem,
    Trail,
    TrailElem,
};

struct PoolDescription final
{
    std::size_t offset;
    std::size_t stride;
    std::size_t limit;
    std::size_t firstFreeOffset;
    std::size_t activeCountOffset;
};

constexpr PoolDescription DescribePool(const PoolKind kind) noexcept
{
    switch (kind)
    {
    case PoolKind::Elem:
        return {ELEMS_OFFSET, ELEM_BYTES, MAX_ELEMS,
                FIRST_FREE_ELEM_OFFSET, ACTIVE_ELEM_COUNT_OFFSET};
    case PoolKind::Trail:
        return {TRAILS_OFFSET, TRAIL_BYTES, MAX_TRAILS,
                FIRST_FREE_TRAIL_OFFSET, ACTIVE_TRAIL_COUNT_OFFSET};
    case PoolKind::TrailElem:
        return {TRAIL_ELEMS_OFFSET, TRAIL_ELEM_BYTES, MAX_TRAIL_ELEMS,
                FIRST_FREE_TRAIL_ELEM_OFFSET,
                ACTIVE_TRAIL_ELEM_COUNT_OFFSET};
    }
    return {};
}

int failures = 0;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

template <std::size_t SIZE>
void StoreU8(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::uint8_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset < SIZE);
    if (bytes && offset < SIZE)
        (*bytes)[offset] = value;
}

template <std::size_t SIZE>
void StoreU16(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::uint16_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= 2u);
    if (!bytes || offset > SIZE || SIZE - offset < 2u)
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

template <std::size_t SIZE>
void StoreU32(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    CHECK(bytes != nullptr);
    CHECK(offset <= SIZE && SIZE - offset >= 4u);
    if (!bytes || offset > SIZE || SIZE - offset < 4u)
        return;
    (*bytes)[offset] = static_cast<std::uint8_t>(value);
    (*bytes)[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    (*bytes)[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    (*bytes)[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

template <std::size_t SIZE>
void StoreI32(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const std::int32_t value)
{
    StoreU32(bytes, offset, static_cast<std::uint32_t>(value));
}

template <std::size_t SIZE>
void StoreFloat(
    std::array<std::uint8_t, SIZE> *const bytes,
    const std::size_t offset,
    const float value)
{
    StoreU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

template <typename TYPE>
std::array<std::uint8_t, sizeof(TYPE)> ObjectBytes(const TYPE &object)
{
    std::array<std::uint8_t, sizeof(TYPE)> bytes{};
    std::memcpy(bytes.data(), &object, bytes.size());
    return bytes;
}

template <typename TYPE>
TYPE ObjectFromBytes(const std::uint8_t *const bytes)
{
    TYPE object{};
    CHECK(bytes != nullptr);
    if (bytes)
        std::memcpy(&object, bytes, sizeof(object));
    return object;
}

std::unique_ptr<BufferBytes> MakePatternBytes()
{
    auto bytes = std::make_unique<BufferBytes>();
    for (std::size_t index = 0; index < bytes->size(); ++index)
    {
        (*bytes)[index] = static_cast<std::uint8_t>(
            (index * 29u + 7u) & 0xFFu);
    }
    return bytes;
}

std::unique_ptr<archive::FxSystemBuffersDisk32> BufferFromBytes(
    const BufferBytes &bytes)
{
    auto buffer = std::make_unique<archive::FxSystemBuffersDisk32>();
    std::memcpy(buffer.get(), bytes.data(), bytes.size());
    return buffer;
}

void SetPoolHeader(
    SystemBytes *const bytes,
    const PoolKind kind,
    const std::int32_t firstFree,
    const std::int32_t activeCount)
{
    const PoolDescription pool = DescribePool(kind);
    StoreI32(bytes, pool.firstFreeOffset, firstFree);
    StoreI32(bytes, pool.activeCountOffset, activeCount);
}

SystemBytes MakeSystemBytes()
{
    SystemBytes bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::uint8_t>(
            (index * 43u + 0x5Du) & 0xFFu);
    }
    SetPoolHeader(
        &bytes, PoolKind::Elem, -1,
        static_cast<std::int32_t>(MAX_ELEMS));
    SetPoolHeader(
        &bytes, PoolKind::Trail, -1,
        static_cast<std::int32_t>(MAX_TRAILS));
    SetPoolHeader(
        &bytes, PoolKind::TrailElem, -1,
        static_cast<std::int32_t>(MAX_TRAIL_ELEMS));
    return bytes;
}

archive::FxSystemDisk32 SystemFromBytes(const SystemBytes &bytes)
{
    archive::FxSystemDisk32 system{};
    std::memcpy(&system, bytes.data(), bytes.size());
    return system;
}

void StorePoolLink(
    BufferBytes *const bytes,
    const PoolKind kind,
    const std::size_t index,
    const std::int32_t nextFree)
{
    const PoolDescription pool = DescribePool(kind);
    CHECK(index < pool.limit);
    if (index < pool.limit)
        StoreI32(bytes, pool.offset + index * pool.stride, nextFree);
}

void StoreRawPoolLink(
    BufferBytes *const bytes,
    const PoolKind kind,
    const std::size_t index,
    const std::uint32_t nextFree)
{
    const PoolDescription pool = DescribePool(kind);
    CHECK(index < pool.limit);
    if (index < pool.limit)
        StoreU32(bytes, pool.offset + index * pool.stride, nextFree);
}

void StoreChain(
    BufferBytes *const bytes,
    const PoolKind kind,
    const std::vector<std::size_t> &chain)
{
    const PoolDescription pool = DescribePool(kind);
    for (std::size_t position = 0; position < chain.size(); ++position)
    {
        CHECK(chain[position] < pool.limit);
        const std::int32_t next = position + 1u == chain.size()
            ? -1
            : static_cast<std::int32_t>(chain[position + 1u]);
        StorePoolLink(bytes, kind, chain[position], next);
    }
}

void SetChainHeader(
    SystemBytes *const system,
    const PoolKind kind,
    const std::vector<std::size_t> &chain)
{
    const PoolDescription pool = DescribePool(kind);
    const std::int32_t firstFree = chain.empty()
        ? -1
        : static_cast<std::int32_t>(chain.front());
    SetPoolHeader(
        system,
        kind,
        firstFree,
        static_cast<std::int32_t>(pool.limit - chain.size()));
}

PoolStates MakeSentinelStates()
{
    PoolStates states{};
    states.elems.allocatedWords.fill(UINT64_C(0xA5A5A5A5A5A5A5A5));
    states.elems.allocatedCount = 17;
    states.elems.initialized = false;
    states.trails.allocatedWords.fill(UINT64_C(0x5A5A5A5A5A5A5A5A));
    states.trails.allocatedCount = 23;
    states.trails.initialized = true;
    states.trailElems.allocatedWords.fill(UINT64_C(0xC33CC33CC33CC33C));
    states.trailElems.allocatedCount = 31;
    states.trailElems.initialized = false;
    return states;
}

bool IsAllocated(
    const PoolStates &states,
    const PoolKind kind,
    const std::size_t index)
{
    switch (kind)
    {
    case PoolKind::Elem:
        return FxPoolAllocationStateIsAllocated(states.elems, index);
    case PoolKind::Trail:
        return FxPoolAllocationStateIsAllocated(states.trails, index);
    case PoolKind::TrailElem:
        return FxPoolAllocationStateIsAllocated(states.trailElems, index);
    }
    return false;
}

std::size_t AllocatedCount(
    const PoolStates &states,
    const PoolKind kind)
{
    switch (kind)
    {
    case PoolKind::Elem:
        return states.elems.allocatedCount;
    case PoolKind::Trail:
        return states.trails.allocatedCount;
    case PoolKind::TrailElem:
        return states.trailElems.allocatedCount;
    }
    return 0;
}

bool IsInitialized(const PoolStates &states, const PoolKind kind)
{
    switch (kind)
    {
    case PoolKind::Elem:
        return states.elems.initialized;
    case PoolKind::Trail:
        return states.trails.initialized;
    case PoolKind::TrailElem:
        return states.trailElems.initialized;
    }
    return false;
}

bool Contains(
    const std::vector<std::size_t> &values,
    const std::size_t value)
{
    for (const std::size_t candidate : values)
    {
        if (candidate == value)
            return true;
    }
    return false;
}

void CheckPoolState(
    const PoolStates &states,
    const PoolKind kind,
    const std::vector<std::size_t> &freeSlots)
{
    const PoolDescription pool = DescribePool(kind);
    CHECK(IsInitialized(states, kind));
    CHECK(AllocatedCount(states, kind) == pool.limit - freeSlots.size());
    for (std::size_t index = 0; index < pool.limit; ++index)
        CHECK(IsAllocated(states, kind, index) != Contains(freeSlots, index));
}

void CheckSourceUnchanged(
    const archive::FxSystemBuffersDisk32 &source,
    const BufferBytes &expected)
{
    CHECK(std::memcmp(&source, expected.data(), expected.size()) == 0);
}

void CheckFailurePreservesOutputs(
    const BufferBytes &bytes,
    const SystemBytes &systemBytes)
{
    const auto source = BufferFromBytes(bytes);
    const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
    PoolStates output = MakeSentinelStates();
    const auto outputBefore = ObjectBytes(output);
    CHECK(!archive::TryRebuildFxSystemBuffersDisk32PoolStates(
        *source, system, &output));
    CHECK(ObjectBytes(output) == outputBefore);
    CHECK(ObjectBytes(system) == systemBytes);
    CheckSourceUnchanged(*source, bytes);
}

void TestHandAuthoredRecordsAndRawSlots()
{
    const auto bytes = MakePatternBytes();

    const std::size_t effect = EFFECTS_OFFSET + 19u * 0x80u;
    StoreU32(bytes.get(), effect + 0x00, UINT32_C(0x1234ABCD));
    StoreI32(bytes.get(), effect + 0x04, -3);
    StoreU16(bytes.get(), effect + 0x14, UINT16_C(0x2460));
    StoreFloat(bytes.get(), effect + 0x7C, 19.25f);

    const std::size_t elem = ELEMS_OFFSET + 37u * ELEM_BYTES;
    StoreU8(bytes.get(), elem + 0x00, UINT8_C(0x11));
    StoreU8(bytes.get(), elem + 0x01, UINT8_C(0x22));
    StoreU8(bytes.get(), elem + 0x02, UINT8_C(0x33));
    StoreU8(bytes.get(), elem + 0x03, UINT8_C(0x44));
    StoreU16(bytes.get(), elem + 0x04, UINT16_C(0x1357));
    StoreU16(bytes.get(), elem + 0x06, UINT16_C(0x2468));
    StoreI32(bytes.get(), elem + 0x08, -1234567);
    StoreFloat(bytes.get(), elem + 0x0C, 1.25f);
    StoreFloat(bytes.get(), elem + 0x10, -2.5f);
    StoreFloat(bytes.get(), elem + 0x14, 3.75f);
    for (std::size_t index = 0; index < 12; ++index)
        StoreU8(bytes.get(), elem + 0x18 + index,
                static_cast<std::uint8_t>(0x80u + index));
    for (std::size_t index = 0; index < 4; ++index)
        StoreU8(bytes.get(), elem + 0x24 + index,
                static_cast<std::uint8_t>(0xE0u + index));

    const std::size_t trail = TRAILS_OFFSET + 67u * TRAIL_BYTES;
    StoreU16(bytes.get(), trail + 0x00, UINT16_C(0x1234));
    StoreU16(bytes.get(), trail + 0x02, UINT16_C(0x5678));
    StoreU16(bytes.get(), trail + 0x04, UINT16_C(0x9ABC));
    StoreU8(bytes.get(), trail + 0x06, UINT8_C(0x5D));
    StoreU8(bytes.get(), trail + 0x07, UINT8_C(0xA6));

    const std::size_t trailElem =
        TRAIL_ELEMS_OFFSET + 1029u * TRAIL_ELEM_BYTES;
    StoreFloat(bytes.get(), trailElem + 0x00, 9.5f);
    StoreFloat(bytes.get(), trailElem + 0x04, -8.25f);
    StoreFloat(bytes.get(), trailElem + 0x08, 7.125f);
    StoreFloat(bytes.get(), trailElem + 0x0C, 6.75f);
    StoreI32(bytes.get(), trailElem + 0x10, -7654321);
    StoreU16(bytes.get(), trailElem + 0x14, UINT16_C(0xBEEF));
    StoreU16(
        bytes.get(), trailElem + 0x16,
        static_cast<std::uint16_t>(INT16_C(-300)));
    for (std::size_t index = 0; index < 6; ++index)
        StoreU8(bytes.get(), trailElem + 0x18 + index,
                static_cast<std::uint8_t>(0xF8u + index));
    StoreU8(bytes.get(), trailElem + 0x1E, UINT8_C(0x73));
    StoreU8(bytes.get(), trailElem + 0x1F, UINT8_C(0x91));

    const std::size_t blocker =
        VIS_STATE_OFFSET + VIS_STATE_BYTES + 255u * VIS_BLOCKER_BYTES;
    StoreFloat(bytes.get(), blocker + 0x00, 31.0f);
    StoreFloat(bytes.get(), blocker + 0x04, -32.0f);
    StoreFloat(bytes.get(), blocker + 0x08, 33.0f);
    StoreU16(bytes.get(), blocker + 0x0C, UINT16_C(0xCAFE));
    StoreU16(bytes.get(), blocker + 0x0E, UINT16_C(0xBABE));
    StoreI32(bytes.get(), VIS_STATE_OFFSET + VIS_STATE_BYTES + 0x1000, 256);
    StoreU32(bytes.get(), VIS_STATE_OFFSET + VIS_STATE_BYTES + 0x1004,
             UINT32_C(0x01234567));
    StoreU16(bytes.get(), DEFERRED_ELEMS_OFFSET + 2047u * 2u,
             UINT16_C(0xA55A));
    StoreU8(bytes.get(), PAD_BUFFER_OFFSET + 95u, UINT8_C(0xC7));

    const auto source = BufferFromBytes(*bytes);
    CheckSourceUnchanged(*source, *bytes);
    CHECK(source->effects[19].definitionKey.value == UINT32_C(0x1234ABCD));
    CHECK(source->effects[19].status == -3);
    CHECK(source->effects[19].owner == UINT16_C(0x2460));
    CHECK(source->effects[19].distanceTraveled == 19.25f);
    CHECK(source->elems[37].bytes[0] == UINT8_C(0x11));
    CHECK(source->elems[37].bytes[ELEM_BYTES - 1u] == UINT8_C(0xE3));
    CHECK(source->trails[67].bytes[0] == UINT8_C(0x34));
    CHECK(source->trails[67].bytes[TRAIL_BYTES - 1u] == UINT8_C(0xA6));
    CHECK(source->trailElems[1029].bytes[0] ==
          static_cast<std::uint8_t>(std::bit_cast<std::uint32_t>(9.5f)));
    CHECK(source->trailElems[1029].bytes[TRAIL_ELEM_BYTES - 1u]
          == UINT8_C(0x91));
    const archive::FxElemDisk32 elemRecord =
        ObjectFromBytes<archive::FxElemDisk32>(bytes->data() + elem);
    CHECK(elemRecord.defIndex == UINT8_C(0x11));
    CHECK(elemRecord.sequence == UINT8_C(0x22));
    CHECK(elemRecord.atRestFraction == UINT8_C(0x33));
    CHECK(elemRecord.emitResidual == UINT8_C(0x44));
    CHECK(elemRecord.nextElemHandleInEffect == UINT16_C(0x1357));
    CHECK(elemRecord.prevElemHandleInEffect == UINT16_C(0x2468));
    CHECK(elemRecord.msecBegin == -1234567);
    CHECK(elemRecord.baseVel[0] == 1.25f);
    CHECK(elemRecord.baseVel[1] == -2.5f);
    CHECK(elemRecord.baseVel[2] == 3.75f);
    CHECK(elemRecord.payload[0] == UINT8_C(0x80));
    CHECK(elemRecord.payload[11] == UINT8_C(0x8B));
    CHECK(elemRecord.value[0] == UINT8_C(0xE0));
    CHECK(elemRecord.value[3] == UINT8_C(0xE3));

    const archive::FxTrailDisk32 trailRecord =
        ObjectFromBytes<archive::FxTrailDisk32>(bytes->data() + trail);
    CHECK(trailRecord.nextTrailHandle == UINT16_C(0x1234));
    CHECK(trailRecord.firstElemHandle == UINT16_C(0x5678));
    CHECK(trailRecord.lastElemHandle == UINT16_C(0x9ABC));
    CHECK(trailRecord.defIndex == UINT8_C(0x5D));
    CHECK(trailRecord.sequence == UINT8_C(0xA6));

    const archive::FxTrailElemDisk32 trailElemRecord =
        ObjectFromBytes<archive::FxTrailElemDisk32>(
            bytes->data() + trailElem);
    CHECK(trailElemRecord.origin[0] == 9.5f);
    CHECK(trailElemRecord.origin[1] == -8.25f);
    CHECK(trailElemRecord.origin[2] == 7.125f);
    CHECK(trailElemRecord.spawnDist == 6.75f);
    CHECK(trailElemRecord.msecBegin == -7654321);
    CHECK(trailElemRecord.nextTrailElemHandle == UINT16_C(0xBEEF));
    CHECK(trailElemRecord.baseVelZ == INT16_C(-300));
    CHECK(trailElemRecord.basis[0][0] == -8);
    CHECK(trailElemRecord.basis[1][2] == -3);
    CHECK(trailElemRecord.sequence == UINT8_C(0x73));
    CHECK(trailElemRecord.unused == UINT8_C(0x91));

    const archive::FxVisBlockerDisk32 blockerRecord =
        ObjectFromBytes<archive::FxVisBlockerDisk32>(bytes->data() + blocker);
    CHECK(blockerRecord.origin[0] == 31.0f);
    CHECK(blockerRecord.origin[1] == -32.0f);
    CHECK(blockerRecord.origin[2] == 33.0f);
    CHECK(blockerRecord.radius == UINT16_C(0xCAFE));
    CHECK(blockerRecord.visibility == UINT16_C(0xBABE));
    CHECK(source->visState[1].blockerCount == 256);
    CHECK(source->visState[1].pad[0] == UINT32_C(0x01234567));
    CHECK(source->deferredElems[2047] == UINT16_C(0xA55A));
    CHECK(source->padBuffer[95] == UINT8_C(0xC7));
}

void TestValidEmptyMixedAndMaximumLists()
{
    {
        const auto bytes = MakePatternBytes();
        const SystemBytes systemBytes = MakeSystemBytes();
        const auto source = BufferFromBytes(*bytes);
        const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
        PoolStates states = MakeSentinelStates();
        CHECK(archive::TryRebuildFxSystemBuffersDisk32PoolStates(
            *source, system, &states));
        CHECK(ObjectBytes(system) == systemBytes);
        CheckPoolState(states, PoolKind::Elem, {});
        CheckPoolState(states, PoolKind::Trail, {});
        CheckPoolState(states, PoolKind::TrailElem, {});
        CheckSourceUnchanged(*source, *bytes);
    }

    {
        auto bytes = MakePatternBytes();
        SystemBytes systemBytes = MakeSystemBytes();
        const std::vector<std::size_t> elemFree{0, 17, 1024, 2047};
        const std::vector<std::size_t> trailFree{127, 64, 1};
        const std::vector<std::size_t> trailElemFree{11, 1299, 3, 2046, 700};
        StoreChain(bytes.get(), PoolKind::Elem, elemFree);
        StoreChain(bytes.get(), PoolKind::Trail, trailFree);
        StoreChain(bytes.get(), PoolKind::TrailElem, trailElemFree);
        SetChainHeader(&systemBytes, PoolKind::Elem, elemFree);
        SetChainHeader(&systemBytes, PoolKind::Trail, trailFree);
        SetChainHeader(&systemBytes, PoolKind::TrailElem, trailElemFree);
        const auto source = BufferFromBytes(*bytes);
        const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
        PoolStates states = MakeSentinelStates();
        CHECK(archive::TryRebuildFxSystemBuffersDisk32PoolStates(
            *source, system, &states));
        CHECK(ObjectBytes(system) == systemBytes);
        CheckPoolState(states, PoolKind::Elem, elemFree);
        CheckPoolState(states, PoolKind::Trail, trailFree);
        CheckPoolState(states, PoolKind::TrailElem, trailElemFree);
        CheckSourceUnchanged(*source, *bytes);
    }

    {
        auto bytes = MakePatternBytes();
        SystemBytes systemBytes = MakeSystemBytes();
        for (const PoolKind kind :
             {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
        {
            const PoolDescription pool = DescribePool(kind);
            std::vector<std::size_t> everySlot;
            everySlot.reserve(pool.limit);
            for (std::size_t index = 0; index < pool.limit; ++index)
                everySlot.push_back((index * 37u + 11u) % pool.limit);
            // 37 is coprime to all three power-of-two capacities.
            StoreChain(bytes.get(), kind, everySlot);
            SetChainHeader(&systemBytes, kind, everySlot);
        }
        const auto source = BufferFromBytes(*bytes);
        const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
        PoolStates states = MakeSentinelStates();
        CHECK(archive::TryRebuildFxSystemBuffersDisk32PoolStates(
            *source, system, &states));
        CHECK(ObjectBytes(system) == systemBytes);
        for (const PoolKind kind :
             {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
        {
            const PoolDescription pool = DescribePool(kind);
            CHECK(IsInitialized(states, kind));
            CHECK(AllocatedCount(states, kind) == 0);
            for (std::size_t index = 0; index < pool.limit; ++index)
                CHECK(!IsAllocated(states, kind, index));
        }
        CheckSourceUnchanged(*source, *bytes);
    }
}

void TestFirstMiddleAndLastHeads()
{
    for (const PoolKind kind :
         {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
    {
        const PoolDescription pool = DescribePool(kind);
        const std::array<std::size_t, 3> heads{
            0u, pool.limit / 2u, pool.limit - 1u};
        for (const std::size_t head : heads)
        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            const std::vector<std::size_t> freeSlots{head};
            StoreChain(bytes.get(), kind, freeSlots);
            SetChainHeader(&systemBytes, kind, freeSlots);
            const auto source = BufferFromBytes(*bytes);
            const archive::FxSystemDisk32 system =
                SystemFromBytes(systemBytes);
            PoolStates states{};
            CHECK(archive::TryRebuildFxSystemBuffersDisk32PoolStates(
                *source, system, &states));
            CHECK(ObjectBytes(system) == systemBytes);
            CheckPoolState(states, kind, freeSlots);
            CheckSourceUnchanged(*source, *bytes);
        }
    }
}

void TestMalformedHeadsLinksCyclesAndCounts()
{
    for (const PoolKind kind :
         {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
    {
        const PoolDescription pool = DescribePool(kind);
        const std::int32_t limit = static_cast<std::int32_t>(pool.limit);

        for (const std::int32_t badHead :
             std::array<std::int32_t, 3>{-2, INT32_MIN, limit})
        {
            const auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            SetPoolHeader(&systemBytes, kind, badHead, limit - 1);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }

        {
            const auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            SetPoolHeader(&systemBytes, kind, -1, limit - 1);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
        {
            const auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            SetPoolHeader(&systemBytes, kind, 0, limit);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
        for (const std::int32_t badCount :
             std::array<std::int32_t, 2>{-1, limit + 1})
        {
            const auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            SetPoolHeader(&systemBytes, kind, -1, badCount);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }

        for (const std::uint32_t badLink :
             std::array<std::uint32_t, 4>{
                 UINT32_C(0xFFFFFFFE), UINT32_C(0x80000000),
                 static_cast<std::uint32_t>(pool.limit),
                 UINT32_C(0x7FFFFFFF)})
        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            StoreRawPoolLink(bytes.get(), kind, 0, badLink);
            SetPoolHeader(&systemBytes, kind, 0, limit - 1);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }

        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            StorePoolLink(bytes.get(), kind, 0, 0);
            SetPoolHeader(&systemBytes, kind, 0, limit - 1);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            StorePoolLink(bytes.get(), kind, 0, 1);
            StorePoolLink(bytes.get(), kind, 1, 0);
            SetPoolHeader(&systemBytes, kind, 0, limit - 2);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            const std::size_t middle = pool.limit / 2u;
            const std::size_t last = pool.limit - 1u;
            StorePoolLink(bytes.get(), kind, 0,
                          static_cast<std::int32_t>(middle));
            StorePoolLink(bytes.get(), kind, middle,
                          static_cast<std::int32_t>(last));
            StorePoolLink(bytes.get(), kind, last,
                          static_cast<std::int32_t>(middle));
            SetPoolHeader(&systemBytes, kind, 0, limit - 3);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            for (std::size_t index = 0; index < pool.limit; ++index)
            {
                StorePoolLink(
                    bytes.get(), kind, index,
                    static_cast<std::int32_t>(
                        index + 1u == pool.limit ? 0u : index + 1u));
            }
            SetPoolHeader(&systemBytes, kind, 0, 0);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }

        {
            auto bytes = MakePatternBytes();
            SystemBytes systemBytes = MakeSystemBytes();
            const std::vector<std::size_t> chain{0, pool.limit - 1u};
            StoreChain(bytes.get(), kind, chain);
            SetPoolHeader(&systemBytes, kind, 0, limit - 1);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
            SetPoolHeader(&systemBytes, kind, 0, limit - 3);
            CheckFailurePreservesOutputs(*bytes, systemBytes);
        }
    }
}

void TestIgnoredBytesAndCompositeTransaction()
{
    {
        auto bytes = MakePatternBytes();
        SystemBytes systemBytes = MakeSystemBytes();
        const std::vector<std::size_t> elemFree{MAX_ELEMS - 1u};
        const std::vector<std::size_t> trailFree{MAX_TRAILS / 2u};
        const std::vector<std::size_t> trailElemFree{0u};
        StoreChain(bytes.get(), PoolKind::Elem, elemFree);
        StoreChain(bytes.get(), PoolKind::Trail, trailFree);
        StoreChain(bytes.get(), PoolKind::TrailElem, trailElemFree);
        SetChainHeader(&systemBytes, PoolKind::Elem, elemFree);
        SetChainHeader(&systemBytes, PoolKind::Trail, trailFree);
        SetChainHeader(&systemBytes, PoolKind::TrailElem, trailElemFree);

        // Poison the first words of allocated slots and every unrelated top-
        // level span. Only visited free-slot first words and six system scalar
        // fields are part of this helper's contract.
        StoreRawPoolLink(bytes.get(), PoolKind::Elem, 0,
                         UINT32_C(0x80000000));
        StoreRawPoolLink(bytes.get(), PoolKind::Trail, 0,
                         UINT32_C(0xFFFFFFFE));
        StoreRawPoolLink(bytes.get(), PoolKind::TrailElem, 1,
                         UINT32_C(0x7FFFFFFF));
        StoreU32(bytes.get(), EFFECTS_OFFSET, UINT32_C(0xFFFFFFFF));
        StoreU32(bytes.get(), VIS_STATE_OFFSET + 0x1000,
                 UINT32_C(0xFFFFFFFF));
        StoreU16(bytes.get(), DEFERRED_ELEMS_OFFSET, UINT16_C(0xFFFF));
        StoreU8(bytes.get(), PAD_BUFFER_OFFSET, UINT8_C(0xFF));

        const auto source = BufferFromBytes(*bytes);
        const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
        PoolStates states{};
        CHECK(archive::TryRebuildFxSystemBuffersDisk32PoolStates(
            *source, system, &states));
        CHECK(ObjectBytes(system) == systemBytes);
        CheckPoolState(states, PoolKind::Elem, elemFree);
        CheckPoolState(states, PoolKind::Trail, trailFree);
        CheckPoolState(states, PoolKind::TrailElem, trailElemFree);
        CheckSourceUnchanged(*source, *bytes);
    }

    for (const PoolKind corruptKind :
         {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
    {
        auto bytes = MakePatternBytes();
        SystemBytes systemBytes = MakeSystemBytes();
        for (const PoolKind kind :
             {PoolKind::Elem, PoolKind::Trail, PoolKind::TrailElem})
        {
            const std::vector<std::size_t> chain{0, 1};
            StoreChain(bytes.get(), kind, chain);
            SetChainHeader(&systemBytes, kind, chain);
        }
        StorePoolLink(bytes.get(), corruptKind, 1, 0);
        CheckFailurePreservesOutputs(*bytes, systemBytes);
    }
}

void TestNullOutput()
{
    const auto bytes = MakePatternBytes();
    const SystemBytes systemBytes = MakeSystemBytes();
    const auto source = BufferFromBytes(*bytes);
    const archive::FxSystemDisk32 system = SystemFromBytes(systemBytes);
    CHECK(!archive::TryRebuildFxSystemBuffersDisk32PoolStates(
        *source, system, nullptr));
    CHECK(ObjectBytes(system) == systemBytes);
    CheckSourceUnchanged(*source, *bytes);
}

void TestConditionalX86RawLayoutOracle()
{
#if KISAK_PTR_BITS == 32
    static_assert(sizeof(FxSystemBuffers) == BUFFER_BYTES);
    static_assert(offsetof(FxSystemBuffers, effects) == EFFECTS_OFFSET);
    static_assert(offsetof(FxSystemBuffers, elems) == ELEMS_OFFSET);
    static_assert(offsetof(FxSystemBuffers, trails) == TRAILS_OFFSET);
    static_assert(offsetof(FxSystemBuffers, trailElems) == TRAIL_ELEMS_OFFSET);
    static_assert(offsetof(FxSystemBuffers, visState) == VIS_STATE_OFFSET);
    static_assert(
        offsetof(FxSystemBuffers, deferredElems) == DEFERRED_ELEMS_OFFSET);
    static_assert(offsetof(FxSystemBuffers, padBuffer) == PAD_BUFFER_OFFSET);

    auto native = std::make_unique<FxSystemBuffers>();
    auto diskBytes = std::make_unique<BufferBytes>();

    FxEffect &nativeEffect = native->effects[23];
    nativeEffect.status = -2;
    nativeEffect.firstElemHandle[0] = UINT16_C(0x0010);
    nativeEffect.firstElemHandle[1] = UINT16_C(0x0020);
    nativeEffect.firstElemHandle[2] = UINT16_C(0x0030);
    nativeEffect.firstSortedElemHandle = UINT16_C(0x0040);
    nativeEffect.firstTrailHandle = UINT16_C(0x0050);
    nativeEffect.randomSeed = UINT16_C(0x0060);
    nativeEffect.owner = UINT16_C(0x0070);
    nativeEffect.packedLighting = UINT16_C(0x0080);
    nativeEffect.boltAndSortOrder.dobjHandle = UINT32_C(0x123);
    nativeEffect.boltAndSortOrder.temporalBits = 1;
    nativeEffect.boltAndSortOrder.boneIndex = UINT32_C(0x345);
    nativeEffect.boltAndSortOrder.sortOrder = UINT32_C(0xA7);
    nativeEffect.frameCount = 123;
    nativeEffect.msecBegin = -456;
    nativeEffect.msecLastUpdate = 789;
    nativeEffect.frameAtSpawn.quat[0] = 1.25f;
    nativeEffect.frameNow.origin[1] = -2.5f;
    nativeEffect.framePrev.quat[3] = 3.75f;
    nativeEffect.distanceTraveled = 44.5f;
    const std::size_t effect = EFFECTS_OFFSET + 23u * 0x80u;
    StoreI32(diskBytes.get(), effect + 0x04, -2);
    StoreU16(diskBytes.get(), effect + 0x08, UINT16_C(0x0010));
    StoreU16(diskBytes.get(), effect + 0x0A, UINT16_C(0x0020));
    StoreU16(diskBytes.get(), effect + 0x0C, UINT16_C(0x0030));
    StoreU16(diskBytes.get(), effect + 0x0E, UINT16_C(0x0040));
    StoreU16(diskBytes.get(), effect + 0x10, UINT16_C(0x0050));
    StoreU16(diskBytes.get(), effect + 0x12, UINT16_C(0x0060));
    StoreU16(diskBytes.get(), effect + 0x14, UINT16_C(0x0070));
    StoreU16(diskBytes.get(), effect + 0x16, UINT16_C(0x0080));
    StoreU32(
        diskBytes.get(), effect + 0x18,
        UINT32_C(0x123) | (UINT32_C(1) << 12u)
            | (UINT32_C(0x345) << 13u) | (UINT32_C(0xA7) << 24u));
    StoreI32(diskBytes.get(), effect + 0x1C, 123);
    StoreI32(diskBytes.get(), effect + 0x20, -456);
    StoreI32(diskBytes.get(), effect + 0x24, 789);
    StoreFloat(diskBytes.get(), effect + 0x28, 1.25f);
    StoreFloat(diskBytes.get(), effect + 0x44 + 0x14, -2.5f);
    StoreFloat(diskBytes.get(), effect + 0x60 + 0x0C, 3.75f);
    StoreFloat(diskBytes.get(), effect + 0x7C, 44.5f);

    FxElem nativeElem{};
    nativeElem.defIndex = UINT8_C(7);
    nativeElem.sequence = UINT8_C(19);
    nativeElem.atRestFraction = UINT8_C(31);
    nativeElem.emitResidual = UINT8_C(43);
    nativeElem.nextElemHandleInEffect = UINT16_C(0x1350);
    nativeElem.prevElemHandleInEffect = UINT16_C(0x2460);
    nativeElem.msecBegin = -34567;
    nativeElem.baseVel[0] = 1.5f;
    nativeElem.baseVel[1] = -2.25f;
    nativeElem.baseVel[2] = 3.125f;
    nativeElem.physObjId = 0x12345678;
    nativeElem.u.lightingHandle = UINT16_C(0xABCD);
    std::construct_at(std::addressof(native->elems[5].item), nativeElem);
    const std::size_t elem = ELEMS_OFFSET + 5u * ELEM_BYTES;
    StoreU8(diskBytes.get(), elem + 0x00, UINT8_C(7));
    StoreU8(diskBytes.get(), elem + 0x01, UINT8_C(19));
    StoreU8(diskBytes.get(), elem + 0x02, UINT8_C(31));
    StoreU8(diskBytes.get(), elem + 0x03, UINT8_C(43));
    StoreU16(diskBytes.get(), elem + 0x04, UINT16_C(0x1350));
    StoreU16(diskBytes.get(), elem + 0x06, UINT16_C(0x2460));
    StoreI32(diskBytes.get(), elem + 0x08, -34567);
    StoreFloat(diskBytes.get(), elem + 0x0C, 1.5f);
    StoreFloat(diskBytes.get(), elem + 0x10, -2.25f);
    StoreFloat(diskBytes.get(), elem + 0x14, 3.125f);
    StoreI32(diskBytes.get(), elem + 0x18, 0x12345678);
    StoreU16(diskBytes.get(), elem + 0x24, UINT16_C(0xABCD));

    FxTrail nativeTrail{};
    nativeTrail.nextTrailHandle = UINT16_C(0x0100);
    nativeTrail.firstElemHandle = UINT16_C(0x0200);
    nativeTrail.lastElemHandle = UINT16_C(0x0300);
    nativeTrail.defIndex = UINT8_C(13);
    nativeTrail.sequence = UINT8_C(29);
    std::construct_at(std::addressof(native->trails[3].item), nativeTrail);
    const std::size_t trail = TRAILS_OFFSET + 3u * TRAIL_BYTES;
    StoreU16(diskBytes.get(), trail + 0x00, UINT16_C(0x0100));
    StoreU16(diskBytes.get(), trail + 0x02, UINT16_C(0x0200));
    StoreU16(diskBytes.get(), trail + 0x04, UINT16_C(0x0300));
    StoreU8(diskBytes.get(), trail + 0x06, UINT8_C(13));
    StoreU8(diskBytes.get(), trail + 0x07, UINT8_C(29));

    FxTrailElem nativeTrailElem{};
    nativeTrailElem.origin[0] = 4.5f;
    nativeTrailElem.origin[1] = -5.25f;
    nativeTrailElem.origin[2] = 6.125f;
    nativeTrailElem.spawnDist = 7.75f;
    nativeTrailElem.msecBegin = -45678;
    nativeTrailElem.nextTrailElemHandle = UINT16_C(0x3450);
    nativeTrailElem.baseVelZ = -1234;
    nativeTrailElem.basis[0][0] = -3;
    nativeTrailElem.basis[0][1] = -2;
    nativeTrailElem.basis[0][2] = -1;
    nativeTrailElem.basis[1][0] = 1;
    nativeTrailElem.basis[1][1] = 2;
    nativeTrailElem.basis[1][2] = 3;
    nativeTrailElem.sequence = UINT8_C(41);
    nativeTrailElem.unused = UINT8_C(53);
    std::construct_at(
        std::addressof(native->trailElems[7].item), nativeTrailElem);
    const std::size_t trailElem =
        TRAIL_ELEMS_OFFSET + 7u * TRAIL_ELEM_BYTES;
    StoreFloat(diskBytes.get(), trailElem + 0x00, 4.5f);
    StoreFloat(diskBytes.get(), trailElem + 0x04, -5.25f);
    StoreFloat(diskBytes.get(), trailElem + 0x08, 6.125f);
    StoreFloat(diskBytes.get(), trailElem + 0x0C, 7.75f);
    StoreI32(diskBytes.get(), trailElem + 0x10, -45678);
    StoreU16(diskBytes.get(), trailElem + 0x14, UINT16_C(0x3450));
    StoreU16(diskBytes.get(), trailElem + 0x16,
             static_cast<std::uint16_t>(INT16_C(-1234)));
    const std::array<std::int8_t, 6> basis{-3, -2, -1, 1, 2, 3};
    for (std::size_t index = 0; index < basis.size(); ++index)
    {
        StoreU8(diskBytes.get(), trailElem + 0x18 + index,
                static_cast<std::uint8_t>(basis[index]));
    }
    StoreU8(diskBytes.get(), trailElem + 0x1E, UINT8_C(41));
    StoreU8(diskBytes.get(), trailElem + 0x1F, UINT8_C(53));

    std::construct_at(std::addressof(native->elems[0].nextFree), -1);
    std::construct_at(std::addressof(native->trails[0].nextFree), -1);
    std::construct_at(std::addressof(native->trailElems[0].nextFree), -1);
    StoreI32(diskBytes.get(), ELEMS_OFFSET, -1);
    StoreI32(diskBytes.get(), TRAILS_OFFSET, -1);
    StoreI32(diskBytes.get(), TRAIL_ELEMS_OFFSET, -1);

    native->visState[1].blocker[255].origin[0] = 10.0f;
    native->visState[1].blocker[255].origin[1] = -11.0f;
    native->visState[1].blocker[255].origin[2] = 12.0f;
    native->visState[1].blocker[255].radius = UINT16_C(0x1234);
    native->visState[1].blocker[255].visibility = UINT16_C(0x5678);
    native->visState[1].blockerCount = 256;
    const std::size_t visBlocker =
        VIS_STATE_OFFSET + VIS_STATE_BYTES + 255u * VIS_BLOCKER_BYTES;
    StoreFloat(diskBytes.get(), visBlocker + 0x00, 10.0f);
    StoreFloat(diskBytes.get(), visBlocker + 0x04, -11.0f);
    StoreFloat(diskBytes.get(), visBlocker + 0x08, 12.0f);
    StoreU16(diskBytes.get(), visBlocker + 0x0C, UINT16_C(0x1234));
    StoreU16(diskBytes.get(), visBlocker + 0x0E, UINT16_C(0x5678));
    StoreI32(diskBytes.get(), VIS_STATE_OFFSET + VIS_STATE_BYTES + 0x1000,
             256);
    native->deferredElems[2047] = UINT16_C(0xA55A);
    native->padBuffer[95] = UINT8_C(0xC7);
    StoreU16(diskBytes.get(), DEFERRED_ELEMS_OFFSET + 2047u * 2u,
             UINT16_C(0xA55A));
    StoreU8(diskBytes.get(), PAD_BUFFER_OFFSET + 95u, UINT8_C(0xC7));

    CHECK(std::memcmp(native.get(), diskBytes->data(), BUFFER_BYTES) == 0);
#else
    static_assert(sizeof(FxSystemBuffers) == 0x49480);
    static_assert(offsetof(FxSystemBuffers, effects) == 0x00000);
    static_assert(offsetof(FxSystemBuffers, elems) == 0x22000);
    static_assert(offsetof(FxSystemBuffers, trails) == 0x36000);
    static_assert(offsetof(FxSystemBuffers, trailElems) == 0x36400);
    static_assert(offsetof(FxSystemBuffers, visState) == 0x46400);
    static_assert(offsetof(FxSystemBuffers, deferredElems) == 0x48420);
    static_assert(offsetof(FxSystemBuffers, padBuffer) == 0x49420);
#endif
}

void CheckStaticLayouts()
{
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(float) == 4);
    static_assert(std::numeric_limits<float>::is_iec559);

    static_assert(sizeof(archive::FxElemDisk32) == ELEM_BYTES);
    static_assert(alignof(archive::FxElemDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxElemDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxElemDisk32>);
    static_assert(offsetof(archive::FxElemDisk32, defIndex) == 0x00);
    static_assert(offsetof(archive::FxElemDisk32, sequence) == 0x01);
    static_assert(offsetof(archive::FxElemDisk32, atRestFraction) == 0x02);
    static_assert(offsetof(archive::FxElemDisk32, emitResidual) == 0x03);
    static_assert(
        offsetof(archive::FxElemDisk32, nextElemHandleInEffect) == 0x04);
    static_assert(
        offsetof(archive::FxElemDisk32, prevElemHandleInEffect) == 0x06);
    static_assert(offsetof(archive::FxElemDisk32, msecBegin) == 0x08);
    static_assert(offsetof(archive::FxElemDisk32, baseVel) == 0x0C);
    static_assert(offsetof(archive::FxElemDisk32, payload) == 0x18);
    static_assert(offsetof(archive::FxElemDisk32, value) == 0x24);
    static_assert(std::is_same_v<
        decltype(std::declval<archive::FxElemDisk32>().defIndex),
        std::uint8_t>);
    static_assert(std::is_same_v<
        std::remove_extent_t<decltype(
            std::declval<archive::FxElemDisk32>().payload)>,
        std::uint8_t>);

    static_assert(sizeof(archive::FxTrailDisk32) == TRAIL_BYTES);
    static_assert(alignof(archive::FxTrailDisk32) == 2);
    static_assert(std::is_standard_layout_v<archive::FxTrailDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxTrailDisk32>);
    static_assert(offsetof(archive::FxTrailDisk32, nextTrailHandle) == 0x00);
    static_assert(offsetof(archive::FxTrailDisk32, firstElemHandle) == 0x02);
    static_assert(offsetof(archive::FxTrailDisk32, lastElemHandle) == 0x04);
    static_assert(offsetof(archive::FxTrailDisk32, defIndex) == 0x06);
    static_assert(offsetof(archive::FxTrailDisk32, sequence) == 0x07);

    static_assert(sizeof(archive::FxTrailElemDisk32) == TRAIL_ELEM_BYTES);
    static_assert(alignof(archive::FxTrailElemDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxTrailElemDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxTrailElemDisk32>);
    static_assert(offsetof(archive::FxTrailElemDisk32, origin) == 0x00);
    static_assert(offsetof(archive::FxTrailElemDisk32, spawnDist) == 0x0C);
    static_assert(offsetof(archive::FxTrailElemDisk32, msecBegin) == 0x10);
    static_assert(
        offsetof(archive::FxTrailElemDisk32, nextTrailElemHandle) == 0x14);
    static_assert(offsetof(archive::FxTrailElemDisk32, baseVelZ) == 0x16);
    static_assert(offsetof(archive::FxTrailElemDisk32, basis) == 0x18);
    static_assert(offsetof(archive::FxTrailElemDisk32, sequence) == 0x1E);
    static_assert(offsetof(archive::FxTrailElemDisk32, unused) == 0x1F);

    static_assert(sizeof(archive::FxVisBlockerDisk32) == VIS_BLOCKER_BYTES);
    static_assert(alignof(archive::FxVisBlockerDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxVisBlockerDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxVisBlockerDisk32>);
    static_assert(offsetof(archive::FxVisBlockerDisk32, origin) == 0x00);
    static_assert(offsetof(archive::FxVisBlockerDisk32, radius) == 0x0C);
    static_assert(offsetof(archive::FxVisBlockerDisk32, visibility) == 0x0E);

    static_assert(sizeof(archive::FxVisStateDisk32) == VIS_STATE_BYTES);
    static_assert(alignof(archive::FxVisStateDisk32) == 4);
    static_assert(std::is_standard_layout_v<archive::FxVisStateDisk32>);
    static_assert(std::is_trivially_copyable_v<archive::FxVisStateDisk32>);
    static_assert(offsetof(archive::FxVisStateDisk32, blocker) == 0x0000);
    static_assert(offsetof(archive::FxVisStateDisk32, blockerCount) == 0x1000);
    static_assert(offsetof(archive::FxVisStateDisk32, pad) == 0x1004);
    static_assert(std::is_same_v<
        decltype(std::declval<archive::FxVisStateDisk32>().blockerCount),
        std::int32_t>);

    static_assert(sizeof(archive::FxElemPoolSlotDisk32) == ELEM_BYTES);
    static_assert(alignof(archive::FxElemPoolSlotDisk32) == 4);
    static_assert(
        offsetof(archive::FxElemPoolSlotDisk32, bytes) == 0x00);
    static_assert(sizeof(archive::FxTrailPoolSlotDisk32) == TRAIL_BYTES);
    static_assert(alignof(archive::FxTrailPoolSlotDisk32) == 4);
    static_assert(
        offsetof(archive::FxTrailPoolSlotDisk32, bytes) == 0x00);
    static_assert(
        sizeof(archive::FxTrailElemPoolSlotDisk32) == TRAIL_ELEM_BYTES);
    static_assert(alignof(archive::FxTrailElemPoolSlotDisk32) == 4);
    static_assert(
        offsetof(archive::FxTrailElemPoolSlotDisk32, bytes) == 0x00);
    static_assert(std::is_same_v<
        std::remove_extent_t<decltype(
            std::declval<archive::FxElemPoolSlotDisk32>().bytes)>,
        std::uint8_t>);

    static_assert(sizeof(archive::FxSystemBuffersDisk32) == BUFFER_BYTES);
    static_assert(alignof(archive::FxSystemBuffersDisk32) == 4);
    static_assert(
        std::is_standard_layout_v<archive::FxSystemBuffersDisk32>);
    static_assert(
        std::is_trivially_copyable_v<archive::FxSystemBuffersDisk32>);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, effects) == EFFECTS_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, elems) == ELEMS_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, trails) == TRAILS_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, trailElems)
        == TRAIL_ELEMS_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, visState)
        == VIS_STATE_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, deferredElems)
        == DEFERRED_ELEMS_OFFSET);
    static_assert(
        offsetof(archive::FxSystemBuffersDisk32, padBuffer)
        == PAD_BUFFER_OFFSET);
    static_assert(
        std::is_standard_layout_v<archive::FxSystemBuffersDisk32PoolStates>);
    static_assert(std::is_trivially_copyable_v<
        archive::FxSystemBuffersDisk32PoolStates>);
}
} // namespace

int main()
{
    CheckStaticLayouts();
    TestHandAuthoredRecordsAndRawSlots();
    TestValidEmptyMixedAndMaximumLists();
    TestFirstMiddleAndLastHeads();
    TestMalformedHeadsLinksCyclesAndCounts();
    TestIgnoredBytesAndCompositeTransaction();
    TestNullOutput();
    TestConditionalX86RawLayoutOracle();
    return failures == 0 ? 0 : 1;
}

#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_runtime_blob.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

struct alignas(sizeof(void *)) FxElemDefLayoutProbe
{
    std::byte bytes[KISAK_PTR_BITS == 32 ? 0xFC : 0x120];
};

struct alignas(4) FxVelSampleLayoutProbe
{
    std::byte bytes[0x60];
};

struct alignas(4) FxVisSampleLayoutProbe
{
    std::byte bytes[0x30];
};

struct alignas(sizeof(void *)) FxVisualLayoutProbe
{
    std::byte bytes[KISAK_PTR_BITS == 32 ? 0x4 : 0x8];
};

struct alignas(sizeof(void *)) FxTrailDefLayoutProbe
{
    std::byte bytes[KISAK_PTR_BITS == 32 ? 0x1C : 0x28];
};

struct alignas(4) FxTrailVertexLayoutProbe
{
    std::byte bytes[0x14];
};

struct alignas(sizeof(void *)) FxMarkVisualLayoutProbe
{
    std::byte bytes[KISAK_PTR_BITS == 32 ? 0x8 : 0x10];
};

struct FxPayloadPointers
{
    FxEffectDef *effect = nullptr;
    FxElemDefLayoutProbe *elemDefs = nullptr;
    FxVelSampleLayoutProbe *velocity = nullptr;
    FxVisSampleLayoutProbe *visibility = nullptr;
    FxVisualLayoutProbe *visuals = nullptr;
    FxTrailDefLayoutProbe *trail = nullptr;
    FxTrailVertexLayoutProbe *vertices = nullptr;
    std::uint16_t *indices = nullptr;
    FxMarkVisualLayoutProbe *markVisuals = nullptr;
};

static bool ReserveRepresentativePayload(
    FxRuntimeBlobCursor *cursor,
    FxPayloadPointers *pointers = nullptr)
{
    FxPayloadPointers ignored;
    FxPayloadPointers &out = pointers ? *pointers : ignored;
    return cursor->ReserveArray(1, &out.effect)
        && cursor->ReserveArray(2, &out.elemDefs)
        && cursor->ReserveArray(2, &out.velocity)
        && cursor->ReserveArray(2, &out.visibility)
        && cursor->ReserveArray(2, &out.visuals)
        && cursor->ReserveArray(1, &out.trail)
        && cursor->ReserveArray(2, &out.vertices)
        && cursor->ReserveArray(2, &out.indices)
        && cursor->ReserveArray(1, &out.markVisuals);
}

static bool IsAligned(const void *pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

int main()
{
    constexpr std::uint32_t finalOffset = KISAK_PTR_BITS == 32 ? 0x390 : 0x400;
    constexpr std::uint32_t preMarkOffset = KISAK_PTR_BITS == 32 ? 0x388 : 0x3EC;
    constexpr std::uint32_t markOffset = KISAK_PTR_BITS == 32 ? 0x388 : 0x3F0;

    FxRuntimeBlobCursor planner;
    const bool planned = ReserveRepresentativePayload(&planner);

    alignas(FxEffectDef) std::array<std::uint8_t, 0x500> bytes{};
    bytes.fill(0xA5);
    FxRuntimeBlobCursor writer(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    FxPayloadPointers pointers;
    const bool written = ReserveRepresentativePayload(&writer, &pointers);

    bool paddingIsZero = true;
    for (std::uint32_t offset = preMarkOffset; offset < markOffset; ++offset)
        paddingIsZero &= bytes[offset] == 0;

    void *sentinel = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
    FxRuntimeBlobCursor exactEnd(nullptr, finalOffset);
    const bool exactCapacity = ReserveRepresentativePayload(&exactEnd)
        && exactEnd.Offset() == finalOffset
        && !exactEnd.ReserveBytes(1, 1, &sentinel)
        && exactEnd.Offset() == finalOffset
        && sentinel == reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));

    FxRuntimeBlobCursor truncated(nullptr, preMarkOffset);
    FxPayloadPointers truncatedPointers;
    const bool prefixFits = truncated.ReserveArray(1, &truncatedPointers.effect)
        && truncated.ReserveArray(2, &truncatedPointers.elemDefs)
        && truncated.ReserveArray(2, &truncatedPointers.velocity)
        && truncated.ReserveArray(2, &truncatedPointers.visibility)
        && truncated.ReserveArray(2, &truncatedPointers.visuals)
        && truncated.ReserveArray(1, &truncatedPointers.trail)
        && truncated.ReserveArray(2, &truncatedPointers.vertices)
        && truncated.ReserveArray(2, &truncatedPointers.indices);
    FxMarkVisualLayoutProbe *markSentinel = reinterpret_cast<FxMarkVisualLayoutProbe *>(
        static_cast<std::uintptr_t>(1));
    const bool failureIsTransactional = prefixFits
        && truncated.Offset() == preMarkOffset
        && !truncated.ReserveArray(1, &markSentinel)
        && truncated.Offset() == preMarkOffset
        && markSentinel == reinterpret_cast<FxMarkVisualLayoutProbe *>(
            static_cast<std::uintptr_t>(1));

    FxRuntimeBlobCursor overflow;
    const bool overflowsRejected = !overflow.ReserveBytes(
            static_cast<std::size_t>(FxRuntimeBlobCursor::MAX_SIZE) + 1, 1)
        && !overflow.ReserveArray<std::uint64_t>(
            FxRuntimeBlobCursor::MAX_SIZE / sizeof(std::uint64_t) + 1)
        && overflow.Offset() == 0;

    FxRuntimeBlobCursor capped(nullptr, (std::numeric_limits<std::uint32_t>::max)());
    const bool signedCapacityIsEnforced = capped.ReserveBytes(
            FxRuntimeBlobCursor::MAX_SIZE, 1)
        && capped.Offset() == FxRuntimeBlobCursor::MAX_SIZE
        && !capped.ReserveBytes(1, 1)
        && capped.Offset() == FxRuntimeBlobCursor::MAX_SIZE;

    FxRuntimeBlobCursor invalidAlignment;
    const std::size_t hugeAlignment =
        std::size_t{1} << ((std::numeric_limits<std::size_t>::digits) - 1);
    const bool invalidAlignmentsRejected = !invalidAlignment.ReserveBytes(1, 0)
        && !invalidAlignment.ReserveBytes(1, 3)
        && invalidAlignment.ReserveBytes(2, 1)
        && !invalidAlignment.ReserveBytes(1, hugeAlignment)
        && invalidAlignment.Offset() == 2;

    std::size_t alignedSentinel = 17;
    const bool alignmentAdditionOverflowRejected = !FxRuntimeBlobTryAlignOffset(
            (std::numeric_limits<std::size_t>::max)() - 3,
            8,
            &alignedSentinel)
        && alignedSentinel == 17;

    return planned && written && planner.Offset() == finalOffset
            && writer.Offset() == planner.Offset() && paddingIsZero
            && IsAligned(pointers.effect, alignof(FxEffectDef))
            && IsAligned(pointers.elemDefs, alignof(FxElemDefLayoutProbe))
            && IsAligned(pointers.velocity, alignof(FxVelSampleLayoutProbe))
            && IsAligned(pointers.visibility, alignof(FxVisSampleLayoutProbe))
            && IsAligned(pointers.visuals, alignof(FxVisualLayoutProbe))
            && IsAligned(pointers.trail, alignof(FxTrailDefLayoutProbe))
            && IsAligned(pointers.vertices, alignof(FxTrailVertexLayoutProbe))
            && IsAligned(pointers.indices, alignof(std::uint16_t))
            && IsAligned(pointers.markVisuals, alignof(FxMarkVisualLayoutProbe))
            && exactCapacity && failureIsTransactional && overflowsRejected
            && signedCapacityIsEnforced && invalidAlignmentsRejected
            && alignmentAdditionOverflowRejected
        ? 0
        : 1;
}

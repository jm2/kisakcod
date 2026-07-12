#include <gfx_d3d/r_model_surface_stream.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace
{
namespace surface_stream = gfx::model_surface_stream;

struct NativeSurfaceInfo
{
    const void *baseMatrix = nullptr;
    std::uint8_t boneIndex = 0u;
    std::uint8_t boneCount = 0u;
    std::uint16_t gfxEntityIndex = 0u;
    std::uint16_t lightingHandle = 0u;
};

struct NativeSkinnedRecord
{
    std::int32_t tag = 0;
    const void *surface = nullptr;
    NativeSurfaceInfo info;
    const void *output = nullptr;
};

struct NativeRigidRecord
{
    NativeSkinnedRecord surface;
    float placement[8] = {};
};

constexpr std::uint32_t kRecordAlignment = static_cast<std::uint32_t>(
    alignof(NativeRigidRecord) > alignof(NativeSkinnedRecord)
        ? alignof(NativeRigidRecord)
        : alignof(NativeSkinnedRecord));
constexpr std::uint32_t kHiddenSize =
    kRecordAlignment > sizeof(std::int32_t)
    ? kRecordAlignment
    : static_cast<std::uint32_t>(sizeof(std::int32_t));
constexpr std::uint32_t kSkinnedSize =
    static_cast<std::uint32_t>(sizeof(NativeSkinnedRecord));
constexpr std::uint32_t kRigidSize =
    static_cast<std::uint32_t>(sizeof(NativeRigidRecord));

static_assert(sizeof(NativeSkinnedRecord) == (sizeof(void *) == 4 ? 24u : 40u));
static_assert(sizeof(NativeRigidRecord) == (sizeof(void *) == 4 ? 56u : 72u));
static_assert(surface_stream::HiddenRecordSize(4u) == 4u);
static_assert(surface_stream::HiddenRecordSize(8u) == 8u);
static_assert(surface_stream::HiddenRecordSize(2u) == 0u);

int Fail(const char *const message)
{
    std::fprintf(stderr, "model surface stream test failed: %s\n", message);
    return 1;
}

bool TestTagClassification()
{
    surface_stream::RecordKind kind = surface_stream::RecordKind::Rigid;
    if (!surface_stream::RecordKindFromTag(surface_stream::kHiddenTag, &kind)
        || kind != surface_stream::RecordKind::Hidden
        || !surface_stream::RecordKindFromTag(surface_stream::kRigidTag, &kind)
        || kind != surface_stream::RecordKind::Rigid
        || !surface_stream::RecordKindFromTag(
            surface_stream::kDirectSkinnedTag, &kind)
        || kind != surface_stream::RecordKind::Skinned
        || !surface_stream::RecordKindFromTag(0, &kind)
        || kind != surface_stream::RecordKind::Skinned
        || !surface_stream::RecordKindFromTag(
            (std::numeric_limits<std::int32_t>::max)(), &kind)
        || kind != surface_stream::RecordKind::Skinned)
    {
        return false;
    }

    kind = surface_stream::RecordKind::Rigid;
    return !surface_stream::RecordKindFromTag(-4, &kind)
        && kind == surface_stream::RecordKind::Rigid
        && !surface_stream::RecordKindFromTag(
            std::numeric_limits<std::int32_t>::min(), &kind)
        && kind == surface_stream::RecordKind::Rigid
        && !surface_stream::RecordKindFromTag(0, nullptr);
}

bool TestCheckedArithmeticAndPlanning()
{
    constexpr std::uint32_t kMax =
        (std::numeric_limits<std::uint32_t>::max)();
    std::uint32_t result = 73u;
    if (!surface_stream::TryAdd(kMax - 2u, 2u, &result) || result != kMax)
        return false;

    result = 73u;
    if (surface_stream::TryAdd(kMax, 1u, &result) || result != 73u)
        return false;
    if (!surface_stream::TryMultiply(kMax, 1u, &result) || result != kMax)
        return false;

    result = 73u;
    if (surface_stream::TryMultiply(kMax, 2u, &result) || result != 73u
        || surface_stream::TryAdd(1u, 1u, nullptr)
        || surface_stream::TryMultiply(1u, 1u, nullptr))
    {
        return false;
    }

    std::uint32_t offset = 19u;
    std::uint32_t total = 23u;
    if (!surface_stream::TryPlanRecord(0u, 4u, 8u, 8u, &offset, &total)
        || offset != 0u || total != 8u)
    {
        return false;
    }

    offset = 19u;
    total = 23u;
    return !surface_stream::TryPlanRecord(
               kMax - 3u, 4u, 8u, kMax, &offset, &total)
        && offset == 19u && total == 23u
        && !surface_stream::TryPlanRecord(4u, 4u, 8u, 32u, &offset, &total)
        && offset == 19u && total == 23u
        && !surface_stream::TryPlanRecord(0u, 4u, 3u, 32u, &offset, &total)
        && offset == 19u && total == 23u;
}

bool TestFormerRigidStackBoundary()
{
    constexpr std::uint32_t kLegacyBufferSize = 150u * 24u;
    constexpr std::uint32_t kLegacyRigidSize = 56u;
    constexpr std::uint32_t kLegacyAlignment = 4u;

    std::uint32_t total = 0u;
    for (std::uint32_t index = 0u; index < 64u; ++index)
    {
        std::uint32_t offset = 0u;
        std::uint32_t next = 0u;
        if (!surface_stream::TryPlanRecord(
                total,
                kLegacyRigidSize,
                kLegacyAlignment,
                kLegacyBufferSize,
                &offset,
                &next)
            || offset != index * kLegacyRigidSize)
        {
            return false;
        }
        total = next;
    }

    if (total != 64u * kLegacyRigidSize)
        return false;

    std::uint32_t offset = 91u;
    std::uint32_t next = 93u;
    return !surface_stream::TryPlanRecord(
               total,
               kLegacyRigidSize,
               kLegacyAlignment,
               kLegacyBufferSize,
               &offset,
               &next)
        && offset == 91u && next == 93u;
}

bool AppendRecord(
    std::uint8_t *const bytes,
    const std::uint32_t capacity,
    std::uint32_t *const total,
    const std::int32_t tag,
    const std::uint32_t rawSize,
    std::uint32_t *const recordOffset)
{
    std::uint32_t next = 0u;
    if (!surface_stream::TryPlanRecord(
            *total,
            rawSize,
            kRecordAlignment,
            capacity,
            recordOffset,
            &next))
    {
        return false;
    }

    std::memcpy(bytes + *recordOffset, &tag, sizeof(tag));
    *total = next;
    return true;
}

bool TestMixedStreamAndNativeAlignment()
{
    alignas(NativeRigidRecord) std::array<std::uint8_t, 512> bytes{};
    if (surface_stream::HiddenRecordSize(kRecordAlignment) != kHiddenSize)
        return false;

    std::uint32_t total = 0u;
    std::uint32_t hiddenOffset = 0u;
    std::uint32_t skinnedOffset = 0u;
    std::uint32_t rigidOffset = 0u;
    if (!AppendRecord(
            bytes.data(),
            static_cast<std::uint32_t>(bytes.size()),
            &total,
            surface_stream::kHiddenTag,
            sizeof(std::int32_t),
            &hiddenOffset)
        || !AppendRecord(
            bytes.data(),
            static_cast<std::uint32_t>(bytes.size()),
            &total,
            17,
            kSkinnedSize,
            &skinnedOffset)
        || !AppendRecord(
            bytes.data(),
            static_cast<std::uint32_t>(bytes.size()),
            &total,
            surface_stream::kRigidTag,
            kRigidSize,
            &rigidOffset))
    {
        return false;
    }

    if (hiddenOffset != 0u || skinnedOffset != kHiddenSize
        || (skinnedOffset % kRecordAlignment) != 0u
        || (rigidOffset % kRecordAlignment) != 0u)
    {
        return false;
    }

    surface_stream::Cursor cursor(bytes.data(), total, 3u);
    const std::array<surface_stream::RecordKind, 3> expectedKinds = {
        surface_stream::RecordKind::Hidden,
        surface_stream::RecordKind::Skinned,
        surface_stream::RecordKind::Rigid,
    };
    const std::array<std::uint32_t, 3> expectedOffsets = {
        hiddenOffset,
        skinnedOffset,
        rigidOffset,
    };
    for (std::size_t index = 0u; index < expectedKinds.size(); ++index)
    {
        surface_stream::View view;
        if (!cursor.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
            || view.kind != expectedKinds[index]
            || view.offset != expectedOffsets[index]
            || view.data != bytes.data() + expectedOffsets[index])
        {
            return false;
        }
    }
    return cursor.Finished();
}

bool TestExactAndMalformedCursorBounds()
{
    alignas(NativeRigidRecord) std::array<std::uint8_t, kSkinnedSize> bytes{};
    const std::int32_t tag = 0;
    std::memcpy(bytes.data(), &tag, sizeof(tag));

    surface_stream::View view;
    surface_stream::Cursor exact(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 1u);
    if (!exact.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        || !exact.Finished())
    {
        return false;
    }

    surface_stream::Cursor truncated(
        bytes.data(), static_cast<std::uint32_t>(bytes.size() - 1u), 1u);
    if (truncated.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        || truncated.Finished())
    {
        return false;
    }

    surface_stream::Cursor zeroAlignment(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 1u);
    if (zeroAlignment.Next(0u, kSkinnedSize, kRigidSize, &view)
        || zeroAlignment.Finished())
    {
        return false;
    }

    surface_stream::Cursor extraBytes(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0u);
    if (extraBytes.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        || extraBytes.Finished())
    {
        return false;
    }

    surface_stream::Cursor extraCount(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 2u);
    if (!extraCount.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        || extraCount.Finished()
        || extraCount.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view))
    {
        return false;
    }

    alignas(NativeRigidRecord) std::array<std::uint8_t, kHiddenSize> corrupt{};
    const std::int32_t corruptTag = -4;
    std::memcpy(corrupt.data(), &corruptTag, sizeof(corruptTag));
    surface_stream::Cursor badTag(
        corrupt.data(), static_cast<std::uint32_t>(corrupt.size()), 1u);
    return !badTag.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        && !badTag.Finished();
}

bool TestPointerBytesRemainNativeWidth()
{
    alignas(NativeRigidRecord) std::array<std::uint8_t, kSkinnedSize> bytes{};
    NativeSkinnedRecord record;
    record.tag = 3;
#if UINTPTR_MAX > UINT32_MAX
    constexpr std::uintptr_t kSurfaceBits = UINT64_C(0xfedcba9876543210);
    constexpr std::uintptr_t kMatrixBits = UINT64_C(0x87654321abcdef00);
#else
    constexpr std::uintptr_t kSurfaceBits = UINT32_C(0xfedcba98);
    constexpr std::uintptr_t kMatrixBits = UINT32_C(0x87654321);
#endif
    record.surface = reinterpret_cast<const void *>(kSurfaceBits);
    record.info.baseMatrix = reinterpret_cast<const void *>(kMatrixBits);
    std::memcpy(bytes.data(), &record, sizeof(record));

    surface_stream::Cursor cursor(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 1u);
    surface_stream::View view;
    if (!cursor.Next(kHiddenSize, kSkinnedSize, kRigidSize, &view)
        || !cursor.Finished())
    {
        return false;
    }

    NativeSkinnedRecord decoded;
    std::memcpy(&decoded, view.data, sizeof(decoded));
    return reinterpret_cast<std::uintptr_t>(decoded.surface) == kSurfaceBits
        && reinterpret_cast<std::uintptr_t>(decoded.info.baseMatrix)
            == kMatrixBits;
}

bool TestExactAlignedArenaCapacityAndFailureStability()
{
    volatile std::uint32_t counter = 1u;
    std::uint32_t offset = 71u;
    if (!surface_stream::TryReserveAligned(&counter, 8u, 16u, 8u, &offset)
        || offset != 8u || Sys_AtomicLoad(&counter) != 16u)
    {
        return false;
    }

    offset = 71u;
    if (surface_stream::TryReserveAligned(&counter, 1u, 16u, 8u, &offset)
        || offset != 71u || Sys_AtomicLoad(&counter) != 16u)
    {
        return false;
    }

    Sys_AtomicStore(&counter, 17u);
    return !surface_stream::TryReserveAligned(
               &counter, 1u, 16u, 8u, &offset)
        && offset == 71u && Sys_AtomicLoad(&counter) == 17u
        && !surface_stream::TryReserveAligned(
            &counter, 1u, 16u, 3u, &offset)
        && Sys_AtomicLoad(&counter) == 17u;
}

bool TestContendedAlignedArenaSlicesDoNotOverlap()
{
    constexpr std::uint32_t kAlignment = 8u;
    constexpr std::uint32_t kSliceSize = 5u;
    constexpr std::uint32_t kSliceCount = 512u;
    constexpr std::uint32_t kCapacity =
        (kSliceCount - 1u) * kAlignment + kSliceSize;
    constexpr std::uint32_t kThreadCount = 8u;

    volatile std::uint32_t counter = 0u;
    std::array<std::atomic<std::uint32_t>, kCapacity> claims{};
    std::atomic<std::uint32_t> successes{0u};
    std::atomic<bool> valid{true};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::uint32_t threadIndex = 0u;
         threadIndex < kThreadCount;
         ++threadIndex)
    {
        workers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (;;)
            {
                std::uint32_t offset =
                    (std::numeric_limits<std::uint32_t>::max)();
                if (!surface_stream::TryReserveAligned(
                        &counter,
                        kSliceSize,
                        kCapacity,
                        kAlignment,
                        &offset))
                {
                    return;
                }
                if ((offset % kAlignment) != 0u
                    || offset > kCapacity - kSliceSize)
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
                for (std::uint32_t byte = 0u; byte < kSliceSize; ++byte)
                {
                    if (claims[offset + byte].fetch_add(
                            1u, std::memory_order_relaxed)
                        != 0u)
                    {
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
                successes.fetch_add(1u, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();

    if (!valid.load(std::memory_order_relaxed)
        || successes.load(std::memory_order_relaxed) != kSliceCount
        || Sys_AtomicLoad(&counter) != kCapacity)
    {
        return false;
    }

    for (std::uint32_t slice = 0u; slice < kSliceCount; ++slice)
    {
        const std::uint32_t offset = slice * kAlignment;
        for (std::uint32_t byte = 0u; byte < kSliceSize; ++byte)
        {
            if (claims[offset + byte].load(std::memory_order_relaxed) != 1u)
                return false;
        }
    }
    return true;
}

bool TestBoneSpanValidation()
{
    return surface_stream::IsBoneSpanValid(0u, 128u)
        && surface_stream::IsBoneSpanValid(127u, 1u)
        && !surface_stream::IsBoneSpanValid(0u, 0u)
        && !surface_stream::IsBoneSpanValid(128u, 1u)
        && !surface_stream::IsBoneSpanValid(127u, 2u)
        && !surface_stream::IsBoneSpanValid(
            (std::numeric_limits<std::uint32_t>::max)(), 1u);
}
} // namespace

int main()
{
    if (!TestTagClassification())
        return Fail("tag classification");
    if (!TestCheckedArithmeticAndPlanning())
        return Fail("checked arithmetic and record planning");
    if (!TestFormerRigidStackBoundary())
        return Fail("former 64/65 rigid stack boundary");
    if (!TestMixedStreamAndNativeAlignment())
        return Fail("mixed stream and native alignment");
    if (!TestExactAndMalformedCursorBounds())
        return Fail("exact, truncated, and malformed cursor bounds");
    if (!TestPointerBytesRemainNativeWidth())
        return Fail("native-width high pointer bytes");
    if (!TestExactAlignedArenaCapacityAndFailureStability())
        return Fail("exact arena capacity and failure stability");
    if (!TestContendedAlignedArenaSlicesDoNotOverlap())
        return Fail("contended aligned arena slices");
    if (!TestBoneSpanValidation())
        return Fail("128-bone span validation");
    return 0;
}

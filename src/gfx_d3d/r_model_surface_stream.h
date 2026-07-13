#pragma once
//
// Checked, native-width helpers for the heterogeneous model-surface stream.
//
// The stream starts every record with an int32 tag.  Hidden records contain
// only that tag, but their stride is rounded to the native record alignment so
// that a following pointer-bearing record remains aligned on 64-bit targets.
// The remaining record bytes are intentionally opaque here: renderer structs
// keep their native pointer widths and this helper only owns framing.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <universal/sys_atomic.h>

namespace gfx::model_surface_stream
{
constexpr std::int32_t kHiddenTag = -3;
constexpr std::int32_t kRigidTag = -2;
constexpr std::int32_t kDirectSkinnedTag = -1;
constexpr std::uint32_t kMaxSkinningBones = 128u;

enum class RecordKind : std::uint8_t
{
    Hidden,
    Rigid,
    Skinned,
};

struct View
{
    std::uint8_t *data = nullptr;
    std::uint32_t offset = 0u;
    std::uint32_t size = 0u;
    std::int32_t tag = 0;
    RecordKind kind = RecordKind::Hidden;
};

[[nodiscard]] constexpr bool IsPowerOfTwo(
    const std::uint32_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

[[nodiscard]] constexpr bool TryAdd(
    const std::uint32_t left,
    const std::uint32_t right,
    std::uint32_t *const result) noexcept
{
    if (!result
        || left > (std::numeric_limits<std::uint32_t>::max)() - right)
        return false;

    *result = left + right;
    return true;
}

[[nodiscard]] constexpr bool TryMultiply(
    const std::uint32_t left,
    const std::uint32_t right,
    std::uint32_t *const result) noexcept
{
    if (!result
        || (left != 0u
            && right
                > (std::numeric_limits<std::uint32_t>::max)() / left))
    {
        return false;
    }

    *result = left * right;
    return true;
}

[[nodiscard]] constexpr bool TryAlignUp(
    const std::uint32_t value,
    const std::uint32_t alignment,
    std::uint32_t *const result) noexcept
{
    if (!result || !IsPowerOfTwo(alignment))
        return false;

    const std::uint32_t mask = alignment - 1u;
    std::uint32_t withPadding = 0u;
    if (!TryAdd(value, mask, &withPadding))
        return false;

    *result = withPadding & ~mask;
    return true;
}

// Returns zero for an alignment that cannot safely align an int32 stream tag.
[[nodiscard]] constexpr std::uint32_t HiddenRecordSize(
    const std::uint32_t recordAlignment) noexcept
{
    if (recordAlignment < alignof(std::int32_t))
        return 0u;

    std::uint32_t size = 0u;
    return TryAlignUp(
               static_cast<std::uint32_t>(sizeof(std::int32_t)),
               recordAlignment,
               &size)
        ? size
        : 0u;
}

[[nodiscard]] inline bool RecordKindFromTag(
    const std::int32_t tag,
    RecordKind *const kind) noexcept
{
    if (!kind)
        return false;

    RecordKind classified = RecordKind::Skinned;
    if (tag == kHiddenTag)
        classified = RecordKind::Hidden;
    else if (tag == kRigidTag)
        classified = RecordKind::Rigid;
    else if (tag < kDirectSkinnedTag)
        return false;

    *kind = classified;
    return true;
}

// Plans one aligned record without modifying any output on failure.  A total
// produced by this function is always aligned and is therefore a valid input
// to the next call.  Rejecting a non-aligned input prevents an unframed padding
// gap from appearing between records.  Exact-capacity records are accepted.
[[nodiscard]] inline bool TryPlanRecord(
    const std::uint32_t totalBytes,
    const std::uint32_t recordSize,
    const std::uint32_t recordAlignment,
    const std::uint32_t capacity,
    std::uint32_t *const recordOffset,
    std::uint32_t *const newTotalBytes) noexcept
{
    if (!recordOffset || !newTotalBytes || recordSize < sizeof(std::int32_t)
        || recordAlignment < alignof(std::int32_t) || totalBytes > capacity)
    {
        return false;
    }

    if (!IsPowerOfTwo(recordAlignment)
        || (totalBytes & (recordAlignment - 1u)) != 0u)
    {
        return false;
    }

    std::uint32_t alignedSize = 0u;
    std::uint32_t end = 0u;
    if (!TryAlignUp(recordSize, recordAlignment, &alignedSize)
        || !TryAdd(totalBytes, alignedSize, &end) || end > capacity)
    {
        return false;
    }

    *recordOffset = totalBytes;
    *newTotalBytes = end;
    return true;
}

[[nodiscard]] constexpr bool IsBoneSpanValid(
    const std::uint32_t firstBone,
    const std::uint32_t boneCount) noexcept
{
    return boneCount != 0u && firstBone < kMaxSkinningBones
        && boneCount <= kMaxSkinningBones - firstBone;
}

// Resolves a native-aligned extent already addressed inside a bounded byte
// arena.  Comparing integer addresses avoids relational comparisons between
// unrelated C++ pointers.  No bytes are read and the output is left untouched
// on failure.
[[nodiscard]] inline bool TryResolveArenaRange(
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const void *const candidate,
    const std::uint32_t recordSize,
    const std::uint32_t recordAlignment,
    const void **const record) noexcept
{
    if (!arenaBegin || !candidate || !record
        || publishedBytes > capacity || recordSize < sizeof(std::int32_t)
        || recordAlignment < alignof(std::int32_t)
        || !IsPowerOfTwo(recordAlignment))
    {
        return false;
    }

    const std::uintptr_t arenaAddress =
        reinterpret_cast<std::uintptr_t>(arenaBegin);
    const std::uintptr_t recordAddress =
        reinterpret_cast<std::uintptr_t>(candidate);
    if (recordAddress < arenaAddress
        || recordAddress - arenaAddress
            > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }

    const std::uint32_t recordOffset = static_cast<std::uint32_t>(
        recordAddress - arenaAddress);
    if (recordOffset > publishedBytes
        || recordSize > publishedBytes - recordOffset
        || recordOffset > capacity || recordSize > capacity - recordOffset)
    {
        return false;
    }

    if ((recordAddress
         & static_cast<std::uintptr_t>(recordAlignment - 1u))
        != 0u)
    {
        return false;
    }

    *record = candidate;
    return true;
}

// Resolves a legacy dword offset into a bounded byte arena.  The offset is
// relative to wordOffsetBase, while capacity and publishedBytes describe the
// arena beginning at arenaBegin.  This separation is needed because renderer
// draw-surface object IDs historically address records from the beginning of
// GfxBackEndData even though the records live in its surface arena.
//
// This extent-only form supports separately encoded records such as BModels,
// whose type tag lives in the draw-surface key rather than the record bytes.
// The output is left untouched on failure.
[[nodiscard]] inline bool TryResolveWordOffsetExtent(
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t dwordOffset,
    const std::uint32_t recordSize,
    const std::uint32_t recordAlignment,
    const void **const record) noexcept
{
    if (!wordOffsetBase || !arenaBegin || !record)
        return false;

    std::uint32_t byteOffset = 0u;
    if (!TryMultiply(
            dwordOffset,
            static_cast<std::uint32_t>(sizeof(std::uint32_t)),
            &byteOffset))
    {
        return false;
    }

    const std::uintptr_t baseAddress =
        reinterpret_cast<std::uintptr_t>(wordOffsetBase);
    const std::uintptr_t arenaAddress =
        reinterpret_cast<std::uintptr_t>(arenaBegin);
    if (arenaAddress < baseAddress
        || arenaAddress - baseAddress
            > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }

    const std::uint32_t arenaBaseOffset = static_cast<std::uint32_t>(
        arenaAddress - baseAddress);
    if (byteOffset < arenaBaseOffset)
        return false;

    const std::uint32_t recordOffset = byteOffset - arenaBaseOffset;
    if (arenaAddress
        > (std::numeric_limits<std::uintptr_t>::max)() - recordOffset)
    {
        return false;
    }

    const void *const candidate = reinterpret_cast<const void *>(
        arenaAddress + recordOffset);
    const void *resolved = nullptr;
    if (!TryResolveArenaRange(
            arenaBegin,
            capacity,
            publishedBytes,
            candidate,
            recordSize,
            recordAlignment,
            &resolved))
    {
        return false;
    }

    *record = resolved;
    return true;
}

template <typename Record>
[[nodiscard]] inline bool TryResolveTypedWordOffsetExtent(
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t dwordOffset,
    const Record **const record) noexcept
{
    if (!record)
        return false;

    const void *resolved = nullptr;
    if (!TryResolveWordOffsetExtent(
            wordOffsetBase,
            arenaBegin,
            capacity,
            publishedBytes,
            dwordOffset,
            static_cast<std::uint32_t>(sizeof(Record)),
            static_cast<std::uint32_t>(alignof(Record)),
            &resolved))
    {
        return false;
    }

    *record = static_cast<const Record *>(resolved);
    return true;
}

// The tagged form copies the first int32 only after the complete requested
// extent and native alignment have been checked.
[[nodiscard]] inline bool TryResolveWordOffsetRange(
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t dwordOffset,
    const std::uint32_t recordSize,
    const std::uint32_t recordAlignment,
    const std::int32_t minimumTag,
    const std::int32_t maximumTag,
    const void **const record,
    std::int32_t *const recordTag = nullptr) noexcept
{
    if (!record || minimumTag > maximumTag)
        return false;

    const void *resolved = nullptr;
    if (!TryResolveWordOffsetExtent(
            wordOffsetBase,
            arenaBegin,
            capacity,
            publishedBytes,
            dwordOffset,
            recordSize,
            recordAlignment,
            &resolved))
    {
        return false;
    }

    std::int32_t tag = 0;
    std::memcpy(&tag, resolved, sizeof(tag));
    if (tag < minimumTag || tag > maximumTag)
        return false;

    *record = resolved;
    if (recordTag)
        *recordTag = tag;
    return true;
}

template <typename Record>
[[nodiscard]] inline bool TryResolveTypedWordOffset(
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t dwordOffset,
    const std::int32_t minimumTag,
    const std::int32_t maximumTag,
    const Record **const record,
    std::int32_t *const recordTag = nullptr) noexcept
{
    if (!record)
        return false;

    const void *resolved = nullptr;
    if (!TryResolveWordOffsetRange(
            wordOffsetBase,
            arenaBegin,
            capacity,
            publishedBytes,
            dwordOffset,
            static_cast<std::uint32_t>(sizeof(Record)),
            static_cast<std::uint32_t>(alignof(Record)),
            minimumTag,
            maximumTag,
            &resolved,
            recordTag))
    {
        return false;
    }

    *record = static_cast<const Record *>(resolved);
    return true;
}

// Atomically reserves an aligned byte slice.  Padding belongs to the
// reservation protocol but is not included in the returned slice.  A rejected
// reservation never changes either the shared counter or *offset.
[[nodiscard]] inline bool TryReserveAligned(
    volatile std::uint32_t *const counter,
    const std::uint32_t size,
    const std::uint32_t capacity,
    const std::uint32_t alignment,
    std::uint32_t *const offset) noexcept
{
    if (!counter || !offset || size == 0u
        || alignment < alignof(std::int32_t) || !IsPowerOfTwo(alignment))
    {
        return false;
    }

    std::uint32_t observed = Sys_AtomicLoad(counter);
    for (;;)
    {
        if (observed > capacity)
            return false;

        std::uint32_t alignedOffset = 0u;
        std::uint32_t end = 0u;
        if (!TryAlignUp(observed, alignment, &alignedOffset)
            || !TryAdd(alignedOffset, size, &end) || end > capacity)
        {
            return false;
        }

        const std::uint32_t previous =
            Sys_AtomicCompareExchange(counter, end, observed);
        if (previous == observed)
        {
            *offset = alignedOffset;
            return true;
        }
        observed = previous;
    }
}

class Cursor
{
public:
    Cursor(
        void *const begin,
        const std::uint32_t byteCount,
        const std::uint32_t recordCount) noexcept
        : begin_(static_cast<std::uint8_t *>(begin)),
          byteCount_(byteCount),
          recordCount_(recordCount),
          valid_(begin != nullptr || (byteCount == 0u && recordCount == 0u))
    {
    }

    [[nodiscard]] bool Next(
        const std::uint32_t hiddenRecordSize,
        const std::uint32_t skinnedRecordSize,
        const std::uint32_t rigidRecordSize,
        View *const view) noexcept
    {
        if (!view || !valid_ || recordIndex_ >= recordCount_)
            return false;

        // HiddenRecordSize(alignment) equals alignment for all supported
        // native alignments (which are powers of two and at least four).
        const std::uint32_t recordAlignment = hiddenRecordSize;
        if (recordAlignment < alignof(std::int32_t)
            || HiddenRecordSize(recordAlignment) != hiddenRecordSize
            || skinnedRecordSize < sizeof(std::int32_t)
            || rigidRecordSize < sizeof(std::int32_t)
            || (skinnedRecordSize % recordAlignment) != 0u
            || (rigidRecordSize % recordAlignment) != 0u
            || offset_ > byteCount_
            || byteCount_ - offset_ < sizeof(std::int32_t))
        {
            valid_ = false;
            return false;
        }

        const std::uintptr_t address =
            reinterpret_cast<std::uintptr_t>(begin_ + offset_);
        if ((address & static_cast<std::uintptr_t>(recordAlignment - 1u)) != 0u)
        {
            valid_ = false;
            return false;
        }

        std::int32_t tag = 0;
        std::memcpy(&tag, begin_ + offset_, sizeof(tag));

        RecordKind kind = RecordKind::Hidden;
        if (!RecordKindFromTag(tag, &kind))
        {
            valid_ = false;
            return false;
        }

        std::uint32_t size = skinnedRecordSize;
        if (kind == RecordKind::Hidden)
            size = hiddenRecordSize;
        else if (kind == RecordKind::Rigid)
            size = rigidRecordSize;

        if (size > byteCount_ - offset_)
        {
            valid_ = false;
            return false;
        }

        const std::uint32_t currentOffset = offset_;
        std::uint32_t nextOffset = 0u;
        if (!TryAdd(offset_, size, &nextOffset))
        {
            valid_ = false;
            return false;
        }

        View nextView;
        nextView.data = begin_ + currentOffset;
        nextView.offset = currentOffset;
        nextView.size = size;
        nextView.tag = tag;
        nextView.kind = kind;

        offset_ = nextOffset;
        ++recordIndex_;
        *view = nextView;
        return true;
    }

    [[nodiscard]] bool Finished() const noexcept
    {
        return valid_ && recordIndex_ == recordCount_ && offset_ == byteCount_;
    }

    [[nodiscard]] std::uint32_t ConsumedBytes() const noexcept
    {
        return offset_;
    }

    [[nodiscard]] std::uint32_t ConsumedRecords() const noexcept
    {
        return recordIndex_;
    }

private:
    std::uint8_t *begin_ = nullptr;
    std::uint32_t byteCount_ = 0u;
    std::uint32_t recordCount_ = 0u;
    std::uint32_t offset_ = 0u;
    std::uint32_t recordIndex_ = 0u;
    bool valid_ = false;
};
} // namespace gfx::model_surface_stream

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

[[nodiscard]] constexpr bool FxRuntimeBlobTryAlignOffset(
    std::size_t offset,
    std::size_t alignment,
    std::size_t *alignedOffset) noexcept
{
    if (!alignedOffset || alignment == 0 || (alignment & (alignment - 1)) != 0)
        return false;

    const std::size_t alignmentMask = alignment - 1;
    if (alignmentMask > (std::numeric_limits<std::size_t>::max)() - offset)
        return false;

    *alignedOffset = (offset + alignmentMask) & ~alignmentMask;
    return true;
}

class FxRuntimeBlobCursor
{
public:
    static constexpr std::uint32_t MAX_SIZE =
        static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)());

    explicit FxRuntimeBlobCursor(
        std::uint8_t *buffer = nullptr,
        std::uint32_t capacity = MAX_SIZE) noexcept
        : buffer_(buffer),
          capacity_(capacity <= MAX_SIZE ? capacity : MAX_SIZE),
          offset_(0)
    {
    }

    [[nodiscard]] bool ReserveBytes(
        std::size_t byteCount,
        std::size_t alignment,
        void **storage = nullptr) noexcept
    {
        std::size_t alignedOffset = 0;
        if (!FxRuntimeBlobTryAlignOffset(offset_, alignment, &alignedOffset))
            return false;
        if (alignedOffset > capacity_ || byteCount > capacity_ - alignedOffset)
            return false;

        if (buffer_ && alignedOffset != offset_)
            std::memset(buffer_ + offset_, 0, alignedOffset - offset_);
        if (storage)
            *storage = buffer_ ? buffer_ + alignedOffset : nullptr;

        offset_ = static_cast<std::uint32_t>(alignedOffset + byteCount);
        return true;
    }

    template <typename T>
    [[nodiscard]] bool ReserveArray(std::uint32_t count, T **storage = nullptr) noexcept
    {
        if (count > MAX_SIZE / sizeof(T))
            return false;

        void *untypedStorage = nullptr;
        if (!ReserveBytes(sizeof(T) * count, alignof(T), storage ? &untypedStorage : nullptr))
            return false;
        if (storage)
            *storage = static_cast<T *>(untypedStorage);
        return true;
    }

    [[nodiscard]] std::uint32_t Offset() const noexcept
    {
        return offset_;
    }

private:
    std::uint8_t *buffer_;
    std::uint32_t capacity_;
    std::uint32_t offset_;
};

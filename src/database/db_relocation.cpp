#include "db_relocation.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>

namespace db::relocation
{
namespace
{
bool SpanContains(const BlockView &block, std::uintptr_t address, std::uint32_t size)
{
    if (!block.base || address < block.base
        || size > (std::numeric_limits<std::uintptr_t>::max)() - address)
        return false;

    const std::uintptr_t offset = address - block.base;
    return offset <= block.size && size <= block.size - offset;
}

}

DirectResolver::DirectResolver(
    std::size_t maxIntervals,
    std::size_t maxStrings)
    : maxIntervals_(maxIntervals),
      maxStrings_(maxStrings)
{
}

void DirectResolver::Reset(
    const BlockView *blocks,
    const std::size_t blockCount) noexcept
{
    contextValid_ = blocks && blockCount == kBlockCount;
    for (std::size_t i = 0; i < kBlockCount; ++i)
    {
        blocks_[i].view = contextValid_ ? blocks[i] : BlockView{};
        blocks_[i].materialized.clear();
    }
    strings_.clear();
    intervalCount_ = 0;
}

void DirectResolver::Invalidate() noexcept
{
    for (BlockState &block : blocks_)
    {
        block.view = {};
        std::vector<Interval>{}.swap(block.materialized);
    }
    std::vector<StringRecord>{}.swap(strings_);
    intervalCount_ = 0;
    contextValid_ = false;
}

bool DirectResolver::ContextMatches(
    const BlockView *const blocks,
    const std::size_t blockCount) const noexcept
{
    if (!contextValid_ || !blocks || blockCount != kBlockCount)
        return false;
    for (std::size_t i = 0; i < kBlockCount; ++i)
    {
        if (blocks_[i].view.base != blocks[i].base
            || blocks_[i].view.size != blocks[i].size)
        {
            return false;
        }
    }
    return true;
}

Status DirectResolver::MarkMaterialized(std::uintptr_t address, std::uint32_t size)
{
    if (!contextValid_)
        return Status::InvalidContext;

    std::uint32_t blockIndex = 0;
    for (; blockIndex < kBlockCount; ++blockIndex)
    {
        if (SpanContains(blocks_[blockIndex].view, address, size))
            break;
    }
    if (blockIndex == kBlockCount)
        return Status::OutOfRange;
    if (!size)
        return Status::Ok;

    const std::uint32_t begin = static_cast<std::uint32_t>(
        address - blocks_[blockIndex].view.base);
    const std::uint32_t end = begin + size;
    std::vector<Interval> &intervals = blocks_[blockIndex].materialized;

    auto first = std::lower_bound(
        intervals.begin(),
        intervals.end(),
        begin,
        [](const Interval &interval, std::uint32_t value)
        {
            return interval.end < value;
        });
    std::uint32_t mergedBegin = begin;
    std::uint32_t mergedEnd = end;
    auto last = first;
    while (last != intervals.end() && last->begin <= mergedEnd)
    {
        mergedBegin = std::min(mergedBegin, last->begin);
        mergedEnd = std::max(mergedEnd, last->end);
        ++last;
    }

    if (first != last)
    {
        const std::size_t mergedCount = static_cast<std::size_t>(last - first);
        first->begin = mergedBegin;
        first->end = mergedEnd;
        intervals.erase(first + 1, last);
        intervalCount_ -= mergedCount - 1;
        return Status::Ok;
    }
    if (intervalCount_ >= maxIntervals_)
        return Status::CapacityExceeded;

    try
    {
        intervals.insert(first, {mergedBegin, mergedEnd});
    }
    catch (const std::bad_alloc &)
    {
        return Status::CapacityExceeded;
    }
    catch (const std::length_error &)
    {
        return Status::CapacityExceeded;
    }
    ++intervalCount_;
    return Status::Ok;
}

bool DirectResolver::ContainsMaterialized(
    std::uint32_t block,
    std::uint32_t offset,
    std::uint32_t size) const
{
    if (!size)
        return true;

    const std::vector<Interval> &intervals = blocks_[block].materialized;
    const auto after = std::upper_bound(
        intervals.begin(),
        intervals.end(),
        offset,
        [](std::uint32_t value, const Interval &interval)
        {
            return value < interval.begin;
        });
    if (after == intervals.begin())
        return false;

    const Interval &candidate = *(after - 1);
    return offset >= candidate.begin
        && offset <= candidate.end
        && size <= candidate.end - offset;
}

Status DirectResolver::RegisterCString(
    std::uintptr_t address,
    std::uint32_t byteCount)
{
    if (!contextValid_)
        return Status::InvalidContext;
    if (!byteCount)
        return Status::InvalidArgument;

    std::uint32_t block = 0;
    for (; block < kBlockCount; ++block)
    {
        if (SpanContains(blocks_[block].view, address, byteCount))
            break;
    }
    if (block == kBlockCount)
        return Status::OutOfRange;

    const std::uint32_t offset = static_cast<std::uint32_t>(
        address - blocks_[block].view.base);
    const auto found = std::lower_bound(
        strings_.begin(),
        strings_.end(),
        StringRecord{block, offset, 0},
        [](const StringRecord &left, const StringRecord &right)
        {
            return left.block < right.block
                || (left.block == right.block && left.offset < right.offset);
        });
    if (found != strings_.end()
        && found->block == block
        && found->offset == offset)
    {
        return found->byteCount == byteCount
            ? Status::Ok
            : Status::MetadataMismatch;
    }
    if (!ContainsMaterialized(block, offset, byteCount))
        return Status::UnmaterializedRange;

    const void *terminator = std::memchr(
        reinterpret_cast<const void *>(address),
        0,
        byteCount);
    if (!terminator)
        return Status::UnterminatedString;
    if (reinterpret_cast<std::uintptr_t>(terminator) - address != byteCount - 1)
        return Status::InvalidStringExtent;
    if (strings_.size() >= maxStrings_)
        return Status::CapacityExceeded;

    try
    {
        strings_.insert(found, {block, offset, byteCount});
    }
    catch (const std::bad_alloc &)
    {
        return Status::CapacityExceeded;
    }
    catch (const std::length_error &)
    {
        return Status::CapacityExceeded;
    }
    return Status::Ok;
}

Status DirectResolver::ValidateCStringAddress(
    std::uintptr_t address,
    std::uint32_t *byteCount) const
{
    if (byteCount)
        *byteCount = 0;
    if (!byteCount)
        return Status::InvalidArgument;
    if (!contextValid_)
        return Status::InvalidContext;

    std::uint32_t block = 0;
    for (; block < kBlockCount; ++block)
    {
        if (SpanContains(blocks_[block].view, address, 1))
            break;
    }
    if (block == kBlockCount)
        return Status::OutOfRange;

    const std::uint32_t offset = static_cast<std::uint32_t>(
        address - blocks_[block].view.base);
    const auto found = std::lower_bound(
        strings_.begin(),
        strings_.end(),
        StringRecord{block, offset, 0},
        [](const StringRecord &left, const StringRecord &right)
        {
            return left.block < right.block
                || (left.block == right.block && left.offset < right.offset);
        });
    if (found == strings_.end()
        || found->block != block
        || found->offset != offset)
    {
        return Status::UnregisteredString;
    }
    if (!ContainsMaterialized(block, offset, found->byteCount))
        return Status::UnmaterializedRange;
    const void *terminator = std::memchr(
        reinterpret_cast<const void *>(address),
        0,
        found->byteCount);
    if (!terminator
        || reinterpret_cast<std::uintptr_t>(terminator) - address
            != found->byteCount - 1)
    {
        return Status::InvalidStringExtent;
    }

    *byteCount = found->byteCount;
    return Status::Ok;
}

Status DirectResolver::ValidateAddress(
    std::uintptr_t address,
    std::uint64_t byteCount,
    std::size_t alignment,
    BlockMask allowedBlocks) const
{
    if (!contextValid_)
        return Status::InvalidContext;
    if (!alignment || (alignment & (alignment - 1)))
        return Status::InvalidAlignment;
    const BlockMask invalidBlocks = static_cast<BlockMask>(
        allowedBlocks & static_cast<BlockMask>(~kAllBlocks));
    if (!allowedBlocks || invalidBlocks)
        return Status::InvalidBlockMask;
    if (byteCount > (std::numeric_limits<std::uint32_t>::max)())
        return Status::SizeOverflow;

    const std::uint32_t extent = static_cast<std::uint32_t>(byteCount);
    std::uint32_t block = kBlockCount;
    bool foundDisallowedBlock = false;
    for (std::uint32_t candidate = 0; candidate < kBlockCount; ++candidate)
    {
        if (!SpanContains(blocks_[candidate].view, address, extent))
            continue;
        if (allowedBlocks & BlockBit(candidate))
        {
            block = candidate;
            break;
        }
        foundDisallowedBlock = true;
    }
    if (block == kBlockCount)
        return foundDisallowedBlock ? Status::WrongBlock : Status::OutOfRange;
    if (address & (alignment - 1))
        return Status::MisalignedAddress;

    const std::uint32_t offset = static_cast<std::uint32_t>(
        address - blocks_[block].view.base);
    if (!ContainsMaterialized(block, offset, extent))
        return Status::UnmaterializedRange;
    return Status::Ok;
}

Status DirectResolver::ResolveBytes(
    disk32::PointerToken token,
    std::uint64_t byteCount,
    std::size_t alignment,
    BlockMask allowedBlocks,
    std::uintptr_t *address) const
{
    if (address)
        *address = 0;
    if (!address)
        return Status::InvalidArgument;
    if (!contextValid_)
        return Status::InvalidContext;
    if (!alignment || (alignment & (alignment - 1)))
        return Status::InvalidAlignment;
    const BlockMask invalidBlocks = static_cast<BlockMask>(
        allowedBlocks & static_cast<BlockMask>(~kAllBlocks));
    if (!allowedBlocks || invalidBlocks)
        return Status::InvalidBlockMask;
    if (!token.isOffset())
        return Status::InvalidToken;

    const std::uint32_t adjusted = token.value - 1;
    const std::uint32_t blockIndex = adjusted >> 28;
    if (blockIndex >= kBlockCount)
        return Status::InvalidToken;
    if (!(allowedBlocks & BlockBit(blockIndex)))
        return Status::WrongBlock;
    if (byteCount > (std::numeric_limits<std::uint32_t>::max)())
        return Status::SizeOverflow;

    std::uint32_t blockSizes[kBlockCount];
    for (std::size_t i = 0; i < kBlockCount; ++i)
        blockSizes[i] = blocks_[i].view.size;

    disk32::DecodedOffset decoded{};
    if (!disk32::DecodeOffset(
            token,
            blockSizes,
            kBlockCount,
            static_cast<std::uint32_t>(byteCount),
            &decoded))
    {
        return Status::OutOfRange;
    }

    const BlockView &block = blocks_[decoded.block].view;
    if (!block.base)
        return Status::UnloadedBlock;
    if (decoded.offset > (std::numeric_limits<std::uintptr_t>::max)() - block.base)
        return Status::OutOfRange;

    const std::uintptr_t resolved = block.base + decoded.offset;
    if (byteCount > (std::numeric_limits<std::uintptr_t>::max)() - resolved)
        return Status::OutOfRange;
    if (resolved & (alignment - 1))
        return Status::MisalignedAddress;
    if (!ContainsMaterialized(
            decoded.block,
            decoded.offset,
            static_cast<std::uint32_t>(byteCount)))
    {
        return Status::UnmaterializedRange;
    }

    *address = resolved;
    return Status::Ok;
}

Status DirectResolver::ResolveArray(
    disk32::PointerToken token,
    std::uint64_t count,
    std::uint64_t elementSize,
    std::size_t alignment,
    BlockMask allowedBlocks,
    std::uintptr_t *address) const
{
    if (address)
        *address = 0;
    if (!elementSize)
        return Status::InvalidArgument;
    if (count > (std::numeric_limits<std::uint64_t>::max)() / elementSize)
        return Status::SizeOverflow;

    return ResolveBytes(token, count * elementSize, alignment, allowedBlocks, address);
}

Status DirectResolver::ResolveCString(
    disk32::PointerToken token,
    BlockMask allowedBlocks,
    std::uintptr_t *address,
    std::uint32_t *byteCount) const
{
    if (address)
        *address = 0;
    if (byteCount)
        *byteCount = 0;
    if (!address || !byteCount)
        return Status::InvalidArgument;

    std::uintptr_t resolved = 0;
    const Status resolvedStatus = ResolveBytes(
        token,
        1,
        1,
        allowedBlocks,
        &resolved);
    if (resolvedStatus != Status::Ok)
        return resolvedStatus;

    std::uint32_t registeredBytes = 0;
    const Status provenanceStatus = ValidateCStringAddress(
        resolved,
        &registeredBytes);
    if (provenanceStatus != Status::Ok)
        return provenanceStatus;

    const Status fullSpanStatus = ResolveBytes(
        token,
        registeredBytes,
        1,
        allowedBlocks,
        &resolved);
    if (fullSpanStatus != Status::Ok)
        return fullSpanStatus;

    *byteCount = registeredBytes;
    *address = resolved;
    return Status::Ok;
}

Status DirectResolver::ContiguousMaterializedBytes(
    std::uint32_t block,
    std::uint32_t offset,
    std::uint32_t *bytes) const
{
    if (bytes)
        *bytes = 0;
    if (!bytes)
        return Status::InvalidArgument;
    if (!contextValid_)
        return Status::InvalidContext;
    if (block >= kBlockCount || offset > blocks_[block].view.size)
        return Status::OutOfRange;

    const std::vector<Interval> &intervals = blocks_[block].materialized;
    const auto after = std::upper_bound(
        intervals.begin(),
        intervals.end(),
        offset,
        [](std::uint32_t value, const Interval &interval)
        {
            return value < interval.begin;
        });
    if (after == intervals.begin())
        return Status::UnmaterializedRange;

    const Interval &candidate = *(after - 1);
    if (offset < candidate.begin || offset >= candidate.end)
        return Status::UnmaterializedRange;

    *bytes = candidate.end - offset;
    return Status::Ok;
}

AliasRegistry::AliasRegistry(
    const std::size_t maxRecords,
    const std::uint64_t initialGeneration) noexcept
    : maxRecords_(std::min(
          maxRecords,
          static_cast<std::size_t>(AliasHandle::kInvalidRecord))),
      generation_(initialGeneration)
{
}

Status AliasRegistry::Reset(
    const BlockView *const blocks,
    const std::size_t blockCount) noexcept
{
    if (!blocks || blockCount != kBlockCount)
    {
        Invalidate();
        return Status::InvalidArgument;
    }
    if (!CanReset())
        return Status::GenerationExhausted;

    records_.clear();
    ++generation_;
    std::copy_n(blocks, kBlockCount, blocks_);
    contextValid_ = true;
    return Status::Ok;
}

void AliasRegistry::Invalidate() noexcept
{
    for (Record &record : records_)
    {
        // These native addresses and publication records must be overwritten
        // before their allocation is released. Volatile stores keep an
        // optimizing compiler from deleting the scrub as dead memory writes.
        volatile std::uintptr_t *const resolvedAddress =
            &record.resolvedAddress;
        volatile std::uint32_t *const metadata = &record.metadata;
        volatile bool *const published = &record.published;
        volatile std::uint32_t *const offset = &record.offset;
        volatile AliasKind *const kind = &record.kind;
        *resolvedAddress = 0;
        *metadata = 0;
        *published = false;
        *offset = 0;
        *kind = AliasKind::Invalid;
    }
    std::vector<Record>{}.swap(records_);
    for (BlockView &block : blocks_)
        block = {};
    contextValid_ = false;
}

bool AliasRegistry::CanReset() const noexcept
{
    return generation_
        != (std::numeric_limits<std::uint64_t>::max)();
}

bool AliasRegistry::ContextMatches(
    const BlockView *const blocks,
    const std::size_t blockCount) const noexcept
{
    if (!contextValid_ || !blocks || blockCount != kBlockCount)
        return false;
    for (std::size_t i = 0; i < kBlockCount; ++i)
    {
        if (blocks_[i].base != blocks[i].base
            || blocks_[i].size != blocks[i].size)
        {
            return false;
        }
    }
    return true;
}

Status AliasRegistry::FindSlot(std::uintptr_t slotAddress, std::uint32_t *offset) const
{
    if (!contextValid_ || !offset)
        return Status::InvalidContext;
    if (slotAddress & (alignof(std::uint32_t) - 1))
        return Status::MisalignedSlot;

    for (std::uint32_t blockIndex = 0; blockIndex < kBlockCount; ++blockIndex)
    {
        if (!SpanContains(
                blocks_[blockIndex],
                slotAddress,
                static_cast<std::uint32_t>(sizeof(std::uint32_t))))
            continue;
        if (blockIndex != kAliasBlock)
            return Status::WrongBlock;

        *offset = static_cast<std::uint32_t>(slotAddress - blocks_[blockIndex].base);
        if (*offset > disk32::kOffsetMask)
            return Status::OutOfRange;
        return Status::Ok;
    }
    return blocks_[kAliasBlock].size && !blocks_[kAliasBlock].base
        ? Status::UnloadedBlock
        : Status::OutOfRange;
}

Status AliasRegistry::RegisterSlot(
    std::uintptr_t slotAddress,
    AliasKind kind,
    AliasHandle *handle)
{
    if (handle)
        *handle = {};
    if (!handle || kind == AliasKind::Invalid || kind >= AliasKind::Count)
        return Status::InvalidArgument;

    std::uint32_t offset = 0;
    const Status slotStatus = FindSlot(slotAddress, &offset);
    if (slotStatus != Status::Ok)
        return slotStatus;

    if (!records_.empty())
    {
        // Registered alias and completed-object slots come from block 4's
        // monotonically advancing stream cursor. Keeping records in that order
        // makes lookup a stable binary search while handles remain indices.
        if (offset == records_.back().offset)
            return Status::DuplicateSlot;
        if (offset < records_.back().offset)
            return Status::NonMonotonicSlot;
    }
    if (records_.size() >= maxRecords_)
        return Status::CapacityExceeded;

    try
    {
        records_.push_back({offset, kind, 0, 0, false});
    }
    catch (const std::bad_alloc &)
    {
        return Status::CapacityExceeded;
    }
    catch (const std::length_error &)
    {
        return Status::CapacityExceeded;
    }

    handle->recordIndex_ = static_cast<std::uint32_t>(records_.size() - 1);
    handle->generation_ = generation_;
    return Status::Ok;
}

Status AliasRegistry::Publish(
    AliasHandle handle,
    AliasKind expectedKind,
    std::uintptr_t resolvedAddress,
    std::uint32_t metadata)
{
    if (!contextValid_)
        return Status::InvalidContext;
    if (expectedKind == AliasKind::Invalid || expectedKind >= AliasKind::Count)
        return Status::InvalidArgument;
    if (!handle)
        return Status::InvalidHandle;
    if (handle.generation_ != generation_)
        return Status::StaleHandle;
    if (handle.recordIndex_ >= records_.size())
        return Status::InvalidHandle;

    Record &record = records_[handle.recordIndex_];
    if (record.kind != expectedKind)
        return Status::KindMismatch;
    if (record.published)
        return Status::AlreadyPublished;
    if (RequiresExactStartPublication(expectedKind))
    {
        const BlockView &aliasBlock = blocks_[kAliasBlock];
        if (!aliasBlock.base
            || record.offset > (std::numeric_limits<std::uintptr_t>::max)() - aliasBlock.base
            || resolvedAddress != aliasBlock.base + record.offset)
        {
            return Status::MetadataMismatch;
        }
    }

    record.resolvedAddress = resolvedAddress;
    record.metadata = metadata;
    record.published = true;
    return Status::Ok;
}

Status AliasRegistry::Resolve(
    disk32::PointerToken token,
    AliasKind expectedKind,
    std::uint32_t expectedMetadata,
    std::uintptr_t *resolvedAddress) const
{
    if (resolvedAddress)
        *resolvedAddress = 0;
    if (!resolvedAddress || expectedKind == AliasKind::Invalid || expectedKind >= AliasKind::Count)
        return Status::InvalidArgument;
    if (!contextValid_)
        return Status::InvalidContext;
    if (!token.isOffset())
        return Status::InvalidToken;

    const std::uint32_t adjusted = token.value - 1;
    const std::uint32_t blockIndex = adjusted >> 28;
    if (blockIndex >= kBlockCount)
        return Status::InvalidToken;
    if (blockIndex != kAliasBlock)
        return Status::WrongBlock;

    std::uint32_t blockSizes[kBlockCount];
    for (std::size_t i = 0; i < kBlockCount; ++i)
        blockSizes[i] = blocks_[i].size;

    disk32::DecodedOffset decoded{};
    if (!disk32::DecodeOffset(
            token,
            blockSizes,
            kBlockCount,
            static_cast<std::uint32_t>(sizeof(std::uint32_t)),
            &decoded))
    {
        return Status::OutOfRange;
    }
    if (!blocks_[decoded.block].base)
        return Status::UnloadedBlock;
    if (decoded.offset > (std::numeric_limits<std::uintptr_t>::max)() - blocks_[decoded.block].base)
        return Status::OutOfRange;

    const std::uintptr_t slotAddress = blocks_[decoded.block].base + decoded.offset;
    if (slotAddress & (alignof(std::uint32_t) - 1))
        return Status::MisalignedSlot;

    const auto found = std::lower_bound(
        records_.begin(),
        records_.end(),
        decoded.offset,
        [](const Record &record, std::uint32_t offset)
        {
            return record.offset < offset;
        });
    if (found == records_.end() || found->offset != decoded.offset)
        return Status::UnregisteredSlot;
    if (!found->published)
        return Status::PendingSlot;
    if (found->kind != expectedKind)
        return Status::KindMismatch;
    if (found->metadata != expectedMetadata)
        return Status::MetadataMismatch;

    *resolvedAddress = found->resolvedAddress;
    return Status::Ok;
}

const char *StatusName(Status status)
{
    switch (status)
    {
    case Status::Ok: return "ok";
    case Status::InvalidArgument: return "invalid argument";
    case Status::InvalidContext: return "invalid context";
    case Status::InvalidToken: return "invalid token";
    case Status::WrongBlock: return "wrong block";
    case Status::UnloadedBlock: return "unloaded block";
    case Status::OutOfRange: return "out of range";
    case Status::MisalignedSlot: return "misaligned slot";
    case Status::DuplicateSlot: return "duplicate slot";
    case Status::NonMonotonicSlot: return "non-monotonic slot";
    case Status::CapacityExceeded: return "capacity exceeded";
    case Status::InvalidHandle: return "invalid handle";
    case Status::StaleHandle: return "stale handle";
    case Status::KindMismatch: return "kind mismatch";
    case Status::AlreadyPublished: return "already published";
    case Status::UnregisteredSlot: return "unregistered slot";
    case Status::PendingSlot: return "pending slot";
    case Status::MetadataMismatch: return "metadata mismatch";
    case Status::InvalidAlignment: return "invalid alignment";
    case Status::InvalidBlockMask: return "invalid block mask";
    case Status::SizeOverflow: return "size overflow";
    case Status::MisalignedAddress: return "misaligned address";
    case Status::UnmaterializedRange: return "unmaterialized range";
    case Status::UnterminatedString: return "unterminated string";
    case Status::InvalidStringExtent: return "invalid string extent";
    case Status::UnregisteredString: return "unregistered string";
    case Status::GenerationExhausted: return "generation exhausted";
    }
    return "unknown";
}
}

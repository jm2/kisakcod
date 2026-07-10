#include "db_relocation.h"

#include <algorithm>
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

DirectResolver::DirectResolver(std::size_t maxIntervals)
    : maxIntervals_(maxIntervals)
{
}

void DirectResolver::Reset(const BlockView *blocks, std::size_t blockCount)
{
    contextValid_ = blocks && blockCount == kBlockCount;
    for (std::size_t i = 0; i < kBlockCount; ++i)
    {
        blocks_[i].view = contextValid_ ? blocks[i] : BlockView{};
        blocks_[i].materialized.clear();
    }
    intervalCount_ = 0;
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

AliasRegistry::AliasRegistry(std::size_t maxRecords)
    : maxRecords_(std::min(
          maxRecords,
          static_cast<std::size_t>(AliasHandle::kInvalidRecord)))
{
}

void AliasRegistry::Reset(const BlockView *blocks, std::size_t blockCount)
{
    for (BlockView &block : blocks_)
        block = {};

    contextValid_ = blocks && blockCount == kBlockCount;
    if (contextValid_)
        std::copy_n(blocks, kBlockCount, blocks_);

    records_.clear();
    ++generation_;
    if (!generation_)
        ++generation_;
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
        // DB_InsertPointer reserves from block 4's monotonically advancing
        // stream cursor. Keeping records in that order makes alias lookup a
        // stable binary search while handles remain vector indices.
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
    }
    return "unknown";
}
}

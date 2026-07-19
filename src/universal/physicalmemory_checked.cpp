#include "physicalmemory_checked.h"

#include <cstddef>
#include <cstdint>

namespace physical_memory
{
namespace
{
constexpr std::uint32_t kPhysicalAllocationTypeCount = 2;
constexpr std::uint32_t kPhysicalAllocationCapacity = 32;
constexpr std::uint32_t kInvalidReceiptIndex = UINT32_MAX;

[[nodiscard]] bool ValidatePrimTopology(
    const PhysicalMemoryPrim &prim,
    const std::uint32_t allocType) noexcept
{
    if (prim.allocListCount > kPhysicalAllocationCapacity)
        return false;

    for (std::uint32_t index = prim.allocListCount;
         index < kPhysicalAllocationCapacity;
         ++index)
    {
        // Positions beyond count are deliberately ignored. The legacy tail
        // collapse leaves stale, bounded historical positions in these slots.
        if (prim.allocList[index].name != nullptr)
            return false;
    }

    if (prim.allocListCount == 0)
        return prim.allocName == nullptr;

    if (prim.allocList[prim.allocListCount - 1].name == nullptr)
        return false;
    if (prim.allocName != nullptr
        && prim.allocName != prim.allocList[prim.allocListCount - 1].name)
    {
        return false;
    }

    for (std::uint32_t index = 0; index < prim.allocListCount; ++index)
    {
        const std::uint32_t position = prim.allocList[index].pos;
        if (allocType == 0)
        {
            if (position > prim.pos
                || (index != 0
                    && prim.allocList[index - 1].pos > position))
            {
                return false;
            }
        }
        else if (position < prim.pos
            || (index != 0
                && prim.allocList[index - 1].pos < position))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateMemoryTopology(
    const PhysicalMemory *const memory) noexcept
{
    return memory->buf != nullptr
        && memory->prim[0].pos <= memory->prim[1].pos
        && ValidatePrimTopology(memory->prim[0], 0)
        && ValidatePrimTopology(memory->prim[1], 1);
}

} // namespace

AllocationReceipt::AllocationReceipt() noexcept
    : self_(this),
      owner_(nullptr),
      prim_(nullptr),
      name_(nullptr),
      allocType_(kPhysicalAllocationTypeCount),
      index_(kInvalidReceiptIndex),
      startPos_(0),
      phase_(Phase::Empty),
      reserved_{}
{
}

bool AllocationReceipt::reservedIsZero() const noexcept
{
    return reserved_[0] == 0 && reserved_[1] == 0 && reserved_[2] == 0;
}

bool AllocationReceipt::hasValidPhase() const noexcept
{
    switch (phase_)
    {
    case Phase::Empty:
    case Phase::Begun:
    case Phase::Ended:
    case Phase::Freed:
        return true;
    }
    return false;
}

bool AllocationReceipt::isPristine() const noexcept
{
    return self_ == this && phase_ == Phase::Empty && owner_ == nullptr
        && prim_ == nullptr && name_ == nullptr
        && allocType_ == kPhysicalAllocationTypeCount
        && index_ == kInvalidReceiptIndex && startPos_ == 0
        && reservedIsZero();
}

bool AllocationReceipt::isBound() const noexcept
{
    if (self_ != this || owner_ == nullptr || prim_ == nullptr
        || name_ == nullptr || allocType_ >= kPhysicalAllocationTypeCount
        || index_ >= kPhysicalAllocationCapacity || !reservedIsZero())
    {
        return false;
    }

    return prim_ == &owner_->prim[allocType_];
}

bool AllocationReceipt::matchesEntry(
    const PhysicalMemoryPrim &prim) const noexcept
{
    if (index_ >= prim.allocListCount)
        return false;
    const PhysicalMemoryAllocation &entry = prim.allocList[index_];
    return entry.name == name_ && entry.pos == startPos_;
}

AllocationScopeStatus TryBegin(
    PhysicalMemory *const memory,
    const std::uint32_t allocType,
    const char *const stableName,
    AllocationReceipt *const receipt) noexcept
{
    if (memory == nullptr || stableName == nullptr || receipt == nullptr)
        return AllocationScopeStatus::InvalidArgument;
    if (allocType >= kPhysicalAllocationTypeCount)
        return AllocationScopeStatus::InvalidAllocationType;
    if (receipt->self_ != receipt || !receipt->hasValidPhase())
        return AllocationScopeStatus::ReceiptMismatch;
    if (receipt->phase_ != AllocationReceipt::Phase::Empty)
        return AllocationScopeStatus::ReceiptInUse;
    if (!receipt->isPristine())
        return AllocationScopeStatus::ReceiptMismatch;
    if (!ValidateMemoryTopology(memory))
        return AllocationScopeStatus::MalformedState;

    PhysicalMemoryPrim &prim = memory->prim[allocType];
    if (prim.allocName != nullptr)
        return AllocationScopeStatus::Busy;
    if (prim.allocListCount == kPhysicalAllocationCapacity)
        return AllocationScopeStatus::CapacityExceeded;

    const std::uint32_t index = prim.allocListCount;
    PhysicalMemoryAllocation &entry = prim.allocList[index];
    entry.name = stableName;
    entry.pos = prim.pos;
    prim.allocListCount = index + 1;
    prim.allocName = stableName;

    receipt->owner_ = memory;
    receipt->prim_ = &prim;
    receipt->name_ = stableName;
    receipt->allocType_ = allocType;
    receipt->index_ = index;
    receipt->startPos_ = entry.pos;
    receipt->phase_ = AllocationReceipt::Phase::Begun;
    return AllocationScopeStatus::Success;
}

AllocationScopeStatus TryEnd(AllocationReceipt *const receipt) noexcept
{
    if (receipt == nullptr)
        return AllocationScopeStatus::InvalidArgument;
    if (receipt->self_ != receipt || !receipt->hasValidPhase())
        return AllocationScopeStatus::ReceiptMismatch;
    if (receipt->phase_ == AllocationReceipt::Phase::Ended
        || receipt->phase_ == AllocationReceipt::Phase::Freed)
    {
        return AllocationScopeStatus::AlreadyComplete;
    }
    if (receipt->phase_ != AllocationReceipt::Phase::Begun)
        return AllocationScopeStatus::WrongPhase;
    if (!receipt->isBound())
        return AllocationScopeStatus::ReceiptMismatch;
    if (!ValidateMemoryTopology(receipt->owner_))
        return AllocationScopeStatus::MalformedState;

    PhysicalMemoryPrim &prim = *receipt->prim_;
    if (receipt->index_ + 1 != prim.allocListCount
        || prim.allocName != receipt->name_
        || !receipt->matchesEntry(prim))
    {
        return AllocationScopeStatus::ReceiptMismatch;
    }

    prim.allocName = nullptr;
    receipt->phase_ = AllocationReceipt::Phase::Ended;
    return AllocationScopeStatus::Success;
}

AllocationScopeStatus TryFree(AllocationReceipt *const receipt) noexcept
{
    if (receipt == nullptr)
        return AllocationScopeStatus::InvalidArgument;
    if (receipt->self_ != receipt || !receipt->hasValidPhase())
        return AllocationScopeStatus::ReceiptMismatch;
    if (receipt->phase_ == AllocationReceipt::Phase::Freed)
        return AllocationScopeStatus::AlreadyComplete;
    if (receipt->phase_ != AllocationReceipt::Phase::Ended)
        return AllocationScopeStatus::WrongPhase;
    if (!receipt->isBound())
        return AllocationScopeStatus::ReceiptMismatch;
    if (!ValidateMemoryTopology(receipt->owner_))
        return AllocationScopeStatus::MalformedState;

    PhysicalMemoryPrim &prim = *receipt->prim_;
    if (prim.allocName != nullptr)
        return AllocationScopeStatus::Busy;
    if (!receipt->matchesEntry(prim))
        return AllocationScopeStatus::ReceiptMismatch;

    PhysicalMemoryAllocation &entry = prim.allocList[receipt->index_];
    entry.name = nullptr;
    if (receipt->index_ + 1 == prim.allocListCount)
    {
        std::uint32_t remaining = prim.allocListCount;
        std::uint32_t restoredPosition = prim.pos;
        while (remaining != 0
            && prim.allocList[remaining - 1].name == nullptr)
        {
            restoredPosition = prim.allocList[remaining - 1].pos;
            --remaining;
        }
        prim.pos = restoredPosition;
        prim.allocListCount = remaining;
    }

    receipt->phase_ = AllocationReceipt::Phase::Freed;
    return AllocationScopeStatus::Success;
}
} // namespace physical_memory

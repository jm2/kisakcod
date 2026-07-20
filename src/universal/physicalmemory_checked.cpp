#include "physicalmemory_checked.h"
#include "physicalmemory_runtime.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace physical_memory
{
namespace
{
constexpr std::uint32_t kPhysicalAllocationTypeCount = 2;
constexpr std::uint32_t kPhysicalAllocationCapacity = 32;
constexpr std::uint32_t kInvalidReceiptIndex = UINT32_MAX;
constexpr std::uint8_t kPhaseWitnessMask = 0xA5;
constexpr std::uint8_t kTerminalTagFirst = 0x6D;
constexpr std::uint8_t kTerminalTagSecond = 0xB2;

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
    {
        return prim.allocName == nullptr
            && (allocType != 0 || prim.pos == 0);
    }

    if (prim.allocList[prim.allocListCount - 1].name == nullptr)
        return false;
    if (allocType == 0 && prim.allocList[0].pos != 0)
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
      phaseWitness_(
          static_cast<std::uint8_t>(Phase::Empty) ^ kPhaseWitnessMask),
      reserved_{}
{
}

AllocationReceipt::~AllocationReceipt() noexcept
{
}

bool AllocationReceipt::reservedIsZero() const noexcept
{
    return reserved_[0] == 0 && reserved_[1] == 0;
}

bool AllocationReceipt::hasValidPhaseWitness() const noexcept
{
    return phaseWitness_
        == (static_cast<std::uint8_t>(phase_) ^ kPhaseWitnessMask);
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

bool AllocationReceipt::hasCanonicalTerminalState() const noexcept
{
    return startPos_ == 0
        && reserved_[0] == kTerminalTagFirst
        && reserved_[1] == kTerminalTagSecond;
}

void AllocationReceipt::setCanonicalTerminalState() noexcept
{
    startPos_ = 0;
    reserved_[0] = kTerminalTagFirst;
    reserved_[1] = kTerminalTagSecond;
}

bool AllocationReceipt::isPristine() const noexcept
{
    return self_ == this && phase_ == Phase::Empty && owner_ == nullptr
        && prim_ == nullptr && name_ == nullptr
        && allocType_ == kPhysicalAllocationTypeCount
        && index_ == kInvalidReceiptIndex && startPos_ == 0
        && hasValidPhaseWitness() && reservedIsZero();
}

bool AllocationReceipt::isBound() const noexcept
{
    if (self_ != this || owner_ == nullptr || prim_ == nullptr
        || name_ == nullptr || allocType_ >= kPhysicalAllocationTypeCount
        || index_ >= kPhysicalAllocationCapacity
        || !hasValidPhase() || !hasValidPhaseWitness())
    {
        return false;
    }

    if (phase_ == Phase::Freed)
    {
        if (!hasCanonicalTerminalState())
            return false;
    }
    else if (!reservedIsZero())
    {
        return false;
    }

    return prim_ == &owner_->prim[allocType_];
}

bool AllocationReceipt::isCanonical() const noexcept
{
    if (self_ != this || !hasValidPhase() || !hasValidPhaseWitness())
        return false;
    if (phase_ == Phase::Empty)
        return isPristine();
    return isBound();
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
    if (!receipt->isCanonical())
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
    receipt->phaseWitness_ =
        static_cast<std::uint8_t>(receipt->phase_) ^ kPhaseWitnessMask;
    return AllocationScopeStatus::Success;
}

AllocationScopeStatus TryEnd(AllocationReceipt *const receipt) noexcept
{
    if (receipt == nullptr)
        return AllocationScopeStatus::InvalidArgument;
    if (!receipt->isCanonical())
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
    receipt->phaseWitness_ =
        static_cast<std::uint8_t>(receipt->phase_) ^ kPhaseWitnessMask;
    return AllocationScopeStatus::Success;
}

AllocationScopeStatus TryFree(AllocationReceipt *const receipt) noexcept
{
    if (receipt == nullptr)
        return AllocationScopeStatus::InvalidArgument;
    if (!receipt->isCanonical())
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
    if (!receipt->matchesEntry(prim))
        return AllocationScopeStatus::ReceiptMismatch;
    if (prim.allocName != nullptr)
        return AllocationScopeStatus::Busy;

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
    receipt->phaseWitness_ =
        static_cast<std::uint8_t>(receipt->phase_) ^ kPhaseWitnessMask;
    receipt->setCanonicalTerminalState();
    return AllocationScopeStatus::Success;
}
} // namespace physical_memory

bool pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
    const physical_memory::AllocationReceipt &receipt,
    const PhysicalMemory &owner,
    const std::uint32_t allocType,
    const std::uint32_t index,
    const char *const stableName,
    const AllocationReceiptPhase expectedPhase) noexcept
{
    using Receipt = physical_memory::AllocationReceipt;
    if (expectedPhase == AllocationReceiptPhase::Pristine)
        return receipt.isPristine();

    Receipt::Phase privatePhase{};
    switch (expectedPhase)
    {
    case AllocationReceiptPhase::Pristine:
        return false;
    case AllocationReceiptPhase::Begun:
        privatePhase = Receipt::Phase::Begun;
        break;
    case AllocationReceiptPhase::Ended:
        privatePhase = Receipt::Phase::Ended;
        break;
    case AllocationReceiptPhase::Freed:
        privatePhase = Receipt::Phase::Freed;
        break;
    default:
        return false;
    }

    if (allocType >= 2u
        || index >= MAX_PHYSICAL_ALLOCATIONS || stableName == nullptr
        || receipt.self_ != &receipt || receipt.owner_ != &owner
        || receipt.prim_ != &owner.prim[allocType]
        || receipt.allocType_ != allocType || receipt.index_ != index
        || receipt.name_ != stableName || !receipt.isCanonical()
        || receipt.phase_ != privatePhase)
    {
        return false;
    }

    if (privatePhase == Receipt::Phase::Freed)
        return true;

    const PhysicalMemoryPrim &prim = owner.prim[allocType];
    if (receipt.startPos_ != prim.allocList[index].pos)
        return false;
    if (!receipt.matchesEntry(prim))
        return false;
    if (privatePhase == Receipt::Phase::Begun)
    {
        return index + 1 == prim.allocListCount
            && prim.allocName == stableName;
    }
    return prim.allocName != stableName;
}

bool pmem_runtime::detail::AuthenticateAllocationRangeNoLock(
    const physical_memory::AllocationReceipt &receipt,
    const PhysicalMemory &owner,
    const std::uint32_t allocType,
    const std::uint32_t index,
    const char *const stableName,
    const void *const storage,
    const std::size_t size,
    const AllocationReceiptPhase expectedPhase) noexcept
{
    if ((expectedPhase != AllocationReceiptPhase::Begun
            && expectedPhase != AllocationReceiptPhase::Ended)
        || !AuthenticateAllocationReceiptNoLock(
            receipt, owner, allocType, index, stableName, expectedPhase)
        || storage == nullptr || size == 0)
    {
        return false;
    }

    const std::uintptr_t storageBegin =
        reinterpret_cast<std::uintptr_t>(storage);
    const std::uintptr_t maximum =
        std::numeric_limits<std::uintptr_t>::max();
    if (storageBegin > maximum - size)
        return false;
    const std::uintptr_t storageEnd = storageBegin + size;

    const PhysicalMemoryPrim &prim = owner.prim[allocType];
    const std::uint32_t nextPosition = index + 1 < prim.allocListCount
        ? prim.allocList[index + 1].pos
        : prim.pos;
    const std::uint32_t entryPosition = prim.allocList[index].pos;
    const std::uint32_t lowerOffset = allocType == 0
        ? entryPosition
        : nextPosition;
    const std::uint32_t upperOffset = allocType == 0
        ? nextPosition
        : entryPosition;
    if (lowerOffset > upperOffset)
        return false;

    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(owner.buf);
    if (base == 0 || base > maximum - upperOffset)
        return false;
    const std::uintptr_t allocationBegin = base + lowerOffset;
    const std::uintptr_t allocationEnd = base + upperOffset;
    return storageBegin >= allocationBegin && storageEnd <= allocationEnd;
}

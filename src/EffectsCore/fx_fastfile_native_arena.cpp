#include <EffectsCore/fx_fastfile_native_arena.h>

#include <cstring>
#include <limits>

namespace fx::fastfile
{
namespace
{
using Status = FxFastFileNativeArenaStatus;

[[nodiscard]] constexpr bool IsPowerOfTwo(const std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1u)) == 0;
}

class OperationGate final
{
public:
    explicit OperationGate(bool *flag) noexcept : flag_(flag)
    {
        if (*flag_)
        {
            flag_ = nullptr;
            return;
        }
        *flag_ = true;
    }

    ~OperationGate() noexcept
    {
        if (flag_)
            *flag_ = false;
    }

    OperationGate(const OperationGate &) = delete;
    OperationGate &operator=(const OperationGate &) = delete;

    [[nodiscard]] bool acquired() const noexcept
    {
        return flag_ != nullptr;
    }

private:
    bool *flag_;
};
} // namespace

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryBind(
    void *const storage,
    const std::size_t capacity,
    const std::uint64_t zoneIdentity) noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!storage || capacity == 0 || zoneIdentity == 0)
        return Status::InvalidArgument;
    if (reinterpret_cast<std::uintptr_t>(storage)
            % kFxFastFileNativeArenaStorageAlignment
        != 0)
    {
        return Status::MisalignedStorage;
    }
    if (capacity
        > (std::numeric_limits<std::uintptr_t>::max)()
            - reinterpret_cast<std::uintptr_t>(storage))
    {
        return Status::SizeOverflow;
    }
    if (storage_ || transactionDepth_ != 0)
        return Status::InvalidPhase;

    storage_ = storage;
    zoneIdentity_ = zoneIdentity;
    capacity_ = capacity;
    cursor_ = 0;
    committed_ = 0;
    for (std::uint32_t depth = 0;
         depth < kFxFastFileNativeArenaMaxTransactionDepth;
         ++depth)
    {
        transactionSerials_[depth] = 0;
        transactionBases_[depth] = 0;
    }
    return Status::Success;
}

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryUnbind() noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!storage_)
        return Status::InvalidPhase;
    if (transactionDepth_ != 0)
        return Status::InvalidPhase;

    storage_ = nullptr;
    zoneIdentity_ = 0;
    capacity_ = 0;
    cursor_ = 0;
    committed_ = 0;
    return Status::Success;
}

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryBeginTransaction(
    FxFastFileNativeArenaTransaction *const outTransaction) noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!outTransaction)
        return Status::InvalidArgument;
    if (!storage_)
        return Status::InvalidPhase;
    if (transactionDepth_ >= kFxFastFileNativeArenaMaxTransactionDepth)
        return Status::TransactionLimit;

    FxFastFileNativeArenaTransaction transaction;
    transaction.arenaIdentity_ = this;
    transaction.serial_ = nextSerial_++;
    transaction.depth_ = transactionDepth_ + 1;

    transactionSerials_[transactionDepth_] = transaction.serial_;
    transactionBases_[transactionDepth_] = cursor_;
    ++transactionDepth_;
    *outTransaction = transaction;
    return Status::Success;
}

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryReserve(
    const FxFastFileNativeArenaTransaction &transaction,
    const std::size_t byteCount,
    const std::size_t alignment,
    void **const outStorage) noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!outStorage || byteCount == 0 || !IsPowerOfTwo(alignment))
        return Status::InvalidArgument;
    if (!storage_)
        return Status::InvalidPhase;
    if (transactionDepth_ == 0
        || transaction.arenaIdentity_ != this
        || transaction.depth_ != transactionDepth_
        || transaction.serial_ != transactionSerials_[transactionDepth_ - 1])
    {
        return Status::InvalidTransaction;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage_);
    const std::uintptr_t unaligned = base + static_cast<std::uintptr_t>(cursor_);
    const std::uintptr_t alignmentMask =
        static_cast<std::uintptr_t>(alignment) - 1u;
    if (alignmentMask > (std::numeric_limits<std::uintptr_t>::max)() - unaligned)
        return Status::SizeOverflow;
    const std::uintptr_t aligned = (unaligned + alignmentMask) & ~alignmentMask;
    const std::uint64_t alignedOffset = static_cast<std::uint64_t>(aligned - base);
    if (alignedOffset > capacity_ || byteCount > capacity_ - alignedOffset)
        return Status::InsufficientCapacity;

    std::uint8_t *const bytes = static_cast<std::uint8_t *>(storage_);
    std::memset(
        bytes + cursor_,
        0,
        static_cast<std::size_t>(alignedOffset - cursor_) + byteCount);
    cursor_ = alignedOffset + byteCount;
    *outStorage = bytes + alignedOffset;
    return Status::Success;
}

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryCommit(
    const FxFastFileNativeArenaTransaction &transaction) noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!storage_)
        return Status::InvalidPhase;
    if (transactionDepth_ == 0
        || transaction.arenaIdentity_ != this
        || transaction.depth_ != transactionDepth_
        || transaction.serial_ != transactionSerials_[transactionDepth_ - 1])
    {
        return Status::InvalidTransaction;
    }

    committed_ = cursor_;
    --transactionDepth_;
    transactionSerials_[transactionDepth_] = 0;
    transactionBases_[transactionDepth_] = 0;
    return Status::Success;
}

FxFastFileNativeArenaStatus FxFastFileNativeArena::TryAbandon(
    const FxFastFileNativeArenaTransaction &transaction) noexcept
{
    OperationGate gate(&operating_);
    if (!gate.acquired())
        return Status::Busy;
    if (!storage_)
        return Status::InvalidPhase;
    if (transactionDepth_ == 0
        || transaction.arenaIdentity_ != this
        || transaction.depth_ != transactionDepth_
        || transaction.serial_ != transactionSerials_[transactionDepth_ - 1])
    {
        return Status::InvalidTransaction;
    }

    const std::uint64_t transactionBase =
        transactionBases_[transactionDepth_ - 1];
    const std::uint64_t reclaimBase =
        transactionBase > committed_ ? transactionBase : committed_;
    if (cursor_ > reclaimBase)
    {
        std::memset(
            static_cast<std::uint8_t *>(storage_) + reclaimBase,
            0,
            static_cast<std::size_t>(cursor_ - reclaimBase));
        cursor_ = reclaimBase;
    }
    --transactionDepth_;
    transactionSerials_[transactionDepth_] = 0;
    transactionBases_[transactionDepth_] = 0;
    return Status::Success;
}
} // namespace fx::fastfile

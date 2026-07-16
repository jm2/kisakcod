#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

namespace fx::fastfile
{
// Zone-owned aligned native arena for widened fast-file FX output.
//
// The arena never allocates: it is bound to one caller-owned storage region
// whose lifetime the owning zone controls, and it linearly reserves aligned,
// zero-filled extents from that region under an explicit LIFO transaction
// protocol.  Reservations exist only inside an open transaction.  Committing
// a transaction ratchets a committed watermark forward to the current cursor;
// abandoning a transaction reclaims and rezeroes only the region above that
// watermark, so storage referenced by an already-committed (published) inner
// transaction is never handed out twice.  Bytes a failed outer transaction
// reserved beneath a later-committed inner watermark stay permanently retired
// until the zone releases the whole region.  The arena is report-free and
// single-threaded by contract; reentrant calls fail Busy.

enum class FxFastFileNativeArenaStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidPhase,
    MisalignedStorage,
    SizeOverflow,
    InsufficientCapacity,
    TransactionLimit,
    InvalidTransaction,
};

inline constexpr std::size_t kFxFastFileNativeArenaStorageAlignment = 16;
inline constexpr std::uint32_t kFxFastFileNativeArenaMaxTransactionDepth = 2;

class FxFastFileNativeArena;

// Opaque value token for one open arena transaction.  Tokens are bound to the
// issuing arena identity, their begin serial, and their LIFO depth; a token
// dies when its transaction commits or is abandoned, and every later use of
// the dead value fails InvalidTransaction.
class alignas(8) FxFastFileNativeArenaTransaction final
{
public:
    constexpr FxFastFileNativeArenaTransaction() noexcept = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return arenaIdentity_ != nullptr && serial_ != 0;
    }

    [[nodiscard]] constexpr bool operator==(
        const FxFastFileNativeArenaTransaction &) const noexcept = default;

private:
    friend class FxFastFileNativeArena;

    const FxFastFileNativeArena *arenaIdentity_ = nullptr;
#if !KISAK_ARCH_64BIT
    // Keep serial_ at its LP64 offset on ILP32 targets.
    std::uint32_t arenaIdentityPadding_ = 0;
#endif
    std::uint64_t serial_ = 0;
    // depth_ occupies 0x10; alignas(8) tail padding completes 0x18 exactly.
    std::uint32_t depth_ = 0;
};

class alignas(8) FxFastFileNativeArena final
{
public:
    FxFastFileNativeArena() noexcept = default;
    ~FxFastFileNativeArena() noexcept = default;

    FxFastFileNativeArena(const FxFastFileNativeArena &) = delete;
    FxFastFileNativeArena &operator=(const FxFastFileNativeArena &) = delete;
    FxFastFileNativeArena(FxFastFileNativeArena &&) = delete;
    FxFastFileNativeArena &operator=(FxFastFileNativeArena &&) = delete;

    // Binds one zone-owned storage region and resets all cursors and
    // accounting.  The arena must currently be unbound; replacing a bound
    // region requires an explicit TryUnbind after every published consumer of
    // its committed storage is unreachable.  storage must be aligned to
    // kFxFastFileNativeArenaStorageAlignment, capacity must be nonzero, and
    // zoneIdentity must be nonzero so an unbound arena is distinguishable.
    // Binding an already-bound arena fails InvalidPhase.
    [[nodiscard]] FxFastFileNativeArenaStatus TryBind(
        void *storage,
        std::size_t capacity,
        std::uint64_t zoneIdentity) noexcept;

    // Releases the bound region without touching its bytes.  The caller owns
    // teardown ordering: every published consumer of committed storage must
    // already be unreachable.  Fails InvalidPhase while a transaction is open.
    [[nodiscard]] FxFastFileNativeArenaStatus TryUnbind() noexcept;

    [[nodiscard]] FxFastFileNativeArenaStatus TryBeginTransaction(
        FxFastFileNativeArenaTransaction *outTransaction) noexcept;

    // Reserves byteCount zero-filled bytes whose absolute address satisfies
    // alignment (a nonzero power of two).  Only the innermost open
    // transaction may reserve.  Failure changes no arena state and never
    // writes outStorage.
    [[nodiscard]] FxFastFileNativeArenaStatus TryReserve(
        const FxFastFileNativeArenaTransaction &transaction,
        std::size_t byteCount,
        std::size_t alignment,
        void **outStorage) noexcept;

    // Commits the innermost open transaction: every reservation it issued
    // (and everything beneath the cursor) becomes permanent by ratcheting the
    // committed watermark to the current cursor.
    [[nodiscard]] FxFastFileNativeArenaStatus TryCommit(
        const FxFastFileNativeArenaTransaction &transaction) noexcept;

    // Abandons the innermost open transaction: the region above the committed
    // watermark (and at or above this transaction's base) is rezeroed and
    // reclaimed; committed inner storage is never reclaimed.
    [[nodiscard]] FxFastFileNativeArenaStatus TryAbandon(
        const FxFastFileNativeArenaTransaction &transaction) noexcept;

    [[nodiscard]] bool bound() const noexcept
    {
        return storage_ != nullptr;
    }

    [[nodiscard]] std::uint64_t zoneIdentity() const noexcept
    {
        return zoneIdentity_;
    }

    [[nodiscard]] std::uint64_t capacity() const noexcept
    {
        return capacity_;
    }

    // Total bytes currently beneath the cursor, including alignment padding
    // and uncommitted open-transaction reservations.
    [[nodiscard]] std::uint64_t usedBytes() const noexcept
    {
        return cursor_;
    }

    // Bytes beneath the committed watermark.
    [[nodiscard]] std::uint64_t committedBytes() const noexcept
    {
        return committed_;
    }

    [[nodiscard]] std::uint32_t openTransactionDepth() const noexcept
    {
        return transactionDepth_;
    }

private:
    void *storage_ = nullptr;
#if !KISAK_ARCH_64BIT
    // Keep the following uint64_t members at their LP64 offsets on ILP32
    // targets.
    std::uint32_t storagePadding_ = 0;
#endif
    std::uint64_t zoneIdentity_ = 0;
    std::uint64_t capacity_ = 0;
    std::uint64_t cursor_ = 0;
    std::uint64_t committed_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t transactionSerials_[
        kFxFastFileNativeArenaMaxTransactionDepth]{};
    std::uint64_t transactionBases_[
        kFxFastFileNativeArenaMaxTransactionDepth]{};
    // transactionDepth_/operating_ occupy 0x50-0x54; alignas(8) tail padding
    // completes 0x58 exactly.
    std::uint32_t transactionDepth_ = 0;
    bool operating_ = false;
};

RUNTIME_SIZE(FxFastFileNativeArenaTransaction, 0x18, 0x18);
RUNTIME_SIZE(FxFastFileNativeArena, 0x58, 0x58);
} // namespace fx::fastfile

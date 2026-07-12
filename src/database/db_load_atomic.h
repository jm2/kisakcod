#pragma once

// Portable atomic protocols shared by the fast-file platform adapter and the
// load-progress UI.  These structures deliberately retain plain, exact-width
// words: Sys_Atomic* supplies the synchronization without importing a native
// file API or changing their layout to std::atomic implementation types.

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>

#include <universal/sys_atomic.h>

namespace db::load_atomic
{
inline constexpr std::uint32_t kFileReadBytes = 0x40000u;
inline constexpr std::uint32_t kFileBufferBytes = 0x80000u;

struct FileReadState
{
    volatile std::uint32_t complete;
    volatile std::uint32_t error;
    volatile std::uint32_t bytes;
};

struct FileReadSnapshot
{
    bool complete;
    std::uint32_t error;
    std::uint32_t bytes;
};

RUNTIME_SIZE(FileReadState, 12, 12);
static_assert(alignof(FileReadState) >= alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<FileReadState>);

// Reset is legal only after the preceding completion has been consumed and no
// producer can still publish to this slot.  Clearing complete first prevents a
// new waiter from treating reset payload words as a completed request.
inline void ResetFileRead(FileReadState *const state) noexcept
{
    Sys_AtomicStore(&state->complete, 0u);
    Sys_AtomicStore(&state->error, 0u);
    Sys_AtomicStore(&state->bytes, 0u);
}

// Publishes the payload before the completion flag.  invalidByteCountError is
// supplied by the platform adapter so this helper remains independent of
// native error-number namespaces.  Invalid transfer sizes still complete the
// slot, avoiding a waiter that can otherwise sleep forever.
inline bool PublishFileRead(
    FileReadState *const state,
    const std::uint32_t error,
    const std::uint32_t bytes,
    const std::uint32_t invalidByteCountError) noexcept
{
    const bool valid = bytes <= kFileReadBytes;
    Sys_AtomicStore(
        &state->error,
        valid ? error : invalidByteCountError);
    Sys_AtomicStore(&state->bytes, valid ? bytes : 0u);
    Sys_AtomicStore(&state->complete, 1u);
    return valid;
}

inline bool FileReadComplete(const FileReadState *const state) noexcept
{
    return Sys_AtomicLoad(&state->complete) != 0u;
}

inline FileReadSnapshot SnapshotFileRead(
    const FileReadState *const state) noexcept
{
    if (!FileReadComplete(state))
        return {false, 0u, 0u};

    // The sequentially consistent completion load observes both payload
    // stores that preceded the producer's completion publication.
    return {
        true,
        Sys_AtomicLoad(&state->error),
        Sys_AtomicLoad(&state->bytes),
    };
}

// Coordinates renderer-resource recovery on the main thread with asset use on
// the database thread.  Only the database side changes assetsSafe_: it stays
// true across any number of back-to-back recoveries and becomes false only
// after the database thread has observed a recovery-free instant and rechecked
// its claim.  This avoids both check/claim races and lost safe publications.
class AssetRecoveryGate final
{
public:
    AssetRecoveryGate() noexcept = default;
    AssetRecoveryGate(const AssetRecoveryGate &) = delete;
    AssetRecoveryGate &operator=(const AssetRecoveryGate &) = delete;

    [[nodiscard]] bool BeginRecovery() noexcept
    {
        if (recoveryRequested_.exchange(true, std::memory_order_seq_cst))
            return false;

        while (!assetsSafe_.load(std::memory_order_seq_cst))
            std::this_thread::yield();
        return true;
    }

    void EndRecovery() noexcept
    {
        // Do not clear assetsSafe_ here.  A database waiter owns the transition
        // back to active use; preserving true also admits back-to-back recovery.
        recoveryRequested_.store(false, std::memory_order_seq_cst);
    }

    void WaitForRecoveryAndClaimAssets() noexcept
    {
        for (;;)
        {
            assetsSafe_.store(true, std::memory_order_seq_cst);
            while (recoveryRequested_.load(std::memory_order_seq_cst))
                std::this_thread::yield();

            assetsSafe_.store(false, std::memory_order_seq_cst);
            if (!recoveryRequested_.load(std::memory_order_seq_cst))
                return;
        }
    }

    void ReleaseAssets() noexcept
    {
        assetsSafe_.store(true, std::memory_order_seq_cst);
    }

    [[nodiscard]] bool IsRecoveryRequested() const noexcept
    {
        return recoveryRequested_.load(std::memory_order_seq_cst);
    }

    [[nodiscard]] bool AreAssetsSafe() const noexcept
    {
        return assetsSafe_.load(std::memory_order_seq_cst);
    }

private:
    std::atomic<bool> recoveryRequested_{false};
    std::atomic<bool> assetsSafe_{true};
};

// Progress has multiple writers in practice (file completions, external media
// accounting, and lifecycle resets) and lock-free readers in the UI.  The
// sequence word is both a writer mutex and a seqlock generation: odd means a
// writer owns the record, while equal even generations bound a coherent read.
struct ProgressState
{
    volatile std::uint32_t sequence;
    volatile std::int32_t totalChunks;
    volatile std::int32_t loadedChunks;
    volatile std::int32_t totalExternalBytes;
    volatile std::int32_t loadedExternalBytes;
};

struct ProgressSnapshot
{
    std::int32_t totalChunks;
    std::int32_t loadedChunks;
    std::int32_t totalExternalBytes;
    std::int32_t loadedExternalBytes;
};

enum class ProgressUpdateResult : std::uint8_t
{
    Applied,
    NegativeValue,
    Overflow,
    InvalidState,
};

RUNTIME_SIZE(ProgressState, 20, 20);
static_assert(alignof(ProgressState) >= alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<ProgressState>);

namespace detail
{
inline std::uint32_t BeginProgressWrite(ProgressState *const state) noexcept
{
    for (;;)
    {
        const std::uint32_t observed = Sys_AtomicLoad(&state->sequence);
        if ((observed & 1u) != 0u)
        {
            std::this_thread::yield();
            continue;
        }

        const std::uint32_t owned = observed + 1u;
        if (Sys_AtomicCompareExchange(
                &state->sequence,
                owned,
                observed) == observed)
        {
            return owned;
        }
        std::this_thread::yield();
    }
}

inline void EndProgressWrite(
    ProgressState *const state,
    const std::uint32_t ownedSequence) noexcept
{
    Sys_AtomicStore(&state->sequence, ownedSequence + 1u);
}

inline bool AddWouldOverflow(
    const std::int32_t current,
    const std::int32_t delta) noexcept
{
    return current > (std::numeric_limits<std::int32_t>::max)() - delta;
}
} // namespace detail

// Installs a new denominator and resets both loaded counters as one coherent
// generation.  ProgressState must initially be zero-initialized and thereafter
// be accessed only through this API.
inline ProgressUpdateResult ConfigureProgress(
    ProgressState *const state,
    const std::int32_t totalChunks,
    const std::int32_t totalExternalBytes) noexcept
{
    if (totalChunks < 0 || totalExternalBytes < 0)
        return ProgressUpdateResult::NegativeValue;

    const std::uint32_t owned = detail::BeginProgressWrite(state);
    Sys_AtomicStore(&state->totalChunks, totalChunks);
    Sys_AtomicStore(&state->loadedChunks, 0);
    Sys_AtomicStore(&state->totalExternalBytes, totalExternalBytes);
    Sys_AtomicStore(&state->loadedExternalBytes, 0);
    detail::EndProgressWrite(state, owned);
    return ProgressUpdateResult::Applied;
}

// Converts whole-file totals into the remaining-work denominator after the
// header has been read.  Initial reads may complete while totals are unknown;
// taking writer ownership makes each such update fall wholly before (and be
// subtracted here) or after (and count against the new denominator) this
// transition.  Already-consumed work clamps the corresponding remainder to
// zero.
inline ProgressUpdateResult RebaseProgress(
    ProgressState *const state,
    const std::int32_t totalChunks,
    const std::int32_t totalExternalBytes) noexcept
{
    if (totalChunks < 0 || totalExternalBytes < 0)
        return ProgressUpdateResult::NegativeValue;

    const std::uint32_t owned = detail::BeginProgressWrite(state);
    const std::int32_t previousTotalChunks =
        Sys_AtomicLoad(&state->totalChunks);
    const std::int32_t loadedChunks =
        Sys_AtomicLoad(&state->loadedChunks);
    const std::int32_t previousTotalExternalBytes =
        Sys_AtomicLoad(&state->totalExternalBytes);
    const std::int32_t loadedExternalBytes =
        Sys_AtomicLoad(&state->loadedExternalBytes);

    if (previousTotalChunks < 0 || loadedChunks < 0
        || previousTotalExternalBytes < 0 || loadedExternalBytes < 0)
    {
        detail::EndProgressWrite(state, owned);
        return ProgressUpdateResult::InvalidState;
    }

    const std::int32_t remainingChunks = loadedChunks >= totalChunks
        ? 0
        : totalChunks - loadedChunks;
    const std::int32_t remainingExternalBytes =
        loadedExternalBytes >= totalExternalBytes
        ? 0
        : totalExternalBytes - loadedExternalBytes;
    Sys_AtomicStore(&state->totalChunks, remainingChunks);
    Sys_AtomicStore(&state->loadedChunks, 0);
    Sys_AtomicStore(
        &state->totalExternalBytes,
        remainingExternalBytes);
    Sys_AtomicStore(&state->loadedExternalBytes, 0);
    detail::EndProgressWrite(state, owned);
    return ProgressUpdateResult::Applied;
}

// Applies both progress deltas transactionally.  This permits independent
// callers to pass zero for the component they do not update while allowing
// tests and future adapters to update related counters in one generation.
inline ProgressUpdateResult AccumulateProgress(
    ProgressState *const state,
    const std::int32_t chunkDelta,
    const std::int32_t externalByteDelta) noexcept
{
    if (chunkDelta < 0 || externalByteDelta < 0)
        return ProgressUpdateResult::NegativeValue;

    const std::uint32_t owned = detail::BeginProgressWrite(state);
    const std::int32_t loadedChunks =
        Sys_AtomicLoad(&state->loadedChunks);
    const std::int32_t loadedExternalBytes =
        Sys_AtomicLoad(&state->loadedExternalBytes);

    ProgressUpdateResult result = ProgressUpdateResult::Applied;
    if (loadedChunks < 0 || loadedExternalBytes < 0)
    {
        result = ProgressUpdateResult::InvalidState;
    }
    else if (detail::AddWouldOverflow(loadedChunks, chunkDelta)
        || detail::AddWouldOverflow(
            loadedExternalBytes,
            externalByteDelta))
    {
        result = ProgressUpdateResult::Overflow;
    }
    else
    {
        Sys_AtomicStore(
            &state->loadedChunks,
            loadedChunks + chunkDelta);
        Sys_AtomicStore(
            &state->loadedExternalBytes,
            loadedExternalBytes + externalByteDelta);
    }

    detail::EndProgressWrite(state, owned);
    return result;
}

inline ProgressSnapshot SnapshotProgress(
    const ProgressState *const state) noexcept
{
    for (;;)
    {
        const std::uint32_t before = Sys_AtomicLoad(&state->sequence);
        if ((before & 1u) != 0u)
        {
            std::this_thread::yield();
            continue;
        }

        const ProgressSnapshot snapshot{
            Sys_AtomicLoad(&state->totalChunks),
            Sys_AtomicLoad(&state->loadedChunks),
            Sys_AtomicLoad(&state->totalExternalBytes),
            Sys_AtomicLoad(&state->loadedExternalBytes),
        };
        const std::uint32_t after = Sys_AtomicLoad(&state->sequence);
        if (before == after && (after & 1u) == 0u)
            return snapshot;
        std::this_thread::yield();
    }
}

inline double LoadedFraction(const ProgressSnapshot &snapshot) noexcept
{
    if (snapshot.totalChunks < 0 || snapshot.loadedChunks < 0
        || snapshot.totalExternalBytes < 0
        || snapshot.loadedExternalBytes < 0)
    {
        return 0.0;
    }

    const std::uint64_t totalInternal =
        static_cast<std::uint64_t>(snapshot.totalChunks) * kFileReadBytes;
    const std::uint64_t loadedInternal =
        static_cast<std::uint64_t>(
            snapshot.loadedChunks > snapshot.totalChunks
                ? snapshot.totalChunks
                : snapshot.loadedChunks)
        * kFileReadBytes;
    const std::uint64_t totalExternal =
        static_cast<std::uint64_t>(snapshot.totalExternalBytes);
    const std::uint64_t loadedExternal =
        static_cast<std::uint64_t>(
            snapshot.loadedExternalBytes > snapshot.totalExternalBytes
                ? snapshot.totalExternalBytes
                : snapshot.loadedExternalBytes);
    const std::uint64_t total = totalInternal + totalExternal;
    if (total == 0u)
        return 0.0;

    return static_cast<double>(loadedInternal + loadedExternal)
        / static_cast<double>(total);
}

inline double LoadedFraction(const ProgressState *const state) noexcept
{
    return LoadedFraction(SnapshotProgress(state));
}
} // namespace db::load_atomic

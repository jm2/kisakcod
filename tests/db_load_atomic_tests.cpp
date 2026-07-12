#include <database/db_load_atomic.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

namespace
{
using db::load_atomic::AccumulateProgress;
using db::load_atomic::AssetRecoveryGate;
using db::load_atomic::ConfigureProgress;
using db::load_atomic::FileReadSnapshot;
using db::load_atomic::FileReadState;
using db::load_atomic::LoadedFraction;
using db::load_atomic::ProgressSnapshot;
using db::load_atomic::ProgressState;
using db::load_atomic::ProgressUpdateResult;
using db::load_atomic::PublishFileRead;
using db::load_atomic::RebaseProgress;
using db::load_atomic::ResetFileRead;
using db::load_atomic::SnapshotFileRead;
using db::load_atomic::SnapshotProgress;
using db::load_atomic::kFileReadBytes;

int Fail(const char *const message)
{
    std::fprintf(stderr, "database load atomic test failed: %s\n", message);
    return 1;
}

bool SameProgress(
    const ProgressSnapshot &left,
    const ProgressSnapshot &right)
{
    return left.totalChunks == right.totalChunks
        && left.loadedChunks == right.loadedChunks
        && left.totalExternalBytes == right.totalExternalBytes
        && left.loadedExternalBytes == right.loadedExternalBytes;
}

bool NearlyEqual(const double left, const double right)
{
    return std::fabs(left - right) <= 1.0e-12;
}

bool TestFileReadBasics()
{
    constexpr std::uint32_t kInvalidDataError = 13u;
    FileReadState state{};

    ResetFileRead(&state);
    FileReadSnapshot snapshot = SnapshotFileRead(&state);
    if (snapshot.complete || snapshot.error != 0u || snapshot.bytes != 0u)
        return false;

    if (!PublishFileRead(
            &state,
            7u,
            kFileReadBytes,
            kInvalidDataError))
    {
        return false;
    }
    snapshot = SnapshotFileRead(&state);
    if (!snapshot.complete || snapshot.error != 7u
        || snapshot.bytes != kFileReadBytes)
    {
        return false;
    }

    ResetFileRead(&state);
    snapshot = SnapshotFileRead(&state);
    if (snapshot.complete || snapshot.error != 0u || snapshot.bytes != 0u)
        return false;

    if (PublishFileRead(
            &state,
            0u,
            kFileReadBytes + 1u,
            kInvalidDataError))
    {
        return false;
    }
    snapshot = SnapshotFileRead(&state);
    return snapshot.complete
        && snapshot.error == kInvalidDataError
        && snapshot.bytes == 0u;
}

bool TestFileReadPublicationContention()
{
    constexpr std::uint32_t kRounds = 4000u;
    constexpr std::uint32_t kInvalidDataError = 13u;
    FileReadState state{};
    std::atomic<std::uint32_t> resetReady{0u};
    std::atomic<std::uint32_t> resetObserved{0u};
    std::atomic<std::uint32_t> consumedRound{0u};
    std::atomic<bool> valid{true};

    std::thread producer([&]() {
        for (std::uint32_t round = 1u; round <= kRounds; ++round)
        {
            ResetFileRead(&state);
            resetReady.store(round, std::memory_order_release);
            while (resetObserved.load(std::memory_order_acquire) != round)
            {
                if (!valid.load(std::memory_order_relaxed))
                    return;
                std::this_thread::yield();
            }

            const std::uint32_t error = 0xA5000000u | round;
            const std::uint32_t bytes = round % (kFileReadBytes + 1u);
            if (!PublishFileRead(
                    &state,
                    error,
                    bytes,
                    kInvalidDataError))
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            while (consumedRound.load(std::memory_order_acquire) != round)
            {
                if (!valid.load(std::memory_order_relaxed))
                    return;
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (std::uint32_t round = 1u; round <= kRounds; ++round)
        {
            while (resetReady.load(std::memory_order_acquire) != round)
                std::this_thread::yield();

            if (SnapshotFileRead(&state).complete)
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            resetObserved.store(round, std::memory_order_release);

            FileReadSnapshot snapshot{};
            do
            {
                snapshot = SnapshotFileRead(&state);
                if (!snapshot.complete)
                    std::this_thread::yield();
            } while (!snapshot.complete);

            const std::uint32_t expectedError = 0xA5000000u | round;
            const std::uint32_t expectedBytes =
                round % (kFileReadBytes + 1u);
            if (snapshot.error != expectedError
                || snapshot.bytes != expectedBytes)
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            consumedRound.store(round, std::memory_order_release);
        }
    });

    producer.join();
    consumer.join();
    return valid.load(std::memory_order_relaxed);
}

bool TestAssetRecoveryGate()
{
    AssetRecoveryGate gate;
    if (gate.IsRecoveryRequested() || !gate.AreAssetsSafe())
        return false;

    gate.WaitForRecoveryAndClaimAssets();
    if (gate.AreAssetsSafe())
        return false;

    std::atomic<bool> recoveryStarted{false};
    std::atomic<bool> recoveryEntered{false};
    std::thread blockedRecovery([&]() {
        recoveryStarted.store(true, std::memory_order_release);
        if (!gate.BeginRecovery())
            return;
        recoveryEntered.store(true, std::memory_order_release);
        gate.EndRecovery();
    });
    while (!recoveryStarted.load(std::memory_order_acquire)
        || !gate.IsRecoveryRequested())
    {
        std::this_thread::yield();
    }
    if (recoveryEntered.load(std::memory_order_acquire))
    {
        gate.ReleaseAssets();
        blockedRecovery.join();
        return false;
    }
    gate.ReleaseAssets();
    blockedRecovery.join();
    if (!recoveryEntered.load(std::memory_order_acquire)
        || gate.IsRecoveryRequested() || !gate.AreAssetsSafe())
    {
        return false;
    }

    // Exercise immediate back-to-back recoveries against repeated database
    // claims.  The gate must never grant both sides at once, and EndRecovery
    // must preserve the safe publication until the database explicitly claims.
    constexpr std::int32_t kRounds = 5000;
    std::atomic<bool> databaseActive{false};
    std::atomic<bool> recoveryActive{false};
    std::atomic<bool> valid{true};
    std::thread database([&]() {
        for (std::int32_t round = 0; round < kRounds; ++round)
        {
            gate.WaitForRecoveryAndClaimAssets();
            databaseActive.store(true, std::memory_order_seq_cst);
            if (recoveryActive.load(std::memory_order_seq_cst))
                valid.store(false, std::memory_order_relaxed);
            std::this_thread::yield();
            databaseActive.store(false, std::memory_order_seq_cst);
            gate.ReleaseAssets();
        }
    });
    std::thread recovery([&]() {
        for (std::int32_t round = 0; round < kRounds; ++round)
        {
            if (!gate.BeginRecovery())
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            recoveryActive.store(true, std::memory_order_seq_cst);
            if (databaseActive.load(std::memory_order_seq_cst))
                valid.store(false, std::memory_order_relaxed);
            std::this_thread::yield();
            recoveryActive.store(false, std::memory_order_seq_cst);
            gate.EndRecovery();
        }
    });
    database.join();
    recovery.join();

    return valid.load(std::memory_order_relaxed)
        && !gate.IsRecoveryRequested()
        && gate.AreAssetsSafe();
}

bool TestProgressValidationAndFractions()
{
    ProgressState state{};
    if (ConfigureProgress(&state, 0, 0) != ProgressUpdateResult::Applied
        || LoadedFraction(&state) != 0.0)
    {
        return false;
    }

    // An external-only load must make progress even when no internal chunks
    // remain after the header has been consumed.
    if (ConfigureProgress(&state, 0, 400)
            != ProgressUpdateResult::Applied
        || AccumulateProgress(&state, 0, 100)
            != ProgressUpdateResult::Applied
        || !NearlyEqual(LoadedFraction(&state), 0.25))
    {
        return false;
    }
    if (AccumulateProgress(&state, 0, 400)
            != ProgressUpdateResult::Applied
        || LoadedFraction(&state) != 1.0)
    {
        return false;
    }

    // Header-time rebasing must subtract reads that completed while the total
    // was still unknown, then start a fresh loaded generation.
    if (ConfigureProgress(&state, 0, 0)
            != ProgressUpdateResult::Applied
        || AccumulateProgress(&state, 2, 30)
            != ProgressUpdateResult::Applied
        || RebaseProgress(&state, 5, 100)
            != ProgressUpdateResult::Applied)
    {
        return false;
    }
    ProgressSnapshot rebased = SnapshotProgress(&state);
    if (rebased.totalChunks != 3 || rebased.loadedChunks != 0
        || rebased.totalExternalBytes != 70
        || rebased.loadedExternalBytes != 0)
    {
        return false;
    }
    if (AccumulateProgress(&state, 4, 80)
            != ProgressUpdateResult::Applied
        || RebaseProgress(&state, 3, 70)
            != ProgressUpdateResult::Applied)
    {
        return false;
    }
    rebased = SnapshotProgress(&state);
    if (rebased.totalChunks != 0 || rebased.loadedChunks != 0
        || rebased.totalExternalBytes != 0
        || rebased.loadedExternalBytes != 0
        || LoadedFraction(rebased) != 0.0)
    {
        return false;
    }

    const ProgressSnapshot beforeRejectedUpdate = SnapshotProgress(&state);
    if (AccumulateProgress(&state, -1, 0)
            != ProgressUpdateResult::NegativeValue
        || AccumulateProgress(&state, 0, -1)
            != ProgressUpdateResult::NegativeValue
        || ConfigureProgress(&state, -1, 0)
            != ProgressUpdateResult::NegativeValue
        || ConfigureProgress(&state, 0, -1)
            != ProgressUpdateResult::NegativeValue
        || !SameProgress(beforeRejectedUpdate, SnapshotProgress(&state)))
    {
        return false;
    }

    if (ConfigureProgress(&state, 1, 1)
            != ProgressUpdateResult::Applied
        || AccumulateProgress(
                &state,
                (std::numeric_limits<std::int32_t>::max)(),
                (std::numeric_limits<std::int32_t>::max)())
            != ProgressUpdateResult::Applied)
    {
        return false;
    }
    const ProgressSnapshot beforeOverflow = SnapshotProgress(&state);
    if (AccumulateProgress(&state, 1, 0)
            != ProgressUpdateResult::Overflow
        || AccumulateProgress(&state, 0, 1)
            != ProgressUpdateResult::Overflow
        || !SameProgress(beforeOverflow, SnapshotProgress(&state))
        || LoadedFraction(&state) != 1.0)
    {
        return false;
    }

    if (ConfigureProgress(&state, 2, 100)
            != ProgressUpdateResult::Applied
        || AccumulateProgress(&state, 1, 50)
            != ProgressUpdateResult::Applied)
    {
        return false;
    }
    const double expected =
        static_cast<double>(kFileReadBytes + 50u)
        / static_cast<double>(2u * kFileReadBytes + 100u);
    if (!NearlyEqual(LoadedFraction(&state), expected))
        return false;

    const ProgressSnapshot invalid{-1, 0, 0, 0};
    if (LoadedFraction(invalid) != 0.0)
        return false;

    if (ConfigureProgress(&state, 1, 1)
            != ProgressUpdateResult::Applied)
    {
        return false;
    }
    const ProgressSnapshot beforeRejectedRebase = SnapshotProgress(&state);
    if (RebaseProgress(&state, -1, 0)
            != ProgressUpdateResult::NegativeValue
        || RebaseProgress(&state, 0, -1)
            != ProgressUpdateResult::NegativeValue
        || !SameProgress(beforeRejectedRebase, SnapshotProgress(&state)))
    {
        return false;
    }
    Sys_AtomicStore(&state.loadedChunks, -1);
    const ProgressSnapshot corruptState = SnapshotProgress(&state);
    if (RebaseProgress(&state, 1, 1)
            != ProgressUpdateResult::InvalidState
        || !SameProgress(corruptState, SnapshotProgress(&state)))
    {
        return false;
    }

    // Exercise the unsigned sequence rollover while the record is quiescent.
    ProgressState rollover{};
    Sys_AtomicStore(&rollover.sequence, UINT32_C(0xfffffffe));
    if (ConfigureProgress(&rollover, 3, 9)
            != ProgressUpdateResult::Applied
        || Sys_AtomicLoad(&rollover.sequence) != 0u)
    {
        return false;
    }
    const ProgressSnapshot rolloverSnapshot = SnapshotProgress(&rollover);
    return rolloverSnapshot.totalChunks == 3
        && rolloverSnapshot.loadedChunks == 0
        && rolloverSnapshot.totalExternalBytes == 9
        && rolloverSnapshot.loadedExternalBytes == 0;
}

bool TestProgressContention()
{
    constexpr std::int32_t kWriterCount = 4;
    constexpr std::int32_t kIterations = 5000;
    constexpr std::int32_t kTotalUpdates = kWriterCount * kIterations;
    ProgressState state{};
    if (ConfigureProgress(&state, kTotalUpdates, kTotalUpdates * 3)
        != ProgressUpdateResult::Applied)
    {
        return false;
    }

    std::atomic<std::int32_t> writersDone{0};
    std::atomic<bool> valid{true};
    std::vector<std::thread> writers;
    writers.reserve(static_cast<std::size_t>(kWriterCount));
    for (std::int32_t writer = 0; writer < kWriterCount; ++writer)
    {
        writers.emplace_back([&]() {
            for (std::int32_t iteration = 0;
                 iteration < kIterations;
                 ++iteration)
            {
                if (AccumulateProgress(&state, 1, 3)
                    != ProgressUpdateResult::Applied)
                {
                    valid.store(false, std::memory_order_relaxed);
                    break;
                }
            }
            writersDone.fetch_add(1, std::memory_order_release);
        });
    }

    std::thread reader([&]() {
        while (writersDone.load(std::memory_order_acquire) < kWriterCount)
        {
            const ProgressSnapshot snapshot = SnapshotProgress(&state);
            if (snapshot.totalChunks != kTotalUpdates
                || snapshot.totalExternalBytes != kTotalUpdates * 3
                || snapshot.loadedChunks < 0
                || snapshot.loadedExternalBytes != snapshot.loadedChunks * 3)
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
            std::this_thread::yield();
        }
    });

    for (std::thread &writer : writers)
        writer.join();
    reader.join();

    const ProgressSnapshot snapshot = SnapshotProgress(&state);
    return valid.load(std::memory_order_relaxed)
        && snapshot.loadedChunks == kTotalUpdates
        && snapshot.loadedExternalBytes == kTotalUpdates * 3
        && LoadedFraction(snapshot) == 1.0;
}

bool TestProgressRebaseBoundary()
{
    constexpr std::int32_t kHalfUpdates = 2000;
    constexpr std::int32_t kTotalUpdates = kHalfUpdates * 2;
    ProgressState state{};
    if (ConfigureProgress(&state, 0, 0)
        != ProgressUpdateResult::Applied)
    {
        return false;
    }

    std::atomic<bool> firstHalfDone{false};
    std::atomic<bool> rebaseDone{false};
    std::atomic<bool> valid{true};
    std::thread producer([&]() {
        for (std::int32_t update = 0; update < kHalfUpdates; ++update)
        {
            if (AccumulateProgress(&state, 1, 2)
                != ProgressUpdateResult::Applied)
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
        }
        firstHalfDone.store(true, std::memory_order_release);
        while (!rebaseDone.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (std::int32_t update = 0; update < kHalfUpdates; ++update)
        {
            if (AccumulateProgress(&state, 1, 2)
                != ProgressUpdateResult::Applied)
            {
                valid.store(false, std::memory_order_relaxed);
                return;
            }
        }
    });

    while (!firstHalfDone.load(std::memory_order_acquire))
        std::this_thread::yield();
    if (RebaseProgress(&state, kTotalUpdates, kTotalUpdates * 2)
        != ProgressUpdateResult::Applied)
    {
        valid.store(false, std::memory_order_relaxed);
    }
    rebaseDone.store(true, std::memory_order_release);
    producer.join();

    const ProgressSnapshot snapshot = SnapshotProgress(&state);
    return valid.load(std::memory_order_relaxed)
        && snapshot.totalChunks == kHalfUpdates
        && snapshot.loadedChunks == kHalfUpdates
        && snapshot.totalExternalBytes == kHalfUpdates * 2
        && snapshot.loadedExternalBytes == kHalfUpdates * 2
        && LoadedFraction(snapshot) == 1.0;
}
} // namespace

int main()
{
    if (!TestFileReadBasics())
        return Fail("file-read reset, validation, or snapshot contract");
    if (!TestFileReadPublicationContention())
        return Fail("file-read payload publication ordering");
    if (!TestAssetRecoveryGate())
        return Fail("asset recovery gate ordering or mutual exclusion");
    if (!TestProgressValidationAndFractions())
        return Fail("progress validation, rollover, or fraction contract");
    if (!TestProgressContention())
        return Fail("progress writer serialization or coherent snapshot");
    if (!TestProgressRebaseBoundary())
        return Fail("progress header rebase transaction");
    return 0;
}

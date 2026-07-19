#include <database/db_registry_ownership_coordinator.h>
#include <script/scr_stringlist.cpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

FastCriticalSection db_hashCritSect{};

namespace
{
static_assert(sizeof(script_string::OwnershipBatch) == 0x20);
static_assert(std::is_standard_layout_v<script_string::OwnershipBatch>);
static_assert(
    !std::is_trivially_destructible_v<script_string::OwnershipBatch>);

std::recursive_mutex g_scriptStringMutex;
std::recursive_mutex g_memoryTreeMutex;
std::recursive_mutex g_scriptStringTransactionMutex;
thread_local std::uint32_t g_scriptStringLockDepth = 0;
thread_local std::uint32_t g_memoryTreeLockDepth = 0;
thread_local std::uint32_t g_scriptStringTransactionLockDepth = 0;
std::uint32_t g_comErrorCount = 0;
std::uint32_t g_assertCount = 0;
bool g_reporterSawOwnedLock = false;
std::atomic<bool> g_pauseDebugMemset{false};
std::atomic<bool> g_debugMemsetEntered{false};
std::atomic<bool> g_releaseDebugMemset{false};
std::atomic<bool> g_countScriptStringLockAttempts{false};
std::atomic<std::uint32_t> g_scriptStringLockAttempts{0};

struct StateImage final
{
    std::vector<std::byte> strings;
    std::vector<std::byte> memoryTree;
    std::vector<std::byte> debug;
    scrStringDebugGlob_t *debugPointer = nullptr;
    char *memoryTreeBuffer = nullptr;
};

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
        std::fprintf(stderr, "script-string ownership test failed: %s\n", message);
    return condition;
}

[[nodiscard]] StateImage CaptureState()
{
    StateImage image{
        std::vector<std::byte>(sizeof(scrStringGlob)),
        std::vector<std::byte>(sizeof(scrMemTreeGlob)),
        {},
        scrStringDebugGlob,
        scrMemTreePub.mt_buffer,
    };
    std::memcpy(image.strings.data(), &scrStringGlob, image.strings.size());
    std::memcpy(
        image.memoryTree.data(), &scrMemTreeGlob, image.memoryTree.size());
    if (scrStringDebugGlob)
    {
        image.debug.resize(sizeof(*scrStringDebugGlob));
        std::memcpy(
            image.debug.data(), scrStringDebugGlob, image.debug.size());
    }
    return image;
}

[[nodiscard]] bool StateMatches(const StateImage &image) noexcept
{
    if (image.debugPointer != scrStringDebugGlob
        || image.memoryTreeBuffer != scrMemTreePub.mt_buffer
        || std::memcmp(
            image.strings.data(), &scrStringGlob, image.strings.size()) != 0
        || std::memcmp(
            image.memoryTree.data(),
            &scrMemTreeGlob,
            image.memoryTree.size()) != 0)
    {
        return false;
    }
    if (!scrStringDebugGlob)
        return image.debug.empty();
    return image.debug.size() == sizeof(*scrStringDebugGlob)
        && std::memcmp(
            image.debug.data(), scrStringDebugGlob, image.debug.size()) == 0;
}

[[nodiscard]] bool LocksReleased() noexcept
{
    return g_scriptStringLockDepth == 0 && g_memoryTreeLockDepth == 0
        && g_scriptStringTransactionLockDepth == 0;
}

[[nodiscard]] bool ReportersUnused() noexcept
{
    return g_comErrorCount == 0 && g_assertCount == 0
        && !g_reporterSawOwnedLock;
}

[[nodiscard]] bool TestInitCheckLeaksRetainsLock() noexcept
{
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;
    if (!Check(!scrStringGlob.inited && scrStringDebugGlob == nullptr,
            "debug-init serialization test began initialized"))
    {
        return false;
    }

    g_debugMemsetEntered.store(false, std::memory_order_relaxed);
    g_releaseDebugMemset.store(false, std::memory_order_relaxed);
    g_pauseDebugMemset.store(true, std::memory_order_release);
    std::atomic<bool> foreignLockReleased{false};
    std::thread foreign([&]() {
        SL_InitCheckLeaks();
        foreignLockReleased.store(
            g_scriptStringLockDepth == 0,
            std::memory_order_release);
    });
    while (!g_debugMemsetEntered.load(std::memory_order_acquire))
        std::this_thread::yield();

    const bool lockWasAvailable = g_scriptStringMutex.try_lock();
    if (lockWasAvailable)
        g_scriptStringMutex.unlock();
    g_releaseDebugMemset.store(true, std::memory_order_release);
    foreign.join();
    g_pauseDebugMemset.store(false, std::memory_order_release);

    const bool valid = Check(!lockWasAvailable,
            "debug reset released the script lock between check and publish")
        && Check(scrStringDebugGlob == &scrStringDebugGlobBuf,
            "serialized debug reset did not publish accounting")
        && Check(foreignLockReleased.load(std::memory_order_acquire),
            "serialized debug reset leaked the script lock")
        && Check(ReportersUnused(),
            "serialized debug reset invoked a reporter");

    Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
    SL_CheckLeaks();
    Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
    return valid
        && Check(scrStringDebugGlob == nullptr,
            "serialized debug reset cleanup left accounting published")
        && Check(LocksReleased(),
            "serialized debug reset cleanup leaked a lock")
        && Check(ReportersUnused(),
            "serialized debug reset cleanup invoked a reporter");
}

[[nodiscard]] std::uint32_t Packed(const std::uint32_t stringId) noexcept
{
    return scr_string_atomic::Load(
        SL_RefStringWord(SL_GetRefStringNoReport(stringId)));
}

[[nodiscard]] bool TestFullInitRejectsDebugOnlyState() noexcept
{
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;
    if (!Check(!scrStringGlob.inited && scrStringDebugGlob == nullptr,
            "debug-only rejection test began initialized"))
    {
        return false;
    }

    SL_InitCheckLeaks();
    if (!Check(scrStringDebugGlob == &scrStringDebugGlobBuf,
            "debug-only rejection setup did not publish accounting")
        || !Check(LocksReleased(),
            "debug-only rejection setup leaked a lock")
        || !Check(ReportersUnused(),
            "debug-only rejection setup invoked a reporter"))
    {
        return false;
    }

    scrStringDebugGlob->ignoreLeaks = 1;
    const StateImage beforeDuplicate = CaptureState();
    SL_Init();
#if defined(USE_ASSERTS)
    constexpr std::uint32_t expectedAssertions = 1;
#else
    constexpr std::uint32_t expectedAssertions = 0;
#endif
    const bool valid = Check(StateMatches(beforeDuplicate),
            "debug-only initialization changed ownership state")
        && Check(!scrStringGlob.inited,
            "debug-only initialization published the string table")
        && Check(LocksReleased(),
            "debug-only initialization rejection leaked a lock")
        && Check(g_comErrorCount == 0,
            "debug-only initialization rejection invoked Com_Error")
        && Check(!g_reporterSawOwnedLock,
            "debug-only initialization rejection asserted under an owned lock")
        && Check(g_assertCount == expectedAssertions,
            "debug-only initialization assertion behavior changed");

    g_assertCount = 0;
    Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
    SL_CheckLeaks();
    Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
    return valid
        && Check(scrStringDebugGlob == nullptr,
            "debug-only initialization cleanup left accounting published")
        && Check(LocksReleased(),
            "debug-only initialization cleanup leaked a lock")
        && Check(ReportersUnused(),
            "debug-only initialization cleanup invoked a reporter");
}

[[nodiscard]] bool CheckOwnership(
    const std::uint32_t stringId,
    const std::uint16_t refCount,
    const std::uint8_t users,
    const std::uint8_t byteLength,
    const std::uint32_t totalDebugRefs,
    const char *const context) noexcept
{
    const std::uint32_t packed = Packed(stringId);
    return Check(
            scr_string_atomic::RefCount(packed) == refCount, context)
        && Check(scr_string_atomic::User(packed) == users, context)
        && Check(scr_string_atomic::ByteLength(packed) == byteLength, context)
        && Check(scrStringDebugGlob != nullptr, context)
        && Check(
            Sys_AtomicLoad(&scrStringDebugGlob->refCount[stringId])
                == refCount,
            context)
        && Check(
            Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount)
                == totalDebugRefs,
            context);
}

[[nodiscard]] bool CheckAllocation(
    const std::uint32_t stringId,
    const std::uint8_t expectedType) noexcept
{
    MT_AllocationInfo info{};
    return Check(
            MT_TryGetAllocationInfo(stringId, &info)
                == MT_AllocationInfoStatus::Success,
            "live string has no allocator allocation")
        && Check(info.type == expectedType, "string allocation type changed")
        && Check(info.reserved == 0, "allocation reserved field changed")
        && Check(
            info.capacityBytes >= sizeof(std::uint32_t) + 1,
            "string allocation has no payload capacity");
}

[[nodiscard]] bool CheckFreed(const std::uint32_t stringId) noexcept
{
    MT_AllocationInfo info{0xA5, 0x5A, 0xA55A, UINT32_C(0xA55AA55A)};
    return Check(
            MT_TryGetAllocationInfo(stringId, &info)
                == MT_AllocationInfoStatus::NotAllocatedNoChange,
            "released string allocation is still live")
        && Check(
            Sys_AtomicLoad(&scrStringDebugGlob->refCount[stringId]) == 0,
            "released string retained debug references");
}

[[nodiscard]] bool CheckFreedLegacy(const std::uint32_t stringId) noexcept
{
    MT_AllocationInfo info{0xA5, 0x5A, 0xA55A, UINT32_C(0xA55AA55A)};
    return Check(
            MT_TryGetAllocationInfoLegacy(stringId, &info)
                == MT_AllocationInfoStatus::NotAllocatedNoChange,
            "released legacy string allocation is still live")
        && Check(
            Sys_AtomicLoad(&scrStringDebugGlob->refCount[stringId]) == 0,
            "released legacy string retained debug references");
}

[[nodiscard]] bool BeginTest() noexcept
{
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;
    if (!Check(!scrStringGlob.inited, "test began with string table initialized")
        || !Check(scrStringDebugGlob == nullptr,
            "test began with leak checking initialized"))
    {
        return false;
    }
    SL_Init();
    return Check(scrStringGlob.inited, "SL_Init did not initialize string table")
        && Check(scrStringDebugGlob != nullptr,
            "SL_Init did not initialize debug accounting")
        && Check(scrStringGlob.nextFreeEntry == nullptr,
            "SL_Init retained stale hash-iteration state")
        && Check(LocksReleased(), "SL_Init leaked a critical section")
        && Check(ReportersUnused(), "SL_Init reported an unexpected error");
}

[[nodiscard]] bool EndTest() noexcept
{
    if (!Check(
            Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount) == 0,
            "test leaked script-string references"))
    {
        return false;
    }
    SL_Shutdown();
    return Check(!scrStringGlob.inited, "SL_Shutdown left the table initialized")
        && Check(scrStringDebugGlob == nullptr,
            "SL_Shutdown left debug accounting published")
        && Check(LocksReleased(), "test leaked a critical section")
        && Check(ReportersUnused(), "report-free API invoked a reporter");
}

[[nodiscard]] bool TestDuplicateInitCheckLeaksNoChange() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "duplicate-debug-init-live-reference";
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(
            acquired.status == script_string::AcquireStatus::Acquired,
            "duplicate debug initialization setup failed"))
    {
        return false;
    }

    const StateImage beforeDuplicate = CaptureState();
    SL_InitCheckLeaks();
#if defined(USE_ASSERTS)
    constexpr std::uint32_t expectedAssertions = 1;
#else
    constexpr std::uint32_t expectedAssertions = 0;
#endif
    const bool valid = Check(StateMatches(beforeDuplicate),
            "duplicate debug initialization changed ownership state")
        && Check(LocksReleased(),
            "duplicate debug initialization leaked a lock")
        && Check(g_comErrorCount == 0,
            "duplicate debug initialization invoked Com_Error")
        && Check(!g_reporterSawOwnedLock,
            "duplicate debug initialization asserted under an owned lock")
        && Check(g_assertCount == expectedAssertions,
            "duplicate debug initialization assertion behavior changed");

    g_assertCount = 0;
    const auto cleanup =
        script_string::TryRemoveOrdinaryReference(acquired.stringId);
    return valid
        && Check(cleanup == script_string::ReleaseStatus::Success,
            "duplicate debug initialization cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestDuplicateInitNoChange() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "duplicate-full-init-live-reference";
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(
            acquired.status == script_string::AcquireStatus::Acquired,
            "duplicate full initialization setup failed"))
    {
        return false;
    }

    const StateImage beforeDuplicate = CaptureState();
    SL_Init();
#if defined(USE_ASSERTS)
    constexpr std::uint32_t expectedAssertions = 1;
#else
    constexpr std::uint32_t expectedAssertions = 0;
#endif
    const bool valid = Check(StateMatches(beforeDuplicate),
            "duplicate full initialization changed ownership state")
        && Check(LocksReleased(),
            "duplicate full initialization leaked a lock")
        && Check(g_comErrorCount == 0,
            "duplicate full initialization invoked Com_Error")
        && Check(!g_reporterSawOwnedLock,
            "duplicate full initialization asserted under an owned lock")
        && Check(g_assertCount == expectedAssertions,
            "duplicate full initialization assertion behavior changed");

    g_assertCount = 0;
    const auto cleanup =
        script_string::TryRemoveOrdinaryReference(acquired.stringId);
    return valid
        && Check(cleanup == script_string::ReleaseStatus::Success,
            "duplicate full initialization cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestInvalidAndNoChange() noexcept
{
    if (!BeginTest())
        return false;

    const StateImage initial = CaptureState();
    const char terminated[] = "x";
    const char unterminated[] = {'x', 'y'};
    using script_string::AcquireStatus;
    using script_string::ReleaseStatus;
    using script_string::TransferStatus;

    const auto nullResult =
        script_string::TryAcquireOrdinaryStringOfSize(nullptr, 1, 15);
    const auto emptyResult =
        script_string::TryAcquireOrdinaryStringOfSize(terminated, 0, 15);
    const auto oversizedResult =
        script_string::TryAcquireOrdinaryStringOfSize(terminated, 65532, 15);
    const auto unterminatedResult =
        script_string::TryAcquireOrdinaryStringOfSize(unterminated, 2, 15);
    const auto zeroTypeResult =
        script_string::TryAcquireOrdinaryStringOfSize(terminated, 2, 0);
    const auto highTypeResult =
        script_string::TryAcquireOrdinaryStringOfSize(terminated, 2, 22);

    if (!Check(nullResult.status == AcquireStatus::InvalidArgumentNoChange,
            "null bytes were accepted")
        || !Check(emptyResult.status == AcquireStatus::InvalidArgumentNoChange,
            "zero byte count was accepted")
        || !Check(
            oversizedResult.status == AcquireStatus::InvalidArgumentNoChange,
            "oversized byte count was accepted")
        || !Check(
            unterminatedResult.status == AcquireStatus::InvalidArgumentNoChange,
            "unterminated bytes were accepted")
        || !Check(
            zeroTypeResult.status == AcquireStatus::InvalidArgumentNoChange,
            "zero allocation type was accepted")
        || !Check(
            highTypeResult.status == AcquireStatus::InvalidArgumentNoChange,
            "out-of-range allocation type was accepted")
        || !Check(
            nullResult.stringId == 0 && emptyResult.stringId == 0
                && oversizedResult.stringId == 0
                && unterminatedResult.stringId == 0
                && zeroTypeResult.stringId == 0
                && highTypeResult.stringId == 0,
            "invalid acquisition published an ID")
        || !Check(StateMatches(initial), "invalid acquisitions changed state"))
    {
        return false;
    }

    for (const std::uint32_t invalidId : {UINT32_C(0), UINT32_C(65536)})
    {
        if (!Check(
                script_string::TryTransferOrdinaryToDatabaseUser(invalidId)
                    == TransferStatus::OwnershipMismatchNoChange,
                "invalid transfer ID was accepted")
            || !Check(
                script_string::TryRemoveOrdinaryReference(invalidId)
                    == ReleaseStatus::OwnershipMismatchNoChange,
                "invalid ordinary release ID was accepted")
            || !Check(
                script_string::TryRemoveDatabaseUserReference(invalidId)
                    == ReleaseStatus::OwnershipMismatchNoChange,
                "invalid database release ID was accepted"))
        {
            return false;
        }
    }
    if (!Check(
            script_string::TryTransferOrdinaryToDatabaseUser(1)
                == TransferStatus::OwnershipMismatchNoChange,
            "unallocated transfer ID was accepted")
        || !Check(
            script_string::TryRemoveOrdinaryReference(1)
                == ReleaseStatus::OwnershipMismatchNoChange,
            "unallocated ordinary release ID was accepted")
        || !Check(
            script_string::TryRemoveDatabaseUserReference(1)
                == ReleaseStatus::OwnershipMismatchNoChange,
            "unallocated database release ID was accepted")
        || !Check(StateMatches(initial), "invalid ID operation changed state")
        || !Check(LocksReleased(), "invalid path leaked a critical section")
        || !Check(ReportersUnused(), "invalid path invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestRepeatedInternAndDatabaseTransfer() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "alpha";
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    const auto second = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired,
            "first ordinary intern failed")
        || !Check(second.status == script_string::AcquireStatus::Acquired,
            "repeated ordinary intern failed")
        || !Check(first.stringId == second.stringId,
            "repeated intern returned a different ID")
        || !CheckAllocation(first.stringId, 15)
        || !CheckOwnership(first.stringId, 2, 0, sizeof(value), 2,
            "repeated intern ownership mismatch"))
    {
        return false;
    }

    if (!Check(
            script_string::TryTransferOrdinaryToDatabaseUser(first.stringId)
                == script_string::TransferStatus::DatabaseUserClaimed,
            "database user claim failed")
        || !CheckOwnership(first.stringId, 2, 4, sizeof(value), 2,
            "database user claim accounting mismatch")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(first.stringId)
                == script_string::TransferStatus::DuplicateReleased,
            "duplicate database transfer was not released")
        || !CheckOwnership(first.stringId, 1, 4, sizeof(value), 1,
            "duplicate transfer accounting mismatch"))
    {
        return false;
    }

    const StateImage claimed = CaptureState();
    if (!Check(
            script_string::TryTransferOrdinaryToDatabaseUser(first.stringId)
                == script_string::TransferStatus::OwnershipMismatchNoChange,
            "database-only reference was transferred again")
        || !Check(StateMatches(claimed),
            "rejected database-only transfer changed state")
        || !Check(
            script_string::TryRemoveDatabaseUserReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "targeted database release failed")
        || !CheckFreed(first.stringId)
        || !Check(
            Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount) == 0,
            "database release retained total debug refs"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestOrdinaryRollbackFreeAndReuse() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "rollback";
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired,
            "rollback setup intern failed")
        || !Check(
            script_string::TryRemoveOrdinaryReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "last ordinary rollback failed")
        || !CheckFreed(first.stringId))
    {
        return false;
    }

    const auto reused = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    const auto repeated = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(reused.status == script_string::AcquireStatus::Acquired,
            "reacquisition after free failed")
        || !Check(reused.stringId == first.stringId,
            "freed allocation was not deterministically reused")
        || !Check(repeated.stringId == reused.stringId,
            "repeated reacquisition returned another ID")
        || !CheckOwnership(reused.stringId, 2, 0, sizeof(value), 2,
            "reacquisition accounting mismatch")
        || !Check(
            script_string::TryRemoveOrdinaryReference(reused.stringId)
                == script_string::ReleaseStatus::Success,
            "first ordinary rollback failed")
        || !CheckOwnership(reused.stringId, 1, 0, sizeof(value), 1,
            "partial ordinary rollback accounting mismatch")
        || !Check(
            script_string::TryRemoveOrdinaryReference(reused.stringId)
                == script_string::ReleaseStatus::Success,
            "second ordinary rollback failed")
        || !CheckFreed(reused.stringId))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestEmbeddedNulByteCount() noexcept
{
    if (!BeginTest())
        return false;

    constexpr std::array<char, 4> firstBytes{'a', '\0', 'b', '\0'};
    constexpr std::array<char, 4> secondBytes{'a', '\0', 'c', '\0'};
    constexpr std::uint32_t byteCount =
        static_cast<std::uint32_t>(firstBytes.size());
    static_assert(firstBytes.size() == secondBytes.size());
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        firstBytes.data(), byteCount, 15);
    const auto repeated = script_string::TryAcquireOrdinaryStringOfSize(
        firstBytes.data(), byteCount, 15);
    const auto second = script_string::TryAcquireOrdinaryStringOfSize(
        secondBytes.data(), byteCount, 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired,
            "embedded-NUL intern failed")
        || !Check(repeated.stringId == first.stringId,
            "embedded-NUL repeated intern did not match")
        || !Check(second.status == script_string::AcquireStatus::Acquired,
            "second embedded-NUL intern failed")
        || !Check(second.stringId != first.stringId,
            "bytes after embedded NUL were ignored")
        || !Check(
            std::memcmp(GetRefString(first.stringId)->str,
                firstBytes.data(), firstBytes.size()) == 0,
            "first embedded-NUL payload changed")
        || !Check(
            std::memcmp(GetRefString(second.stringId)->str,
                secondBytes.data(), secondBytes.size()) == 0,
            "second embedded-NUL payload changed")
        || !CheckOwnership(first.stringId, 2, 0, 4, 3,
            "first embedded-NUL accounting mismatch")
        || !CheckOwnership(second.stringId, 1, 0, 4, 3,
            "second embedded-NUL accounting mismatch"))
    {
        return false;
    }

    if (!Check(
            script_string::TryRemoveOrdinaryReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "embedded-NUL repeated release failed")
        || !Check(
            script_string::TryRemoveOrdinaryReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "embedded-NUL final release failed")
        || !Check(
            script_string::TryRemoveOrdinaryReference(second.stringId)
                == script_string::ReleaseStatus::Success,
            "second embedded-NUL release failed"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestCollidingByteLengthBounds() noexcept
{
    if (!BeginTest())
        return false;

    constexpr std::array<char, 3> shortBytes{
        static_cast<char>(123), static_cast<char>(97), '\0'};
    constexpr std::uint32_t shortByteCount =
        static_cast<std::uint32_t>(shortBytes.size());
    if (!Check(GetHashCode(shortBytes.data(), shortByteCount) == 1217,
            "short bounds-regression hash changed"))
    {
        return false;
    }

    const auto shortResult = script_string::TryAcquireOrdinaryStringOfSize(
        shortBytes.data(), shortByteCount, 15);
    if (!Check(shortResult.status == script_string::AcquireStatus::Acquired,
            "short collision intern failed"))
    {
        return false;
    }

    // Mirror the bytes following the tiny allocation. The old modulo-256
    // length check would authorize the complete over-read and incorrectly
    // report this 4,867-byte value as the existing three-byte string. The
    // earlier NUL also makes this value unrepresentable in RefString's packed
    // length format, so it must be rejected before any table mutation.
    std::vector<char> longBytes(4867);
    const std::uint32_t longByteCount =
        static_cast<std::uint32_t>(longBytes.size());
    std::memcpy(
        longBytes.data(),
        GetRefString(shortResult.stringId)->str,
        longBytes.size());
    if (!Check(longBytes.back() == '\0',
            "bounds-regression mirrored source lost its terminator")
        || !Check(GetHashCode(longBytes.data(), longByteCount) == 1217,
            "long bounds-regression hash changed")
        || !Check((shortBytes.size() & 0xFFu) == (longBytes.size() & 0xFFu),
            "bounds-regression byte-length aliases changed"))
    {
        return false;
    }

    const StateImage shortOnly = CaptureState();
    const auto longResult = script_string::TryAcquireOrdinaryStringOfSize(
        longBytes.data(), longByteCount, 15);
    if (!Check(
            longResult.status
                == script_string::AcquireStatus::InvalidArgumentNoChange,
            "ambiguous long collision source was accepted")
        || !Check(longResult.stringId == 0,
            "rejected long collision source published an ID")
        || !Check(StateMatches(shortOnly),
            "rejected long collision source changed state")
        || !CheckOwnership(shortResult.stringId, 1, 0, 3, 1,
            "short collision accounting mismatch"))
    {
        return false;
    }

    if (!Check(
            script_string::TryRemoveOrdinaryReference(shortResult.stringId)
                == script_string::ReleaseStatus::Success,
            "short collision release failed"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestLegacyBinaryInternCompatibility() noexcept
{
    if (!BeginTest())
        return false;

    // XAnimToXModel-style records use an explicit byte count and do not end
    // in NUL. The report-free database API is stricter, but the shared intern
    // implementation must preserve this established legacy contract.
    constexpr std::array<char, 18> binaryRecord{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 1, 127};
    constexpr std::uint32_t byteCount =
        static_cast<std::uint32_t>(binaryRecord.size());
    static_assert(binaryRecord.back() != '\0');

    const std::uint32_t first = SL_GetStringOfSize(
        binaryRecord.data(), 0, byteCount, 11);
    const std::uint32_t second = SL_GetStringOfSize(
        binaryRecord.data(), 0, byteCount, 11);
    if (!Check(first != 0, "legacy binary intern failed")
        || !Check(second == first,
            "repeated legacy binary intern returned a different ID")
        || !CheckAllocation(first, 11)
        || !CheckOwnership(
            first, 2, 0, static_cast<std::uint8_t>(byteCount), 2,
            "legacy binary ownership mismatch")
        || !Check(
            std::memcmp(
                GetRefString(first)->str,
                binaryRecord.data(),
                binaryRecord.size()) == 0,
            "legacy binary payload changed"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(first, byteCount);
    SL_RemoveRefToStringOfSize(first, byteCount);
    if (!CheckFreed(first)
        || !Check(ReportersUnused(),
            "legacy binary compatibility path invoked a reporter"))
    {
        return false;
    }

    const std::uint32_t transferFirst = SL_GetStringOfSize(
        binaryRecord.data(), 0, byteCount, 11);
    const std::uint32_t transferSecond = SL_GetStringOfSize(
        binaryRecord.data(), 0, byteCount, 11);
    if (!Check(transferFirst != 0 && transferSecond == transferFirst,
            "legacy binary transfer setup failed")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(transferFirst)
                == script_string::TransferStatus::DatabaseUserClaimed,
            "legacy binary report-free transfer failed")
        || !CheckOwnership(
            transferFirst, 2, 4, static_cast<std::uint8_t>(byteCount), 2,
            "legacy binary transferred ownership mismatch")
        || !Check(
            script_string::TryRemoveOrdinaryReference(transferFirst)
                == script_string::ReleaseStatus::Success,
            "legacy binary ordinary rollback failed")
        || !Check(
            script_string::TryRemoveDatabaseUserReference(transferFirst)
                == script_string::ReleaseStatus::Success,
            "legacy binary database rollback failed")
        || !CheckFreed(transferFirst))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestLegacyFreeListSpliceBoundaries() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstValue[] = "AAA";
    constexpr char collidingValue[] = "UZE";
    constexpr std::uint32_t firstByteCount =
        static_cast<std::uint32_t>(sizeof(firstValue));
    constexpr std::uint32_t collidingByteCount =
        static_cast<std::uint32_t>(sizeof(collidingValue));
    if (!Check(GetHashCode(firstValue, firstByteCount)
                == GetHashCode(collidingValue, collidingByteCount),
            "legacy splice fixture strings no longer collide"))
    {
        return false;
    }

    const std::uint32_t firstId = SL_GetStringOfSize(
        firstValue, 0, firstByteCount, 15);
    if (!Check(firstId != 0,
            "legacy splice fixture first intern failed"))
    {
        return false;
    }

    const std::uint32_t freeHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    const std::uint32_t freeHeadNext = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[freeHead].status_next);
    if (!Check(freeHead != 0 && freeHead < STRINGLIST_SIZE
            && freeHeadNext != 0 && freeHeadNext < STRINGLIST_SIZE,
            "legacy splice fixture lacks adjacent free entries"))
    {
        return false;
    }

    const std::uint32_t savedHeadNextPrevious =
        scrStringGlob.hashTable[freeHeadNext].u.prev;
    scrStringGlob.hashTable[freeHeadNext].u.prev = 0;
    const StateImage corruptConsumeHead = CaptureState();
    const std::uint32_t corruptConsumeResult = SL_GetStringOfSize(
        collidingValue, 0, collidingByteCount, 15);
    if (!Check(corruptConsumeResult == 0,
            "legacy collision intern trusted a corrupt head neighbor")
        || !Check(StateMatches(corruptConsumeHead),
            "legacy corrupt-head consumption changed state")
        || !Check(g_comErrorCount == 1 && g_assertCount == 0,
            "legacy corrupt-head consumption reported unexpectedly")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "legacy corrupt-head reporter retained a lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[freeHeadNext].u.prev = savedHeadNextPrevious;
    g_comErrorCount = 0;
    g_assertCount = 0;

    const std::uint32_t collidingId = SL_GetStringOfSize(
        collidingValue, 0, collidingByteCount, 15);
    if (!Check(collidingId != 0 && collidingId != firstId,
            "legacy collision intern did not allocate a distinct string")
        || !Check(static_cast<std::uint16_t>(
                scrStringGlob.hashTable[0].status_next) == freeHeadNext,
            "legacy collision intern did not consume the free-list head")
        || !Check(scrStringGlob.hashTable[freeHeadNext].u.prev == 0,
            "legacy collision intern did not repair the new head backlink"))
    {
        return false;
    }

    const std::uint32_t releaseHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    const HashEntry savedReleaseHead = scrStringGlob.hashTable[releaseHead];
    scrStringGlob.hashTable[releaseHead].u.prev = releaseHead;
    const StateImage corruptPrepend = CaptureState();
    SL_RemoveRefToStringOfSize(collidingId, collidingByteCount);
    if (!Check(g_comErrorCount == 0 && g_assertCount <= 1,
            "legacy corrupt prepend reported unexpectedly")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "legacy corrupt-prepend reporter retained a lock")
        || !Check(StateMatches(corruptPrepend),
            "legacy failed prepend did not restore exact ownership state")
        || !Check(scr_string_atomic::RefCount(Packed(collidingId)) == 1,
            "legacy failed prepend stranded a zero-reference string")
        || !Check(static_cast<std::uint16_t>(
                scrStringGlob.hashTable[0].status_next) == releaseHead,
            "legacy failed prepend published a new free head")
        || !Check(scrStringGlob.hashTable[releaseHead].u.prev == releaseHead,
            "legacy failed prepend changed the corrupt neighbor"))
    {
        return false;
    }
    scrStringGlob.hashTable[releaseHead] = savedReleaseHead;
    g_comErrorCount = 0;
    g_assertCount = 0;

    if (!CheckOwnership(collidingId, 1, 0,
            static_cast<std::uint8_t>(collidingByteCount), 2,
            "legacy failed-prepend recovery changed ownership"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(collidingId, collidingByteCount);
    if (!CheckFreed(collidingId)
        || !Check(static_cast<std::uint16_t>(
                scrStringGlob.hashTable[0].status_next) == freeHead,
            "legacy final release did not prepend the consumed entry")
        || !Check(static_cast<std::uint16_t>(
                scrStringGlob.hashTable[freeHead].status_next) == releaseHead,
            "legacy final release did not link the previous head")
        || !Check(scrStringGlob.hashTable[releaseHead].u.prev == freeHead,
            "legacy final release did not update the head neighbor"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(firstId, firstByteCount);
    return EndTest();
}

[[nodiscard]] bool TestLegacyEmptyAndOneNodeFreeList() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstValue[] = "AAA";
    constexpr char collidingValue[] = "UZE";
    constexpr std::uint32_t byteCount =
        static_cast<std::uint32_t>(sizeof(firstValue));
    static_assert(sizeof(firstValue) == sizeof(collidingValue));
    if (!Check(GetHashCode(firstValue, byteCount)
                == GetHashCode(collidingValue, byteCount),
            "legacy endpoint fixture strings no longer collide"))
    {
        return false;
    }

    const std::uint32_t firstId = SL_GetStringOfSize(
        firstValue, 0, byteCount, 15);
    const std::uint32_t freeEntry = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(firstId != 0 && freeEntry != 0,
            "legacy endpoint fixture setup failed"))
    {
        return false;
    }

    // Isolate the endpoint nodes without populating all 19,999 hash slots.
    // LegacyLocal deliberately validates only the list component it mutates;
    // this fixture exercises the exact one-node consume and empty prepend.
    scrStringGlob.hashTable[0].status_next =
        static_cast<std::uint16_t>(freeEntry) | HASH_STAT_FREE;
    scrStringGlob.hashTable[0].u.prev = freeEntry;
    scrStringGlob.hashTable[freeEntry].status_next = HASH_STAT_FREE;
    scrStringGlob.hashTable[freeEntry].u.prev = 0;
    if (!Check(SL_IsFreeListLocallyValidNoReport(),
            "legacy isolated one-node list failed local validation"))
    {
        return false;
    }

    const std::uint32_t collidingId = SL_GetStringOfSize(
        collidingValue, 0, byteCount, 15);
    std::memset(sl_freeListVisited, 0xA5, sizeof(sl_freeListVisited));
    const bool completeEmptyListValid =
        SL_IsFreeListHeadValidNoReport();
    bool emptyListScratchCleared = true;
    for (const std::uint8_t value : sl_freeListVisited)
        emptyListScratchCleared = emptyListScratchCleared && value == 0;
    if (!Check(collidingId != 0,
            "legacy one-node consumption failed")
        || !Check(static_cast<std::uint16_t>(
                scrStringGlob.hashTable[0].status_next) == 0
            && scrStringGlob.hashTable[0].u.prev == 0,
            "legacy one-node consumption did not empty the list")
        || !Check(completeEmptyListValid,
            "complete empty free list failed validation")
        || !Check(emptyListScratchCleared,
            "complete empty free list retained stale reachability bits")
        || !Check(SL_IsFreeListLocallyValidNoReport(),
            "legacy empty list failed local validation"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(collidingId, byteCount);
    const std::uint32_t restoredEntry = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(restoredEntry == freeEntry
            && scrStringGlob.hashTable[0].u.prev == restoredEntry,
            "legacy empty-list prepend did not restore both endpoints")
        || !Check(scrStringGlob.hashTable[restoredEntry].u.prev == 0
            && static_cast<std::uint16_t>(
                scrStringGlob.hashTable[restoredEntry].status_next) == 0,
            "legacy empty-list prepend did not form one node")
        || !Check(SL_IsFreeListLocallyValidNoReport(),
            "legacy restored one-node list failed local validation"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(firstId, byteCount);
    if (!Check(ReportersUnused(),
            "legacy endpoint boundary invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestShutdownStaleIterationRollback() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "shutdown-stale-iteration";
    constexpr std::uint32_t byteCount =
        static_cast<std::uint32_t>(sizeof(value));
    const std::uint32_t stringId = SL_GetStringOfSize(
        value, 4, byteCount, 15);
    if (!Check(stringId != 0,
            "shutdown stale-iteration fixture intern failed"))
    {
        return false;
    }

    const std::uint32_t hash = GetHashCode(value, byteCount);
    const std::uint32_t freeHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(freeHead != 0 && freeHead < STRINGLIST_SIZE,
            "shutdown stale-iteration fixture has no free head"))
    {
        return false;
    }

    const HashEntry savedFreeHead = scrStringGlob.hashTable[freeHead];
    scrStringGlob.hashTable[freeHead].u.prev = freeHead;
    // Model the non-null iteration marker left after moving a collision-chain
    // entry into its home slot. A failed release must restore this marker but
    // still terminate the do/while instead of retrying forever.
    scrStringGlob.nextFreeEntry = &scrStringGlob.hashTable[hash];
    const StateImage corruptState = CaptureState();
    SL_ShutdownSystem(4);

    if (!Check(g_comErrorCount == 1 && g_assertCount == 0,
            "stale-iteration shutdown rollback reported unexpectedly")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "stale-iteration shutdown reporter retained a lock")
        || !Check(StateMatches(corruptState),
            "stale-iteration shutdown did not restore exact state"))
    {
        return false;
    }

    scrStringGlob.hashTable[freeHead] = savedFreeHead;
    g_comErrorCount = 0;
    g_assertCount = 0;
    SL_ShutdownSystem(4);
    if (!CheckFreed(stringId)
        || !Check(scrStringGlob.nextFreeEntry == nullptr,
            "successful shutdown retained stale iteration state")
        || !Check(ReportersUnused(),
            "stale-iteration shutdown cleanup invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestSystemIterationAuthenticatesPhysicalEntries() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstValue[] = "system-physical-entry-alpha";
    constexpr char secondValue[] = "system-physical-entry-beta";
    constexpr std::uint32_t firstByteCount =
        static_cast<std::uint32_t>(sizeof(firstValue));
    constexpr std::uint32_t secondByteCount =
        static_cast<std::uint32_t>(sizeof(secondValue));
    const std::uint32_t firstHash = GetHashCode(firstValue, firstByteCount);
    const std::uint32_t secondHash = GetHashCode(secondValue, secondByteCount);
    if (!Check(firstHash != secondHash,
            "physical-entry fixture strings unexpectedly collided"))
    {
        return false;
    }

    const std::uint32_t firstId = SL_GetStringOfSize(
        firstValue, 4, firstByteCount, 15);
    const std::uint32_t secondId = SL_GetStringOfSize(
        secondValue, 4, secondByteCount, 15);
    if (!Check(firstId != 0 && secondId != 0,
            "physical-entry fixture intern failed"))
    {
        return false;
    }
    SL_AddUser(firstId, 8);
    if (!CheckOwnership(firstId, 2, 12,
            static_cast<std::uint8_t>(firstByteCount), 3,
            "physical-entry duplicate-user setup failed")
        || !CheckOwnership(secondId, 1, 4,
            static_cast<std::uint8_t>(secondByteCount), 3,
            "physical-entry transfer-user setup failed"))
    {
        return false;
    }

    const std::uint32_t forgedHash =
        firstHash > secondHash ? firstHash : secondHash;
    const std::uint32_t victimId =
        firstHash > secondHash ? secondId : firstId;
    const HashEntry savedEntry = scrStringGlob.hashTable[forgedHash];
    if (!Check((savedEntry.status_next & HASH_STAT_MASK) == HASH_STAT_HEAD,
            "physical-entry fixture has no home entry"))
    {
        return false;
    }

    scrStringGlob.hashTable[forgedHash].u.prev = victimId;
    const StateImage corruptShutdown = CaptureState();
    SL_ShutdownSystem(4);
    if (!Check(g_comErrorCount == 1 && g_assertCount == 0,
            "forged shutdown entry did not report exactly once")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "forged shutdown entry reported under an owned lock")
        || !Check(StateMatches(corruptShutdown),
            "forged shutdown entry changed ownership state"))
    {
        return false;
    }

    scrStringGlob.hashTable[forgedHash] = savedEntry;
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;

    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[firstId]);
    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[secondId]);
    const StateImage corruptPerIdDebug = CaptureState();
    SL_TransferSystem(4, 8);
    if (!Check(g_comErrorCount == 1 && g_assertCount == 0,
            "per-ID debug corruption did not report exactly once")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "per-ID debug corruption reported under an owned lock")
        || !Check(StateMatches(corruptPerIdDebug),
            "per-ID debug corruption changed ownership state"))
    {
        return false;
    }
    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[firstId]);
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[secondId]);
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;

    Sys_AtomicIncrement(&scrStringDebugGlob->totalRefCount);
    const StateImage corruptAggregate = CaptureState();
    SL_TransferSystem(4, 8);
    if (!Check(g_comErrorCount == 1 && g_assertCount == 0,
            "aggregate debug corruption did not report exactly once")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "aggregate debug corruption reported under an owned lock")
        || !Check(StateMatches(corruptAggregate),
            "aggregate debug corruption changed ownership state"))
    {
        return false;
    }
    Sys_AtomicDecrement(&scrStringDebugGlob->totalRefCount);
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;

    scrStringGlob.hashTable[forgedHash].u.prev = victimId;
    const StateImage corruptTransfer = CaptureState();
    SL_TransferSystem(4, 8);
    if (!Check(g_comErrorCount == 1 && g_assertCount == 0,
            "forged transfer entry did not report exactly once")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "forged transfer entry reported under an owned lock")
        || !Check(StateMatches(corruptTransfer),
            "forged transfer entry changed ownership state"))
    {
        return false;
    }

    scrStringGlob.hashTable[forgedHash] = savedEntry;
    g_comErrorCount = 0;
    g_assertCount = 0;
    g_reporterSawOwnedLock = false;

    SL_TransferSystem(4, 8);
    if (!CheckOwnership(firstId, 1, 8,
            static_cast<std::uint8_t>(firstByteCount), 2,
            "physical-entry first transfer cleanup failed")
        || !CheckOwnership(secondId, 1, 8,
            static_cast<std::uint8_t>(secondByteCount), 2,
            "physical-entry second transfer cleanup failed")
        || !Check(ReportersUnused(),
            "valid physical-entry transfer invoked a reporter"))
    {
        return false;
    }

    SL_ShutdownSystem(8);
    if (!CheckFreed(firstId) || !CheckFreed(secondId)
        || !Check(ReportersUnused(),
            "physical-entry cleanup invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestShutdownMixedCollisionChain() noexcept
{
    if (!BeginTest())
        return false;

    // GetHashCode deliberately hashes records of at least 256 bytes by length.
    // Insert in this order so the final value is the HEAD and the preceding
    // values occupy MOVABLE slots in reverse order. Shutdown must then free the
    // head, promote the mixed-user value, skip the absent-user value, and
    // splice the final user-4-only movable value without skipping/revisiting a
    // chain member.
    constexpr std::size_t valueCount = 4;
    constexpr std::uint32_t byteCount = 260;
    std::array<std::array<char, byteCount>, valueCount> values{};
    std::array<std::uint32_t, valueCount> ids{};
    for (std::size_t index = 0; index < valueCount; ++index)
    {
        values[index].fill(static_cast<char>('a' + index));
        values[index].back() = '\0';
    }
    const std::uint32_t owningHash =
        GetHashCode(values[0].data(), byteCount);
    for (std::size_t index = 0; index < valueCount; ++index)
    {
        if (!Check(GetHashCode(values[index].data(), byteCount) == owningHash,
                "mixed collision fixture hash changed"))
        {
            return false;
        }
        const std::uint32_t user = index == 1 ? 8 : 4;
        ids[index] = SL_GetStringOfSize(
            values[index].data(), user, byteCount, 15);
        if (!Check(ids[index] != 0,
                "mixed collision fixture intern failed"))
        {
            return false;
        }
    }
    SL_AddUser(ids[2], 8);

    if (!Check(scrStringGlob.hashTable[owningHash].u.prev == ids[3],
            "mixed collision fixture did not place final value at head")
        || !CheckOwnership(ids[2], 2, 12,
            static_cast<std::uint8_t>(byteCount), 5,
            "mixed collision dual-user setup failed")
        || !CheckOwnership(ids[1], 1, 8,
            static_cast<std::uint8_t>(byteCount), 5,
            "mixed collision absent-user setup failed"))
    {
        return false;
    }

    SL_ShutdownSystem(4);
    if (!CheckFreed(ids[0]) || !CheckFreed(ids[3])
        || !CheckOwnership(ids[2], 1, 8,
            static_cast<std::uint8_t>(byteCount), 2,
            "mixed collision survivor lost the wrong ownership")
        || !CheckOwnership(ids[1], 1, 8,
            static_cast<std::uint8_t>(byteCount), 2,
            "mixed collision absent user changed")
        || !Check(FindStringOfSize(values[2].data(), byteCount) == ids[2],
            "mixed collision promoted survivor is not lookupable")
        || !Check(FindStringOfSize(values[1].data(), byteCount) == ids[1],
            "mixed collision movable survivor is not lookupable")
        || !Check(FindStringOfSize(values[0].data(), byteCount) == 0,
            "mixed collision freed movable remains lookupable")
        || !Check(FindStringOfSize(values[3].data(), byteCount) == 0,
            "mixed collision freed head remains lookupable")
        || !Check(ReportersUnused(),
            "mixed collision shutdown invoked a reporter"))
    {
        return false;
    }

    SL_ShutdownSystem(8);
    if (!CheckFreed(ids[1]) || !CheckFreed(ids[2])
        || !Check(ReportersUnused(),
            "mixed collision cleanup invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestLegacyCompatibilityAvoidsCompleteScans() noexcept
{
    if (!BeginTest())
        return false;

    struct LegacyReference final
    {
        std::uint32_t stringId;
        std::uint32_t byteCount;
    };

    MT_ResetCompleteValidationCountForTesting();
    sl_completeFreeListValidationCount = 0;

    constexpr std::uint32_t uniqueCount = 2000;
    std::vector<LegacyReference> uniqueReferences;
    uniqueReferences.reserve(uniqueCount);
    const auto uniqueStart = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < uniqueCount; ++index)
    {
        std::array<char, 32> value{};
        const int characterCount = std::snprintf(
            value.data(), value.size(), "legacy-short-%04u", index);
        if (!Check(characterCount > 0
                && static_cast<std::size_t>(characterCount) < value.size(),
                "legacy unique benchmark formatting failed"))
        {
            return false;
        }
        const std::uint32_t byteCount =
            static_cast<std::uint32_t>(characterCount + 1);
        const std::uint32_t stringId = SL_GetStringOfSize(
            value.data(), 0, byteCount, 15);
        if (!Check(stringId != 0,
                "legacy unique benchmark intern failed"))
        {
            return false;
        }
        uniqueReferences.push_back({stringId, byteCount});
    }
    const auto uniqueEnd = std::chrono::steady_clock::now();

    constexpr char repeatedValue[] = "legacy-repeat";
    constexpr std::uint32_t repeatedByteCount =
        static_cast<std::uint32_t>(sizeof(repeatedValue));
    constexpr std::uint32_t repeatedCount = 10000;
    std::uint32_t repeatedId = 0;
    const auto repeatedStart = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < repeatedCount; ++index)
    {
        const std::uint32_t stringId = SL_GetStringOfSize(
            repeatedValue, 0, repeatedByteCount, 15);
        if (!Check(stringId != 0
                && (repeatedId == 0 || stringId == repeatedId),
                "legacy repeated benchmark intern changed identity"))
        {
            return false;
        }
        repeatedId = stringId;
    }
    const auto repeatedEnd = std::chrono::steady_clock::now();

    const auto makeLongValue = [](const std::uint32_t index) {
        std::array<char, 300> value{};
        value.fill(static_cast<char>('a' + index % 23));
        value[0] = 'L';
        value[1] = static_cast<char>('0' + index / 100);
        value[2] = static_cast<char>('0' + (index / 10) % 10);
        value[3] = static_cast<char>('0' + index % 10);
        value.back() = '\0';
        return value;
    };
    constexpr std::uint32_t longCount = 200;
    std::vector<LegacyReference> longReferences;
    longReferences.reserve(longCount);
    const auto longStart = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < longCount; ++index)
    {
        const auto value = makeLongValue(index);
        const std::uint32_t stringId = SL_GetStringOfSize(
            value.data(), 0,
            static_cast<std::uint32_t>(value.size()), 15);
        if (!Check(stringId != 0,
                "legacy long benchmark intern failed"))
        {
            return false;
        }
        longReferences.push_back(
            {stringId, static_cast<std::uint32_t>(value.size())});
    }
    const auto longEnd = std::chrono::steady_clock::now();

    std::array<char, 32> lastUnique{};
    const int lastUniqueLength = std::snprintf(
        lastUnique.data(), lastUnique.size(),
        "legacy-short-%04u", uniqueCount - 1);
    const auto lastLong = makeLongValue(longCount - 1);
    constexpr char transferValue[] = "legacy-transfer-bounded";
    const std::uint32_t transferId = SL_GetStringOfSize(
        transferValue, 0, sizeof(transferValue), 15);
    const std::uint32_t duplicateTransferId = SL_GetStringOfSize(
        transferValue, 0, sizeof(transferValue), 15);
    SL_TransferRefToUser(transferId, 4);
    SL_TransferRefToUser(duplicateTransferId, 4);
    constexpr char wrapperReleaseValue[] = "legacy-wrapper-release-bounded";
    const std::uint32_t wrapperReleaseId = SL_GetStringOfSize(
        wrapperReleaseValue, 0, sizeof(wrapperReleaseValue), 15);
    SL_RemoveRefToString(wrapperReleaseId);
    if (!Check(lastUniqueLength > 0,
            "legacy lookup benchmark formatting failed")
        || !Check(transferId != 0 && duplicateTransferId == transferId,
            "legacy transfer benchmark setup failed")
        || !CheckFreedLegacy(wrapperReleaseId)
        || !Check(SL_FindString(lastUnique.data())
                == uniqueReferences.back().stringId,
            "legacy unique lookup failed")
        || !Check(SL_FindString(repeatedValue) == repeatedId,
            "legacy repeated lookup failed")
        || !Check(SL_FindString(lastLong.data())
                == longReferences.back().stringId,
            "legacy long lookup failed")
        || !Check(MT_CompleteValidationCountForTesting() == 0,
            "legacy string path invoked exhaustive allocator validation")
        || !Check(MT_CompleteForestValidationCountForTesting() == 0,
            "legacy string path invoked exhaustive free-forest validation")
        || !Check(sl_completeFreeListValidationCount == 0,
            "legacy string path walked the complete free list"))
    {
        return false;
    }

    for (const LegacyReference &reference : longReferences)
        SL_RemoveRefToStringOfSize(reference.stringId, reference.byteCount);
    for (std::uint32_t index = 0; index < repeatedCount; ++index)
        SL_RemoveRefToStringOfSize(repeatedId, repeatedByteCount);
    for (const LegacyReference &reference : uniqueReferences)
        SL_RemoveRefToStringOfSize(reference.stringId, reference.byteCount);

    if (!Check(MT_CompleteValidationCountForTesting() == 0,
            "legacy release path invoked exhaustive allocator validation")
        || !Check(MT_CompleteForestValidationCountForTesting() == 0,
            "legacy release path invoked exhaustive free-forest validation")
        || !Check(sl_completeFreeListValidationCount == 0,
            "legacy release path walked the complete free list")
        || !Check(ReportersUnused(),
            "legacy benchmark path invoked a reporter"))
    {
        return false;
    }

    SL_ShutdownSystem(4);
    if (!CheckFreed(transferId)
        || !Check(ReportersUnused(),
            "legacy transfer benchmark cleanup invoked a reporter"))
    {
        return false;
    }

    const auto elapsedMilliseconds = [](const auto start, const auto end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    std::printf(
        "legacy string benchmark: 2000 unique %.3f ms, "
        "10000 repeated %.3f ms, 200 long %.3f ms\n",
        elapsedMilliseconds(uniqueStart, uniqueEnd),
        elapsedMilliseconds(repeatedStart, repeatedEnd),
        elapsedMilliseconds(longStart, longEnd));
    return EndTest();
}

[[nodiscard]] bool TestLegacyHashScratchResetIsChainBounded() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "legacy-scratch-bounded";
    constexpr std::uint32_t byteCount =
        static_cast<std::uint32_t>(sizeof(value));
    const std::uint32_t stringId =
        SL_GetStringOfSize(value, 0, byteCount, 15);
    if (!Check(stringId != 0, "legacy scratch seed intern failed"))
        return false;

    // Discard scratch retained by earlier validations, then measure only the
    // occupied one-entry chain below. Each validation leaves one hash index
    // and one string ID for the following reset to clear.
    SL_ResetHashChainValidationNoReport();
    sl_hashValidationScratchResetCount = 0;
    sl_hashValidationScratchResetEntryCount = 0;

    constexpr std::uint32_t iterationCount = 512;
    for (std::uint32_t iteration = 0;
        iteration < iterationCount;
        ++iteration)
    {
        if (!Check(
                SL_GetStringOfSize(value, 0, byteCount, 15) == stringId,
                "legacy scratch repeated intern changed identity")
            || !Check(
                SL_FindString(value) == stringId,
                "legacy scratch repeated lookup failed"))
        {
            return false;
        }
    }

    constexpr std::uint32_t validationCount = iterationCount * 2;
    constexpr std::uint64_t expectedResetEntries =
        static_cast<std::uint64_t>(validationCount - 1) * 2;
    if (!Check(
            sl_hashValidationScratchResetCount == validationCount,
            "legacy hash validation reset count changed")
        || !Check(
            sl_hashValidationScratchResetEntryCount
                == expectedResetEntries,
            "legacy hash validation cleared beyond its touched chain")
        || !Check(ReportersUnused(),
            "legacy bounded scratch path invoked a reporter"))
    {
        return false;
    }

    for (std::uint32_t reference = 0;
        reference <= iterationCount;
        ++reference)
    {
        SL_RemoveRefToStringOfSize(stringId, byteCount);
    }
    return EndTest();
}

[[nodiscard]] bool TestLegacyLocalCorruptionAndReporterUnwind() noexcept
{
    if (!BeginTest())
        return false;

    const auto checkExpectedError = [](const char *const context) {
        return Check(g_comErrorCount == 1, context)
            && Check(g_assertCount == 0, context)
            && Check(!g_reporterSawOwnedLock, context)
            && Check(LocksReleased(), context);
    };
    const auto resetExpectedError = []() {
        g_comErrorCount = 0;
        g_assertCount = 0;
    };

    constexpr char existingValue[] = "legacy-corruption-live";
    constexpr std::uint32_t existingByteCount =
        static_cast<std::uint32_t>(sizeof(existingValue));
    const std::uint32_t existingId = SL_GetStringOfSize(
        existingValue, 0, existingByteCount, 15);
    if (!Check(existingId != 0,
            "legacy corruption setup intern failed"))
    {
        return false;
    }

    const std::uint32_t freeHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(freeHead != 0 && freeHead < STRINGLIST_SIZE,
            "legacy free-list corruption setup has no head"))
    {
        return false;
    }
    const HashEntry savedFreeHead = scrStringGlob.hashTable[freeHead];
    scrStringGlob.hashTable[freeHead].status_next =
        (savedFreeHead.status_next & HASH_STAT_MASK) | UINT16_MAX;
    const StateImage corruptFreeHead = CaptureState();
    constexpr char freeHeadValue[] = "legacy-free-head-corrupt";
    constexpr std::uint32_t freeHeadByteCount =
        static_cast<std::uint32_t>(sizeof(freeHeadValue));
    const std::uint32_t freeHeadResult = SL_GetStringOfSize(
        freeHeadValue, 0, freeHeadByteCount, 15);
    if (!Check(freeHeadResult == 0,
            "legacy intern trusted malformed free-list head")
        || !Check(StateMatches(corruptFreeHead),
            "legacy free-list head rejection changed state")
        || !checkExpectedError(
            "legacy free-list head reporter ran under an owned lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[freeHead] = savedFreeHead;
    resetExpectedError();

    const std::uint32_t savedFreeTail = scrStringGlob.hashTable[0].u.prev;
    scrStringGlob.hashTable[0].u.prev = UINT32_MAX;
    const StateImage corruptFreeTail = CaptureState();
    constexpr char freeTailValue[] = "legacy-free-tail-corrupt";
    constexpr std::uint32_t freeTailByteCount =
        static_cast<std::uint32_t>(sizeof(freeTailValue));
    const std::uint32_t freeTailResult = SL_GetStringOfSize(
        freeTailValue, 0, freeTailByteCount, 15);
    if (!Check(freeTailResult == 0,
            "legacy intern trusted malformed free-list tail")
        || !Check(StateMatches(corruptFreeTail),
            "legacy free-list tail rejection changed state")
        || !checkExpectedError(
            "legacy free-list tail reporter ran under an owned lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[0].u.prev = savedFreeTail;
    resetExpectedError();

    const std::uint32_t hash =
        GetHashCode(existingValue, existingByteCount);
    const HashEntry savedHashEntry = scrStringGlob.hashTable[hash];
    if (!Check((savedHashEntry.status_next & HASH_STAT_MASK)
                == HASH_STAT_HEAD,
            "legacy hash corruption setup has no owning head"))
    {
        return false;
    }
    scrStringGlob.hashTable[hash].status_next =
        HASH_STAT_HEAD | UINT16_MAX;
    const StateImage corruptHash = CaptureState();
    if (!Check(SL_FindString(existingValue) == 0,
            "legacy lookup trusted malformed hash cycle")
        || !Check(StateMatches(corruptHash),
            "legacy malformed lookup changed state")
        || !Check(ReportersUnused(),
            "legacy malformed lookup invoked a reporter"))
    {
        return false;
    }
    const std::uint32_t corruptHashResult = SL_GetStringOfSize(
        existingValue, 0, existingByteCount, 15);
    if (!Check(corruptHashResult == 0,
            "legacy intern trusted malformed hash cycle")
        || !Check(StateMatches(corruptHash),
            "legacy malformed hash rejection changed state")
        || !checkExpectedError(
            "legacy hash reporter ran under an owned lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[hash] = savedHashEntry;
    resetExpectedError();

    scrStringGlob.hashTable[hash].status_next =
        savedHashEntry.status_next | UINT32_C(0x40000);
    const StateImage corruptHashEncoding = CaptureState();
    if (!Check(SL_FindString(existingValue) == 0,
            "legacy lookup trusted reserved hash-entry bits")
        || !Check(StateMatches(corruptHashEncoding),
            "reserved-bit lookup rejection changed state")
        || !Check(ReportersUnused(),
            "reserved-bit lookup rejection invoked a reporter"))
    {
        return false;
    }
    const std::uint32_t corruptEncodingResult = SL_GetStringOfSize(
        existingValue, 0, existingByteCount, 15);
    if (!Check(corruptEncodingResult == 0,
            "legacy intern trusted reserved hash-entry bits")
        || !Check(StateMatches(corruptHashEncoding),
            "reserved-bit intern rejection changed state")
        || !checkExpectedError(
            "reserved-bit intern reporter ran under an owned lock"))
    {
        return false;
    }
    resetExpectedError();

    if (!Check(
            script_string::TryRemoveOrdinaryReference(existingId)
                == script_string::ReleaseStatus::UnsafeFailure,
            "typed release trusted reserved hash-entry bits")
        || !Check(StateMatches(corruptHashEncoding),
            "reserved-bit release rejection changed state")
        || !Check(ReportersUnused(),
            "reserved-bit release rejection invoked a reporter")
        || !Check(LocksReleased(),
            "reserved-bit release retained a lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[hash] = savedHashEntry;
    resetExpectedError();

    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[existingId]);
    const StateImage corruptLegacyDebug = CaptureState();
    SL_RemoveRefToStringOfSize(existingId, existingByteCount);
    if (!Check(StateMatches(corruptLegacyDebug),
            "legacy debug-accounting rejection changed state")
        || !Check(g_comErrorCount == 0 && g_assertCount <= 1,
            "legacy debug-accounting rejection reported unexpectedly")
        || !Check(!g_reporterSawOwnedLock && LocksReleased(),
            "legacy debug-accounting reporter retained a lock"))
    {
        return false;
    }
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[existingId]);
    resetExpectedError();

    constexpr char allocatorValue[] = "legacy-allocator-corrupt";
    constexpr std::uint32_t allocatorByteCount =
        static_cast<std::uint32_t>(sizeof(allocatorValue));
    const int allocationSize = MT_GetSize(
        static_cast<int>(allocatorByteCount + 4));
    int treeSize = allocationSize;
    while (treeSize <= MEMORY_NODE_BITS
        && scrMemTreeGlob.head[treeSize] == 0)
    {
        ++treeSize;
    }
    if (!Check(treeSize <= MEMORY_NODE_BITS,
            "legacy allocator corruption setup has no free root"))
    {
        return false;
    }
    const std::uint16_t root = scrMemTreeGlob.head[treeSize];
    const MemoryNode savedRoot = scrMemTreeGlob.nodes[root];
    scrMemTreeGlob.nodes[root].prev = root;
    const StateImage corruptAllocator = CaptureState();
    const std::uint32_t corruptAllocatorResult = SL_GetStringOfSize(
        allocatorValue, 0, allocatorByteCount, 15);
    if (!Check(corruptAllocatorResult == 0,
            "legacy intern trusted a cyclic allocator path")
        || !Check(StateMatches(corruptAllocator),
            "legacy allocator rejection changed state")
        || !checkExpectedError(
            "legacy allocator reporter ran under an owned lock"))
    {
        return false;
    }
    scrMemTreeGlob.nodes[root] = savedRoot;
    resetExpectedError();

    constexpr char shutdownValue[] = "legacy-shutdown-corrupt";
    constexpr std::uint32_t shutdownByteCount =
        static_cast<std::uint32_t>(sizeof(shutdownValue));
    const std::uint32_t shutdownId = SL_GetStringOfSize(
        shutdownValue, 4, shutdownByteCount, 15);
    if (!Check(shutdownId != 0,
            "legacy shutdown rollback setup failed"))
    {
        return false;
    }
    const std::uint32_t shutdownFreeHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    const HashEntry savedShutdownFreeHead =
        scrStringGlob.hashTable[shutdownFreeHead];
    scrStringGlob.hashTable[shutdownFreeHead].u.prev = shutdownFreeHead;
    const StateImage corruptShutdown = CaptureState();
    SL_ShutdownSystem(4);
    if (!Check(StateMatches(corruptShutdown),
            "legacy shutdown failure did not restore exact ownership")
        || !checkExpectedError(
            "legacy shutdown reporter ran under an owned lock"))
    {
        return false;
    }
    scrStringGlob.hashTable[shutdownFreeHead] = savedShutdownFreeHead;
    resetExpectedError();

    SL_ShutdownSystem(4);
    if (!CheckFreed(shutdownId)
        || !Check(ReportersUnused(),
            "legacy shutdown rollback cleanup invoked a reporter"))
    {
        return false;
    }

    SL_RemoveRefToStringOfSize(existingId, existingByteCount);
    if (!CheckFreed(existingId)
        || !Check(ReportersUnused(),
            "legacy corruption cleanup invoked a reporter"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestMalformedStateFailsClosed() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char freeListValue[] = "free-list-corrupt";
    const auto freeListString = script_string::TryAcquireOrdinaryStringOfSize(
        freeListValue, sizeof(freeListValue), 15);
    const std::uint32_t freeHead = static_cast<std::uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(
            freeListString.status == script_string::AcquireStatus::Acquired,
            "free-list-corruption setup failed")
        || !Check(freeHead != 0, "free-list-corruption setup has no head"))
    {
        return false;
    }

    const HashEntry savedFreeHead = scrStringGlob.hashTable[freeHead];
    scrStringGlob.hashTable[freeHead].status_next =
        (savedFreeHead.status_next & HASH_STAT_MASK)
        | UINT16_MAX;
    const StateImage corruptFreeForward = CaptureState();
    if (!Check(
            script_string::TryRemoveOrdinaryReference(
                freeListString.stringId)
                == script_string::ReleaseStatus::UnsafeFailure,
            "malformed free-list forward link was trusted by release")
        || !Check(StateMatches(corruptFreeForward),
            "malformed free-list forward rejection changed state")
        || !Check(ReportersUnused(),
            "malformed free-list forward rejection invoked a reporter"))
    {
        return false;
    }
    scrStringGlob.hashTable[freeHead] = savedFreeHead;

    if (!Check(
            script_string::TryTransferOrdinaryToDatabaseUser(
                freeListString.stringId)
                == script_string::TransferStatus::DatabaseUserClaimed,
            "free-list tail-corruption transfer setup failed"))
    {
        return false;
    }
    const std::uint32_t savedFreeTail = scrStringGlob.hashTable[0].u.prev;
    scrStringGlob.hashTable[0].u.prev = savedFreeTail ^ UINT32_C(1);
    const StateImage corruptFreeTail = CaptureState();
    if (!Check(
            script_string::TryRemoveDatabaseUserReference(
                freeListString.stringId)
                == script_string::ReleaseStatus::UnsafeFailure,
            "malformed free-list sentinel tail was trusted by release")
        || !Check(StateMatches(corruptFreeTail),
            "malformed free-list tail rejection changed state")
        || !Check(ReportersUnused(),
            "malformed free-list tail rejection invoked a reporter"))
    {
        return false;
    }
    scrStringGlob.hashTable[0].u.prev = savedFreeTail;
    if (!Check(
            script_string::TryRemoveDatabaseUserReference(
                freeListString.stringId)
                == script_string::ReleaseStatus::Success,
            "free-list-corruption cleanup failed")
        || !CheckFreed(freeListString.stringId))
    {
        return false;
    }

    constexpr char hashValue[] = "hash-corrupt";
    const auto hashString = script_string::TryAcquireOrdinaryStringOfSize(
        hashValue, sizeof(hashValue), 15);
    const std::uint32_t hash = GetHashCode(hashValue, sizeof(hashValue));
    const HashEntry savedHashEntry = scrStringGlob.hashTable[hash];
    scrStringGlob.hashTable[hash].status_next = HASH_STAT_HEAD;
    const StateImage corruptHash = CaptureState();
    const auto hashAcquire = script_string::TryAcquireOrdinaryStringOfSize(
        hashValue, sizeof(hashValue), 15);
    if (!Check(hashString.status == script_string::AcquireStatus::Acquired,
            "hash-corruption setup failed")
        || !Check(hashAcquire.status == script_string::AcquireStatus::UnsafeFailure,
            "malformed hash chain was trusted by acquisition")
        || !Check(hashAcquire.stringId == 0,
            "malformed hash acquisition published an ID")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(hashString.stringId)
                == script_string::TransferStatus::UnsafeFailure,
            "malformed hash chain was trusted by transfer")
        || !Check(StateMatches(corruptHash),
            "malformed hash rejection changed state")
        || !Check(ReportersUnused(),
            "malformed hash rejection invoked a reporter"))
    {
        return false;
    }
    scrStringGlob.hashTable[hash] = savedHashEntry;
    if (!Check(
            script_string::TryRemoveOrdinaryReference(hashString.stringId)
                == script_string::ReleaseStatus::Success,
            "hash-corruption cleanup failed"))
    {
        return false;
    }

    constexpr char debugValue[] = "debug-corrupt";
    const auto debugString = script_string::TryAcquireOrdinaryStringOfSize(
        debugValue, sizeof(debugValue), 15);
    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[debugString.stringId]);
    const StateImage corruptDebug = CaptureState();
    const auto debugAcquire = script_string::TryAcquireOrdinaryStringOfSize(
        debugValue, sizeof(debugValue), 15);
    if (!Check(debugString.status == script_string::AcquireStatus::Acquired,
            "debug-corruption setup failed")
        || !Check(
            debugAcquire.status == script_string::AcquireStatus::UnsafeFailure,
            "malformed debug accounting was trusted by acquisition")
        || !Check(
            script_string::TryRemoveOrdinaryReference(debugString.stringId)
                == script_string::ReleaseStatus::UnsafeFailure,
            "malformed debug accounting was trusted by release")
        || !Check(StateMatches(corruptDebug),
            "malformed debug rejection changed state")
        || !Check(ReportersUnused(),
            "malformed debug rejection invoked a reporter"))
    {
        return false;
    }
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[debugString.stringId]);
    if (!Check(
            script_string::TryRemoveOrdinaryReference(debugString.stringId)
                == script_string::ReleaseStatus::Success,
            "debug-corruption cleanup failed"))
    {
        return false;
    }

    constexpr char allocationValue[] = "12345678";
    const auto allocationString = script_string::TryAcquireOrdinaryStringOfSize(
        allocationValue, sizeof(allocationValue), 15);
    MT_AllocationInfo allocationInfo{};
    if (!Check(allocationString.status == script_string::AcquireStatus::Acquired,
            "allocator-corruption setup failed")
        || !Check(
            MT_TryGetAllocationInfo(allocationString.stringId, &allocationInfo)
                == MT_AllocationInfoStatus::Success,
            "allocator-corruption setup query failed")
        || !Check(allocationInfo.size >= 1,
            "allocator-corruption allocation has no interior bucket"))
    {
        return false;
    }
    const std::uint16_t savedHead = scrMemTreeGlob.head[0];
    scrMemTreeGlob.head[0] =
        static_cast<std::uint16_t>(allocationString.stringId + 1u);
    const StateImage corruptAllocation = CaptureState();
    const auto allocationAcquire =
        script_string::TryAcquireOrdinaryStringOfSize(
            allocationValue, sizeof(allocationValue), 15);
    if (!Check(
            allocationAcquire.status == script_string::AcquireStatus::UnsafeFailure,
            "overlapping allocator partition was trusted by acquisition")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(
                allocationString.stringId)
                == script_string::TransferStatus::UnsafeFailure,
            "overlapping allocator partition was trusted by transfer")
        || !Check(StateMatches(corruptAllocation),
            "allocator-corruption rejection changed state")
        || !Check(LocksReleased(),
            "malformed-state rejection leaked a critical section")
        || !Check(ReportersUnused(),
            "allocator-corruption rejection invoked a reporter"))
    {
        return false;
    }
    scrMemTreeGlob.head[0] = savedHead;
    if (!Check(
            script_string::TryRemoveOrdinaryReference(allocationString.stringId)
                == script_string::ReleaseStatus::Success,
            "allocator-corruption cleanup failed"))
    {
        return false;
    }
    return EndTest();
}

[[nodiscard]] bool TestOwnershipBatchLifecycle() noexcept
{
    if (!BeginTest())
        return false;

    MT_ResetCompleteValidationCountForTesting();
    script_string::ResetOwnershipValidationCountersForTesting();
    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(nullptr)
                == script_string::OwnershipBatchStatus::InvalidArgument,
            "null ownership batch was accepted")
        || !Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "ownership batch admission failed")
        || !Check(batch.active() && !batch.poisoned(),
            "admitted ownership batch has invalid state")
        || !Check(batch.serial() != 0 && batch.operationCount() == 0,
            "admitted ownership batch has invalid accounting")
        || !Check(g_scriptStringLockDepth == 1,
            "ownership batch did not retain the script lock")
        || !Check(g_memoryTreeLockDepth == 1,
            "ownership batch did not retain the allocator lock"))
    {
        return false;
    }

    constexpr char value[] = "ownership-batch-lifecycle";
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        batch, value, sizeof(value), 15);
    const auto second = script_string::TryAcquireOrdinaryStringOfSize(
        batch, value, sizeof(value), 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired,
            "batch first acquisition failed")
        || !Check(second.status == script_string::AcquireStatus::Acquired,
            "batch repeated acquisition failed")
        || !Check(first.stringId == second.stringId,
            "batch repeated acquisition changed identity")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(
                batch, first.stringId)
                == script_string::TransferStatus::DatabaseUserClaimed,
            "batch database ownership claim failed")
        || !Check(
            script_string::TryTransferOrdinaryToDatabaseUser(
                batch, first.stringId)
                == script_string::TransferStatus::DuplicateReleased,
            "batch duplicate ownership collapse failed")
        || !CheckOwnership(
            first.stringId,
            1,
            static_cast<std::uint8_t>(script_string::kDatabaseUserMask),
            static_cast<std::uint8_t>(sizeof(value)),
            1,
            "batch ownership accounting changed")
        || !Check(
            script_string::TryRemoveDatabaseUserReference(
                batch, first.stringId)
                == script_string::ReleaseStatus::Success,
            "batch database rollback failed")
        || !Check(batch.operationCount() == 5,
            "batch operation accounting changed"))
    {
        return false;
    }

    const auto openStringCounters =
        script_string::GetOwnershipValidationCountersForTesting();
    if (!Check(openStringCounters.completeStringPasses == 1,
            "batch operations repeated complete string validation")
        || !Check(MT_CompleteValidationCountForTesting() == 1,
            "batch operations repeated complete allocator validation")
        || !Check(MT_CompleteForestValidationCountForTesting() == 1,
            "batch operations repeated complete forest validation")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "valid ownership batch close failed"))
    {
        return false;
    }

    const auto closedStringCounters =
        script_string::GetOwnershipValidationCountersForTesting();
    return Check(closedStringCounters.completeStringPasses == 2,
            "ownership boundaries did not perform exactly two string passes")
        && Check(MT_CompleteValidationCountForTesting() == 2,
            "ownership boundaries did not perform exactly two allocator passes")
        && Check(MT_CompleteForestValidationCountForTesting() == 2,
            "ownership boundaries did not perform exactly two forest passes")
        && Check(!batch.active() && batch.serial() == 0,
            "closed ownership batch retained authentication")
        && Check(batch.operationCount() == 0,
            "closed ownership batch retained operation accounting")
        && Check(LocksReleased(),
            "closed ownership batch leaked a critical section")
        && CheckFreed(first.stringId)
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchAllowsSharedVectorDebugSlots() noexcept
{
    if (!BeginTest())
        return false;

    constexpr int foreignAllocationBytes = static_cast<int>(sizeof(RefVector));
    std::uint16_t foreignIndex = 0;
    const auto allocationStatus =
        MT_TryAllocIndex(foreignAllocationBytes, 2, &foreignIndex);
    if (!Check(allocationStatus == MT_AllocIndexStatus::Success,
            "shared-debug fixture allocation failed"))
    {
        return false;
    }

    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[foreignIndex]);
    script_string::OwnershipBatch batch;
    const auto beginStatus = script_string::TryBeginOwnershipBatch(&batch);
    const auto finishStatus = beginStatus
        == script_string::OwnershipBatchStatus::Success
        ? script_string::FinishOwnershipBatch(&batch)
        : script_string::OwnershipBatchStatus::UnsafeFailure;
    const std::uint32_t foreignDebugRefs =
        Sys_AtomicLoad(&scrStringDebugGlob->refCount[foreignIndex]);
    const std::uint32_t stringDebugTotal =
        Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount);
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[foreignIndex]);
    const auto freeStatus =
        MT_TryFreeIndex(foreignIndex, foreignAllocationBytes);

    return Check(
            beginStatus == script_string::OwnershipBatchStatus::Success,
            "vector-only debug slot blocked ownership admission")
        && Check(
            finishStatus == script_string::OwnershipBatchStatus::Success,
            "vector-only debug slot blocked ownership close")
        && Check(foreignDebugRefs == 1,
            "ownership validation changed a vector-only debug slot")
        && Check(stringDebugTotal == 0,
            "vector-only debug accounting changed the string total")
        && Check(freeStatus == MT_FreeIndexStatus::Success,
            "shared-debug fixture cleanup failed")
        && Check(LocksReleased(),
            "shared-debug ownership validation leaked a lock")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchRejectsUnauthorizedEntries() noexcept
{
    if (!BeginTest())
        return false;

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "legacy exclusion batch admission failed"))
    {
        return false;
    }

    const StateImage before = CaptureState();
    constexpr char value[] = "unauthorized-batch-entry";
    uint16_t callerSlot = UINT16_C(0xA55A);
    uint32_t rawStringId = UINT32_C(0xA55AA55A);
    volatile std::uintptr_t rejectedTextAddress = 0;
    volatile std::uint32_t rejectedStringValueSource = UINT32_MAX;
    const std::uint32_t rejectedStringValue = rejectedStringValueSource;
    const char *const rejectedText =
        reinterpret_cast<const char *>(rejectedTextAddress);
    char *const rejectedMutableText =
        reinterpret_cast<char *>(rejectedTextAddress);
    const float *const rejectedVector =
        reinterpret_cast<const float *>(rejectedTextAddress);
    RefString *const rejectedRefString =
        reinterpret_cast<RefString *>(rejectedTextAddress);
    const auto typedAcquire = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    const auto rawIntern = SL_TryInternStringOfSize(
        rejectedText, UINT32_MAX, UINT32_MAX, 0, &rawStringId);

    SL_Init();
    SL_InitCheckLeaks();
    SL_Shutdown();
    SL_ShutdownSystem(UINT32_MAX);
    SL_TransferSystem(UINT32_MAX, UINT32_MAX);
    SL_CheckLeaks();
    SL_AddRefToString(0);
    SL_TransferRefToUser(0, UINT32_MAX);
    SL_RemoveRefToString(0);
    SL_RemoveRefToStringOfSize(0, UINT32_MAX);
    SL_AddUser(0, UINT32_MAX);
    Scr_SetString(&callerSlot, 1);
    Scr_SetStringFromCharString(&callerSlot, rejectedText);

    SL_CheckExists(rejectedStringValue);
    const int rejectedLowercase = SL_IsLowercaseString(rejectedStringValue);
    const char *const rejectedConversion =
        SL_ConvertToString(rejectedStringValue);
    RefString *const rejectedIndexLookup =
        GetRefString(rejectedStringValue);
    RefString *const rejectedPointerLookup = GetRefString(rejectedText);
    const int rejectedStringLength = SL_GetStringLen(rejectedStringValue);
    const int rejectedRefStringLength =
        SL_GetRefStringLen(rejectedRefString);
    const char *const rejectedDebugConversion =
        SL_DebugConvertToString(rejectedStringValue);
    const std::uint32_t rejectedStringId =
        SL_ConvertFromString(rejectedText);
    const std::uint32_t rejectedUsers = SL_GetUser(rejectedStringValue);
    const char *const rejectedSafeConversion =
        SL_ConvertToStringSafe(rejectedStringValue);

    const bool valid = Check(
            typedAcquire.status == script_string::AcquireStatus::UnsafeFailure,
            "unbatched typed acquisition entered an ownership batch")
        && Check(
            script_string::TryTransferOrdinaryToDatabaseUser(0)
                == script_string::TransferStatus::UnsafeFailure,
            "unbatched typed transfer entered an ownership batch")
        && Check(
            script_string::TryRemoveOrdinaryReference(0)
                == script_string::ReleaseStatus::UnsafeFailure,
            "unbatched typed release entered an ownership batch")
        && Check(
            script_string::TryRemoveDatabaseUserReference(0)
                == script_string::ReleaseStatus::UnsafeFailure,
            "unbatched database release entered an ownership batch")
        && Check(rawIntern == SL_InternStatus::UnsafeFailure,
            "raw typed intern entered an ownership batch")
        && Check(rawStringId == UINT32_C(0xA55AA55A),
            "rejected raw intern published a string ID")
        && Check(!SL_AddUserInternal(rejectedRefString, UINT32_MAX),
            "raw user mutation entered an ownership batch")
        && Check(!SL_FreeString(0, rejectedRefString, 0),
            "raw free entered an ownership batch")
        && Check(Scr_AllocString(rejectedMutableText, 0) == 0,
            "reporting allocation entered an ownership batch")
        && Check(SL_GetString_(rejectedText, UINT32_MAX, 0) == 0,
            "legacy intern wrapper entered an ownership batch")
        && Check(
            SL_GetStringOfSize(
                rejectedText, UINT32_MAX, UINT32_MAX, 0) == 0,
            "legacy sized intern entered an ownership batch")
        && Check(SL_GetStringForVector(rejectedVector) == 0,
            "vector formatter entered an ownership batch")
        && Check(SL_GetStringForInt(1) == 0,
            "integer formatter entered an ownership batch")
        && Check(SL_GetStringForFloat(1.0f) == 0,
            "float formatter entered an ownership batch")
        && Check(SL_GetString(rejectedText, UINT32_MAX) == 0,
            "legacy string wrapper entered an ownership batch")
        && Check(SL_GetLowercaseString_(rejectedText, UINT32_MAX, 0) == 0,
            "lowercase intern entered an ownership batch")
        && Check(SL_FindString(rejectedText) == 0,
            "legacy lookup entered an ownership batch")
        && Check(SL_FindLowercaseString(rejectedText) == 0,
            "lowercase lookup entered an ownership batch")
        && Check(SL_ConvertToLowercase(0, UINT32_MAX, 0) == 0,
            "lowercase conversion entered an ownership batch")
        && Check(Scr_CreateCanonicalFilename(rejectedText) == 0,
            "canonicalization entered an ownership batch")
        && Check(rejectedLowercase == 0,
            "lowercase reader entered an ownership batch")
        && Check(rejectedConversion == nullptr,
            "string conversion entered an ownership batch")
        && Check(rejectedIndexLookup == nullptr,
            "raw string-index lookup entered an ownership batch")
        && Check(rejectedPointerLookup == nullptr,
            "raw string-pointer lookup entered an ownership batch")
        && Check(rejectedStringLength == 0,
            "string length reader entered an ownership batch")
        && Check(rejectedRefStringLength == 0,
            "raw string length reader entered an ownership batch")
        && Check(rejectedDebugConversion != nullptr
                && std::strcmp(
                    rejectedDebugConversion, "<UNAVAILABLE>") == 0,
            "debug string conversion entered an ownership batch")
        && Check(rejectedStringId == 0,
            "raw string conversion entered an ownership batch")
        && Check(rejectedUsers == 0,
            "string user reader entered an ownership batch")
        && Check(rejectedSafeConversion != nullptr
                && std::strcmp(
                    rejectedSafeConversion, "<UNAVAILABLE>") == 0,
            "safe string conversion entered an ownership batch")
        && Check(callerSlot == UINT16_C(0xA55A),
            "rejected setter changed caller state")
        && Check(StateMatches(before),
            "unauthorized ownership entries changed persistent state")
        && Check(ReportersUnused(),
            "unauthorized ownership entry invoked a reporter")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "legacy exclusion batch close failed")
        && Check(LocksReleased(),
            "legacy exclusion batch leaked a critical section");
    return valid && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchLocalPoisoning() noexcept
{
    if (!BeginTest())
        return false;

    MT_ResetCompleteValidationCountForTesting();
    script_string::ResetOwnershipValidationCountersForTesting();
    script_string::OwnershipBatch batch;
    constexpr char value[] = "batch-local-corruption";
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "local-corruption batch admission failed"))
    {
        return false;
    }
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        batch, value, sizeof(value), 15);
    const uint32_t freeHead = static_cast<uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "local-corruption acquisition failed")
        || !Check(freeHead != 0,
            "local-corruption setup has no free-list head"))
    {
        return false;
    }

    const HashEntry savedFreeHead = scrStringGlob.hashTable[freeHead];
    scrStringGlob.hashTable[freeHead].u.prev = freeHead;
    const StateImage corrupt = CaptureState();
    const auto rejected = script_string::TryRemoveOrdinaryReference(
        batch, acquired.stringId);
    const bool noChange = Check(
            rejected == script_string::ReleaseStatus::UnsafeFailure,
            "batch operation trusted corrupt local free-list state")
        && Check(batch.poisoned(),
            "unsafe batch operation did not poison its token")
        && Check(StateMatches(corrupt),
            "unsafe batch operation changed corrupt state")
        && Check(
            script_string::GetOwnershipValidationCountersForTesting()
                    .completeStringPasses
                == 1,
            "batch operation rebuilt the complete string certificate")
        && Check(MT_CompleteValidationCountForTesting() == 1,
            "batch operation repeated complete allocator validation");

    scrStringGlob.hashTable[freeHead] = savedFreeHead;
    const bool closeRejected = Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "poisoned ownership batch closed successfully")
        && Check(
            script_string::GetOwnershipValidationCountersForTesting()
                    .completeStringPasses
                == 2,
            "poisoned close skipped complete string validation")
        && Check(MT_CompleteValidationCountForTesting() == 2,
            "poisoned close skipped complete allocator validation")
        && Check(LocksReleased(),
            "poisoned ownership close leaked a critical section");
    return noChange && closeRejected
        && Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "poisoned batch cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchBoundaryValidation() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "batch-boundary-validation";
    const auto existing = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(existing.status == script_string::AcquireStatus::Acquired,
            "boundary-validation setup failed"))
    {
        return false;
    }

    constexpr char secondValue[] = "batch-boundary-debug-peer";
    const auto secondString = script_string::TryAcquireOrdinaryStringOfSize(
        secondValue, sizeof(secondValue), 15);
    if (!Check(secondString.status == script_string::AcquireStatus::Acquired,
            "boundary-validation peer setup failed"))
    {
        return false;
    }

    script_string::OwnershipBatch rejected;
    scrStringDebugGlob_t *const canonicalDebug = scrStringDebugGlob;
    scrStringDebugGlob = nullptr;
    const bool nullDebugRejected = Check(
            script_string::TryBeginOwnershipBatch(&rejected)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "batch admission trusted missing debug ownership")
        && Check(!rejected.active(),
            "missing debug ownership published a token")
        && Check(scrStringDebugGlob == nullptr,
            "missing debug ownership admission changed state")
        && Check(LocksReleased(),
            "missing debug ownership admission leaked a critical section")
        && Check(ReportersUnused(),
            "missing debug ownership admission invoked a reporter");
    scrStringDebugGlob = canonicalDebug;
    if (!nullDebugRejected)
        return false;

    scrStringDebugGlob = reinterpret_cast<scrStringDebugGlob_t *>(
        static_cast<std::uintptr_t>(1));
    const bool debugPointerRejected = Check(
            script_string::TryBeginOwnershipBatch(&rejected)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "batch admission dereferenced a noncanonical debug pointer")
        && Check(!rejected.active(),
            "bad debug pointer admission published a token")
        && Check(scrStringDebugGlob
                == reinterpret_cast<scrStringDebugGlob_t *>(
                    static_cast<std::uintptr_t>(1)),
            "bad debug pointer admission changed corrupt state")
        && Check(LocksReleased(),
            "bad debug pointer admission leaked a critical section")
        && Check(ReportersUnused(),
            "bad debug pointer admission invoked a reporter");
    scrStringDebugGlob = canonicalDebug;
    if (!debugPointerRejected)
        return false;

    // Preserve the aggregate while shifting one debug reference between live
    // IDs. Boundary admission must authenticate each ID, not only the total.
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[existing.stringId]);
    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[secondString.stringId]);
    const StateImage corruptPerIdDebug = CaptureState();
    const bool perIdDebugRejected = Check(
            script_string::TryBeginOwnershipBatch(&rejected)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "batch admission trusted shifted per-ID debug accounting")
        && Check(!rejected.active(),
            "shifted per-ID debug accounting published a token")
        && Check(StateMatches(corruptPerIdDebug),
            "shifted per-ID debug rejection changed corrupt state")
        && Check(LocksReleased(),
            "shifted per-ID debug rejection leaked a critical section")
        && Check(ReportersUnused(),
            "shifted per-ID debug rejection invoked a reporter");
    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[existing.stringId]);
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[secondString.stringId]);
    if (!perIdDebugRejected
        || !Check(
            script_string::TryRemoveOrdinaryReference(secondString.stringId)
                == script_string::ReleaseStatus::Success,
            "boundary-validation peer cleanup failed"))
    {
        return false;
    }

    Sys_AtomicIncrement(&scrStringDebugGlob->refCount[existing.stringId]);
    const StateImage corruptAdmission = CaptureState();
    const bool admissionRejected = Check(
            script_string::TryBeginOwnershipBatch(&rejected)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "batch admission trusted corrupt debug accounting")
        && Check(!rejected.active(),
            "rejected batch admission published a token")
        && Check(StateMatches(corruptAdmission),
            "rejected batch admission changed corrupt state")
        && Check(LocksReleased(),
            "rejected batch admission leaked a critical section");
    Sys_AtomicDecrement(&scrStringDebugGlob->refCount[existing.stringId]);
    if (!admissionRejected)
        return false;

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "boundary-close batch admission failed"))
    {
        return false;
    }

    const uint32_t head = static_cast<uint16_t>(
        scrStringGlob.hashTable[0].status_next);
    const uint32_t second = static_cast<uint16_t>(
        scrStringGlob.hashTable[head].status_next);
    const uint32_t distant = static_cast<uint16_t>(
        scrStringGlob.hashTable[second].status_next);
    if (!Check(head != 0 && second != 0 && distant != 0,
            "boundary-close setup has too few free entries"))
    {
        return false;
    }
    const HashEntry savedDistant = scrStringGlob.hashTable[distant];
    scrStringGlob.hashTable[distant].u.prev = distant;
    const bool closeRejected = Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "batch close trusted untouched free-list corruption")
        && Check(LocksReleased(),
            "unsafe boundary close leaked a critical section");
    scrStringGlob.hashTable[distant] = savedDistant;

    return closeRejected
        && Check(
            script_string::TryRemoveOrdinaryReference(existing.stringId)
                == script_string::ReleaseStatus::Success,
            "boundary-validation cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchAuthenticationAndOverflow() noexcept
{
    if (!BeginTest())
        return false;

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "authentication batch admission failed"))
    {
        return false;
    }
    const uint64_t serial = batch.serial();
    batch.SetAuthenticationFieldsForTesting(serial + 1, 0, 0);
    if (!Check(
            script_string::TryAcquireOrdinaryStringOfSize(
                batch, "x", 2, 15).status
                == script_string::AcquireStatus::UnsafeFailure,
            "corrupt batch serial authenticated an operation")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::InvalidToken,
            "corrupt batch serial authenticated close")
        || !Check(g_scriptStringLockDepth == 1
                && g_memoryTreeLockDepth == 1,
            "invalid batch token released a retained lock"))
    {
        return false;
    }
    batch.SetAuthenticationFieldsForTesting(serial, 0, 0);
    if (!Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "restored corrupt-token batch did not close fail-closed"))
    {
        return false;
    }

    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry-mirror batch admission failed"))
    {
        return false;
    }
    const uint64_t savedMirror = sl_activeOwnershipBatchSerialMirror;
    sl_activeOwnershipBatchSerialMirror ^= UINT64_C(1);
    const bool mirrorRejected = Check(
            script_string::TryRemoveOrdinaryReference(batch, 0)
                == script_string::ReleaseStatus::UnsafeFailure,
            "inconsistent batch serial mirror authenticated an operation")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::InvalidToken,
            "inconsistent batch serial mirror authenticated close")
        && Check(g_scriptStringLockDepth == 1
                && g_memoryTreeLockDepth == 1,
            "inconsistent batch registry released a retained lock");
    sl_activeOwnershipBatchSerialMirror = savedMirror;
    if (!mirrorRejected
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "restored corrupt-registry batch did not close fail-closed"))
    {
        return false;
    }

    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "address-mirror batch admission failed"))
    {
        return false;
    }
    const uintptr_t savedAddressMirror =
        sl_activeOwnershipBatchAddressMirror;
    sl_activeOwnershipBatchAddressMirror ^= static_cast<uintptr_t>(1);
    const bool addressMirrorRejected = Check(
            script_string::TryAcquireOrdinaryStringOfSize(
                batch, "m", 2, 15).status
                == script_string::AcquireStatus::UnsafeFailure,
            "inconsistent batch address mirror authenticated an operation")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::InvalidToken,
            "inconsistent batch address mirror authenticated close")
        && Check(g_scriptStringLockDepth == 1
                && g_memoryTreeLockDepth == 1,
            "inconsistent batch address registry released a retained lock");
    sl_activeOwnershipBatchAddressMirror = savedAddressMirror;
    if (!addressMirrorRejected
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "restored corrupt-address batch did not close fail-closed"))
    {
        return false;
    }

    const uint64_t savedNextSerial = sl_nextOwnershipBatchSerial;
    script_string::SetNextOwnershipBatchSerialForTesting(UINT64_MAX);
    const bool serialOverflowRejected = Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "ownership batch serial wrapped")
        && Check(!batch.active() && LocksReleased(),
            "serial-overflow rejection retained state");
    script_string::SetNextOwnershipBatchSerialForTesting(savedNextSerial);
    if (!serialOverflowRejected
        || !Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "operation-overflow batch admission failed"))
    {
        return false;
    }
    batch.SetOperationCountForTesting(UINT32_MAX);
    const bool operationOverflowRejected = Check(
            script_string::TryAcquireOrdinaryStringOfSize(
                batch, "y", 2, 15).status
                == script_string::AcquireStatus::UnsafeFailure,
            "ownership operation counter wrapped")
        && Check(batch.poisoned(),
            "operation-counter overflow did not poison the batch")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "overflow-poisoned batch closed successfully")
        && Check(LocksReleased(),
            "overflow-poisoned batch leaked a critical section");
    if (!operationOverflowRejected
        || !Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "allocator-overflow batch admission failed"))
    {
        return false;
    }
    batch.SetMemoryTreeMutationCountForTesting(UINT32_MAX);
    const StateImage beforeAllocatorOverflow = CaptureState();
    const bool allocatorOverflowRejected = Check(
            script_string::TryAcquireOrdinaryStringOfSize(
                batch, "z", 2, 15).status
                == script_string::AcquireStatus::UnsafeFailure,
            "allocator lease mutation counter entered string mutation")
        && Check(StateMatches(beforeAllocatorOverflow),
            "allocator mutation-count rejection changed ownership state")
        && Check(batch.poisoned(),
            "allocator mutation-count overflow did not poison the batch")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "allocator-overflow-poisoned batch closed successfully")
        && Check(LocksReleased(),
            "allocator-overflow-poisoned batch leaked a critical section");
    return allocatorOverflowRejected && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchRejectsRawAllocatorMutation() noexcept
{
    if (!BeginTest())
        return false;

    script_string::OwnershipBatch batch;
    constexpr char value[] = "batch-raw-allocator";
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "raw-allocator batch admission failed"))
    {
        return false;
    }
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        batch, value, sizeof(value), 15);
    if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "raw-allocator acquisition failed"))
    {
        return false;
    }

    const StateImage before = CaptureState();
    uint16_t allocated = UINT16_C(0xA55A);
    const auto rawAlloc = MT_TryAllocIndex(12, 15, &allocated);
    const auto rawFree = MT_TryFreeIndex(
        acquired.stringId, static_cast<int>(sizeof(value) + 4));
    MT_RemoveHeadMemoryNode(-1);
    const bool rawRemoved = MT_RemoveMemoryNode(-1, UINT32_MAX);
    MT_AddMemoryNode(-1, -1);
    const bool rawReallocated = MT_Realloc(-1, -1);
    const int rawSize = MT_GetSize(-1);
    const int rawScore = MT_GetScore(0);
    const int rawSubTreeSize = MT_GetSubTreeSize(-1);
    const char *const rawNodeInfo = MT_NodeInfoString(UINT32_MAX);
    MT_DumpTree();
    MT_Error(nullptr, -1);
    const bool rejected = Check(
            rawAlloc == MT_AllocIndexStatus::UnsafeFailure,
            "raw allocator mutation entered an ownership batch")
        && Check(allocated == UINT16_C(0xA55A),
            "rejected raw allocation published an index")
        && Check(rawFree == MT_FreeIndexStatus::UnsafeFailure,
            "raw allocator free entered an ownership batch")
        && Check(!rawRemoved,
            "raw targeted removal entered an ownership batch")
        && Check(!rawReallocated && rawSize == 0 && rawScore == 0
                && rawSubTreeSize == 0,
            "raw allocator reader entered an ownership batch")
        && Check(rawNodeInfo != nullptr
                && std::strcmp(rawNodeInfo, "<UNAVAILABLE>") == 0,
            "raw allocator diagnostic entered an ownership batch")
        && Check(StateMatches(before),
            "raw allocator rejection changed ownership state")
        && Check(batch.poisoned(),
            "raw allocator reentry did not poison the batch")
        && Check(ReportersUnused(),
            "raw allocator rejection invoked a reporter")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "raw-allocator-poisoned batch closed successfully")
        && Check(LocksReleased(),
            "raw-allocator-poisoned batch leaked a critical section");
    return rejected
        && Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "raw-allocator batch cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchForeignSerialization() noexcept
{
    if (!BeginTest())
        return false;

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "foreign-serialization batch admission failed"))
    {
        return false;
    }

    constexpr char value[] = "batch-foreign-serialization";
    std::atomic<bool> completed{false};
    script_string::AcquireResult foreignResult{};
    g_scriptStringLockAttempts.store(0, std::memory_order_relaxed);
    g_countScriptStringLockAttempts.store(true, std::memory_order_release);
    std::thread foreign([&]() {
        foreignResult = script_string::TryAcquireOrdinaryStringOfSize(
            value, sizeof(value), 15);
        completed.store(true, std::memory_order_release);
    });
    while (g_scriptStringLockAttempts.load(std::memory_order_acquire) != 1)
        std::this_thread::yield();
    g_countScriptStringLockAttempts.store(false, std::memory_order_release);
    const bool blocked = !completed.load(std::memory_order_acquire);
    const auto closeStatus = script_string::FinishOwnershipBatch(&batch);
    foreign.join();

    const bool valid = Check(blocked,
            "foreign ownership caller bypassed the retained script lock")
        && Check(closeStatus == script_string::OwnershipBatchStatus::Success,
            "foreign-serialization batch close failed")
        && Check(completed.load(std::memory_order_acquire),
            "foreign ownership caller did not resume after close")
        && Check(
            foreignResult.status == script_string::AcquireStatus::Acquired,
            "resumed foreign ownership caller failed")
        && Check(LocksReleased(),
            "foreign ownership serialization leaked a critical section")
        && Check(ReportersUnused(),
            "foreign ownership serialization invoked a reporter");
    return valid
        && Check(
            script_string::TryRemoveOrdinaryReference(
                foreignResult.stringId)
                == script_string::ReleaseStatus::Success,
            "foreign ownership serialization cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchForeignReaderSerialization() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "batch-foreign-reader";
    const auto existing = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(existing.status == script_string::AcquireStatus::Acquired,
            "foreign-reader fixture acquisition failed"))
    {
        return false;
    }

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "foreign-reader batch admission failed"))
    {
        return false;
    }

    std::atomic<bool> completed{false};
    int foreignLength = -1;
    g_scriptStringLockAttempts.store(0, std::memory_order_relaxed);
    g_countScriptStringLockAttempts.store(true, std::memory_order_release);
    std::thread foreign([&]() {
        foreignLength = SL_GetStringLen(existing.stringId);
        completed.store(true, std::memory_order_release);
    });
    while (g_scriptStringLockAttempts.load(std::memory_order_acquire) != 1)
        std::this_thread::yield();
    g_countScriptStringLockAttempts.store(false, std::memory_order_release);
    const bool blocked = !completed.load(std::memory_order_acquire);
    const auto closeStatus = script_string::FinishOwnershipBatch(&batch);
    foreign.join();

    return Check(blocked,
            "foreign reader bypassed the retained script lock")
        && Check(closeStatus == script_string::OwnershipBatchStatus::Success,
            "foreign-reader batch close failed")
        && Check(foreignLength == static_cast<int>(sizeof(value) - 1),
            "foreign reader did not resume after batch close")
        && Check(
            script_string::TryRemoveOrdinaryReference(existing.stringId)
                == script_string::ReleaseStatus::Success,
            "foreign-reader fixture cleanup failed")
        && Check(LocksReleased(),
            "foreign-reader serialization leaked a critical section")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchCanonicalResetGate() noexcept
{
    if (!BeginTest())
        return false;
    std::array<short, SL_MAX_STRING_INDEX> storage{};
    storage.front() = 17;
    storage.back() = 29;
    std::uint16_t count = 3;

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "canonical-reset batch admission failed"))
    {
        return false;
    }
    auto &canonicalStrings = *reinterpret_cast<
        short (*)[SL_MAX_STRING_INDEX]>(storage.data());
    const bool rejected = Check(
            !SL_TryResetCanonicalStringState(canonicalStrings, &count),
            "canonical reset entered an ownership batch")
        && Check(storage.front() == 17 && storage.back() == 29 && count == 3,
            "rejected canonical reset changed output")
        && Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "canonical-reset batch close failed")
        && Check(ReportersUnused(),
            "canonical reset rejection entered a reporter");
    const bool reset = SL_TryResetCanonicalStringState(
        canonicalStrings, &count);
    return rejected
        && Check(reset && count == 0,
            "canonical reset did not publish the cleared count")
        && Check(std::all_of(
                storage.begin(), storage.end(),
                [](const short value) { return value == 0; }),
            "canonical reset did not clear all entries")
        && Check(LocksReleased(),
            "canonical reset leaked a lock")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchLifetimeBoundaries() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "batch-lifetime-waiters";
    const auto existing = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(existing.status == script_string::AcquireStatus::Acquired,
            "lifetime-waiter fixture acquisition failed"))
    {
        return false;
    }

    bool waitedActive = true;
    bool waitedPoisoned = true;
    std::uint64_t waitedSerial = UINT64_MAX;
    std::uint32_t waitedOperations = UINT32_MAX;
    int waitedLength = -1;
    script_string::AcquireResult waitedAcquire{
        script_string::AcquireStatus::Acquired, UINT32_C(0xBEEF)};
    std::thread activeWaiter;
    std::thread poisonedWaiter;
    std::thread serialWaiter;
    std::thread operationWaiter;
    std::thread acquireWaiter;
    std::thread readerWaiter;
    StateImage beforeClose;
    {
        script_string::OwnershipBatch batch;
        if (!Check(
                script_string::TryBeginOwnershipBatch(&batch)
                    == script_string::OwnershipBatchStatus::Success,
                "lifetime-waiter batch admission failed"))
        {
            return false;
        }
        beforeClose = CaptureState();

        g_scriptStringLockAttempts.store(0, std::memory_order_relaxed);
        g_countScriptStringLockAttempts.store(true, std::memory_order_release);
        activeWaiter = std::thread([&]() { waitedActive = batch.active(); });
        poisonedWaiter = std::thread(
            [&]() { waitedPoisoned = batch.poisoned(); });
        serialWaiter = std::thread([&]() { waitedSerial = batch.serial(); });
        operationWaiter = std::thread(
            [&]() { waitedOperations = batch.operationCount(); });
        acquireWaiter = std::thread([&]() {
            waitedAcquire = script_string::TryAcquireOrdinaryStringOfSize(
                batch, "x", 2, 15);
        });
        readerWaiter = std::thread([&]() {
            waitedLength = SL_GetStringLen(existing.stringId);
        });
        while (g_scriptStringLockAttempts.load(std::memory_order_acquire) != 6)
            std::this_thread::yield();
        g_countScriptStringLockAttempts.store(false, std::memory_order_release);
        // The batch must outlive every call that received it. Finish on the
        // admitting thread, then join every waiter before destruction.
        const auto finishStatus =
            script_string::FinishOwnershipBatch(&batch);

        activeWaiter.join();
        poisonedWaiter.join();
        serialWaiter.join();
        operationWaiter.join();
        acquireWaiter.join();
        readerWaiter.join();

        if (!Check(
                finishStatus == script_string::OwnershipBatchStatus::Success,
                "lifetime-waiter batch close failed")
            || !Check(
                !waitedActive && !waitedPoisoned && waitedSerial == 0
                    && waitedOperations == 0,
                "blocked snapshots read a finished ownership batch")
            || !Check(
                waitedAcquire.status
                        == script_string::AcquireStatus::UnsafeFailure
                    && waitedAcquire.stringId == 0,
                "blocked ownership operation authenticated after batch close")
            || !Check(waitedLength == static_cast<int>(sizeof(value) - 1),
                "blocked legacy reader failed after batch close")
            || !Check(StateMatches(beforeClose),
                "finished waiter changed string/allocator state")
            || !Check(LocksReleased(),
                "lifetime-waiter close leaked an owner lock")
            || !Check(ReportersUnused(),
                "lifetime waiter entered a reporter"))
        {
            return false;
        }
    }

    StateImage beforeAbandonment;
    {
        script_string::OwnershipBatch abandoned;
        if (!Check(
                script_string::TryBeginOwnershipBatch(&abandoned)
                    == script_string::OwnershipBatchStatus::Success,
                "exact abandonment batch admission failed"))
        {
            return false;
        }
        beforeAbandonment = CaptureState();
    }
    const auto frozenAcquire = script_string::TryAcquireOrdinaryStringOfSize(
        "x", 2, 15);
    const bool rejected = Check(
            frozenAcquire.status == script_string::AcquireStatus::UnsafeFailure
                && frozenAcquire.stringId == 0,
            "exact abandonment admitted a frozen ownership operation")
        && Check(SL_GetStringLen(existing.stringId) == 0,
            "exact abandonment admitted a frozen legacy reader")
        && Check(StateMatches(beforeAbandonment),
            "exact abandonment changed string/allocator state")
        && Check(LocksReleased(),
            "exact ownership abandonment leaked an owner lock")
        && Check(ReportersUnused(),
            "exact abandonment entered a reporter");

    script_string::ResetAbandonedOwnershipBatchForTesting(false, false);
    const bool recovered = Check(LocksReleased(),
            "exact abandonment recovery leaked a lock")
        && Check(MT_TryValidateState(),
            "exact abandonment recovery left allocator frozen")
        && Check(
            script_string::TryRemoveOrdinaryReference(existing.stringId)
                == script_string::ReleaseStatus::Success,
            "lifetime-waiter fixture cleanup failed");
    return rejected && recovered && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchOuterAuthorityTears() noexcept
{
    if (!BeginTest())
        return false;

    enum class Tear : std::uint8_t
    {
        ActiveAddress,
        ActiveAddressMirror,
        ActiveSerial,
        ActiveSerialMirror,
        ActiveNestedAddress,
        ActiveNestedAddressMirror,
        Lifecycle,
        LifecycleMirror,
        RetainedAddress,
        RetainedAddressMirror,
        RetainedSerial,
        RetainedSerialMirror,
        RetainedNestedAddress,
        RetainedNestedAddressMirror,
        ArbitraryAddresses,
    };
    constexpr std::array tears{
        Tear::ActiveAddress,
        Tear::ActiveAddressMirror,
        Tear::ActiveSerial,
        Tear::ActiveSerialMirror,
        Tear::ActiveNestedAddress,
        Tear::ActiveNestedAddressMirror,
        Tear::Lifecycle,
        Tear::LifecycleMirror,
        Tear::RetainedAddress,
        Tear::RetainedAddressMirror,
        Tear::RetainedSerial,
        Tear::RetainedSerialMirror,
        Tear::RetainedNestedAddress,
        Tear::RetainedNestedAddressMirror,
        Tear::ArbitraryAddresses,
    };

    for (const Tear tear : tears)
    {
        std::uintptr_t address = 0;
        std::uint64_t serial = 0;
        std::uintptr_t nestedAddress = 0;
        {
            script_string::OwnershipBatch batch;
            if (!Check(
                    script_string::TryBeginOwnershipBatch(&batch)
                        == script_string::OwnershipBatchStatus::Success,
                    "outer-tear batch admission failed"))
            {
                return false;
            }
            address = sl_activeOwnershipBatchAddress;
            serial = sl_activeOwnershipBatchSerial;
            nestedAddress = sl_activeOwnershipBatchNestedLeaseAddress;
            switch (tear)
            {
            case Tear::ActiveAddress:
                sl_activeOwnershipBatchAddress ^= std::uintptr_t{1};
                break;
            case Tear::ActiveAddressMirror:
                sl_activeOwnershipBatchAddressMirror ^= std::uintptr_t{1};
                break;
            case Tear::ActiveSerial:
                sl_activeOwnershipBatchSerial ^= UINT64_C(1);
                break;
            case Tear::ActiveSerialMirror:
                sl_activeOwnershipBatchSerialMirror ^= UINT64_C(1);
                break;
            case Tear::ActiveNestedAddress:
                sl_activeOwnershipBatchNestedLeaseAddress ^=
                    std::uintptr_t{1};
                break;
            case Tear::ActiveNestedAddressMirror:
                sl_activeOwnershipBatchNestedLeaseAddressMirror ^=
                    std::uintptr_t{1};
                break;
            case Tear::Lifecycle:
                sl_ownershipBatchLifecycle =
                    SL_OwnershipBatchLifecycle::Poisoned;
                break;
            case Tear::LifecycleMirror:
                sl_ownershipBatchLifecycleMirror =
                    SL_OwnershipBatchLifecycle::Poisoned;
                break;
            case Tear::RetainedAddress:
                sl_retainedOwnershipBatchAddress ^= std::uintptr_t{1};
                break;
            case Tear::RetainedAddressMirror:
                sl_retainedOwnershipBatchAddressMirror ^= std::uintptr_t{1};
                break;
            case Tear::RetainedSerial:
                sl_retainedOwnershipBatchSerial ^= UINT64_C(1);
                break;
            case Tear::RetainedSerialMirror:
                sl_retainedOwnershipBatchSerialMirror ^= UINT64_C(1);
                break;
            case Tear::RetainedNestedAddress:
                sl_retainedOwnershipBatchNestedLeaseAddress ^=
                    std::uintptr_t{1};
                break;
            case Tear::RetainedNestedAddressMirror:
                sl_retainedOwnershipBatchNestedLeaseAddressMirror ^=
                    std::uintptr_t{1};
                break;
            case Tear::ArbitraryAddresses:
                sl_activeOwnershipBatchAddress =
                    (std::numeric_limits<std::uintptr_t>::max)();
                sl_activeOwnershipBatchAddressMirror =
                    (std::numeric_limits<std::uintptr_t>::max)();
                sl_activeOwnershipBatchNestedLeaseAddress =
                    (std::numeric_limits<std::uintptr_t>::max)() - 1;
                sl_activeOwnershipBatchNestedLeaseAddressMirror =
                    (std::numeric_limits<std::uintptr_t>::max)() - 1;
                break;
            }
        }

        script_string::OwnershipBatch probe;
        const bool frozen = Check(g_scriptStringLockDepth == 1,
                "torn outer authority released the retained script lock")
            && Check(g_memoryTreeLockDepth == 0,
                "independently authenticated nested destructor leaked its lock")
            && Check(
                script_string::TryBeginOwnershipBatch(&probe)
                    == script_string::OwnershipBatchStatus::UnsafeFailure,
                "torn outer authority reopened the frozen boundary")
            && Check(g_scriptStringLockDepth == 1,
                "frozen outer probe changed retained lock depth")
            && Check(ReportersUnused(),
                "torn outer abandonment entered a reporter");

        script_string::SetRetainedOwnershipBatchAuthenticationForTesting(
            address,
            serial,
            nestedAddress,
            address,
            serial,
            nestedAddress);
        script_string::ResetAbandonedOwnershipBatchForTesting(true, false);
        if (!frozen
            || !Check(LocksReleased(),
                "authenticated outer-tear recovery leaked a lock")
            || !Check(MT_TryValidateState(),
                "outer-tear recovery left allocator frozen"))
        {
            return false;
        }
    }
    return EndTest();
}

[[nodiscard]] bool TestOwnershipBatchNestedAuthorityTears() noexcept
{
    if (!BeginTest())
        return false;

    enum class Tear : std::uint8_t
    {
        ActiveAddress,
        ActiveAddressMirror,
        ActiveSerial,
        ActiveSerialMirror,
        Lifecycle,
        LifecycleMirror,
        RetainedAddress,
        RetainedAddressMirror,
        RetainedSerial,
        RetainedSerialMirror,
        LocalSerial,
        LocalReserved,
        ArbitraryAddresses,
    };
    constexpr std::array tears{
        Tear::ActiveAddress,
        Tear::ActiveAddressMirror,
        Tear::ActiveSerial,
        Tear::ActiveSerialMirror,
        Tear::Lifecycle,
        Tear::LifecycleMirror,
        Tear::RetainedAddress,
        Tear::RetainedAddressMirror,
        Tear::RetainedSerial,
        Tear::RetainedSerialMirror,
        Tear::LocalSerial,
        Tear::LocalReserved,
        Tear::ArbitraryAddresses,
    };

    for (const Tear tear : tears)
    {
        std::uintptr_t nestedAddress = 0;
        std::uint64_t nestedSerial = 0;
        {
            script_string::OwnershipBatch batch;
            if (!Check(
                    script_string::TryBeginOwnershipBatch(&batch)
                        == script_string::OwnershipBatchStatus::Success,
                    "nested-tear batch admission failed"))
            {
                return false;
            }
            MT_ValidationLease &lease =
                batch.MemoryTreeLeaseForTesting();
            nestedAddress = reinterpret_cast<std::uintptr_t>(&lease);
            nestedSerial = lease.serial();
            switch (tear)
            {
            case Tear::ActiveAddress:
                MT_SetValidationLeaseRegistryMirrorsForTesting(
                    nestedAddress ^ std::uintptr_t{1},
                    nestedSerial,
                    nestedAddress,
                    nestedSerial);
                break;
            case Tear::ActiveAddressMirror:
                MT_SetValidationLeaseRegistryMirrorsForTesting(
                    nestedAddress,
                    nestedSerial,
                    nestedAddress ^ std::uintptr_t{1},
                    nestedSerial);
                break;
            case Tear::ActiveSerial:
                MT_SetValidationLeaseRegistryMirrorsForTesting(
                    nestedAddress,
                    nestedSerial ^ UINT64_C(1),
                    nestedAddress,
                    nestedSerial);
                break;
            case Tear::ActiveSerialMirror:
                MT_SetValidationLeaseRegistryMirrorsForTesting(
                    nestedAddress,
                    nestedSerial,
                    nestedAddress,
                    nestedSerial ^ UINT64_C(1));
                break;
            case Tear::Lifecycle:
                MT_SetValidationLeaseLifecycleForTesting(2, 1);
                break;
            case Tear::LifecycleMirror:
                MT_SetValidationLeaseLifecycleForTesting(1, 2);
                break;
            case Tear::RetainedAddress:
                MT_SetRetainedValidationLeaseAuthenticationForTesting(
                    nestedAddress ^ std::uintptr_t{1},
                    nestedSerial,
                    nestedAddress,
                    nestedSerial);
                break;
            case Tear::RetainedAddressMirror:
                MT_SetRetainedValidationLeaseAuthenticationForTesting(
                    nestedAddress,
                    nestedSerial,
                    nestedAddress ^ std::uintptr_t{1},
                    nestedSerial);
                break;
            case Tear::RetainedSerial:
                MT_SetRetainedValidationLeaseAuthenticationForTesting(
                    nestedAddress,
                    nestedSerial ^ UINT64_C(1),
                    nestedAddress,
                    nestedSerial);
                break;
            case Tear::RetainedSerialMirror:
                MT_SetRetainedValidationLeaseAuthenticationForTesting(
                    nestedAddress,
                    nestedSerial,
                    nestedAddress,
                    nestedSerial ^ UINT64_C(1));
                break;
            case Tear::LocalSerial:
                lease.SetAuthenticationFieldsForTesting(
                    nestedSerial ^ UINT64_C(1), 0, 0);
                break;
            case Tear::LocalReserved:
                lease.SetAuthenticationFieldsForTesting(
                    nestedSerial, 1, 0);
                break;
            case Tear::ArbitraryAddresses:
                MT_SetValidationLeaseRegistryMirrorsForTesting(
                    (std::numeric_limits<std::uintptr_t>::max)(),
                    nestedSerial,
                    (std::numeric_limits<std::uintptr_t>::max)(),
                    nestedSerial);
                break;
            }
        }

        script_string::OwnershipBatch probe;
        const bool frozen = Check(g_scriptStringLockDepth == 1,
                "torn nested authority released retained script lock")
            && Check(g_memoryTreeLockDepth == 1,
                "torn nested authority released retained allocator lock")
            && Check(
                script_string::TryBeginOwnershipBatch(&probe)
                    == script_string::OwnershipBatchStatus::UnsafeFailure,
                "torn nested authority reopened outer boundary")
            && Check(ReportersUnused(),
                "torn nested abandonment entered a reporter");

        MT_SetRetainedValidationLeaseAuthenticationForTesting(
            nestedAddress,
            nestedSerial,
            nestedAddress,
            nestedSerial);
        script_string::ResetAbandonedOwnershipBatchForTesting(true, true);
        if (!frozen
            || !Check(LocksReleased(),
                "authenticated nested-tear recovery leaked a lock")
            || !Check(MT_TryValidateState(),
                "nested-tear recovery left allocator frozen"))
        {
            return false;
        }
    }

    {
        script_string::OwnershipBatch batch;
        if (!Check(
                script_string::TryBeginOwnershipBatch(&batch)
                    == script_string::OwnershipBatchStatus::Success,
                "nested-mutation exhaustion admission failed"))
        {
            return false;
        }
        batch.SetMemoryTreeMutationCountForTesting(UINT32_MAX);
    }
    const bool exhaustedReleased = Check(LocksReleased(),
            "authenticated exhausted nested token leaked owner locks")
        && Check(!MT_TryValidateState(),
            "abandoned exhausted nested token did not freeze allocator");
    script_string::ResetAbandonedOwnershipBatchForTesting(false, false);
    return exhaustedReleased
        && Check(MT_TryValidateState(),
            "exhausted nested abandonment recovery failed")
        && EndTest();
}

[[nodiscard]] bool TestOwnershipBatchUnrelatedDestruction() noexcept
{
    if (!BeginTest())
        return false;

    {
        script_string::OwnershipBatch malformed;
        malformed.ActivateForTesting(UINT64_C(0xA55A));
    }
    if (!Check(LocksReleased(),
            "idle malformed unrelated destructor leaked a lock")
        || !Check(MT_TryValidateState(),
            "idle malformed unrelated destructor froze allocator"))
    {
        return false;
    }

    script_string::OwnershipBatch owner;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&owner)
                == script_string::OwnershipBatchStatus::Success,
            "unrelated-destruction owner admission failed"))
    {
        return false;
    }
    {
        script_string::OwnershipBatch canonical;
    }
    {
        script_string::OwnershipBatch malformed;
        malformed.ActivateForTesting(UINT64_C(0x5AA5));
    }
    return Check(owner.active(),
            "unrelated destructor revoked the live owner")
        && Check(
            script_string::FinishOwnershipBatch(&owner)
                == script_string::OwnershipBatchStatus::Success,
            "unrelated destructor poisoned live owner close")
        && Check(LocksReleased(),
            "unrelated-destruction test leaked a lock")
        && Check(ReportersUnused(),
            "unrelated destructor entered a reporter")
        && EndTest();
}

[[nodiscard]] bool ReadersRejectNonString(
    const std::uint32_t stringId,
    const char *const candidate) noexcept
{
    const bool lowercaseRejected = SL_IsLowercaseString(stringId) == 0;
    const bool lowercaseConversionRejected =
        SL_ConvertToLowercase(stringId, 0, 0) == 0;
    const bool conversionRejected = SL_ConvertToString(stringId) == nullptr;
    const bool idLookupRejected = GetRefString(stringId) == nullptr;
    const bool pointerLookupRejected = GetRefString(candidate) == nullptr;
    const bool lengthRejected = SL_GetStringLen(stringId) == 0;
    const bool refLengthRejected = SL_GetRefStringLen(
        reinterpret_cast<RefString *>(
            const_cast<char *>(candidate) - offsetof(RefString, str))) == 0;
    const bool debugRejected = std::strcmp(
        SL_DebugConvertToString(stringId), "<UNAVAILABLE>") == 0;
    const bool reverseRejected = SL_ConvertFromString(candidate) == 0;
    const bool userRejected = SL_GetUser(stringId) == 0;
    const bool safeRejected = std::strcmp(
        SL_ConvertToStringSafe(stringId), "(NULL)") == 0;
    const bool reporterLockSafe = !g_reporterSawOwnedLock;
    // Invalid legacy compatibility readers may assert after releasing their
    // lock in assertion builds. This fixture verifies the lock discipline and
    // then clears expected diagnostics before the report-free test epilogue.
    g_assertCount = 0;
    return Check(lowercaseRejected, "lowercase reader exposed non-string")
        && Check(lowercaseConversionRejected,
            "lowercase conversion exposed non-string")
        && Check(conversionRejected, "string conversion exposed non-string")
        && Check(idLookupRejected, "ID lookup exposed non-string")
        && Check(pointerLookupRejected, "pointer lookup exposed non-string")
        && Check(lengthRejected, "string length exposed non-string")
        && Check(refLengthRejected, "RefString length exposed non-string")
        && Check(debugRejected, "debug conversion exposed non-string")
        && Check(reverseRejected, "reverse conversion exposed non-string")
        && Check(userRejected, "user reader exposed non-string")
        && Check(safeRejected, "safe conversion exposed non-string")
        && Check(reporterLockSafe,
            "invalid legacy reader asserted under an owned lock");
}

[[nodiscard]] bool TestLegacyReadersAuthenticateExactStrings() noexcept
{
    if (!BeginTest())
        return false;

    std::uint16_t vectorId = 0;
    if (!Check(
            MT_TryAllocIndex(
                static_cast<int>(sizeof(RefVector)), 2, &vectorId)
                == MT_AllocIndexStatus::Success,
            "reader-auth vector allocation failed"))
    {
        return false;
    }
    auto *const vector = reinterpret_cast<RefVector *>(
        scrMemTreePub.mt_buffer + vectorId * MT_NODE_SIZE);
    std::memset(vector, 0, sizeof(*vector));
    const char *const vectorCandidate = reinterpret_cast<const char *>(vector)
        + offsetof(RefString, str);
    if (!ReadersRejectNonString(vectorId, vectorCandidate)
        || !Check(
            MT_TryFreeIndex(
                vectorId, static_cast<int>(sizeof(RefVector)))
                == MT_FreeIndexStatus::Success,
            "reader-auth vector cleanup failed")
        || !ReadersRejectNonString(vectorId, vectorCandidate))
    {
        return false;
    }

    constexpr char value[] = "reader-exact-allocation";
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "reader-auth string acquisition failed"))
    {
        return false;
    }
    RefString *const refString = GetRefString(acquired.stringId);
    if (!Check(refString != nullptr,
            "reader-auth could not resolve valid string"))
    {
        return false;
    }
    refString->str[sizeof(value) - 1] = 'X';
    const bool corruptRejected = ReadersRejectNonString(
        acquired.stringId, refString->str);
    refString->str[sizeof(value) - 1] = '\0';

    const char *const arbitrary = reinterpret_cast<const char *>(
        (std::numeric_limits<std::uintptr_t>::max)());
    const bool arbitraryRejected = SL_ConvertFromString(arbitrary) == 0;
    const bool reporterLockSafe = !g_reporterSawOwnedLock;
    g_assertCount = 0;
    return corruptRejected
        && Check(arbitraryRejected,
            "arbitrary integer string address was dereferenced")
        && Check(reporterLockSafe,
            "arbitrary address rejection asserted under a lock")
        && Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "reader-auth string cleanup failed")
        && Check(LocksReleased(),
            "reader-auth test leaked a lock")
        && EndTest();
}

[[nodiscard]] bool TestLegacyMutatorsAuthenticateExactStrings() noexcept
{
    if (!BeginTest())
        return false;

    std::uint16_t vectorId = 0;
    if (!Check(
            MT_TryAllocIndex(
                static_cast<int>(sizeof(RefVector)), 2, &vectorId)
                == MT_AllocIndexStatus::Success,
            "mutator-auth vector allocation failed"))
    {
        return false;
    }
    auto *const vector = reinterpret_cast<RefVector *>(
        scrMemTreePub.mt_buffer + vectorId * MT_NODE_SIZE);
    std::memset(vector, 0, sizeof(*vector));
    RefString *const nonString = reinterpret_cast<RefString *>(vector);
    RefString *const arbitrary = reinterpret_cast<RefString *>(
        (std::numeric_limits<std::uintptr_t>::max)());
    const StateImage beforeRejection = CaptureState();

    const bool pointerInputsRejected = Check(
            !SL_AddUserInternal(nullptr, 8)
                && !SL_AddUserInternal(arbitrary, 8)
                && !SL_AddUserInternal(nonString, 8),
            "opaque pointer user mutation accepted non-string storage")
        && Check(StateMatches(beforeRejection),
            "opaque pointer user rejection changed ownership state")
        && Check(LocksReleased(),
            "opaque pointer user rejection leaked a lock")
        && Check(ReportersUnused(),
            "opaque pointer user rejection invoked a reporter");

    SL_AddRefToString(vectorId);
    const bool idRefRejected = Check(g_comErrorCount == 1,
            "ID ref mutation accepted a non-string allocation")
        && Check(!g_reporterSawOwnedLock,
            "ID ref rejection reported under an owned lock")
        && Check(StateMatches(beforeRejection),
            "ID ref rejection changed ownership state")
        && Check(LocksReleased(), "ID ref rejection leaked a lock");
    g_comErrorCount = 0;

    SL_AddUser(vectorId, 8);
    const bool idUserRejected = Check(g_comErrorCount == 1,
            "ID user mutation accepted a non-string allocation")
        && Check(!g_reporterSawOwnedLock,
            "ID user rejection reported under an owned lock")
        && Check(StateMatches(beforeRejection),
            "ID user rejection changed ownership state")
        && Check(LocksReleased(), "ID user rejection leaked a lock");
    g_comErrorCount = 0;

    if (!pointerInputsRejected || !idRefRejected || !idUserRejected
        || !Check(
            MT_TryFreeIndex(
                vectorId, static_cast<int>(sizeof(RefVector)))
                == MT_FreeIndexStatus::Success,
            "mutator-auth vector cleanup failed"))
    {
        return false;
    }

    constexpr char value[] = "mutator-exact-allocation";
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "mutator-auth string acquisition failed"))
    {
        return false;
    }
    RefString *const refString = GetRefString(acquired.stringId);
    const StateImage beforeInvalidUser = CaptureState();
    const bool validPath = Check(refString != nullptr,
            "mutator-auth could not resolve valid string")
        && Check(!SL_AddUserInternal(refString, UINT32_MAX),
            "opaque pointer mutation accepted invalid user mask")
        && Check(StateMatches(beforeInvalidUser),
            "invalid user mask changed ownership state")
        && Check(SL_AddUserInternal(
                refString, script_string::kDatabaseUserMask),
            "exact opaque pointer mutation rejected a valid string")
        && Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "mutator-auth ordinary cleanup failed")
        && Check(
            script_string::TryRemoveDatabaseUserReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "mutator-auth database cleanup failed")
        && Check(LocksReleased(), "mutator-auth test leaked a lock")
        && Check(ReportersUnused(),
            "mutator-auth valid path invoked a reporter");
    return validPath && EndTest();
}

[[nodiscard]] bool RegistrySweepScratchCleared() noexcept
{
    for (std::uint32_t index = 0; index < STRINGLIST_SIZE; ++index)
    {
        if (sl_registryOwnershipSweepIds[index] != 0)
            return false;
    }
    return true;
}

[[nodiscard]] bool RegistryBulkScratchCleared() noexcept
{
    if (!RegistrySweepScratchCleared())
        return false;
    for (const std::uint8_t value : sl_registryOwnershipBulkVisited)
    {
        if (value != 0)
            return false;
    }
    return true;
}

constexpr std::size_t kRegistryCollisionNameBytes = 256;

void MakeRegistryCollisionName(
    std::array<char, kRegistryCollisionNameBytes> &name,
    const std::size_t index) noexcept
{
    name.fill('x');
    name[0] = static_cast<char>('A' + (index / 26) % 26);
    name[1] = static_cast<char>('a' + index % 26);
    name.back() = '\0';
}

[[nodiscard]] bool TestRegistryTypedOperationsRejectCorruptDebugPointer()
    noexcept
{
    if (!BeginTest())
        return false;

    constexpr char value[] = "registry-corrupt-debug-pointer";
    const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
        value, sizeof(value), 15);
    if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "registry debug-pointer setup failed"))
    {
        return false;
    }

    const std::array<std::uint32_t, 1> ids{acquired.stringId};
    for (std::uint32_t scenario = 0; scenario < 2; ++scenario)
    {
        script_string::OwnershipBatch batch;
        if (!Check(
                script_string::TryBeginOwnershipBatch(&batch)
                    == script_string::OwnershipBatchStatus::Success,
                "registry debug-pointer batch admission failed"))
        {
            return false;
        }
        const auto admission =
            script_string::RegistryOwnershipAdmission::ForTesting(batch);
        const StateImage beforeCorruption = CaptureState();
        SL_ResetHashChainValidationNoReport();
        script_string::ResetOwnershipValidationCountersForTesting();
        scrStringDebugGlob_t *const canonicalDebug = scrStringDebugGlob;
        scrStringDebugGlob = scenario == 0
            ? nullptr
            : reinterpret_cast<scrStringDebugGlob_t *>(
                static_cast<std::uintptr_t>(1));
        const auto rejected = script_string::TryAddDatabaseUser4References(
            admission,
            ids.data(),
            static_cast<std::uint32_t>(ids.size()));
        scrStringDebugGlob = canonicalDebug;

        const bool rejectedCleanly = Check(
                rejected.status
                    == script_string::DatabaseUserAddBulkStatus::UnsafeFailure,
                "registry typed operation trusted a corrupt debug pointer")
            && Check(rejected.addedCount == 0
                    && rejected.unchangedCount == 0,
                "registry debug-pointer rejection published counts")
            && Check(batch.poisoned(),
                "registry debug-pointer rejection did not poison the batch")
            && Check(batch.operationCount() == 0,
                "registry debug-pointer rejection consumed an operation")
            && Check(StateMatches(beforeCorruption),
                "registry debug-pointer rejection changed state")
            && Check(RegistryBulkScratchCleared(),
                "registry debug-pointer rejection retained scratch state")
            && Check(sl_registryOwnershipTopologyPreflightCount == 0
                    && sl_registryOwnershipTopologyPreflightWorkCount == 0,
                "registry debug-pointer rejection entered bulk preflight")
            && Check(sl_hashChainVisitedCount == 0
                    && sl_stringIdVisitedCount == 0,
                "registry debug-pointer rejection retained validation scratch");
        const bool closeRejected = Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "registry debug-pointer-poisoned batch closed successfully");
        if (!rejectedCleanly || !closeRejected)
            return false;
    }

    return Check(
            script_string::TryRemoveOrdinaryReference(acquired.stringId)
                == script_string::ReleaseStatus::Success,
            "registry debug-pointer cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestRegistryRetainedBulkRejectsCorruptAggregate() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstValue[] = "registry-aggregate-first";
    constexpr char secondValue[] = "registry-aggregate-second";
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        firstValue, sizeof(firstValue), 15);
    const auto second = script_string::TryAcquireOrdinaryStringOfSize(
        secondValue, sizeof(secondValue), 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired
                && second.status == script_string::AcquireStatus::Acquired,
            "registry aggregate setup failed"))
    {
        return false;
    }
    const std::array<std::uint32_t, 2> ids{
        first.stringId, second.stringId};
    const std::array<const char *, 2> names{
        GetRefString(first.stringId)->str,
        GetRefString(second.stringId)->str};

    script_string::OwnershipBatch setupBatch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&setupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry aggregate setup batch admission failed"))
    {
        return false;
    }
    const auto setupAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(setupBatch);
    const auto setupAdd = script_string::TryAddDatabaseUser4References(
        setupAdmission,
        ids.data(),
        static_cast<std::uint32_t>(ids.size()));
    if (!Check(
            setupAdd.status
                == script_string::DatabaseUserAddBulkStatus::Success
                && setupAdd.addedCount == ids.size(),
            "registry aggregate setup add failed")
        || !Check(
            script_string::TryTransferDatabaseUsers4To8(setupAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry aggregate setup transfer failed")
        || !Check(
            script_string::FinishOwnershipBatch(&setupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry aggregate setup batch close failed"))
    {
        return false;
    }

    script_string::OwnershipBatch corruptBatch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&corruptBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry aggregate corruption batch admission failed"))
    {
        return false;
    }
    const auto corruptAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(corruptBatch);
    const std::uint32_t canonicalTotal =
        Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount);
    script_string::ResetOwnershipValidationCountersForTesting();
    Sys_AtomicStore(&scrStringDebugGlob->totalRefCount, UINT32_MAX);
    const StateImage corruptState = CaptureState();
    const auto rejected = script_string::TryReAddRetainedDatabaseNames(
        corruptAdmission,
        names.data(),
        static_cast<std::uint32_t>(names.size()));
    const bool rejectedCleanly = Check(
            rejected.status
                == script_string::DatabaseUserAddBulkStatus::UnsafeFailure,
            "registry retained bulk treated corrupt aggregate as exhaustion")
        && Check(rejected.addedCount == 0 && rejected.unchangedCount == 0,
            "registry aggregate rejection published partial counts")
        && Check(corruptBatch.poisoned(),
            "registry aggregate rejection did not poison the batch")
        && Check(corruptBatch.operationCount() == 0,
            "registry aggregate rejection consumed an operation")
        && Check(StateMatches(corruptState),
            "registry aggregate rejection changed corrupt state")
        && Check(RegistryBulkScratchCleared(),
            "registry aggregate rejection retained scratch state")
        && Check(sl_registryOwnershipTopologyPreflightCount == 1,
            "registry aggregate rejection skipped topology preflight")
        && Check(
            sl_registryOwnershipTopologyPreflightWorkCount
                == 2 * (STRINGLIST_SIZE - 1)
                    + SL_MAX_STRING_INDEX + ids.size(),
            "registry aggregate preflight work changed")
        && Check(sl_hashChainVisitedCount == 0
                && sl_stringIdVisitedCount == 0,
            "registry aggregate rejection retained validation scratch");
    Sys_AtomicStore(&scrStringDebugGlob->totalRefCount, canonicalTotal);
    const bool closeRejected = Check(
        script_string::FinishOwnershipBatch(&corruptBatch)
            == script_string::OwnershipBatchStatus::UnsafeFailure,
        "registry aggregate-poisoned batch closed successfully");
    if (!rejectedCleanly || !closeRejected)
        return false;

    script_string::OwnershipBatch cleanupBatch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&cleanupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry aggregate cleanup batch admission failed"))
    {
        return false;
    }
    const auto cleanupAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(cleanupBatch);
    if (!Check(
            script_string::TryShutdownDatabaseUser8(cleanupAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry aggregate retained cleanup failed")
        || !Check(
            script_string::FinishOwnershipBatch(&cleanupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry aggregate cleanup batch close failed"))
    {
        return false;
    }

    return Check(
            script_string::TryRemoveOrdinaryReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "registry aggregate first cleanup failed")
        && Check(
            script_string::TryRemoveOrdinaryReference(second.stringId)
                == script_string::ReleaseStatus::Success,
            "registry aggregate second cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestRegistryBulkCertificateCorruptionFailsClosed() noexcept
{
    if (!BeginTest())
        return false;

    constexpr std::size_t kNameCount = 3;
    std::array<
        std::array<char, kRegistryCollisionNameBytes>, kNameCount> names{};
    std::array<std::uint32_t, kNameCount> ids{};
    for (std::size_t index = 0; index < kNameCount; ++index)
    {
        MakeRegistryCollisionName(names[index], index);
        const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
            names[index].data(),
            static_cast<std::uint32_t>(names[index].size()),
            15);
        if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
                "registry certificate corruption setup failed"))
        {
            return false;
        }
        ids[index] = acquired.stringId;
    }

    const std::uint32_t owningHash = GetHashCode(
        names[0].data(), static_cast<std::uint32_t>(names[0].size()));
    script_string::OwnershipBatch setupBatch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&setupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry certificate setup batch admission failed"))
    {
        return false;
    }
    const auto setupAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(setupBatch);
    const auto setupAdd = script_string::TryAddDatabaseUser4References(
        setupAdmission,
        ids.data(),
        static_cast<std::uint32_t>(ids.size()));
    if (!Check(
            setupAdd.status
                == script_string::DatabaseUserAddBulkStatus::Success
                && setupAdd.addedCount == ids.size(),
            "registry certificate retained setup add failed")
        || !Check(
            script_string::TryTransferDatabaseUsers4To8(setupAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry certificate retained setup transfer failed")
        || !Check(
            script_string::FinishOwnershipBatch(&setupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry certificate setup batch close failed"))
    {
        return false;
    }

    for (std::uint32_t scenario = 0; scenario < 5; ++scenario)
    {
        script_string::OwnershipBatch batch;
        if (!Check(
                script_string::TryBeginOwnershipBatch(&batch)
                    == script_string::OwnershipBatchStatus::Success,
                "registry certificate corruption batch admission failed"))
        {
            return false;
        }
        const auto admission =
            script_string::RegistryOwnershipAdmission::ForTesting(batch);

        std::uint32_t targetId = 0;
        std::uint32_t targetIndex = 0;
        for (const std::uint32_t stringId : ids)
        {
            const std::uint32_t candidateIndex =
                sl_systemSweepEntryByStringId[stringId];
            if (candidateIndex != 0 && candidateIndex != owningHash
                && sl_systemSweepPreviousEntry[candidateIndex] != owningHash)
            {
                targetId = stringId;
                targetIndex = candidateIndex;
                break;
            }
        }
        const std::uint32_t previousIndex = targetIndex != 0
            ? sl_systemSweepPreviousEntry[targetIndex]
            : 0;
        if (!Check(targetId != 0 && targetIndex != 0
                && previousIndex != 0 && previousIndex != owningHash
                && previousIndex != targetIndex,
            "registry certificate corruption fixture has no deep target"))
        {
            return false;
        }

        const HashEntry savedTarget = scrStringGlob.hashTable[targetIndex];
        const HashEntry savedPrevious =
            scrStringGlob.hashTable[previousIndex];
        const HashEntry savedHead = scrStringGlob.hashTable[owningHash];
        const char *const targetName =
            SL_GetRefStringNoReport(targetId)->str;
        if (scenario == 0)
        {
            scrStringGlob.hashTable[targetIndex].u.prev = ids[0] == targetId
                ? ids[1]
                : ids[0];
        }
        else if (scenario == 1)
        {
            scrStringGlob.hashTable[previousIndex].status_next =
                static_cast<std::uint16_t>(savedPrevious.status_next)
                | HASH_STAT_FREE;
        }
        else if (scenario == 2)
        {
            scrStringGlob.hashTable[previousIndex].u.prev = targetId;
        }
        else
        {
            scrStringGlob.hashTable[owningHash].status_next =
                static_cast<std::uint16_t>(owningHash) | HASH_STAT_HEAD;
        }

        script_string::ResetOwnershipValidationCountersForTesting();
        const StateImage corruptState = CaptureState();
        const std::array<std::uint32_t, 1> targetSpan{targetId};
        const std::array<const char *, 1> retainedSpan{targetName};
        const auto rejected = scenario == 4
            ? script_string::TryReAddRetainedDatabaseNames(
                admission,
                retainedSpan.data(),
                static_cast<std::uint32_t>(retainedSpan.size()))
            : script_string::TryAddDatabaseUser4References(
                admission,
                targetSpan.data(),
                static_cast<std::uint32_t>(targetSpan.size()));
        const bool rejectedCleanly = Check(
                rejected.status
                    == script_string::DatabaseUserAddBulkStatus::UnsafeFailure,
                "registry bulk trusted corrupt inverse certificate state")
            && Check(rejected.addedCount == 0
                    && rejected.unchangedCount == 0,
                "registry corrupt bulk rejection published partial counts")
            && Check(batch.poisoned(),
                "registry corrupt bulk rejection did not poison the batch")
            && Check(batch.operationCount() == 0,
                "registry corrupt bulk rejection consumed an operation")
            && Check(StateMatches(corruptState),
                "registry corrupt bulk rejection changed table state")
            && Check(RegistryBulkScratchCleared(),
                "registry corrupt bulk rejection retained scratch state")
            && Check(sl_registryOwnershipHashChainEntryVisitCount == 0,
                "registry corrupt bulk rejection traversed a collision chain")
            && Check(sl_registryOwnershipTopologyPreflightCount == 1,
                "registry corrupt bulk rejection skipped topology preflight")
            && Check(
                sl_registryOwnershipTopologyPreflightWorkCount
                    <= SL_MAX_STRING_INDEX + 3 * STRINGLIST_SIZE,
                "registry corrupt bulk preflight exceeded linear work")
            && Check(sl_hashChainVisitedCount == 0
                    && sl_stringIdVisitedCount == 0,
                "registry corrupt bulk rejection retained validation scratch");

        scrStringGlob.hashTable[owningHash] = savedHead;
        scrStringGlob.hashTable[targetIndex] = savedTarget;
        scrStringGlob.hashTable[previousIndex] = savedPrevious;
        const bool closeRejected = Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::UnsafeFailure,
            "registry corruption-poisoned batch closed successfully");
        if (!rejectedCleanly || !closeRejected)
            return false;
    }

    script_string::OwnershipBatch cleanupBatch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&cleanupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry certificate cleanup batch admission failed"))
    {
        return false;
    }
    const auto cleanupAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(cleanupBatch);
    if (!Check(
            script_string::TryShutdownDatabaseUser8(cleanupAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry certificate retained cleanup failed")
        || !Check(
            script_string::FinishOwnershipBatch(&cleanupBatch)
                == script_string::OwnershipBatchStatus::Success,
            "registry certificate cleanup batch close failed"))
    {
        return false;
    }

    for (const std::uint32_t stringId : ids)
    {
        if (!Check(
                script_string::TryRemoveOrdinaryReference(stringId)
                    == script_string::ReleaseStatus::Success,
                "registry certificate corruption cleanup failed"))
        {
            return false;
        }
    }
    return EndTest();
}

[[nodiscard]] bool TestRegistryCollisionBulkAndShutdownRemainLinear() noexcept
{
    if (!BeginTest())
        return false;

    constexpr std::size_t kOrdinaryCount = 48;
    std::array<
        std::array<char, kRegistryCollisionNameBytes>, kOrdinaryCount> names{};
    std::array<std::uint32_t, kOrdinaryCount> ids{};
    for (std::size_t index = 0; index < kOrdinaryCount; ++index)
    {
        MakeRegistryCollisionName(names[index], index);
        const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
            names[index].data(),
            static_cast<std::uint32_t>(names[index].size()),
            15);
        if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
                "registry linear collision setup failed"))
        {
            return false;
        }
        ids[index] = acquired.stringId;
    }

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry linear collision batch admission failed"))
    {
        return false;
    }
    const auto admission =
        script_string::RegistryOwnershipAdmission::ForTesting(batch);
    std::array<char, kRegistryCollisionNameBytes> databaseName{};
    MakeRegistryCollisionName(databaseName, kOrdinaryCount);
    const auto database = script_string::TryInternDatabaseUser4Name(
        admission,
        databaseName.data(),
        static_cast<std::uint32_t>(databaseName.size()),
        15);
    if (!Check(database.status == script_string::DatabaseNameStatus::Success,
            "registry linear collision database intern failed"))
    {
        return false;
    }

    script_string::ResetOwnershipValidationCountersForTesting();
    const auto added = script_string::TryAddDatabaseUser4References(
        admission, ids.data(), static_cast<std::uint32_t>(ids.size()));
    const std::array<std::uint32_t, 1> databaseSpan{database.stringId};
    const auto databaseUnchanged =
        script_string::TryAddDatabaseUser4References(
            admission,
            databaseSpan.data(),
            static_cast<std::uint32_t>(databaseSpan.size()));
    if (!Check(
            added.status == script_string::DatabaseUserAddBulkStatus::Success,
            "registry linear collision bulk add failed")
        || !Check(added.addedCount == kOrdinaryCount
                && added.unchangedCount == 0,
            "registry linear collision bulk accounting changed")
        || !Check(
            databaseUnchanged.status
                == script_string::DatabaseUserAddBulkStatus::NoChange,
            "registry leased-intern certificate was not immediately usable")
        || !Check(sl_registryOwnershipHashChainEntryVisitCount == 0,
            "registry collision bulk add traversed collision chains")
        || !Check(sl_registryOwnershipLinearSweepEntryVisitCount == 0,
            "registry collision bulk add entered the shutdown walker")
        || !Check(sl_registryOwnershipTopologyPreflightCount == 2,
            "registry collision bulk preflight count changed")
        || !Check(
            sl_registryOwnershipTopologyPreflightWorkCount
                == 2 * (2 * (STRINGLIST_SIZE - 1)
                    + SL_MAX_STRING_INDEX + kOrdinaryCount + 1),
            "registry collision bulk preflight was not linear"))
    {
        return false;
    }

    if (!Check(
            script_string::TryTransferDatabaseUsers4To8(admission)
                == script_string::DatabaseSweepStatus::Success,
            "registry linear collision transfer failed"))
    {
        return false;
    }
    script_string::ResetOwnershipValidationCountersForTesting();
    if (!Check(
            script_string::TryShutdownDatabaseUser8(admission)
                == script_string::DatabaseSweepStatus::Success,
            "registry linear collision shutdown failed")
        || !Check(sl_registryOwnershipHashChainEntryVisitCount == 0,
            "registry collision shutdown rebuilt per-ID unlink plans")
        || !Check(
            sl_registryOwnershipLinearSweepEntryVisitCount
                == kOrdinaryCount + 1,
            "registry collision shutdown did not visit each entry once")
        || !Check(sl_registryOwnershipTopologyPreflightCount == 0
                && sl_registryOwnershipTopologyPreflightWorkCount == 0,
            "registry collision shutdown entered bulk preflight")
        || !Check(
            !SL_IsRegistryStringCertificateMemberNoReport(database.stringId)
                && sl_systemSweepEntryByStringId[database.stringId] == 0,
            "registry shutdown retained a freed inverse certificate"))
    {
        return false;
    }

    const auto stale = script_string::TryAddDatabaseUser4References(
        admission,
        databaseSpan.data(),
        static_cast<std::uint32_t>(databaseSpan.size()));
    if (!Check(
            stale.status
                == script_string::DatabaseUserAddBulkStatus::
                    OwnershipMismatchNoChange,
            "registry bulk accepted an ID freed during the same batch")
        || !Check(!batch.poisoned(),
            "registry stale-ID rejection poisoned a valid batch")
        || !Check(RegistryBulkScratchCleared(),
            "registry stale-ID rejection retained scratch state")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry linear collision batch close failed")
        || !CheckFreed(database.stringId))
    {
        return false;
    }

    for (const std::uint32_t stringId : ids)
    {
        if (!Check(
                script_string::TryRemoveOrdinaryReference(stringId)
                    == script_string::ReleaseStatus::Success,
                "registry linear collision cleanup failed"))
        {
            return false;
        }
    }
    return EndTest();
}

[[nodiscard]] bool TestRegistryCoordinatorProductionStack() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstName[] = "registry-stack-first";
    constexpr char secondName[] = "registry-stack-second";
    constexpr char databaseName[] = "registry-stack-database";
    const auto first = script_string::TryAcquireOrdinaryStringOfSize(
        firstName, sizeof(firstName), 15);
    const auto second = script_string::TryAcquireOrdinaryStringOfSize(
        secondName, sizeof(secondName), 15);
    if (!Check(first.status == script_string::AcquireStatus::Acquired
                && second.status == script_string::AcquireStatus::Acquired,
            "registry production-stack setup failed"))
    {
        return false;
    }

    using namespace db::registry_ownership;
    const auto admission = RegistryOwnershipCoordinatorAdmission::ForTesting();
    RegistryOwnershipCoordinator coordinator;
    Sys_LockRead(&db_hashCritSect);
    const RegistryOwnershipStatus preheldReader =
        TryBeginStandaloneRegistryOwnershipCoordinator(
            admission, &coordinator);
    const bool preheldReaderRejected = Check(
            preheldReader == RegistryOwnershipStatus::Busy,
            "registry production-stack admitted a pre-held hash reader")
        && Check(coordinator.isEmptyCanonical(),
            "registry production-stack reader rejection retained authority")
        && Check(Sys_AtomicLoad(&db_hashCritSect.readCount) == 1
                && Sys_AtomicLoad(&db_hashCritSect.writeCount) == 0,
            "registry production-stack reader rejection changed hash state")
        && Check(g_scriptStringTransactionLockDepth == 0,
            "registry production-stack reader rejection retained transaction");
    Sys_UnlockRead(&db_hashCritSect);
    if (!preheldReaderRejected)
        return false;

    if (!Check(
            TryBeginStandaloneRegistryOwnershipCoordinator(
                admission, &coordinator)
                == RegistryOwnershipStatus::Success,
            "registry production-stack coordinator admission failed")
        || !Check(
            coordinator.phase() == RegistryOwnershipCoordinatorPhase::Ready
                && coordinator.mode()
                    == RegistryOwnershipCoordinatorMode::Standalone,
            "registry production-stack coordinator was not ready")
        || !Check(
            Sys_IsWriteLocked(&db_hashCritSect)
                && Sys_AtomicLoad(&db_hashCritSect.readCount) == 0
                && Sys_AtomicLoad(&db_hashCritSect.writeCount) == 1,
            "registry production-stack did not retain the real hash lock")
        || !Check(g_scriptStringTransactionLockDepth == 1,
            "registry production-stack did not retain the transaction lock"))
    {
        return false;
    }

    const std::array<std::uint32_t, 2> ids{
        first.stringId, second.stringId};
    RegistryOwnershipBulkResult bulk{99, 88};
    if (!Check(
            TryRegistryAddDatabaseUsers4(
                &coordinator,
                ids.data(),
                static_cast<std::uint32_t>(ids.size()),
                &bulk) == RegistryOwnershipStatus::Success,
            "registry production-stack bulk add failed")
        || !Check(bulk.addedCount == ids.size()
                && bulk.unchangedCount == 0,
            "registry production-stack bulk accounting changed"))
    {
        return false;
    }

    RegistryOwnershipName database{};
    if (!Check(
            TryRegistryInternBoundedName(
                &coordinator,
                databaseName,
                sizeof(databaseName),
                &database) == RegistryOwnershipStatus::Success,
            "registry production-stack database intern failed")
        || !Check(database.stringId != 0
                && database.canonicalName
                    == GetRefString(database.stringId)->str,
            "registry production-stack intern did not publish canonical data")
        || !Check(
            TryRegistryTransferDatabaseUsers4To8(&coordinator)
                == RegistryOwnershipStatus::Success,
            "registry production-stack user transfer failed")
        || !Check(
            TryRegistryShutdownDatabaseUser8(&coordinator)
                == RegistryOwnershipStatus::Success,
            "registry production-stack user shutdown failed")
        || !CheckOwnership(
            first.stringId,
            1,
            0,
            sizeof(firstName),
            2,
            "registry production-stack changed first ordinary owner")
        || !CheckOwnership(
            second.stringId,
            1,
            0,
            sizeof(secondName),
            2,
            "registry production-stack changed second ordinary owner"))
    {
        return false;
    }

    if (!Check(
            FinishRegistryOwnershipCoordinator(&coordinator)
                == RegistryOwnershipStatus::Success,
            "registry production-stack coordinator finish failed")
        || !Check(coordinator.isEmptyCanonical(),
            "registry production-stack coordinator did not reset")
        || !Check(!Sys_IsWriteLocked(&db_hashCritSect)
                && Sys_AtomicLoad(&db_hashCritSect.readCount) == 0
                && Sys_AtomicLoad(&db_hashCritSect.writeCount) == 0,
            "registry production-stack retained the hash lock")
        || !Check(g_scriptStringTransactionLockDepth == 0,
            "registry production-stack retained the transaction lock")
        || !CheckFreed(database.stringId))
    {
        return false;
    }

    return Check(
            script_string::TryRemoveOrdinaryReference(first.stringId)
                == script_string::ReleaseStatus::Success,
            "registry production-stack first cleanup failed")
        && Check(
            script_string::TryRemoveOrdinaryReference(second.stringId)
                == script_string::ReleaseStatus::Success,
            "registry production-stack second cleanup failed")
        && EndTest();
}

[[nodiscard]] bool TestRegistryOwnershipBatchOperations() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char ordinaryName[] = "registry-ordinary";
    constexpr char databaseName[] = "registry-database";
    constexpr char unterminated[]{'b', 'a', 'd'};
    std::array<char, 260> ambiguousLength{};
    ambiguousLength.fill('x');
    ambiguousLength[3] = '\0';
    ambiguousLength.back() = '\0';
    const script_string::AcquireResult ordinary =
        script_string::TryAcquireOrdinaryStringOfSize(
            ordinaryName, sizeof(ordinaryName), 15);
    RefString *const ordinaryRef = ordinary.stringId != 0
        ? GetRefString(ordinary.stringId)
        : nullptr;
    const char *const ordinaryCanonical = ordinaryRef
        ? ordinaryRef->str
        : nullptr;
    if (!Check(ordinary.status == script_string::AcquireStatus::Acquired,
            "registry batch ordinary setup failed")
        || !Check(ordinaryCanonical != nullptr,
            "registry batch ordinary canonical pointer was unavailable"))
    {
        return false;
    }

    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry batch admission failed"))
    {
        return false;
    }
    const auto registryAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(batch);

    const StateImage beforeRejectedAdd = CaptureState();
    const bool rejectedAdd = Check(
            script_string::TryAddDatabaseUser4Reference(registryAdmission, 0)
                == script_string::DatabaseUserAddStatus::
                    OwnershipMismatchNoChange,
            "registry user4 add accepted an invalid ID")
        && Check(StateMatches(beforeRejectedAdd),
            "registry user4 rejection changed ownership state")
        && Check(batch.operationCount() == 0,
            "registry user4 rejection consumed an operation");
    if (!rejectedAdd
        || !Check(
            script_string::TryAddDatabaseUser4Reference(
                registryAdmission, ordinary.stringId)
                == script_string::DatabaseUserAddStatus::Added,
            "registry user4 add failed")
        || !CheckOwnership(
            ordinary.stringId,
            2,
            static_cast<std::uint8_t>(script_string::kDatabaseUserMask),
            sizeof(ordinaryName),
            2,
            "registry user4 add ownership mismatch"))
    {
        return false;
    }

    const StateImage beforeDuplicateAdd = CaptureState();
    if (!Check(
            script_string::TryAddDatabaseUser4Reference(
                registryAdmission, ordinary.stringId)
                == script_string::DatabaseUserAddStatus::
                    AlreadyOwnedNoChange,
            "registry duplicate user4 add was not idempotent")
        || !Check(StateMatches(beforeDuplicateAdd),
            "registry duplicate user4 add changed ownership state")
        || !Check(batch.operationCount() == 1,
            "registry duplicate user4 add consumed an operation"))
    {
        return false;
    }

    const StateImage beforeRejectedNames = CaptureState();
    const auto nullName = script_string::TryInternDatabaseUser4Name(
        registryAdmission, nullptr, 1, 15);
    const auto zeroName = script_string::TryInternDatabaseUser4Name(
        registryAdmission, databaseName, 0, 15);
    const auto unterminatedName =
        script_string::TryInternDatabaseUser4Name(
            registryAdmission, unterminated, sizeof(unterminated), 15);
    const auto ambiguousName = script_string::TryInternDatabaseUser4Name(
        registryAdmission,
        ambiguousLength.data(),
        static_cast<std::uint32_t>(ambiguousLength.size()),
        15);
    const auto invalidTypeName = script_string::TryInternDatabaseUser4Name(
        registryAdmission, databaseName, sizeof(databaseName), 0);
    if (!Check(
            nullName.status
                    == script_string::DatabaseNameStatus::
                        InvalidArgumentNoChange
                && zeroName.status
                    == script_string::DatabaseNameStatus::
                        InvalidArgumentNoChange
                && unterminatedName.status
                    == script_string::DatabaseNameStatus::
                        InvalidArgumentNoChange
                && ambiguousName.status
                    == script_string::DatabaseNameStatus::
                        InvalidArgumentNoChange
                && invalidTypeName.status
                    == script_string::DatabaseNameStatus::
                        InvalidArgumentNoChange,
            "registry bounded-name validation accepted invalid input")
        || !Check(
            nullName.stringId == 0 && nullName.canonicalName == nullptr
                && zeroName.stringId == 0
                && zeroName.canonicalName == nullptr
                && unterminatedName.stringId == 0
                && unterminatedName.canonicalName == nullptr
                && ambiguousName.stringId == 0
                && ambiguousName.canonicalName == nullptr
                && invalidTypeName.stringId == 0
                && invalidTypeName.canonicalName == nullptr,
            "registry bounded-name rejection published output")
        || !Check(StateMatches(beforeRejectedNames),
            "registry bounded-name rejection changed ownership state")
        || !Check(batch.operationCount() == 1,
            "registry bounded-name rejection consumed an operation"))
    {
        return false;
    }

    const script_string::DatabaseNameResult database =
        script_string::TryInternDatabaseUser4Name(
            registryAdmission, databaseName, sizeof(databaseName), 15);
    if (!Check(
            database.status == script_string::DatabaseNameStatus::Success,
            "registry bounded-name intern failed")
        || !Check(database.stringId != 0 && database.canonicalName != nullptr,
            "registry bounded-name intern omitted canonical output")
        || !Check(std::memcmp(
                database.canonicalName,
                databaseName,
                sizeof(databaseName)) == 0,
            "registry bounded-name intern changed bytes")
        || !CheckOwnership(
            database.stringId,
            1,
            static_cast<std::uint8_t>(script_string::kDatabaseUserMask),
            sizeof(databaseName),
            3,
            "registry bounded-name ownership mismatch")
        || !Check(batch.operationCount() == 2,
            "registry bounded-name success did not consume an operation"))
    {
        return false;
    }

    if (!Check(
            script_string::TryTransferDatabaseUsers4To8(registryAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry 4-to-8 transfer failed")
        || !CheckOwnership(
            ordinary.stringId,
            2,
            static_cast<std::uint8_t>(
                script_string::kRetainedDatabaseUserMask),
            sizeof(ordinaryName),
            3,
            "registry ordinary 4-to-8 ownership mismatch")
        || !CheckOwnership(
            database.stringId,
            1,
            static_cast<std::uint8_t>(
                script_string::kRetainedDatabaseUserMask),
            sizeof(databaseName),
            3,
            "registry name 4-to-8 ownership mismatch"))
    {
        return false;
    }

    const StateImage beforeRejectedRetained = CaptureState();
    if (!Check(
            script_string::TryReAddRetainedDatabaseName(
                registryAdmission, nullptr)
                == script_string::DatabaseNameStatus::
                    InvalidArgumentNoChange,
            "registry retained-name re-add accepted null")
        || !Check(
            script_string::TryReAddRetainedDatabaseName(
                registryAdmission, database.canonicalName + 1)
                == script_string::DatabaseNameStatus::
                    OwnershipMismatchNoChange,
            "registry retained-name re-add accepted a noncanonical pointer")
        || !Check(StateMatches(beforeRejectedRetained),
            "registry retained-name rejection changed ownership state")
        || !Check(batch.operationCount() == 3,
            "registry retained-name rejection consumed an operation"))
    {
        return false;
    }

    const std::array<const char *, 2> rejectedRetainedSpan{
        ordinaryCanonical, database.canonicalName + 1};
    const auto rejectedRetainedBulk =
        script_string::TryReAddRetainedDatabaseNames(
            registryAdmission,
            rejectedRetainedSpan.data(),
            static_cast<std::uint32_t>(rejectedRetainedSpan.size()));
    if (!Check(
            rejectedRetainedBulk.status
                == script_string::DatabaseUserAddBulkStatus::
                    OwnershipMismatchNoChange,
            "registry retained bulk accepted a noncanonical member")
        || !Check(StateMatches(beforeRejectedRetained),
            "registry retained bulk changed an earlier valid member")
        || !Check(RegistryBulkScratchCleared(),
            "registry retained bulk rejection leaked scratch state"))
    {
        return false;
    }

    const std::array<const char *, 3> retainedSpan{
        ordinaryCanonical, database.canonicalName, ordinaryCanonical};
    const auto retainedBulk = script_string::TryReAddRetainedDatabaseNames(
        registryAdmission,
        retainedSpan.data(),
        static_cast<std::uint32_t>(retainedSpan.size()));
    if (!Check(
            retainedBulk.status
                == script_string::DatabaseUserAddBulkStatus::Success
                && retainedBulk.addedCount == 2
                && retainedBulk.unchangedCount == 1,
            "registry retained bulk re-add failed")
        || !Check(RegistryBulkScratchCleared(),
            "registry retained bulk success leaked scratch state")
        || !CheckOwnership(
            ordinary.stringId,
            3,
            static_cast<std::uint8_t>(
                script_string::kDatabaseUserMask
                | script_string::kRetainedDatabaseUserMask),
            sizeof(ordinaryName),
            5,
            "registry retained ordinary re-add ownership mismatch")
        || !CheckOwnership(
            database.stringId,
            2,
            static_cast<std::uint8_t>(
                script_string::kDatabaseUserMask
                | script_string::kRetainedDatabaseUserMask),
            sizeof(databaseName),
            5,
            "registry retained database re-add ownership mismatch")
        || !Check(batch.operationCount() == 5,
            "registry retained-name success count mismatch"))
    {
        return false;
    }

    if (!Check(
            script_string::TryTransferDatabaseUsers4To8(registryAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry duplicate 4-to-8 transfer failed")
        || !CheckOwnership(
            ordinary.stringId,
            2,
            static_cast<std::uint8_t>(
                script_string::kRetainedDatabaseUserMask),
            sizeof(ordinaryName),
            3,
            "registry duplicate ordinary transfer ownership mismatch")
        || !CheckOwnership(
            database.stringId,
            1,
            static_cast<std::uint8_t>(
                script_string::kRetainedDatabaseUserMask),
            sizeof(databaseName),
            3,
            "registry duplicate name transfer ownership mismatch")
        || !Check(
            script_string::TryShutdownDatabaseUser8(registryAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry user8 shutdown failed")
        || !Check(RegistrySweepScratchCleared(),
            "registry user8 shutdown retained scratch IDs")
        || !CheckOwnership(
            ordinary.stringId,
            1,
            0,
            sizeof(ordinaryName),
            1,
            "registry user8 shutdown changed ordinary ownership")
        || !Check(
            script_string::TryRemoveOrdinaryReference(
                batch, ordinary.stringId)
                == script_string::ReleaseStatus::Success,
            "registry ordinary cleanup failed")
        || !Check(batch.operationCount() == 8,
            "registry operation accounting mismatch")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry batch close failed"))
    {
        return false;
    }
    return CheckFreed(database.stringId)
        && CheckFreed(ordinary.stringId)
        && EndTest();
}

[[nodiscard]] bool TestRegistryOwnershipBulkOperations() noexcept
{
    if (!BeginTest())
        return false;

    constexpr std::size_t kUniqueCount = 64;
    constexpr std::size_t kSpanCount = kUniqueCount * 2;
    std::array<std::array<char, 40>, kUniqueCount> names{};
    std::array<std::uint32_t, kUniqueCount> ids{};
    std::array<std::uint32_t, kSpanCount> span{};
    for (std::size_t index = 0; index < kUniqueCount; ++index)
    {
        const int length = std::snprintf(
            names[index].data(),
            names[index].size(),
            "registry-bulk-%03zu",
            index);
        if (!Check(length > 0
                && static_cast<std::size_t>(length) + 1
                    < names[index].size(),
            "registry bulk name construction failed"))
        {
            return false;
        }
        const auto acquired = script_string::TryAcquireOrdinaryStringOfSize(
            names[index].data(),
            static_cast<std::uint32_t>(length + 1),
            15);
        if (!Check(acquired.status == script_string::AcquireStatus::Acquired,
            "registry bulk setup acquisition failed"))
        {
            return false;
        }
        ids[index] = acquired.stringId;
        span[index * 2] = acquired.stringId;
        span[index * 2 + 1] = acquired.stringId;
    }

    MT_ResetCompleteValidationCountForTesting();
    script_string::ResetOwnershipValidationCountersForTesting();
    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry bulk batch admission failed"))
    {
        return false;
    }
    const auto admission =
        script_string::RegistryOwnershipAdmission::ForTesting(batch);

    const std::array<std::uint32_t, 3> rejectedSpan{
        ids[0], 0, ids[1]};
    const StateImage beforeRejected = CaptureState();
    const auto rejected = script_string::TryAddDatabaseUser4References(
        admission,
        rejectedSpan.data(),
        static_cast<std::uint32_t>(rejectedSpan.size()));
    if (!Check(
            rejected.status
                == script_string::DatabaseUserAddBulkStatus::
                    OwnershipMismatchNoChange,
            "registry bulk accepted an invalid member")
        || !Check(rejected.addedCount == 0 && rejected.unchangedCount == 0,
            "registry bulk rejection published partial counts")
        || !Check(StateMatches(beforeRejected),
            "registry bulk rejection changed an earlier valid member")
        || !Check(RegistryBulkScratchCleared(),
            "registry bulk rejection retained scratch state"))
    {
        return false;
    }

    const auto added = script_string::TryAddDatabaseUser4References(
        admission,
        span.data(),
        static_cast<std::uint32_t>(span.size()));
    if (!Check(
            added.status == script_string::DatabaseUserAddBulkStatus::Success,
            "registry bulk add failed")
        || !Check(added.addedCount == kUniqueCount
                && added.unchangedCount == kUniqueCount,
            "registry bulk duplicate accounting changed")
        || !Check(batch.operationCount() == kUniqueCount,
            "registry bulk operation accounting changed")
        || !Check(RegistryBulkScratchCleared(),
            "registry bulk success retained scratch state"))
    {
        return false;
    }

    const auto repeated = script_string::TryAddDatabaseUser4References(
        admission,
        span.data(),
        static_cast<std::uint32_t>(span.size()));
    const auto openCounters =
        script_string::GetOwnershipValidationCountersForTesting();
    if (!Check(
            repeated.status
                == script_string::DatabaseUserAddBulkStatus::NoChange,
            "registry repeated bulk add was not idempotent")
        || !Check(repeated.addedCount == 0
                && repeated.unchangedCount == span.size(),
            "registry repeated bulk accounting changed")
        || !Check(batch.operationCount() == kUniqueCount,
            "registry repeated bulk consumed mutation operations")
        || !Check(openCounters.completeStringPasses == 1,
            "registry N-ID bulk repeated complete string validation")
        || !Check(MT_CompleteValidationCountForTesting() == 1,
            "registry N-ID bulk repeated complete allocator validation")
        || !Check(RegistryBulkScratchCleared(),
            "registry repeated bulk retained scratch state"))
    {
        return false;
    }

    const auto oversized = script_string::TryAddDatabaseUser4References(
        admission,
        span.data(),
        script_string::kRegistryOwnershipBulkCapacity + 1);
    if (!Check(
            oversized.status
                == script_string::DatabaseUserAddBulkStatus::CapacityNoChange,
            "registry bulk capacity bound was not enforced")
        || !Check(RegistryBulkScratchCleared(),
            "registry oversized bulk retained scratch state")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry bulk batch close failed"))
    {
        return false;
    }

    const auto closedCounters =
        script_string::GetOwnershipValidationCountersForTesting();
    if (!Check(closedCounters.completeStringPasses == 2,
            "registry bulk boundaries did not perform exactly two passes")
        || !Check(MT_CompleteValidationCountForTesting() == 2,
            "registry bulk allocator boundaries did not perform two passes"))
    {
        return false;
    }

    for (const std::uint32_t stringId : ids)
    {
        if (!Check(
                script_string::TryRemoveDatabaseUserReference(stringId)
                    == script_string::ReleaseStatus::Success,
                "registry bulk database cleanup failed")
            || !Check(
                script_string::TryRemoveOrdinaryReference(stringId)
                    == script_string::ReleaseStatus::Success,
                "registry bulk ordinary cleanup failed")
            || !CheckFreed(stringId))
        {
            return false;
        }
    }
    return EndTest();
}

[[nodiscard]] bool TestRegistryShutdownCapacityAtomicity() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char firstName[] = "registry-capacity-first";
    constexpr char secondName[] = "registry-capacity-second";
    script_string::OwnershipBatch batch;
    if (!Check(
            script_string::TryBeginOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry capacity batch admission failed"))
    {
        return false;
    }
    const auto registryAdmission =
        script_string::RegistryOwnershipAdmission::ForTesting(batch);
    const auto first = script_string::TryInternDatabaseUser4Name(
        registryAdmission, firstName, sizeof(firstName), 15);
    const auto second = script_string::TryInternDatabaseUser4Name(
        registryAdmission, secondName, sizeof(secondName), 15);
    if (!Check(first.status == script_string::DatabaseNameStatus::Success
                && second.status
                    == script_string::DatabaseNameStatus::Success,
            "registry capacity setup intern failed")
        || !Check(
            script_string::TryTransferDatabaseUsers4To8(registryAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry capacity setup transfer failed"))
    {
        return false;
    }

    batch.SetMemoryTreeMutationCountForTesting(UINT32_MAX - 1);
    const StateImage beforeCapacity = CaptureState();
    const std::uint32_t operationsBeforeCapacity = batch.operationCount();
    if (!Check(
            script_string::TryShutdownDatabaseUser8(registryAdmission)
                == script_string::DatabaseSweepStatus::CapacityNoChange,
            "registry shutdown ignored lease mutation capacity")
        || !Check(StateMatches(beforeCapacity),
            "registry shutdown capacity rejection changed ownership state")
        || !Check(
            batch.operationCount() == operationsBeforeCapacity,
            "registry shutdown capacity rejection consumed an operation")
        || !Check(RegistrySweepScratchCleared(),
            "registry shutdown capacity rejection retained scratch IDs"))
    {
        return false;
    }

    batch.SetMemoryTreeMutationCountForTesting(2);
    if (!Check(
            script_string::TryShutdownDatabaseUser8(registryAdmission)
                == script_string::DatabaseSweepStatus::Success,
            "registry shutdown did not recover after capacity rejection")
        || !Check(RegistrySweepScratchCleared(),
            "registry recovered shutdown retained scratch IDs")
        || !Check(
            script_string::FinishOwnershipBatch(&batch)
                == script_string::OwnershipBatchStatus::Success,
            "registry capacity batch close failed"))
    {
        return false;
    }
    return CheckFreed(first.stringId)
        && CheckFreed(second.stringId)
        && EndTest();
}

[[nodiscard]] bool TestLegacyCharacterFoldingUsesUnsignedInput() noexcept
{
    if (!BeginTest())
        return false;

    constexpr char mixedCase[]{
        static_cast<char>(0xE1), 'A', '\\', 'B', '\0'};
    const char foldedHigh = static_cast<char>(tolower(
        static_cast<unsigned char>(mixedCase[0])));
    const char expectedLower[]{foldedHigh, 'a', '\\', 'b', '\0'};
    const std::uint32_t stringId =
        SL_GetLowercaseString_(mixedCase, 0, 15);
    RefString *const refString = GetRefString(stringId);

    char canonical[32]{};
    CreateCanonicalFilename(
        canonical, mixedCase, static_cast<int>(sizeof(canonical)));
    const char expectedCanonical[]{foldedHigh, 'a', '/', 'b', '\0'};

    const bool valid = Check(refString != nullptr,
            "unsigned-fold string acquisition failed")
        && Check(std::memcmp(
                refString->str, expectedLower, sizeof(expectedLower)) == 0,
            "lowercase intern mishandled a high-bit byte")
        && Check(SL_FindLowercaseString(mixedCase) == stringId,
            "lowercase lookup mishandled a high-bit byte")
        && Check(std::memcmp(
                canonical,
                expectedCanonical,
                sizeof(expectedCanonical)) == 0,
            "canonical filename mishandled a high-bit byte")
        && Check(
            script_string::TryRemoveOrdinaryReference(stringId)
                == script_string::ReleaseStatus::Success,
            "unsigned-fold string cleanup failed")
        && Check(LocksReleased(), "unsigned-fold test leaked a lock")
        && Check(ReportersUnused(),
            "unsigned-fold test invoked a reporter");
    return valid && EndTest();
}
} // namespace

namespace db::zone_script_string_ownership
{
bool ZoneScriptStringOwnershipController::trySnapshotRegistryTransaction(
    const zone_load::ZoneLoadContextKey &,
    std::uint32_t *) const noexcept
{
    return false;
}

bool ZoneScriptStringOwnershipController::authenticatesRegistryTransaction(
    const zone_load::ZoneLoadContextKey &,
    const std::uint32_t) const noexcept
{
    return false;
}
} // namespace db::zone_script_string_ownership

void KISAK_CDECL Sys_Sleep(const std::uint32_t)
{
    std::this_thread::yield();
}

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    if (critSect == CRITSECT_SCRIPT_STRING)
    {
        if (g_countScriptStringLockAttempts.load(std::memory_order_acquire))
        {
            g_scriptStringLockAttempts.fetch_add(
                1, std::memory_order_acq_rel);
        }
        g_scriptStringMutex.lock();
        ++g_scriptStringLockDepth;
        return;
    }
    if (critSect == CRITSECT_MEMORY_TREE)
    {
        g_memoryTreeMutex.lock();
        ++g_memoryTreeLockDepth;
        return;
    }
    if (critSect == CRITSECT_DB_SCRIPT_STRING_TRANSACTION)
    {
        g_scriptStringTransactionMutex.lock();
        ++g_scriptStringTransactionLockDepth;
        return;
    }
    std::abort();
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int critSect)
{
    if (critSect == CRITSECT_SCRIPT_STRING && g_scriptStringLockDepth != 0)
    {
        --g_scriptStringLockDepth;
        g_scriptStringMutex.unlock();
        return;
    }
    if (critSect == CRITSECT_MEMORY_TREE && g_memoryTreeLockDepth != 0)
    {
        --g_memoryTreeLockDepth;
        g_memoryTreeMutex.unlock();
        return;
    }
    if (critSect == CRITSECT_DB_SCRIPT_STRING_TRANSACTION
        && g_scriptStringTransactionLockDepth != 0)
    {
        --g_scriptStringTransactionLockDepth;
        g_scriptStringTransactionMutex.unlock();
        return;
    }
    std::abort();
}

void Com_Memset(void *const destination, const int value, const std::size_t count)
{
    if (g_pauseDebugMemset.load(std::memory_order_acquire))
    {
        g_debugMemsetEntered.store(true, std::memory_order_release);
        while (!g_releaseDebugMemset.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
    std::memset(destination, value, count);
}

void QDECL Com_Printf(int, const char *, ...)
{
}

void QDECL Com_Error(errorParm_t, const char *, ...)
{
    if (g_scriptStringLockDepth != 0 || g_memoryTreeLockDepth != 0
        || g_scriptStringTransactionLockDepth != 0)
        g_reporterSawOwnedLock = true;
    ++g_comErrorCount;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    if (g_scriptStringLockDepth != 0 || g_memoryTreeLockDepth != 0
        || g_scriptStringTransactionLockDepth != 0)
        g_reporterSawOwnedLock = true;
    ++g_assertCount;
}

char *QDECL va(const char *, ...)
{
    static thread_local char empty[1]{};
    return empty;
}

int main()
{
    if (!TestInitCheckLeaksRetainsLock()
        || !TestFullInitRejectsDebugOnlyState()
        || !TestDuplicateInitCheckLeaksNoChange()
        || !TestDuplicateInitNoChange()
        || !TestInvalidAndNoChange()
        || !TestRepeatedInternAndDatabaseTransfer()
        || !TestOrdinaryRollbackFreeAndReuse()
        || !TestEmbeddedNulByteCount()
        || !TestCollidingByteLengthBounds()
        || !TestLegacyBinaryInternCompatibility()
        || !TestLegacyFreeListSpliceBoundaries()
        || !TestLegacyEmptyAndOneNodeFreeList()
        || !TestShutdownStaleIterationRollback()
        || !TestSystemIterationAuthenticatesPhysicalEntries()
        || !TestShutdownMixedCollisionChain()
        || !TestLegacyCompatibilityAvoidsCompleteScans()
        || !TestLegacyHashScratchResetIsChainBounded()
        || !TestLegacyLocalCorruptionAndReporterUnwind()
        || !TestMalformedStateFailsClosed()
        || !TestOwnershipBatchLifecycle()
        || !TestOwnershipBatchAllowsSharedVectorDebugSlots()
        || !TestOwnershipBatchRejectsUnauthorizedEntries()
        || !TestOwnershipBatchLocalPoisoning()
        || !TestOwnershipBatchBoundaryValidation()
        || !TestOwnershipBatchAuthenticationAndOverflow()
        || !TestOwnershipBatchRejectsRawAllocatorMutation()
        || !TestOwnershipBatchForeignSerialization()
        || !TestOwnershipBatchForeignReaderSerialization()
        || !TestOwnershipBatchCanonicalResetGate()
        || !TestOwnershipBatchLifetimeBoundaries()
        || !TestOwnershipBatchOuterAuthorityTears()
        || !TestOwnershipBatchNestedAuthorityTears()
        || !TestOwnershipBatchUnrelatedDestruction()
        || !TestLegacyReadersAuthenticateExactStrings()
        || !TestLegacyMutatorsAuthenticateExactStrings()
        || !TestRegistryTypedOperationsRejectCorruptDebugPointer()
        || !TestRegistryRetainedBulkRejectsCorruptAggregate()
        || !TestRegistryBulkCertificateCorruptionFailsClosed()
        || !TestRegistryCollisionBulkAndShutdownRemainLinear()
        || !TestRegistryCoordinatorProductionStack()
        || !TestRegistryOwnershipBatchOperations()
        || !TestRegistryOwnershipBulkOperations()
        || !TestRegistryShutdownCapacityAtomicity()
        || !TestLegacyCharacterFoldingUsesUnsignedInput())
    {
        return 1;
    }

    std::puts("script-string report-free ownership contracts passed");
    return 0;
}

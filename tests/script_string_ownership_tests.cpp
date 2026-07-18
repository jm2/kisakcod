#include <script/scr_stringlist.cpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
std::recursive_mutex g_scriptStringMutex;
std::recursive_mutex g_memoryTreeMutex;
thread_local std::uint32_t g_scriptStringLockDepth = 0;
thread_local std::uint32_t g_memoryTreeLockDepth = 0;
std::uint32_t g_comErrorCount = 0;
std::uint32_t g_assertCount = 0;
bool g_reporterSawOwnedLock = false;

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
    return g_scriptStringLockDepth == 0 && g_memoryTreeLockDepth == 0;
}

[[nodiscard]] bool ReportersUnused() noexcept
{
    return g_comErrorCount == 0 && g_assertCount == 0
        && !g_reporterSawOwnedLock;
}

[[nodiscard]] std::uint32_t Packed(const std::uint32_t stringId) noexcept
{
    return scr_string_atomic::Load(SL_RefStringWord(GetRefString(stringId)));
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
    if (!Check(lastUniqueLength > 0,
            "legacy lookup benchmark formatting failed")
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
} // namespace

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    if (critSect == CRITSECT_SCRIPT_STRING)
    {
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
    std::abort();
}

void Com_Memset(void *const destination, const int value, const std::size_t count)
{
    std::memset(destination, value, count);
}

void QDECL Com_Printf(int, const char *, ...)
{
}

void QDECL Com_Error(errorParm_t, const char *, ...)
{
    if (g_scriptStringLockDepth != 0 || g_memoryTreeLockDepth != 0)
        g_reporterSawOwnedLock = true;
    ++g_comErrorCount;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    if (g_scriptStringLockDepth != 0 || g_memoryTreeLockDepth != 0)
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
    if (!TestInvalidAndNoChange()
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
        || !TestLegacyLocalCorruptionAndReporterUnwind()
        || !TestMalformedStateFailsClosed())
    {
        return 1;
    }

    std::puts("script-string report-free ownership contracts passed");
    return 0;
}

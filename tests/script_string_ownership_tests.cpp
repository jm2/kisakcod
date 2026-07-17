#include <script/scr_stringlist.cpp>

#include <array>
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
    return g_comErrorCount == 0 && g_assertCount == 0;
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
    ++g_comErrorCount;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
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
        || !TestMalformedStateFailsClosed())
    {
        return 1;
    }

    std::puts("script-string report-free ownership contracts passed");
    return 0;
}

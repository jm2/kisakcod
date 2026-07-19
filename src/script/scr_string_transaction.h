#pragma once

#include <script/scr_memorytree.h>
#include <universal/kisak_abi.h>

#include <cstdint>
#include <type_traits>

namespace db::registry_ownership
{
class RegistryOwnershipCoordinator;
}

namespace script_string
{
inline constexpr std::uint32_t kDatabaseUserMask = UINT32_C(4);
inline constexpr std::uint32_t kRetainedDatabaseUserMask = UINT32_C(8);
inline constexpr std::uint32_t kCurrentRuntimeStringLimit = UINT32_C(65536);

[[nodiscard]] constexpr bool IsCurrentRuntimeStringId(
    const std::uint32_t stringId) noexcept
{
    return stringId > 0 && stringId < kCurrentRuntimeStringLimit;
}

enum class AcquireStatus : std::uint8_t
{
    Acquired,
    InvalidArgumentNoChange,
    CapacityNoChange,
    RefCountExhaustedNoChange,
    UnsafeFailure,
};

struct AcquireResult final
{
    AcquireStatus status = AcquireStatus::InvalidArgumentNoChange;
    std::uint32_t stringId = 0;
};

enum class TransferStatus : std::uint8_t
{
    DatabaseUserClaimed,
    DuplicateReleased,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

enum class ReleaseStatus : std::uint8_t
{
    Success,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

enum class OwnershipBatchStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidToken,
    InvalidState,
    UnsafeFailure,
};

// Fixed registry-facing operations deliberately expose no arbitrary user
// mask.  User 4 is the live database owner and user 8 is the temporary owner
// retained across the global unused-resource sweep.
enum class DatabaseUserAddStatus : std::uint8_t
{
    Added,
    AlreadyOwnedNoChange,
    OwnershipMismatchNoChange,
    RefCountExhaustedNoChange,
    UnsafeFailure,
};

enum class DatabaseNameStatus : std::uint8_t
{
    Success,
    InvalidArgumentNoChange,
    CapacityNoChange,
    RefCountExhaustedNoChange,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

struct DatabaseNameResult final
{
    DatabaseNameStatus status = DatabaseNameStatus::InvalidArgumentNoChange;
    std::uint32_t stringId = 0;
    const char *canonicalName = nullptr;
};

enum class DatabaseSweepStatus : std::uint8_t
{
    Success,
    CapacityNoChange,
    UnsafeFailure,
};

inline constexpr std::uint32_t kRegistryOwnershipBulkCapacity =
    UINT32_C(19999);

enum class DatabaseUserAddBulkStatus : std::uint8_t
{
    Success,
    NoChange,
    InvalidArgumentNoChange,
    CapacityNoChange,
    OwnershipMismatchNoChange,
    RefCountExhaustedNoChange,
    UnsafeFailure,
};

struct DatabaseUserAddBulkResult final
{
    DatabaseUserAddBulkStatus status =
        DatabaseUserAddBulkStatus::InvalidArgumentNoChange;
    std::uint32_t addedCount = 0;
    std::uint32_t unchangedCount = 0;
};

class OwnershipBatch;

// Source-level capability for the five registry-only user-4/user-8
// operations. Production can construct one only through the exact active
// RegistryOwnershipCoordinator. It carries numeric, mirrored identities so
// the string boundary can authenticate the active OwnershipBatch before the
// first pointer dereference. The type is deliberately neither default
// constructible nor copyable, preventing ordinary callers with an arbitrary
// OwnershipBatch from bypassing the registry/hash coordinator.
class RegistryOwnershipAdmission final
{
public:
    RegistryOwnershipAdmission() = delete;
    ~RegistryOwnershipAdmission() noexcept = default;
    RegistryOwnershipAdmission(const RegistryOwnershipAdmission &) = delete;
    RegistryOwnershipAdmission &operator=(
        const RegistryOwnershipAdmission &) = delete;
    RegistryOwnershipAdmission(RegistryOwnershipAdmission &&) = delete;
    RegistryOwnershipAdmission &operator=(
        RegistryOwnershipAdmission &&) = delete;

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
    [[nodiscard]] static RegistryOwnershipAdmission ForTesting(
        OwnershipBatch &batch) noexcept;
#endif

private:
    friend class db::registry_ownership::RegistryOwnershipCoordinator;
    friend DatabaseUserAddStatus TryAddDatabaseUser4Reference(
        const RegistryOwnershipAdmission &, std::uint32_t) noexcept;
    friend DatabaseUserAddBulkResult TryAddDatabaseUser4References(
        const RegistryOwnershipAdmission &,
        const std::uint32_t *,
        std::uint32_t) noexcept;
    friend DatabaseNameResult TryInternDatabaseUser4Name(
        const RegistryOwnershipAdmission &,
        const char *,
        std::uint32_t,
        int) noexcept;
    friend DatabaseNameStatus TryReAddRetainedDatabaseName(
        const RegistryOwnershipAdmission &, const char *) noexcept;
    friend DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames(
        const RegistryOwnershipAdmission &,
        const char *const *,
        std::uint32_t) noexcept;
    friend DatabaseSweepStatus TryTransferDatabaseUsers4To8(
        const RegistryOwnershipAdmission &) noexcept;
    friend DatabaseSweepStatus TryShutdownDatabaseUser8(
        const RegistryOwnershipAdmission &) noexcept;

    RegistryOwnershipAdmission(
        std::uintptr_t coordinatorAddress,
        std::uint64_t coordinatorSerial,
        std::uintptr_t batchAddress,
        std::uint64_t batchSerial) noexcept;
    [[nodiscard]] OwnershipBatch *tryAuthenticateBatchLocked() const noexcept;

    std::uintptr_t coordinatorAddress_ = 0;
    std::uintptr_t coordinatorAddressMirror_ = 0;
    std::uint64_t coordinatorSerial_ = 0;
    std::uint64_t coordinatorSerialMirror_ = 0;
    std::uintptr_t batchAddress_ = 0;
    std::uintptr_t batchAddressMirror_ = 0;
    std::uint64_t batchSerial_ = 0;
    std::uint64_t batchSerialMirror_ = 0;
};
RUNTIME_SIZE(RegistryOwnershipAdmission, 0x30, 0x40);
static_assert(std::is_standard_layout_v<RegistryOwnershipAdmission>);
static_assert(!std::is_default_constructible_v<RegistryOwnershipAdmission>);
static_assert(!std::is_copy_constructible_v<RegistryOwnershipAdmission>);

// A same-thread ownership batch retains locks in the only permitted order:
// CRITSECT_SCRIPT_STRING first, then CRITSECT_MEMORY_TREE. Admission and close
// validate the complete string table and allocator. Operations between those
// boundaries retain bounded hash/free-list/debug checks and use the retained
// allocator validation lease.
//
// This is not a controller, zone, or transaction phase token. Begin and
// Finish must be co-located around one bounded, callback-free ownership loop
// using only the fixed batch operations below. No legacy string API, reporter,
// callback, or unrelated memory-tree work may run while a batch is active.
//
// Storage-lifetime and thread-affinity contract: the batch object must remain
// alive until every API call that received its address/reference has returned,
// and Begin, every operation, Finish, and destruction must run on the admitting
// thread. Normal Finish clears both retained acquisitions. Destroying an
// admitted batch without Finish permanently freezes both ownership boundaries;
// an exactly authenticated destructor releases only the acquisitions proven to
// belong to that live object, while torn authority remains held fail closed.
class OwnershipBatch final
{
public:
    OwnershipBatch() noexcept = default;
    ~OwnershipBatch() noexcept;

    OwnershipBatch(const OwnershipBatch &) = delete;
    OwnershipBatch &operator=(const OwnershipBatch &) = delete;
    OwnershipBatch(OwnershipBatch &&) = delete;
    OwnershipBatch &operator=(OwnershipBatch &&) = delete;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    [[nodiscard]] std::uint64_t serial() const noexcept;
    [[nodiscard]] std::uint32_t operationCount() const noexcept;
    [[nodiscard]] bool canonicalInactive() const noexcept;
    [[nodiscard]] bool authenticates(std::uint64_t serial) const noexcept;
    [[nodiscard]] bool ownsMemoryTreeLease(
        const MT_ValidationLease *lease) const noexcept;

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
    [[nodiscard]] MT_ValidationLease &MemoryTreeLeaseForTesting() noexcept;
    void ActivateForTesting(std::uint64_t serial) noexcept;
    void SetAuthenticationFieldsForTesting(
        std::uint64_t serial,
        std::uint8_t reserved0,
        std::uint8_t reserved1) noexcept;
    void SetOperationCountForTesting(std::uint32_t operationCount) noexcept;
    void SetMemoryTreeMutationCountForTesting(
        std::uint32_t mutationCount) noexcept;
#endif

private:
    friend OwnershipBatchStatus TryBeginOwnershipBatch(
        OwnershipBatch *batch) noexcept;
    friend OwnershipBatchStatus FinishOwnershipBatch(
        OwnershipBatch *batch) noexcept;
    friend AcquireResult TryAcquireOrdinaryStringOfSize(
        OwnershipBatch &batch,
        const char *bytes,
        std::uint32_t byteCount,
        int type) noexcept;
    friend TransferStatus TryTransferOrdinaryToDatabaseUser(
        OwnershipBatch &batch,
        std::uint32_t stringId) noexcept;
    friend ReleaseStatus TryRemoveOrdinaryReference(
        OwnershipBatch &batch,
        std::uint32_t stringId) noexcept;
    friend ReleaseStatus TryRemoveDatabaseUserReference(
        OwnershipBatch &batch,
        std::uint32_t stringId) noexcept;
    friend class RegistryOwnershipAdmission;
    friend DatabaseUserAddStatus TryAddDatabaseUser4Reference(
        const RegistryOwnershipAdmission &,
        std::uint32_t) noexcept;
    friend DatabaseUserAddBulkResult TryAddDatabaseUser4References(
        const RegistryOwnershipAdmission &,
        const std::uint32_t *,
        std::uint32_t) noexcept;
    friend DatabaseNameResult TryInternDatabaseUser4Name(
        const RegistryOwnershipAdmission &,
        const char *,
        std::uint32_t,
        int) noexcept;
    friend DatabaseNameStatus TryReAddRetainedDatabaseName(
        const RegistryOwnershipAdmission &,
        const char *) noexcept;
    friend DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames(
        const RegistryOwnershipAdmission &,
        const char *const *,
        std::uint32_t) noexcept;
    friend DatabaseSweepStatus TryTransferDatabaseUsers4To8(
        const RegistryOwnershipAdmission &) noexcept;
    friend DatabaseSweepStatus TryShutdownDatabaseUser8(
        const RegistryOwnershipAdmission &) noexcept;

    [[nodiscard]] static const MT_ValidationLeaseAdmission &
    MakeMemoryTreeLeaseAdmission() noexcept;
    [[nodiscard]] bool isCanonicalClearNoLock() const noexcept;
    [[nodiscard]] bool ownsRegistryNoLock() const noexcept;
#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
    [[nodiscard]] bool registryNamesStorageNoLock() const noexcept;
#endif
    void activateNoLock(std::uint64_t serial) noexcept;
    void poisonNoLock() noexcept;
    void clearNoLock() noexcept;
    void poisonBoundaryLocked() noexcept;
    [[nodiscard]] bool canOperateNoLock() const noexcept;
    [[nodiscard]] bool tryAuthenticateOperationLocked() noexcept;

    MT_ValidationLease memoryTreeLease_{};
    std::uint64_t serial_ = 0;
    std::uint32_t operationCount_ = 0;
    bool active_ = false;
    bool poisoned_ = false;
    std::uint8_t reserved_[2]{};
};
RUNTIME_SIZE(OwnershipBatch, 0x20, 0x20);
static_assert(std::is_standard_layout_v<OwnershipBatch>);
static_assert(!std::is_trivially_destructible_v<OwnershipBatch>);

[[nodiscard]] OwnershipBatchStatus TryBeginOwnershipBatch(
    OwnershipBatch *batch) noexcept;

// An authenticated owner releases both retained locks even when validation
// fails. InvalidToken cannot identify a retained acquisition safely and
// therefore leaves the batch held for explicit recovery.
[[nodiscard]] OwnershipBatchStatus FinishOwnershipBatch(
    OwnershipBatch *batch) noexcept;

// These operations are the report-free ownership boundary used by the
// fast-file transaction journal. They never call Com_Error, MT_Error, strlen,
// or any other nonlocal reporting path. Every ID-taking operation validates
// the current 16-bit runtime domain before looking up a RefString.
[[nodiscard]] AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *bytes,
    std::uint32_t byteCount,
    int type) noexcept;

[[nodiscard]] AcquireResult TryAcquireOrdinaryStringOfSize(
    OwnershipBatch &batch,
    const char *bytes,
    std::uint32_t byteCount,
    int type) noexcept;

[[nodiscard]] TransferStatus TryTransferOrdinaryToDatabaseUser(
    std::uint32_t stringId) noexcept;

[[nodiscard]] TransferStatus TryTransferOrdinaryToDatabaseUser(
    OwnershipBatch &batch,
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveOrdinaryReference(
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveOrdinaryReference(
    OwnershipBatch &batch,
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveDatabaseUserReference(
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveDatabaseUserReference(
    OwnershipBatch &batch,
    std::uint32_t stringId) noexcept;

// Registry operations use only the fixed legacy database masks. Each function
// requires the private exact coordinator admission and is callback-free/
// report-free. All
// ordinary rejection statuses leave string, hash, debug, allocator, batch
// operation count, and caller outputs unchanged. UnsafeFailure is terminal
// fail-closed evidence rather than a recoverable rejection.
[[nodiscard]] DatabaseUserAddStatus TryAddDatabaseUser4Reference(
    const RegistryOwnershipAdmission &admission,
    std::uint32_t stringId) noexcept;

// Atomically preflights and adds user 4 to every unique ID in one bounded,
// callback-free batch. Duplicate/already-owned IDs count as unchanged. Every
// ordinary rejection leaves all IDs unchanged; UnsafeFailure is terminal.
[[nodiscard]] DatabaseUserAddBulkResult TryAddDatabaseUser4References(
    const RegistryOwnershipAdmission &admission,
    const std::uint32_t *stringIds,
    std::uint32_t count) noexcept;

[[nodiscard]] DatabaseNameResult TryInternDatabaseUser4Name(
    const RegistryOwnershipAdmission &admission,
    const char *bytes,
    std::uint32_t byteCount,
    int type) noexcept;

// retainedName must be the exact canonical payload address of a live user-8
// string. The operation recovers its bounded allocation extent without
// strlen, then restores user 4 while user 8 keeps the address alive.
[[nodiscard]] DatabaseNameStatus TryReAddRetainedDatabaseName(
    const RegistryOwnershipAdmission &admission,
    const char *retainedName) noexcept;

// Bounded pointer-span counterpart used by the retained-default sweep. Every
// pointer must be an exact canonical live user-8 payload. The whole unique set
// is preflighted before user 4 is added to any member.
[[nodiscard]] DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames(
    const RegistryOwnershipAdmission &admission,
    const char *const *retainedNames,
    std::uint32_t count) noexcept;

// Exhaustive operations preflight the complete fixed-capacity table before
// their first ownership mutation. The 4 -> 8 transfer never frees storage.
// Shutdown snapshots at most STRINGLIST_SIZE-1 authenticated IDs in fixed BSS
// and reserves all required lease mutation counts before removing user 8.
[[nodiscard]] DatabaseSweepStatus TryTransferDatabaseUsers4To8(
    const RegistryOwnershipAdmission &admission) noexcept;

[[nodiscard]] DatabaseSweepStatus TryShutdownDatabaseUser8(
    const RegistryOwnershipAdmission &admission) noexcept;

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
struct OwnershipValidationCounters final
{
    std::uint64_t completeStringPasses;
    std::uint64_t stringEntriesVisited;
};
RUNTIME_SIZE(OwnershipValidationCounters, 0x10, 0x10);

void ResetOwnershipValidationCountersForTesting() noexcept;
[[nodiscard]] OwnershipValidationCounters
GetOwnershipValidationCountersForTesting() noexcept;
void SetNextOwnershipBatchSerialForTesting(std::uint64_t serial) noexcept;
void SetOwnershipBatchRegistryMirrorsForTesting(
    std::uintptr_t address,
    std::uint64_t serial,
    std::uintptr_t nestedLeaseAddress,
    std::uintptr_t addressMirror,
    std::uint64_t serialMirror,
    std::uintptr_t nestedLeaseAddressMirror) noexcept;
void SetOwnershipBatchLifecycleForTesting(
    std::uint8_t lifecycle,
    std::uint8_t lifecycleMirror) noexcept;
void SetRetainedOwnershipBatchAuthenticationForTesting(
    std::uintptr_t address,
    std::uint64_t serial,
    std::uintptr_t nestedLeaseAddress,
    std::uintptr_t addressMirror,
    std::uint64_t serialMirror,
    std::uintptr_t nestedLeaseAddressMirror) noexcept;
// Production has no recovery path for a terminal abandoned batch. Tests must
// explicitly state which independently authenticated retained acquisitions
// their fixture deliberately left held.
void ResetAbandonedOwnershipBatchForTesting(
    bool releaseRetainedScriptAcquisition,
    bool releaseRetainedMemoryTreeAcquisition) noexcept;
#endif

} // namespace script_string

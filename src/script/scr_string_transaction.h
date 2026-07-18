#pragma once

#include <script/scr_memorytree.h>
#include <universal/kisak_abi.h>

#include <cstdint>
#include <type_traits>

namespace script_string
{
inline constexpr std::uint32_t kDatabaseUserMask = UINT32_C(4);
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

// A same-thread ownership batch retains locks in the only permitted order:
// CRITSECT_SCRIPT_STRING first, then CRITSECT_MEMORY_TREE. Admission and close
// validate the complete string table and allocator. Operations between those
// boundaries retain bounded hash/free-list/debug checks and use the retained
// allocator validation lease.
//
// This is not a controller, zone, or transaction phase token. Begin and
// Finish must be co-located around one bounded, callback-free ownership loop
// using only the four batch overloads below. No legacy string API, reporter,
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
    void SetAuthenticationFieldsForTesting(
        std::uint64_t serial,
        std::uint8_t reserved0,
        std::uint8_t reserved1) noexcept;
    void SetOperationCountForTesting(std::uint32_t operationCount) noexcept;
    void SetMemoryTreeMutationCountForTesting(
        std::uint32_t mutationCount) noexcept;
#endif

private:
    friend struct OwnershipBatchAccess;
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

    [[nodiscard]] static MT_ValidationLeaseAdmission
    MakeMemoryTreeLeaseAdmission() noexcept;
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

#pragma once

#include <database/db_script_string_transaction.h>
#include <database/db_zone_load_context.h>
#include <database/db_zone_script_string_ownership.h>
#include <script/scr_string_transaction.h>
#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace db::zone_runtime
{
class ZoneRuntimeFacade;
}

namespace db::registry_ownership
{
class RegistryOwnershipCoordinator;
enum class RegistryOwnershipStatus : std::uint8_t;
struct RegistryOwnershipName;
struct RegistryOwnershipBulkResult;

class RegistryOwnershipCoordinatorFacade final
{
public:
    RegistryOwnershipCoordinatorFacade() = delete;
    ~RegistryOwnershipCoordinatorFacade() noexcept = default;
    RegistryOwnershipCoordinatorFacade(
        const RegistryOwnershipCoordinatorFacade &) = delete;
    RegistryOwnershipCoordinatorFacade &operator=(
        const RegistryOwnershipCoordinatorFacade &) = delete;
    RegistryOwnershipCoordinatorFacade(
        RegistryOwnershipCoordinatorFacade &&) = delete;
    RegistryOwnershipCoordinatorFacade &operator=(
        RegistryOwnershipCoordinatorFacade &&) = delete;

private:
    friend class db::zone_runtime::ZoneRuntimeFacade;

    [[nodiscard]] static RegistryOwnershipStatus
    TryBeginStandalone() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryBorrow(
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController &controller,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryBorrowActiveRuntimeCallback(
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController &controller,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus Finish() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus ValidateInactive() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus ValidateActive() noexcept;
    [[nodiscard]] static bool WritableOutputIsSeparated(
        const void *output,
        std::size_t outputSize,
        std::size_t outputAlignment) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    authenticateConstructedStorage(
        RegistryOwnershipCoordinator *coordinator) noexcept;

    // The five fixed registry-operation families below are checked and
    // report-free. They require the already-authenticated Ready coordinator;
    // the coordinator layer invokes no callback, reporter, or general-purpose
    // allocator. Each admitted call uses only its private OwnershipBatch and
    // never acquires or releases the retained outer transaction or hash-write
    // lock. A recoverable result closes that batch and restores the same Ready
    // coordinator identity and acquisitions. UnsafeFailure poisons the boundary
    // and intentionally retains those outer acquisitions fail closed.
    // The runtime facade owns caller input/output span separation before
    // forwarding here.
    [[nodiscard]] static RegistryOwnershipStatus TryAddDatabaseUser4(
        std::uint32_t stringId) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryAddDatabaseUsers4(
        const std::uint32_t *stringIds,
        std::uint32_t count,
        RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryInternBoundedName(
        const char *bytes,
        std::uint32_t byteCount,
        RegistryOwnershipName *outName) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryReAddRetainedDefaultName(
        const char *retainedCanonicalName) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryReAddRetainedDefaultNames(
        const char *const *retainedCanonicalNames,
        std::uint32_t count,
        RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryTransferDatabaseUsers4To8() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryShutdownDatabaseUser8() noexcept;
};

// Production lock-order boundary for the five legacy registry string
// operations that still use raw user-4/user-8 APIs. Only the private runtime
// facade is enrolled; legacy registry/load callers remain uncut over.
//
// A coordinator either owns a standalone script-string transaction or borrows
// the exact active transaction of one ZoneScriptStringOwnershipController. It
// acquires db_hashCritSect exactly once at admission and retains it until
// Finish. Every operation temporarily adds, and releases in reverse order:
//
//   script-string transaction -> db_hashCritSect -> OwnershipBatch
//
// OwnershipBatch retains SCRIPT_STRING -> MEMORY_TREE internally. Operations
// are fixed, bounded, callback-free, allocation-free at the coordination
// layer, and expose no arbitrary user mask. Storage must remain alive and on
// the admitting thread until Finish succeeds. Exact nonthrowing abandonment
// poisons the durable boundary without invoking a backend or releasing an
// acquisition; torn storage remains held fail closed.
enum class RegistryOwnershipCoordinatorPhase : std::uint8_t
{
    Empty,
    Acquiring,
    Ready,
    Operating,
    Finishing,
    UnsafeFailure,
};

enum class RegistryOwnershipCoordinatorMode : std::uint8_t
{
    None = 0,
    Standalone = 1,
    BorrowedZoneController = 2,
    // Appended so the established mode values above remain stable.
    BorrowedActiveRuntimeCallback = 3,
};

enum class RegistryOwnershipStatus : std::uint8_t
{
    Success,
    NoChange,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    CapacityExceeded,
    RefCountExhausted,
    OwnershipMismatch,
    UnsafeFailure,
};

struct RegistryOwnershipName final
{
    std::uint32_t stringId = 0;
    const char *canonicalName = nullptr;
};

struct RegistryOwnershipBulkResult final
{
    std::uint32_t addedCount = 0;
    std::uint32_t unchangedCount = 0;
};

// Narrow admission for creating a coordinator boundary. Only the eventual
// registry/lifecycle façade may mint production values; tests receive an
// explicitly guarded fixture factory. This keeps arbitrary code with a raw
// controller pointer/key from borrowing its retained transaction.
class RegistryOwnershipCoordinatorAdmission final
{
public:
    RegistryOwnershipCoordinatorAdmission() = delete;
    ~RegistryOwnershipCoordinatorAdmission() noexcept = default;
    RegistryOwnershipCoordinatorAdmission(
        const RegistryOwnershipCoordinatorAdmission &) = delete;
    RegistryOwnershipCoordinatorAdmission &operator=(
        const RegistryOwnershipCoordinatorAdmission &) = delete;
    RegistryOwnershipCoordinatorAdmission(
        RegistryOwnershipCoordinatorAdmission &&) = delete;
    RegistryOwnershipCoordinatorAdmission &operator=(
        RegistryOwnershipCoordinatorAdmission &&) = delete;

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
    [[nodiscard]] static RegistryOwnershipCoordinatorAdmission
    ForTesting() noexcept;
#endif

private:
    friend class RegistryOwnershipCoordinatorFacade;
    friend RegistryOwnershipStatus
    TryBeginStandaloneRegistryOwnershipCoordinator(
        const RegistryOwnershipCoordinatorAdmission &,
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
        const RegistryOwnershipCoordinatorAdmission &,
        RegistryOwnershipCoordinator *,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &) noexcept;

    RegistryOwnershipCoordinatorAdmission(
        std::uint64_t seal,
        std::uint64_t sealMirror) noexcept;
    [[nodiscard]] bool authenticates() const noexcept;

    std::uint64_t seal_ = 0;
    std::uint64_t sealMirror_ = 0;
};
RUNTIME_SIZE(RegistryOwnershipCoordinatorAdmission, 0x10, 0x10);
static_assert(std::is_standard_layout_v<RegistryOwnershipCoordinatorAdmission>);
static_assert(
    !std::is_default_constructible_v<RegistryOwnershipCoordinatorAdmission>);
static_assert(
    !std::is_copy_constructible_v<RegistryOwnershipCoordinatorAdmission>);

#ifdef KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING
struct RegistryOwnershipCoordinatorTestAccess;
#endif

class alignas(8) RegistryOwnershipCoordinator final
{
public:
    RegistryOwnershipCoordinator() noexcept = default;
    ~RegistryOwnershipCoordinator() noexcept;

    RegistryOwnershipCoordinator(const RegistryOwnershipCoordinator &) =
        delete;
    RegistryOwnershipCoordinator &operator=(
        const RegistryOwnershipCoordinator &) = delete;
    RegistryOwnershipCoordinator(RegistryOwnershipCoordinator &&) = delete;
    RegistryOwnershipCoordinator &operator=(
        RegistryOwnershipCoordinator &&) = delete;

    [[nodiscard]] RegistryOwnershipCoordinatorPhase phase() const noexcept;
    [[nodiscard]] RegistryOwnershipCoordinatorMode mode() const noexcept;
    [[nodiscard]] std::uint64_t serial() const noexcept;
    [[nodiscard]] bool hashLockRetained() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    [[nodiscard]] bool isEmptyCanonical() const noexcept;

private:
    friend class RegistryOwnershipCoordinatorFacade;
    friend RegistryOwnershipStatus
    TryBeginStandaloneRegistryOwnershipCoordinator(
        const RegistryOwnershipCoordinatorAdmission &,
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
        const RegistryOwnershipCoordinatorAdmission &,
        RegistryOwnershipCoordinator *,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend RegistryOwnershipStatus FinishRegistryOwnershipCoordinator(
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryRegistryAddDatabaseUser4(
        RegistryOwnershipCoordinator *, std::uint32_t) noexcept;
    friend RegistryOwnershipStatus TryRegistryAddDatabaseUsers4(
        RegistryOwnershipCoordinator *,
        const std::uint32_t *,
        std::uint32_t,
        RegistryOwnershipBulkResult *) noexcept;
    friend RegistryOwnershipStatus TryRegistryInternBoundedName(
        RegistryOwnershipCoordinator *,
        const char *,
        std::uint32_t,
        RegistryOwnershipName *) noexcept;
    friend RegistryOwnershipStatus TryRegistryReAddRetainedDefaultName(
        RegistryOwnershipCoordinator *, const char *) noexcept;
    friend RegistryOwnershipStatus TryRegistryReAddRetainedDefaultNames(
        RegistryOwnershipCoordinator *,
        const char *const *,
        std::uint32_t,
        RegistryOwnershipBulkResult *) noexcept;
    friend RegistryOwnershipStatus TryRegistryTransferDatabaseUsers4To8(
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
        RegistryOwnershipCoordinator *) noexcept;
#ifdef KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING
    friend struct RegistryOwnershipCoordinatorTestAccess;
#endif

    [[nodiscard]] bool canonicalAfterStandaloneBegin() const noexcept;
    [[nodiscard]] bool representationConsistent() const noexcept;
    [[nodiscard]] bool ownsRegistryBoundary() const noexcept;
    [[nodiscard]] bool authenticatesOuterTransaction() const noexcept;
    [[nodiscard]] static RegistryOwnershipStatus beginRegistered(
        RegistryOwnershipCoordinator *,
        RegistryOwnershipCoordinatorMode,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t,
        std::uint8_t,
        std::uint8_t) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus tryBorrowController(
        RegistryOwnershipCoordinator *,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &,
        RegistryOwnershipCoordinatorMode) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus beginOperation(
        RegistryOwnershipCoordinator *) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus finishOperation(
        RegistryOwnershipCoordinator *,
        RegistryOwnershipStatus,
        bool) noexcept;
    [[nodiscard]] script_string::RegistryOwnershipAdmission
    makeOperationAdmission() const noexcept;
    void publishPhase(RegistryOwnershipCoordinatorPhase) noexcept;
    void publishHashLockRetained(bool) noexcept;
    void poisonBoundary() noexcept;
    void resetAfterFinish() noexcept;

    script_string::OwnershipBatch operationBatch_{};
    script_string_transaction::ScriptStringTransactionToken
        standaloneTransaction_{};
    std::uintptr_t borrowedControllerAddress_ = 0;
    std::uintptr_t borrowedControllerAddressMirror_ = 0;
    zone_load::ZoneLoadContextKey borrowedKey_{};
    zone_load::ZoneLoadContextKey borrowedKeyMirror_{};
    std::uint64_t serial_ = 0;
    std::uint64_t serialMirror_ = 0;
    std::uint32_t borrowedTransactionSerial_ = 0;
    std::uint32_t borrowedTransactionSerialMirror_ = 0;
    std::uint32_t standaloneTransactionSerial_ = 0;
    std::uint32_t standaloneTransactionSerialMirror_ = 0;
    RegistryOwnershipCoordinatorPhase phase_ =
        RegistryOwnershipCoordinatorPhase::Empty;
    RegistryOwnershipCoordinatorPhase phaseMirror_ =
        RegistryOwnershipCoordinatorPhase::Empty;
    bool hashLockRetained_ = false;
    bool hashLockRetainedMirror_ = false;
    // These two packed receipts preserve the established ABI while retaining
    // the mode, callback purpose, and exact nonzero callback-window witness.
    // All access goes through the coordinator's validated packing helpers.
    std::uint16_t modeCallbackReceipt_ = 0;
    std::uint16_t modeCallbackReceiptMirror_ = 0;
};

RUNTIME_SIZE(RegistryOwnershipCoordinator, 0x78, 0x80);
static_assert(std::is_standard_layout_v<RegistryOwnershipCoordinator>);
static_assert(!std::is_trivially_destructible_v<RegistryOwnershipCoordinator>);

[[nodiscard]] RegistryOwnershipStatus
TryBeginStandaloneRegistryOwnershipCoordinator(
    const RegistryOwnershipCoordinatorAdmission &admission,
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
    const RegistryOwnershipCoordinatorAdmission &admission,
    RegistryOwnershipCoordinator *coordinator,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *controller,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept;

[[nodiscard]] RegistryOwnershipStatus FinishRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryAddDatabaseUser4(
    RegistryOwnershipCoordinator *coordinator,
    std::uint32_t stringId) noexcept;

// Processes a caller-precollected bounded ID span in one callback-free batch.
// The complete unique set is authenticated and preflighted before the first
// mutation, avoiding one complete string-table validation per generated mark
// leaf while preserving all-or-no-change recoverable failures.
[[nodiscard]] RegistryOwnershipStatus TryRegistryAddDatabaseUsers4(
    RegistryOwnershipCoordinator *coordinator,
    const std::uint32_t *stringIds,
    std::uint32_t count,
    RegistryOwnershipBulkResult *outResult) noexcept;

// Publishes *outName only when both the fixed user-4 intern and complete
// reverse-order operation close succeed. Every other status leaves it exact.
[[nodiscard]] RegistryOwnershipStatus TryRegistryInternBoundedName(
    RegistryOwnershipCoordinator *coordinator,
    const char *bytes,
    std::uint32_t byteCount,
    RegistryOwnershipName *outName) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryReAddRetainedDefaultName(
    RegistryOwnershipCoordinator *coordinator,
    const char *retainedCanonicalName) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryReAddRetainedDefaultNames(
    RegistryOwnershipCoordinator *coordinator,
    const char *const *retainedCanonicalNames,
    std::uint32_t count,
    RegistryOwnershipBulkResult *outResult) noexcept;

[[nodiscard]] RegistryOwnershipStatus
TryRegistryTransferDatabaseUsers4To8(
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
    RegistryOwnershipCoordinator *coordinator) noexcept;

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
struct RegistryOwnershipCoordinatorTestAccess final
{
    [[nodiscard]] static RegistryOwnershipStatus
    TryBorrowActiveRuntimeCallback(
        RegistryOwnershipCoordinator *coordinator,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *controller,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    static script_string::OwnershipBatch &OperationBatch(
        RegistryOwnershipCoordinator *coordinator) noexcept;
    static void SetCallbackReceipt(
        RegistryOwnershipCoordinator *coordinator,
        std::uint8_t callbackPurpose,
        std::uint8_t callbackPurposeMirror,
        std::uint8_t callbackWindowWitness,
        std::uint8_t callbackWindowWitnessMirror) noexcept;
    static void SetRepresentationMirrors(
        RegistryOwnershipCoordinator *coordinator,
        std::uint64_t serialMirror,
        std::uintptr_t borrowedAddressMirror,
        std::uint32_t borrowedTransactionSerialMirror,
        std::uint32_t standaloneTransactionSerialMirror,
        const zone_load::ZoneLoadContextKey &borrowedKeyMirror,
        std::uint8_t phaseMirror,
        std::uint8_t modeMirror,
        bool hashMirror) noexcept;
    static void ResetStorageForTesting(
        RegistryOwnershipCoordinator *coordinator) noexcept;
};

void SetRegistryOwnershipCoordinatorBoundaryForTesting(
    std::uintptr_t address,
    std::uint64_t serial,
    std::uintptr_t addressMirror,
    std::uint64_t serialMirror,
    std::uint8_t state,
    std::uint8_t stateMirror) noexcept;
void SetNextRegistryOwnershipCoordinatorSerialForTesting(
    std::uint64_t serial) noexcept;
void SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting(
    std::uint64_t nextSerialMirror,
    std::uintptr_t borrowedAddress,
    std::uintptr_t borrowedAddressMirror,
    std::uint32_t borrowedTransactionSerial,
    std::uint32_t borrowedTransactionSerialMirror,
    std::uint8_t phase,
    std::uint8_t phaseMirror,
    std::uint8_t mode,
    std::uint8_t modeMirror,
    bool hashRetained,
    bool hashRetainedMirror,
    std::uint8_t callbackWindowWitness,
    std::uint8_t callbackWindowWitnessMirror) noexcept;
#endif

} // namespace db::registry_ownership

#pragma once

#include <database/db_script_string_transaction.h>
#include <database/db_zone_load_context.h>
#include <database/db_zone_script_string_ownership.h>
#include <script/scr_string_transaction.h>
#include <universal/kisak_abi.h>

#include <cstdint>
#include <type_traits>

namespace db::registry_ownership
{
// Production-neutral lock-order boundary for the five legacy registry string
// operations that still use raw user-4/user-8 APIs. There are deliberately no
// production callers yet.
//
// A coordinator either owns a standalone script-string transaction or borrows
// the exact active transaction of one ZoneScriptStringOwnershipController.
// Every operation then acquires, and releases in reverse order:
//
//   script-string transaction -> db_hashCritSect -> OwnershipBatch
//
// OwnershipBatch retains SCRIPT_STRING -> MEMORY_TREE internally. Operations
// are fixed, bounded, callback-free, allocation-free at the coordination
// layer, and expose no arbitrary user mask. Storage must remain alive and on
// the admitting thread until Finish succeeds. Destruction never unlocks an
// active boundary; abandoning live storage intentionally fails closed.
enum class RegistryOwnershipCoordinatorPhase : std::uint8_t
{
    Empty,
    Ready,
    Operating,
    Finishing,
    UnsafeFailure,
};

enum class RegistryOwnershipCoordinatorMode : std::uint8_t
{
    None,
    Standalone,
    BorrowedZoneController,
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

#ifdef KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING
struct RegistryOwnershipCoordinatorTestAccess;
#endif

class alignas(8) RegistryOwnershipCoordinator final
{
public:
    RegistryOwnershipCoordinator() noexcept = default;
    ~RegistryOwnershipCoordinator() noexcept = default;

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
    friend RegistryOwnershipStatus
    TryBeginStandaloneRegistryOwnershipCoordinator(
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
        RegistryOwnershipCoordinator *,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend RegistryOwnershipStatus FinishRegistryOwnershipCoordinator(
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryRegistryAddDatabaseUser4(
        RegistryOwnershipCoordinator *, std::uint32_t) noexcept;
    friend RegistryOwnershipStatus TryRegistryInternBoundedName(
        RegistryOwnershipCoordinator *,
        const char *,
        std::uint32_t,
        int,
        RegistryOwnershipName *) noexcept;
    friend RegistryOwnershipStatus TryRegistryReAddRetainedDefaultName(
        RegistryOwnershipCoordinator *, const char *) noexcept;
    friend RegistryOwnershipStatus TryRegistryTransferDatabaseUsers4To8(
        RegistryOwnershipCoordinator *) noexcept;
    friend RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
        RegistryOwnershipCoordinator *) noexcept;
#ifdef KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING
    friend struct RegistryOwnershipCoordinatorTestAccess;
#endif

    [[nodiscard]] bool canonicalAfterStandaloneBegin() const noexcept;
    [[nodiscard]] bool ownsRegistryBoundary() const noexcept;
    [[nodiscard]] bool authenticatesOuterTransaction() const noexcept;
    [[nodiscard]] static RegistryOwnershipStatus beginRegistered(
        RegistryOwnershipCoordinator *,
        RegistryOwnershipCoordinatorMode,
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus beginOperation(
        RegistryOwnershipCoordinator *) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus finishOperation(
        RegistryOwnershipCoordinator *,
        RegistryOwnershipStatus,
        bool) noexcept;
    void poisonBoundary() noexcept;
    void resetAfterFinish() noexcept;

    script_string::OwnershipBatch operationBatch_{};
    script_string_transaction::ScriptStringTransactionToken
        standaloneTransaction_{};
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *borrowedController_ = nullptr;
    zone_load::ZoneLoadContextKey borrowedKey_{};
    std::uint64_t serial_ = 0;
    std::uint32_t borrowedTransactionSerial_ = 0;
    RegistryOwnershipCoordinatorPhase phase_ =
        RegistryOwnershipCoordinatorPhase::Empty;
    RegistryOwnershipCoordinatorMode mode_ =
        RegistryOwnershipCoordinatorMode::None;
    bool hashLockRetained_ = false;
    std::uint8_t reserved_ = 0;
};

RUNTIME_SIZE(RegistryOwnershipCoordinator, 0x50, 0x50);
static_assert(std::is_standard_layout_v<RegistryOwnershipCoordinator>);
static_assert(!std::is_trivially_destructible_v<RegistryOwnershipCoordinator>);

[[nodiscard]] RegistryOwnershipStatus
TryBeginStandaloneRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *coordinator,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController *controller,
    const zone_load::ZoneLoadContextKey &expectedKey) noexcept;

[[nodiscard]] RegistryOwnershipStatus FinishRegistryOwnershipCoordinator(
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryAddDatabaseUser4(
    RegistryOwnershipCoordinator *coordinator,
    std::uint32_t stringId) noexcept;

// Publishes *outName only when both the fixed user-4 intern and complete
// reverse-order operation close succeed. Every other status leaves it exact.
[[nodiscard]] RegistryOwnershipStatus TryRegistryInternBoundedName(
    RegistryOwnershipCoordinator *coordinator,
    const char *bytes,
    std::uint32_t byteCount,
    int type,
    RegistryOwnershipName *outName) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryReAddRetainedDefaultName(
    RegistryOwnershipCoordinator *coordinator,
    const char *retainedCanonicalName) noexcept;

[[nodiscard]] RegistryOwnershipStatus
TryRegistryTransferDatabaseUsers4To8(
    RegistryOwnershipCoordinator *coordinator) noexcept;

[[nodiscard]] RegistryOwnershipStatus TryRegistryShutdownDatabaseUser8(
    RegistryOwnershipCoordinator *coordinator) noexcept;

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
struct RegistryOwnershipCoordinatorTestAccess final
{
    static script_string::OwnershipBatch &OperationBatch(
        RegistryOwnershipCoordinator *coordinator) noexcept;
    static void SetReserved(
        RegistryOwnershipCoordinator *coordinator,
        std::uint8_t reserved) noexcept;
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
#endif

} // namespace db::registry_ownership

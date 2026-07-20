#pragma once

#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace db::zone_pending_copy
{
class PendingCopyAdmissionReceipt;
class PendingCopyLedger;
} // namespace db::zone_pending_copy

namespace db::zone_runtime::detail
{
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const zone_pending_copy::PendingCopyAdmissionReceipt &receipt) noexcept;
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const zone_pending_copy::PendingCopyLedger &ledger) noexcept;
} // namespace db::zone_runtime::detail

namespace db::zone_pending_copy
{
inline constexpr std::uint32_t kPendingCopyRecordCapacity = 2048;
inline constexpr std::uint32_t kPendingCopyGenerationCapacity = 8;
inline constexpr std::uint32_t kFirstAssetEntryIndex = 1;
inline constexpr std::uint32_t kLastAssetEntryIndex = 0x7FFF;
inline constexpr std::uint32_t kInvalidGenerationIndex = UINT32_MAX;

enum class PendingCopyStatus : std::uint8_t
{
    Success,
    AlreadyComplete,
    Retry,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    InvalidRecord,
    CapacityExceeded,
    GenerationCapacityExceeded,
    GenerationExhausted,
    UnsafeFailure,
};

enum class PendingCopyAdmissionPhase : std::uint8_t
{
    Pristine,
    Collecting,
    Prepared,
    Admitting,
    Admitted,
    Drained,
    Discarded,
    UnsafeFailure,
};

enum class PendingCopyDrainCallbackStatus : std::uint8_t
{
    Success,
    Retry,
    UnsafeFailure,
};

struct PendingCopyRecord final
{
    zone_load::ZoneLoadContextKey key{};
    std::uint32_t assetEntryIndex = 0;
    std::uint32_t reserved = 0;

    [[nodiscard]] constexpr bool operator==(
        const PendingCopyRecord &) const noexcept = default;
};

RUNTIME_SIZE(PendingCopyRecord, 0x18, 0x18);

struct PendingCopyAdmissionCompletion final
{
    void *context = nullptr;

    // Preparation binds this exact identity while the generation is still
    // Loading. The unchecked finalizer invokes it exactly once, after the
    // outer lifecycle has published Live and finalized its journal. It must
    // be a no-fail, report-free ensure-postcondition operation and must not
    // throw, longjmp, reenter, or mutate this ledger, receipt, or lifecycle
    // slot. A nonlocal exit leaves callback-active authority fail-closed.
    void (*complete)(void *context) noexcept = nullptr;
};

struct PendingCopyDrainCallback final
{
    void *context = nullptr;

    // Receives a by-value record so no pointer into the stable ledger can
    // escape. Success means the ordered copy is durably consumed. Retry must
    // leave it safe to present the same record again. UnsafeFailure poisons
    // the ledger. The callback must not throw, longjmp, report, or reenter.
    PendingCopyDrainCallbackStatus (*consume)(
        void *context,
        PendingCopyRecord record) noexcept = nullptr;
};

class PendingCopyLedger;

#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING
struct PendingCopyLedgerTestAccess;
#endif

// Stable, allocation-independent authority for one generation's ordered
// pending-copy records and eventual Live admission. The receipt must live
// outside zone PMem, at one address, until it reaches Drained or Discarded.
// It is deliberately noncopyable/nonmovable and performs no destructor
// cleanup. Reuse requires an explicit exact terminal reset.
class alignas(8) PendingCopyAdmissionReceipt final
{
public:
    PendingCopyAdmissionReceipt() noexcept;
    ~PendingCopyAdmissionReceipt() noexcept;

    PendingCopyAdmissionReceipt(
        const PendingCopyAdmissionReceipt &) = delete;
    PendingCopyAdmissionReceipt &operator=(
        const PendingCopyAdmissionReceipt &) = delete;
    PendingCopyAdmissionReceipt(PendingCopyAdmissionReceipt &&) = delete;
    PendingCopyAdmissionReceipt &operator=(
        PendingCopyAdmissionReceipt &&) = delete;

    [[nodiscard]] PendingCopyAdmissionPhase phase() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextSlot *lifecycle()
        const noexcept;
    [[nodiscard]] std::uint32_t recordCount() const noexcept;
    [[nodiscard]] bool canonical() const noexcept;

private:
    friend class PendingCopyLedger;
    friend PendingCopyStatus TryInitializePendingCopyLedger(
        PendingCopyLedger *) noexcept;
    friend PendingCopyStatus TryBeginPendingCopyAdmission(
        PendingCopyLedger *,
        PendingCopyAdmissionReceipt *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend PendingCopyStatus TryAppendPendingCopyRecord(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t) noexcept;
    friend PendingCopyStatus TryReadPendingCopyRecord(
        const PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t,
        PendingCopyRecord *) noexcept;
    friend PendingCopyStatus TryPreparePendingCopyAdmission(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        const PendingCopyAdmissionCompletion &) noexcept;
    friend void FinalizePreparedPendingCopyAdmission(
        PendingCopyAdmissionReceipt &) noexcept;
    friend PendingCopyStatus TryDiscardPendingCopyAdmission(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend PendingCopyStatus TryFinishPendingCopyDrain(
        PendingCopyLedger *) noexcept;
    friend PendingCopyStatus TryResetPendingCopyAdmissionReceipt(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    // Exact const-only friendship for passive runtime-table authentication.
    friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt(
        const PendingCopyAdmissionReceipt &receipt) noexcept;
#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING
    friend struct PendingCopyLedgerTestAccess;
#endif

    [[nodiscard]] bool isPristine() const noexcept;
    [[nodiscard]] bool isCanonical() const noexcept;
    void setPhase(PendingCopyAdmissionPhase phase) noexcept;
    void reset() noexcept;

    zone_load::ZoneLoadContextKey key_{};
    PendingCopyLedger *ledger_ = nullptr;
    zone_load::ZoneLoadContextSlot *lifecycle_ = nullptr;
    const PendingCopyAdmissionReceipt *self_ = nullptr;
    void *completionContext_ = nullptr;
    void (*completion_)(void *) noexcept = nullptr;
    std::uint64_t generationSerial_ = 0;
    std::uint32_t generationIndex_ = kInvalidGenerationIndex;
    PendingCopyAdmissionPhase phase_ = PendingCopyAdmissionPhase::Pristine;
    std::uint8_t phaseWitness_ = 0;
    std::uint8_t reserved_[2]{};
};

// Process-lifetime ordered storage shared by at most eight queued exact
// generations. It stores asset-pool indices rather than native pointers. The
// caller must externally serialize initialization, every operation, callback,
// accessor, and receipt reset. There is no internal cross-thread locking.
class alignas(8) PendingCopyLedger final
{
public:
    PendingCopyLedger() noexcept;
    ~PendingCopyLedger() noexcept;

    PendingCopyLedger(const PendingCopyLedger &) = delete;
    PendingCopyLedger &operator=(const PendingCopyLedger &) = delete;
    PendingCopyLedger(PendingCopyLedger &&) = delete;
    PendingCopyLedger &operator=(PendingCopyLedger &&) = delete;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::uint32_t recordCount() const noexcept;
    [[nodiscard]] std::uint32_t generationCount() const noexcept;
    [[nodiscard]] bool canonical() const noexcept;

private:
    enum class Phase : std::uint8_t
    {
        Pristine,
        Ready,
        AdmissionPrepared,
        Draining,
        UnsafeFailure,
    };

    enum class GenerationPhase : std::uint8_t
    {
        Empty,
        Collecting,
        Prepared,
        Admitted,
    };

    struct GenerationDescriptor final
    {
        zone_load::ZoneLoadContextKey key{};
        std::uint64_t serial = 0;
        PendingCopyAdmissionReceipt *receipt = nullptr;
        std::uint32_t firstRecord = 0;
        std::uint32_t recordCount = 0;
        GenerationPhase phase = GenerationPhase::Empty;
        std::uint8_t reserved[3]{};
    };

    // Keep the 64-bit serial ahead of the pointer so MSVC x86 does not insert
    // an otherwise ABI-specific four-byte hole before it. The descriptor is
    // private runtime state and is never serialized.
    RUNTIME_SIZE(GenerationDescriptor, 0x28, 0x30);

    friend class PendingCopyAdmissionReceipt;
    friend PendingCopyStatus TryInitializePendingCopyLedger(
        PendingCopyLedger *) noexcept;
    friend PendingCopyStatus TryBeginPendingCopyAdmission(
        PendingCopyLedger *,
        PendingCopyAdmissionReceipt *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend PendingCopyStatus TryAppendPendingCopyRecord(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t) noexcept;
    friend PendingCopyStatus TryReadPendingCopyRecord(
        const PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t,
        PendingCopyRecord *) noexcept;
    friend PendingCopyStatus TryPreparePendingCopyAdmission(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &,
        const PendingCopyAdmissionCompletion &) noexcept;
    friend void FinalizePreparedPendingCopyAdmission(
        PendingCopyAdmissionReceipt &) noexcept;
    friend PendingCopyStatus TryDiscardPendingCopyAdmission(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend PendingCopyStatus TryBeginPendingCopyDrain(
        PendingCopyLedger *) noexcept;
    friend PendingCopyStatus TryDrainNextPendingCopy(
        PendingCopyLedger *,
        const PendingCopyDrainCallback &) noexcept;
    friend PendingCopyStatus TryFinishPendingCopyDrain(
        PendingCopyLedger *) noexcept;
    friend PendingCopyStatus TryResetPendingCopyAdmissionReceipt(
        PendingCopyAdmissionReceipt *,
        const zone_load::ZoneLoadContextKey &) noexcept;
    // Exact const-only friendship for passive runtime-table authentication.
    friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt(
        const PendingCopyLedger &ledger) noexcept;
#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING
    friend struct PendingCopyLedgerTestAccess;
#endif

    [[nodiscard]] bool isPristine() const noexcept;
    [[nodiscard]] bool hasCanonicalHeader() const noexcept;
    [[nodiscard]] bool isCanonical() const noexcept;
    [[nodiscard]] bool descriptorMatchesReceipt(
        std::uint32_t index) const noexcept;
    void setPhase(Phase phase) noexcept;
    void poison() noexcept;

    std::array<PendingCopyRecord, kPendingCopyRecordCapacity> records_{};
    std::array<GenerationDescriptor, kPendingCopyGenerationCapacity>
        generations_{};
    const PendingCopyLedger *self_ = nullptr;
    std::uint64_t nextGenerationSerial_ = 0;
    std::uint32_t recordCount_ = 0;
    std::uint32_t generationCount_ = 0;
    std::uint32_t drainCursor_ = 0;
    Phase phase_ = Phase::Pristine;
    std::uint8_t phaseWitness_ = 0;
    std::uint8_t callbackActive_ = 0;
    std::uint8_t reserved_ = 0;
};

#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING
struct PendingCopyLedgerTestAccess final
{
    static void SetNextGenerationSerial(
        PendingCopyLedger *ledger,
        std::uint64_t serial) noexcept;
    static void SetRecordCount(
        PendingCopyLedger *ledger,
        std::uint32_t count) noexcept;
    static void SetGenerationCount(
        PendingCopyLedger *ledger,
        std::uint32_t count) noexcept;
    static void SetDrainCursor(
        PendingCopyLedger *ledger,
        std::uint32_t cursor) noexcept;
    static void SetLedgerPhaseWitness(
        PendingCopyLedger *ledger,
        std::uint8_t witness) noexcept;
    static void SetLedgerReserved(
        PendingCopyLedger *ledger,
        std::uint8_t reserved) noexcept;
    static void SetRecord(
        PendingCopyLedger *ledger,
        std::uint32_t index,
        const PendingCopyRecord &record) noexcept;
    static void SetDescriptorFirstRecord(
        PendingCopyLedger *ledger,
        std::uint32_t index,
        std::uint32_t firstRecord) noexcept;
    static void SetDescriptorRecordCount(
        PendingCopyLedger *ledger,
        std::uint32_t index,
        std::uint32_t recordCount) noexcept;
    static void SetDescriptorSerial(
        PendingCopyLedger *ledger,
        std::uint32_t index,
        std::uint64_t serial) noexcept;
    static void SetDescriptorReserved(
        PendingCopyLedger *ledger,
        std::uint32_t index,
        std::uint8_t reserved) noexcept;
    static void SetReceiptGenerationIndex(
        PendingCopyAdmissionReceipt *receipt,
        std::uint32_t index) noexcept;
    static void SetReceiptGenerationSerial(
        PendingCopyAdmissionReceipt *receipt,
        std::uint64_t serial) noexcept;
    static void SetReceiptPhaseWitness(
        PendingCopyAdmissionReceipt *receipt,
        std::uint8_t witness) noexcept;
    static void SetReceiptReserved(
        PendingCopyAdmissionReceipt *receipt,
        std::uint8_t reserved) noexcept;
};
#endif

[[nodiscard]] PendingCopyStatus TryInitializePendingCopyLedger(
    PendingCopyLedger *ledger) noexcept;

// Appends one exact Loading generation after every earlier retained
// generation has reached Admitted. Repeating an exact active begin is
// idempotent; exact terminal receipts return AlreadyComplete before the
// ledger or lifecycle is inspected.
[[nodiscard]] PendingCopyStatus TryBeginPendingCopyAdmission(
    PendingCopyLedger *ledger,
    PendingCopyAdmissionReceipt *receipt,
    zone_load::ZoneLoadContextSlot *lifecycle,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Adds one asset-pool index to the current generation. Indices are restricted
// to the fixed 1..32767 pool range. The production adapter must additionally
// prove the originating pointer's range/alignment and exact zone slot.
[[nodiscard]] PendingCopyStatus TryAppendPendingCopyRecord(
    PendingCopyAdmissionReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key,
    std::uint32_t assetEntryIndex) noexcept;

// Copies one record by value. No ledger pointer or mutable storage escapes,
// and output remains unchanged on every non-Success result.
[[nodiscard]] PendingCopyStatus TryReadPendingCopyRecord(
    const PendingCopyAdmissionReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key,
    std::uint32_t ordinal,
    PendingCopyRecord *outRecord) noexcept;

// Performs the complete fallible ledger/receipt/lifecycle preflight while the
// exact generation is Loading. An exact retry requires the same completion
// identity. Success freezes structural mutation until finalization/discard.
[[nodiscard]] PendingCopyStatus TryPreparePendingCopyAdmission(
    PendingCopyAdmissionReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key,
    const PendingCopyAdmissionCompletion &completion) noexcept;

// Valid only after a matching successful prepare and after the outer owner
// has invalidated delayed-load state, ended PMem, published Live, and
// finalized its journal. It is deliberately status-free: it publishes
// Admitted, invokes the prebound completion exactly once, then publishes the
// terminal receipt. An exact reentrant/repeated call does not replay
// completion.
void FinalizePreparedPendingCopyAdmission(
    PendingCopyAdmissionReceipt &receipt) noexcept;

// Stable-compacts one exact Collecting, Prepared, or Admitted generation.
// Surviving records retain global order and stale terminal retries return
// before consulting a potentially newer ledger generation.
[[nodiscard]] PendingCopyStatus TryDiscardPendingCopyAdmission(
    PendingCopyAdmissionReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Freezes a complete all-Admitted ledger for ordered one-record consumption.
[[nodiscard]] PendingCopyStatus TryBeginPendingCopyDrain(
    PendingCopyLedger *ledger) noexcept;

// Presents the next record by value. Retry preserves the cursor; Success
// advances it exactly once; unsafe or unknown callback results poison state.
[[nodiscard]] PendingCopyStatus TryDrainNextPendingCopy(
    PendingCopyLedger *ledger,
    const PendingCopyDrainCallback &callback) noexcept;

// Requires every record consumed, marks retained receipts Drained, scrubs all
// descriptor/record storage, and returns the ledger to Ready.
[[nodiscard]] PendingCopyStatus TryFinishPendingCopyDrain(
    PendingCopyLedger *ledger) noexcept;

// Resets only an exact Drained or Discarded terminal receipt after its
// descriptor has left the ledger. No live/prepared authority can be rebound.
[[nodiscard]] PendingCopyStatus TryResetPendingCopyAdmissionReceipt(
    PendingCopyAdmissionReceipt *receipt,
    const zone_load::ZoneLoadContextKey &key) noexcept;
} // namespace db::zone_pending_copy

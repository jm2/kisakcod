#include <database/db_zone_runtime_table.h>
#include <database/db_zone_memory.h>
#include <database/db_zone_runtime_storage_fx_bridge.h>

#include <limits>
#include <new>

namespace db::zone_runtime
{
enum class TableState : std::uint32_t
{
    Uninitialized,
    Initialized,
    Poisoned,
};

enum class SharedState : std::uint32_t
{
    Pristine,
    Ready,
    Draining,
};

namespace
{
inline constexpr std::uint8_t kGenerationBindingWitnessMask = 0xA7u;

[[nodiscard]] constexpr bool IsKnownSetupStage(
    const ZoneRuntimeSetupStage stage) noexcept
{
    return stage >= ZoneRuntimeSetupStage::Passive
        && stage <= ZoneRuntimeSetupStage::AllocationEnded;
}

[[nodiscard]] constexpr bool IsKnownExecutionMode(
    const ZoneRuntimeExecutionMode mode) noexcept
{
    return mode >= ZoneRuntimeExecutionMode::Passive
        && mode <= ZoneRuntimeExecutionMode::Terminal;
}

[[nodiscard]] constexpr std::uint8_t BindingWitness(
    const ZoneRuntimeSetupStage stage,
    const ZoneRuntimeExecutionMode mode) noexcept
{
    return static_cast<std::uint8_t>(stage)
        ^ static_cast<std::uint8_t>(mode)
        ^ kGenerationBindingWitnessMask;
}

[[nodiscard]] constexpr bool CallbacksAreComplete(
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    return callbacks.ensureUnreachable
        && callbacks.performExternalCleanup
        && callbacks.completePendingAdmission
        && callbacks.admitLive;
}

[[nodiscard]] constexpr bool PendingDrainCallbackIsEmpty(
    const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept
{
    return callback.context == nullptr && callback.consume == nullptr;
}

[[nodiscard]] constexpr bool PendingDrainCallbacksMatch(
    const zone_pending_copy::PendingCopyDrainCallback &left,
    const zone_pending_copy::PendingCopyDrainCallback &right) noexcept
{
    return left.context == right.context
        && left.consume == right.consume;
}

[[nodiscard]] bool AddressRangesAreDisjoint(
    const void *const leftStorage,
    const std::size_t leftSize,
    const void *const rightStorage,
    const std::size_t rightSize) noexcept
{
    if (!leftStorage || leftSize == 0 || !rightStorage || rightSize == 0)
        return false;

    const std::uintptr_t left =
        reinterpret_cast<std::uintptr_t>(leftStorage);
    const std::uintptr_t right =
        reinterpret_cast<std::uintptr_t>(rightStorage);
    const std::uintptr_t maximum =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (leftSize > maximum - left || rightSize > maximum - right)
        return false;
    const std::uintptr_t leftEnd = left + leftSize;
    const std::uintptr_t rightEnd = right + rightSize;
    return leftEnd <= right || rightEnd <= left;
}

[[nodiscard]] bool WritableOutputIsSeparated(
    const ZoneRuntimeTable *const table,
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment,
    const zone_load::ZoneLoadContextKey *const inputKey = nullptr) noexcept
{
    if (!table || !output || outputSize == 0 || outputAlignment == 0
        || reinterpret_cast<std::uintptr_t>(output) % outputAlignment != 0
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), output, outputSize))
    {
        return false;
    }
    return !inputKey
        || AddressRangesAreDisjoint(
            inputKey, sizeof(*inputKey), output, outputSize);
}

[[nodiscard]] bool WritableOutputIsSeparatedFromRetainedRuntime(
    const zone_stream_ownership::ActiveZoneStreamBinding &binding,
    const SharedState sharedState,
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment) noexcept
{
    if (!zone_stream_ownership::AuthenticateZoneStreamOutputSpan(
            binding, output, outputSize, outputAlignment))
        return false;

    if (sharedState == SharedState::Pristine)
        return true;
    if (sharedState != SharedState::Ready
        && sharedState != SharedState::Draining)
        return false;
    return pmem_runtime::StorageIsOutsideManagedMemory(
        output, outputSize);
}

[[nodiscard]] ZoneRuntimeTableStatus
AuthenticateRetainedLegacyCallbackContext(
    const ZoneRuntimeTable *const table,
    const void *const context) noexcept
{
    if (!context)
        return ZoneRuntimeTableStatus::Success;
    if (!table
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), context, 1))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    switch (pmem_runtime::TryClassifyStorageIsolation(context, 1))
    {
    case pmem_runtime::StorageIsolationStatus::Success:
    case pmem_runtime::StorageIsolationStatus::Uninitialized:
        return ZoneRuntimeTableStatus::Success;
    case pmem_runtime::StorageIsolationStatus::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case pmem_runtime::StorageIsolationStatus::InvalidArgument:
    case pmem_runtime::StorageIsolationStatus::ProtectedStorageOverlap:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case pmem_runtime::StorageIsolationStatus::Poisoned:
    case pmem_runtime::StorageIsolationStatus::CorruptState:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}
} // namespace

ZoneRuntimeSetupStage ZoneRuntimeGenerationBinding::setupStage()
    const noexcept
{
    return setupStage_;
}

ZoneRuntimeExecutionMode ZoneRuntimeGenerationBinding::executionMode()
    const noexcept
{
    return executionMode_;
}

bool ZoneRuntimeGenerationBinding::isPristine() const noexcept
{
    return key_ == zone_load::ZoneLoadContextKey{}
        && table_ == nullptr && lifecycle_ == nullptr && self_ == nullptr
        && callbacks_.context == nullptr
        && callbacks_.ensureUnreachable == nullptr
        && callbacks_.performExternalCleanup == nullptr
        && callbacks_.completePendingAdmission == nullptr
        && callbacks_.admitLive == nullptr
        && placementJournal_ == nullptr && placementEntries_ == nullptr
        && placementCapacity_ == 0 && placementExpectedCount_ == 0
        && allocationType_ == 0
        && setupStage_ == ZoneRuntimeSetupStage::Passive
        && executionMode_ == ZoneRuntimeExecutionMode::Passive
        && callbackMarker_ == CallbackMarker::Idle
        && witness_ == 0 && reserved_ == 0;
}

bool ZoneRuntimeGenerationBinding::canonicalFor(
    const ZoneRuntimeTable *const table,
    const zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &key) const noexcept
{
    if (self_ != this || table_ != table || !table
        || lifecycle_ != lifecycle || !lifecycle
        || key_ != key || !static_cast<bool>(key)
        || lifecycle->slotIndex() != key.slot
        || lifecycle->generation() != key.generation
        || !CallbacksAreComplete(callbacks_)
        || !IsKnownSetupStage(setupStage_)
        || !IsKnownExecutionMode(executionMode_)
        || setupStage_ < ZoneRuntimeSetupStage::CallbacksBound
        || executionMode_ == ZoneRuntimeExecutionMode::Passive
        || callbackMarker_ > CallbackMarker::ActiveRegistryBorrow
        || witness_ != BindingWitness(setupStage_, executionMode_)
        || reserved_ != 0)
    {
        return false;
    }
    if (setupStage_ >= ZoneRuntimeSetupStage::AllocationBegun
        && allocationType_ >= 2u)
    {
        return false;
    }
    if (setupStage_ < ZoneRuntimeSetupStage::AllocationBegun
        && allocationType_ != 0)
    {
        return false;
    }
    if (setupStage_ < ZoneRuntimeSetupStage::StorageBound)
    {
        if (placementJournal_ != nullptr || placementEntries_ != nullptr
            || placementCapacity_ != 0 || placementExpectedCount_ != 0)
        {
            return false;
        }
    }
    else if (!placementJournal_
        || placementExpectedCount_ > placementCapacity_
        || (placementCapacity_ == 0) != (placementEntries_ == nullptr)
        || (setupStage_ < ZoneRuntimeSetupStage::ScriptStringsBegun
            && placementExpectedCount_ != 0))
    {
        return false;
    }
    if ((executionMode_ == ZoneRuntimeExecutionMode::Admitting
            || executionMode_ == ZoneRuntimeExecutionMode::Live
            || executionMode_ == ZoneRuntimeExecutionMode::Unloading)
        && setupStage_ != ZoneRuntimeSetupStage::AllocationEnded)
    {
        return false;
    }
    return true;
}

bool ZoneRuntimeGenerationBinding::callbacksMatch(
    const ZoneRuntimeGenerationCallbacks &callbacks) const noexcept
{
    return callbacks_.context == callbacks.context
        && callbacks_.ensureUnreachable == callbacks.ensureUnreachable
        && callbacks_.performExternalCleanup
            == callbacks.performExternalCleanup
        && callbacks_.completePendingAdmission
            == callbacks.completePendingAdmission
        && callbacks_.admitLive == callbacks.admitLive;
}

void ZoneRuntimeGenerationBinding::bind(
    ZoneRuntimeTable *const table,
    zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    key_ = key;
    table_ = table;
    lifecycle_ = lifecycle;
    self_ = this;
    callbacks_ = callbacks;
    placementJournal_ = nullptr;
    placementEntries_ = nullptr;
    placementCapacity_ = 0;
    placementExpectedCount_ = 0;
    allocationType_ = 0;
    setupStage_ = ZoneRuntimeSetupStage::CallbacksBound;
    executionMode_ = ZoneRuntimeExecutionMode::Loading;
    callbackMarker_ = CallbackMarker::Idle;
    reserved_ = 0;
    witness_ = BindingWitness(setupStage_, executionMode_);
}

void ZoneRuntimeGenerationBinding::setSetupStage(
    const ZoneRuntimeSetupStage stage) noexcept
{
    setupStage_ = stage;
    witness_ = BindingWitness(setupStage_, executionMode_);
}

void ZoneRuntimeGenerationBinding::setExecutionMode(
    const ZoneRuntimeExecutionMode mode) noexcept
{
    executionMode_ = mode;
    witness_ = BindingWitness(setupStage_, executionMode_);
}

void ZoneRuntimeGenerationBinding::reset() noexcept
{
    key_ = {};
    table_ = nullptr;
    lifecycle_ = nullptr;
    self_ = nullptr;
    callbacks_ = {};
    placementJournal_ = nullptr;
    placementEntries_ = nullptr;
    placementCapacity_ = 0;
    placementExpectedCount_ = 0;
    allocationType_ = 0;
    setupStage_ = ZoneRuntimeSetupStage::Passive;
    executionMode_ = ZoneRuntimeExecutionMode::Passive;
    callbackMarker_ = CallbackMarker::Idle;
    witness_ = 0;
    reserved_ = 0;
}

// Each component exposes only narrow const, report-free authenticators. The
// table-wide variants include the complete shared singleton/topology state;
// none can begin, bind, reset, or otherwise operate any authority.
namespace detail
{
bool IsPristineRuntimeReceipt(
    const physical_memory::AllocationReceipt &receipt) noexcept
{
    return receipt.isPristine();
}

bool IsPristineRuntimeReceipt(
    const zone_stream_ownership::ZoneStreamGenerationReceipt &receipt)
    noexcept
{
    return receipt.isPristine();
}

bool IsPristineRuntimeReceipt(
    const zone_pending_copy::PendingCopyAdmissionReceipt &receipt) noexcept
{
    return receipt.isPristine();
}

bool IsPristineRuntimeReceipt(
    const zone_runtime_storage::ZoneRuntimeStorageBinding &binding) noexcept
{
    return binding.isPristine();
}

bool IsPristineRuntimeReceipt(
    const zone_stream_ownership::ActiveZoneStreamBinding &binding) noexcept
{
    return zone_stream_ownership::AuthenticatePassiveZoneStreamSingleton(binding);
}

bool IsPristineRuntimeReceipt(
    const zone_pending_copy::PendingCopyLedger &ledger) noexcept
{
    return zone_pending_copy::AuthenticatePassivePendingCopyLedger(ledger);
}

bool IsPristineRuntimeReceipt(
    const ZoneRuntimeReceiptCapsule &capsule) noexcept
{
    return capsule.isPristine();
}

bool EntryReceiptsArePristine(const ZoneRuntimeEntry &entry) noexcept
{
    return IsPristineRuntimeReceipt(entry.receiptCapsule_);
}
} // namespace detail

bool ZoneRuntimeReceiptCapsule::isPristine() const noexcept
{
    return detail::IsPristineRuntimeReceipt(allocationReceipt_)
        && detail::IsPristineRuntimeReceipt(streamGenerationReceipt_)
        && detail::IsPristineRuntimeReceipt(pendingCopyAdmissionReceipt_)
        && detail::IsPristineRuntimeReceipt(storageBinding_);
}

namespace
{
[[nodiscard]] constexpr bool IsNullKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return key.generation == 0
        && key.slot == zone_load::kInvalidZoneLoadSlot
        && key.reserved == 0;
}

[[nodiscard]] bool IsEmptyOwnership(
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController &ownership) noexcept
{
    return ownership.isEmptyCanonical();
}

[[nodiscard]] bool IsPristineLifecycle(
    const zone_load::ZoneLoadContextSlot &lifecycle,
    const bool initialized,
    const std::uint32_t physicalSlot) noexcept
{
    return lifecycle.canonical()
        && lifecycle.initialized() == initialized
        && lifecycle.slotIndex()
            == (initialized
                    ? physicalSlot
                    : zone_load::kInvalidZoneLoadSlot)
        && lifecycle.generation() == 0
        && lifecycle.phase() == zone_load::ZoneLoadContextPhase::Empty
        && lifecycle.terminalKind()
            == zone_load::ZoneLoadTerminalKind::None
        && lifecycle.nextCleanupOperation()
            == zone_load::ZoneLoadCleanupOperation::
                CancelLoadInputAndInflate
        && !lifecycle.cleanupActive()
        && !lifecycle.cleanupPoisoned();
}

[[nodiscard]] bool IsPristineEntry(
    const ZoneRuntimeEntry &entry,
    const bool initialized,
    const std::uint32_t physicalSlot) noexcept
{
    return IsNullKey(entry.key())
        && IsPristineLifecycle(
            entry.lifecycle(),
            initialized,
            physicalSlot)
        && IsEmptyOwnership(entry.scriptStringOwnership())
        && detail::EntryReceiptsArePristine(entry)
        && entry.executionMode() == ZoneRuntimeExecutionMode::Passive
        && entry.setupStage() == ZoneRuntimeSetupStage::Passive
        && entry.generationBindingPristine();
}

[[nodiscard]] constexpr bool IsKnownOwnershipPhase(
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase phase) noexcept
{
    using Phase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    switch (phase)
    {
    case Phase::Empty:
    case Phase::Staging:
    case Phase::Sealed:
    case Phase::Transferring:
    case Phase::Transferred:
    case Phase::CommitReady:
    case Phase::Unpublishing:
    case Phase::UnpublishingCallback:
    case Phase::RollingBack:
    case Phase::OwnershipRolledBack:
    case Phase::Cleaning:
    case Phase::Admitting:
    case Phase::Live:
    case Phase::Abandoned:
    case Phase::UnsafeFailure:
    case Phase::Unloading:
    case Phase::UnloadingCallback:
    case Phase::Unloaded:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsOwnershipCallbackPhase(
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase phase) noexcept
{
    using Phase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    return phase == Phase::UnpublishingCallback
        || phase == Phase::Cleaning
        || phase == Phase::Admitting
        || phase == Phase::UnloadingCallback;
}

[[nodiscard]] ZoneRuntimeTableStatus ValidateLifecycleAndOwnership(
    const ZoneRuntimeEntry &entry,
    const std::uint32_t physicalSlot) noexcept
{
    const auto &lifecycle = entry.lifecycle();
    const auto &key = entry.key();
    const auto &ownership = entry.scriptStringOwnership();
    if (!lifecycle.canonical()
        || !lifecycle.initialized()
        || lifecycle.slotIndex() != physicalSlot)
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (lifecycle.cleanupActive())
        return ZoneRuntimeTableStatus::Busy;
    if (lifecycle.cleanupPoisoned())
        return ZoneRuntimeTableStatus::UnsafeFailure;

    if (IsNullKey(key))
    {
        // No public transition erases a durable key after a generation has
        // been issued.  Accepting a null key with a hidden nonzero generation
        // would discard the table's ABA evidence and silently advance from a
        // corrupt representation on the next claim.
        return IsPristineLifecycle(lifecycle, true, physicalSlot)
                && IsEmptyOwnership(ownership)
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (!static_cast<bool>(key)
        || key.slot != physicalSlot
        || lifecycle.generation() != key.generation)
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const auto ownershipPhase = ownership.phase();
    if (!IsKnownOwnershipPhase(ownershipPhase)
        || ownership.poisoned())
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (ownershipPhase
        == zone_script_string_ownership::
            ZoneScriptStringOwnershipPhase::Empty)
    {
        if (!IsEmptyOwnership(ownership))
            return ZoneRuntimeTableStatus::UnsafeFailure;
        if (lifecycle.phase() == zone_load::ZoneLoadContextPhase::Loading)
        {
            return lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::None
                ? ZoneRuntimeTableStatus::Success
                : ZoneRuntimeTableStatus::UnsafeFailure;
        }
        if (lifecycle.phase()
            == zone_load::ZoneLoadContextPhase::Abandoning)
        {
            return lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Abandoned
                ? ZoneRuntimeTableStatus::Success
                : ZoneRuntimeTableStatus::UnsafeFailure;
        }
        if (lifecycle.phase() == zone_load::ZoneLoadContextPhase::Empty)
        {
            return lifecycle.terminalKind()
                        == zone_load::ZoneLoadTerminalKind::Abandoned
                    || lifecycle.terminalKind()
                        == zone_load::ZoneLoadTerminalKind::Unloaded
                ? ZoneRuntimeTableStatus::Success
                : ZoneRuntimeTableStatus::UnsafeFailure;
        }
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (ownership.key() != key
        || !ownership.canonicalForBinding(&lifecycle, key))
        return ZoneRuntimeTableStatus::UnsafeFailure;

    using OwnershipPhase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    if (ownershipPhase == OwnershipPhase::Live)
    {
        return !ownership.serializerRetained()
                && lifecycle.phase()
                    == zone_load::ZoneLoadContextPhase::Live
                && lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::None
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (ownershipPhase == OwnershipPhase::Abandoned)
    {
        return !ownership.serializerRetained()
                && lifecycle.phase()
                    == zone_load::ZoneLoadContextPhase::Empty
                && lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Abandoned
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (ownershipPhase == OwnershipPhase::Unloaded)
    {
        return !ownership.serializerRetained()
                && lifecycle.phase()
                    == zone_load::ZoneLoadContextPhase::Empty
                && lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Unloaded
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (!ownership.serializerRetained())
        return ZoneRuntimeTableStatus::UnsafeFailure;

    bool lifecycleMatches = false;
    switch (ownershipPhase)
    {
    case OwnershipPhase::Staging:
    case OwnershipPhase::Sealed:
    case OwnershipPhase::Transferring:
    case OwnershipPhase::Transferred:
    case OwnershipPhase::CommitReady:
    case OwnershipPhase::Unpublishing:
    case OwnershipPhase::UnpublishingCallback:
        lifecycleMatches =
            lifecycle.phase() == zone_load::ZoneLoadContextPhase::Loading
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::None;
        break;
    case OwnershipPhase::RollingBack:
    case OwnershipPhase::OwnershipRolledBack:
    case OwnershipPhase::Cleaning:
        lifecycleMatches =
            lifecycle.phase()
                == zone_load::ZoneLoadContextPhase::Abandoning
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::Abandoned;
        break;
    case OwnershipPhase::Admitting:
        lifecycleMatches =
            lifecycle.phase() == zone_load::ZoneLoadContextPhase::Live
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::None;
        break;
    case OwnershipPhase::Unloading:
    case OwnershipPhase::UnloadingCallback:
        lifecycleMatches =
            (lifecycle.phase() == zone_load::ZoneLoadContextPhase::Live
                && lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::None)
            || (lifecycle.phase()
                    == zone_load::ZoneLoadContextPhase::Abandoning
                && lifecycle.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Unloaded);
        break;
    case OwnershipPhase::Empty:
    case OwnershipPhase::Live:
    case OwnershipPhase::Abandoned:
    case OwnershipPhase::UnsafeFailure:
    case OwnershipPhase::Unloaded:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (!lifecycleMatches)
        return ZoneRuntimeTableStatus::UnsafeFailure;
    return IsOwnershipCallbackPhase(ownershipPhase)
        ? ZoneRuntimeTableStatus::Busy
        : ZoneRuntimeTableStatus::Success;
}

[[nodiscard]] bool HasKnownState(const std::uint32_t state) noexcept
{
    switch (static_cast<TableState>(state))
    {
    case TableState::Uninitialized:
    case TableState::Initialized:
    case TableState::Poisoned:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool HasKnownSharedState(const std::uint32_t state) noexcept
{
    switch (static_cast<SharedState>(state))
    {
    case SharedState::Pristine:
    case SharedState::Ready:
    case SharedState::Draining:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus ValidateUsableSlot(
    const std::uint32_t physicalSlot) noexcept
{
    return zone_slots::IsUsableZoneSlot(physicalSlot)
        ? ZoneRuntimeTableStatus::Success
        : ZoneRuntimeTableStatus::InvalidSlot;
}

[[nodiscard]] ZoneRuntimeTableStatus MapLifecycleStatus(
    const zone_load::ZoneLoadContextStatus status) noexcept
{
    using LifecycleStatus = zone_load::ZoneLoadContextStatus;
    switch (status)
    {
    case LifecycleStatus::Success:
        return ZoneRuntimeTableStatus::Success;
    case LifecycleStatus::Retry:
        return ZoneRuntimeTableStatus::Retry;
    case LifecycleStatus::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case LifecycleStatus::InvalidArgument:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case LifecycleStatus::InvalidState:
        return ZoneRuntimeTableStatus::InvalidState;
    case LifecycleStatus::InvalidKey:
        return ZoneRuntimeTableStatus::InvalidKey;
    case LifecycleStatus::StaleKey:
        return ZoneRuntimeTableStatus::StaleKey;
    case LifecycleStatus::InvalidPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case LifecycleStatus::GenerationExhausted:
        return ZoneRuntimeTableStatus::GenerationExhausted;
    case LifecycleStatus::UnsafeFailure:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapOwnershipStatus(
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipStatus status) noexcept
{
    using OwnershipStatus = zone_script_string_ownership::
        ZoneScriptStringOwnershipStatus;
    switch (status)
    {
    case OwnershipStatus::Success:
        return ZoneRuntimeTableStatus::Success;
    case OwnershipStatus::Retry:
        return ZoneRuntimeTableStatus::Retry;
    case OwnershipStatus::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case OwnershipStatus::InvalidArgument:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case OwnershipStatus::InvalidState:
        return ZoneRuntimeTableStatus::InvalidState;
    case OwnershipStatus::InvalidKey:
        return ZoneRuntimeTableStatus::InvalidKey;
    case OwnershipStatus::StaleKey:
        return ZoneRuntimeTableStatus::StaleKey;
    case OwnershipStatus::InvalidPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case OwnershipStatus::Rejected:
        return ZoneRuntimeTableStatus::Rejected;
    case OwnershipStatus::CountMismatch:
        return ZoneRuntimeTableStatus::CountMismatch;
    case OwnershipStatus::CapacityExceeded:
        return ZoneRuntimeTableStatus::CapacityExceeded;
    case OwnershipStatus::UnsafeFailure:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapLiveUnloadOwnershipStatus(
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipStatus status) noexcept
{
    using OwnershipStatus = zone_script_string_ownership::
        ZoneScriptStringOwnershipStatus;
    switch (status)
    {
    case OwnershipStatus::Success:
    case OwnershipStatus::Retry:
    case OwnershipStatus::Busy:
    case OwnershipStatus::InvalidArgument:
    case OwnershipStatus::InvalidState:
    case OwnershipStatus::InvalidKey:
    case OwnershipStatus::StaleKey:
    case OwnershipStatus::InvalidPhase:
        return MapOwnershipStatus(status);
    // These are journal/loading-only results.  Accepting one from the
    // terminal Live-unload controller would silently broaden that operation's
    // contract if a future implementation accidentally leaked such a value.
    case OwnershipStatus::Rejected:
    case OwnershipStatus::CountMismatch:
    case OwnershipStatus::CapacityExceeded:
    case OwnershipStatus::UnsafeFailure:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapAllocationReceiptStatus(
    const pmem_runtime::AllocationReceiptStatus status) noexcept
{
    using Status = pmem_runtime::AllocationReceiptStatus;
    switch (status)
    {
    case Status::Success:
    case Status::AlreadyComplete:
        return ZoneRuntimeTableStatus::Success;
    case Status::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case Status::InvalidArgument:
    case Status::InvalidAllocationType:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::NotReady:
        return ZoneRuntimeTableStatus::InvalidState;
    case Status::CapacityExceeded:
        return ZoneRuntimeTableStatus::CapacityExceeded;
    case Status::WrongPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case Status::ReceiptInUse:
    case Status::ReceiptMismatch:
    case Status::CorruptState:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapAllocationStatus(
    const pmem_runtime::AllocationStatus status) noexcept
{
    using Status = pmem_runtime::AllocationStatus;
    switch (status)
    {
    case Status::Success:
        return ZoneRuntimeTableStatus::Success;
    case Status::InvalidRequest:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::NotReady:
        return ZoneRuntimeTableStatus::InvalidState;
    case Status::ScopeInactive:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case Status::Exhausted:
        return ZoneRuntimeTableStatus::CapacityExceeded;
    case Status::CorruptState:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapAllocationRangeStatus(
    const pmem_runtime::AllocationReceiptStatus status) noexcept
{
    using Status = pmem_runtime::AllocationReceiptStatus;
    switch (status)
    {
    case Status::Success:
        return ZoneRuntimeTableStatus::Success;
    case Status::InvalidArgument:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::Busy:
    case Status::AlreadyComplete:
    case Status::NotReady:
    case Status::InvalidAllocationType:
    case Status::CapacityExceeded:
    case Status::ReceiptInUse:
    case Status::ReceiptMismatch:
    case Status::WrongPhase:
    case Status::CorruptState:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapStorageStatus(
    const zone_runtime_storage::ZoneRuntimeStorageStatus status) noexcept
{
    using Status = zone_runtime_storage::ZoneRuntimeStorageStatus;
    switch (status)
    {
    case Status::Success:
    case Status::AlreadyComplete:
        return ZoneRuntimeTableStatus::Success;
    case Status::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case Status::InvalidArgument:
    case Status::InvalidCount:
    case Status::InvalidPlan:
    case Status::MisalignedStorage:
    case Status::SizeOverflow:
    case Status::InsufficientCapacity:
    case Status::OverlappingStorage:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::InvalidPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case Status::InvalidBinding:
    case Status::ArenaFailed:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapStreamStatus(
    const zone_stream_ownership::ZoneStreamOwnershipStatus status) noexcept
{
    using Status = zone_stream_ownership::ZoneStreamOwnershipStatus;
    switch (status)
    {
    case Status::Success:
    case Status::AlreadyComplete:
        return ZoneRuntimeTableStatus::Success;
    case Status::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case Status::InvalidArgument:
    case Status::InvalidLayout:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::InvalidState:
        return ZoneRuntimeTableStatus::InvalidState;
    case Status::InvalidKey:
        return ZoneRuntimeTableStatus::InvalidKey;
    case Status::StaleKey:
        return ZoneRuntimeTableStatus::StaleKey;
    case Status::InvalidPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case Status::GenerationExhausted:
        return ZoneRuntimeTableStatus::GenerationExhausted;
    case Status::UnsafeFailure:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

[[nodiscard]] ZoneRuntimeTableStatus MapPendingStatus(
    const zone_pending_copy::PendingCopyStatus status) noexcept
{
    using Status = zone_pending_copy::PendingCopyStatus;
    switch (status)
    {
    case Status::Success:
    case Status::AlreadyComplete:
        return ZoneRuntimeTableStatus::Success;
    case Status::Retry:
        return ZoneRuntimeTableStatus::Retry;
    case Status::Busy:
        return ZoneRuntimeTableStatus::Busy;
    case Status::InvalidArgument:
    case Status::InvalidRecord:
        return ZoneRuntimeTableStatus::InvalidArgument;
    case Status::InvalidState:
        return ZoneRuntimeTableStatus::InvalidState;
    case Status::InvalidKey:
        return ZoneRuntimeTableStatus::InvalidKey;
    case Status::StaleKey:
        return ZoneRuntimeTableStatus::StaleKey;
    case Status::InvalidPhase:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case Status::CapacityExceeded:
    case Status::GenerationCapacityExceeded:
        return ZoneRuntimeTableStatus::CapacityExceeded;
    case Status::GenerationExhausted:
        return ZoneRuntimeTableStatus::GenerationExhausted;
    case Status::UnsafeFailure:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
struct PendingCopyReadTestHook final
{
    ZoneRuntimeTableTestAccess::PendingCopyReadHookStage stage =
        ZoneRuntimeTableTestAccess::PendingCopyReadHookStage::BeforeLowerRead;
    void *context = nullptr;
    ZoneRuntimeTableTestAccess::PendingCopyReadHook callback = nullptr;
    bool armed = false;
};

PendingCopyReadTestHook g_pendingCopyReadTestHook{};

void InvokePendingCopyReadTestHook(
    ZoneRuntimeTableTestAccess::PendingCopyReadHookStage stage,
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!g_pendingCopyReadTestHook.armed
        || g_pendingCopyReadTestHook.stage != stage)
    {
        return;
    }
    const PendingCopyReadTestHook hook = g_pendingCopyReadTestHook;
    g_pendingCopyReadTestHook = {};
    if (hook.callback)
        hook.callback(hook.context, table, physicalSlot, key);
}
#endif

ZoneRuntimeTable g_productionZoneRuntimeTable{};
} // namespace

ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactEntry(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus bindingStatus =
        validateEntryBinding(physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
        return bindingStatus;
    const ZoneRuntimeEntry &entry = entries_[physicalSlot];
    if (entry.key() != key
        || entry.lifecycle().slotIndex() != physicalSlot
        || entry.lifecycle().generation() != key.generation)
    {
        return ZoneRuntimeTableStatus::StaleKey;
    }
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactMutableEntry(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeEntry **const outEntry) noexcept
{
    if (!outEntry)
        return ZoneRuntimeTableStatus::InvalidArgument;
    const ZoneRuntimeTableStatus tableStatus = validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const ZoneRuntimeTableStatus slotStatus =
        ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = entries_[physicalSlot];
    const ZoneRuntimeTableStatus authentication =
        authenticateExactEntry(physicalSlot, key);
    if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)
        poison();
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    *outEntry = &entry;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus
ZoneRuntimeTable::authenticateExactRegistryLifecycleCallback(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    using Marker = ZoneRuntimeGenerationBinding::CallbackMarker;
    const ZoneRuntimeTableStatus status =
        authenticateExactLifecycleCallbackMarker(
            physicalSlot, key, Marker::ActiveRegistryBorrow);
    if (status == ZoneRuntimeTableStatus::Success)
    {
        entries_[physicalSlot].generationBinding_.callbackMarker_ =
            Marker::ActiveNoRegistry;
    }
    return status;
}

ZoneRuntimeTableStatus
ZoneRuntimeTable::restoreExactRegistryLifecycleCallback(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    using Marker = ZoneRuntimeGenerationBinding::CallbackMarker;
    const ZoneRuntimeTableStatus status =
        authenticateExactLifecycleCallbackMarker(
            physicalSlot, key, Marker::ActiveNoRegistry);
    if (status == ZoneRuntimeTableStatus::Success)
    {
        entries_[physicalSlot].generationBinding_.callbackMarker_ =
            Marker::ActiveRegistryBorrow;
    }
    return status;
}

ZoneRuntimeTableStatus
ZoneRuntimeTable::authenticateExactLifecycleCallbackMarker(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationBinding::CallbackMarker expectedMarker)
    noexcept
{
    using Marker = ZoneRuntimeGenerationBinding::CallbackMarker;
    if (expectedMarker == Marker::Idle
        || expectedMarker > Marker::ActiveRegistryBorrow)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (!HasKnownState(state_) || !HasKnownSharedState(sharedState_))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const TableState state = static_cast<TableState>(state_);
    if (state == TableState::Poisoned)
        return ZoneRuntimeTableStatus::UnsafeFailure;
    if (state != TableState::Initialized)
        return ZoneRuntimeTableStatus::InvalidState;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const ZoneRuntimeTableStatus slotStatus =
        ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = entries_[physicalSlot];
    if (entry.key_ != key)
        return ZoneRuntimeTableStatus::StaleKey;
    if (entry.lifecycle_.slotIndex() != physicalSlot
        || entry.lifecycle_.generation() != key.generation)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    // Whole-table authentication is expected to classify the synchronous
    // callback as Busy.  It still validates every stable binding and both
    // process-wide authorities before the callback-specific exception below.
    const ZoneRuntimeTableStatus tableStatus = validateSharedComposition();
    if (tableStatus == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (tableStatus != ZoneRuntimeTableStatus::Success
        && tableStatus != ZoneRuntimeTableStatus::Busy)
    {
        return tableStatus;
    }

    ZoneRuntimeGenerationBinding &binding = entry.generationBinding_;
    if (!binding.canonicalFor(this, &entry.lifecycle_, key))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    // A process-wide serialized table can have only the one callback that is
    // currently on this call stack.  A second marker is a torn or forged
    // authority, even when that marker would independently decode as known.
    std::size_t activeMarkerCount = 0;
    const ZoneRuntimeEntry *activeMarkerEntry = nullptr;
    for (const ZoneRuntimeEntry &candidate : entries_)
    {
        const Marker marker = candidate.generationBinding_.callbackMarker_;
        if (marker > Marker::ActiveRegistryBorrow)
        {
            poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        if (marker == Marker::Idle)
            continue;
        ++activeMarkerCount;
        if (activeMarkerCount != 1)
        {
            poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        activeMarkerEntry = &candidate;
    }

    // A valid callback for another generation is ordinary contention, not a
    // structural contradiction.  Refuse the wrong target without consuming
    // or poisoning the sole callback's authority.
    if (activeMarkerEntry && activeMarkerEntry != &entry)
        return ZoneRuntimeTableStatus::Busy;

    if (binding.callbackMarker_ == Marker::Idle
        || binding.callbackMarker_ != expectedMarker)
    {
        // Idle includes an ordinary exact generation. A different known
        // marker is either non-borrowable callback work or the opposite side
        // of the private consume/restore transition. Neither publishes the
        // requested authority.
        return ZoneRuntimeTableStatus::Busy;
    }
    if (activeMarkerCount != 1
        || tableStatus != ZoneRuntimeTableStatus::Busy
        || !entry.scriptStringOwnership_.canonicalForBinding(
            &entry.lifecycle_, key)
        || !exactRegistryLifecycleCallbackPhaseMatches(entry))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    // This helper only authenticates the marker. Mutable registry admission
    // and its exact Busy rollback perform their one-shot transition after it
    // returns; pending-copy inspection deliberately leaves the borrow intact.
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyRead(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeEntry **const outEntry) noexcept
{
    using Marker = ZoneRuntimeGenerationBinding::CallbackMarker;
    using PendingPhase = zone_pending_copy::PendingCopyAdmissionPhase;

    if (!outEntry)
        return ZoneRuntimeTableStatus::InvalidArgument;
    if (!HasKnownState(state_) || !HasKnownSharedState(sharedState_))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const TableState state = static_cast<TableState>(state_);
    if (state == TableState::Poisoned)
        return ZoneRuntimeTableStatus::UnsafeFailure;
    if (state != TableState::Initialized)
        return ZoneRuntimeTableStatus::InvalidState;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const ZoneRuntimeTableStatus slotStatus =
        ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = entries_[physicalSlot];
    if (entry.key_ != key)
        return ZoneRuntimeTableStatus::StaleKey;
    if (entry.lifecycle_.slotIndex() != physicalSlot
        || entry.lifecycle_.generation() != key.generation)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const Marker marker = entry.generationBinding_.callbackMarker_;
    std::size_t activeMarkerCount = 0;
    const ZoneRuntimeEntry *activeMarkerEntry = nullptr;
    for (const ZoneRuntimeEntry &candidate : entries_)
    {
        const Marker candidateMarker =
            candidate.generationBinding_.callbackMarker_;
        if (candidateMarker > Marker::ActiveRegistryBorrow)
        {
            poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        if (candidateMarker == Marker::Idle)
            continue;
        ++activeMarkerCount;
        if (activeMarkerCount != 1)
        {
            poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        activeMarkerEntry = &candidate;
    }
    if (marker != Marker::Idle)
    {
        const ZoneRuntimeTableStatus callbackStatus =
            authenticateExactLifecycleCallbackMarker(
                physicalSlot, key, Marker::ActiveRegistryBorrow);
        if (callbackStatus != ZoneRuntimeTableStatus::Success)
            return callbackStatus;
        // ActiveRegistryBorrow is the sole callback window that may inspect
        // this exact generation. The authentication above is non-mutating.
    }
    else
    {
        const ZoneRuntimeTableStatus tableStatus =
            validateInitializedHeader();
        if (activeMarkerEntry)
        {
            if (tableStatus != ZoneRuntimeTableStatus::Busy
                || activeMarkerEntry == &entry)
            {
                poison();
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
            return ZoneRuntimeTableStatus::Busy;
        }
        if (tableStatus != ZoneRuntimeTableStatus::Success)
            return tableStatus;
        const ZoneRuntimeTableStatus entryStatus =
            authenticateExactEntry(physicalSlot, key);
        if (entryStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            poison();
        if (entryStatus != ZoneRuntimeTableStatus::Success)
            return entryStatus;
    }

    if (static_cast<SharedState>(sharedState_) == SharedState::Draining)
        return ZoneRuntimeTableStatus::Busy;
    if (entry.setupStage() < ZoneRuntimeSetupStage::PendingCopyBegun)
        return ZoneRuntimeTableStatus::InvalidPhase;

    const auto *const receipt = pendingCopyAdmissionReceipt(&entry);
    if (!receipt)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    switch (receipt->phase())
    {
    case PendingPhase::Collecting:
    case PendingPhase::Prepared:
    case PendingPhase::Admitted:
    case PendingPhase::Drained:
    case PendingPhase::Discarded:
        *outEntry = &entry;
        return ZoneRuntimeTableStatus::Success;
    case PendingPhase::Admitting:
        return ZoneRuntimeTableStatus::Busy;
    case PendingPhase::Pristine:
        return ZoneRuntimeTableStatus::InvalidPhase;
    case PendingPhase::UnsafeFailure:
    default:
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
}

ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactPendingCopyOutput(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const void *const output,
    const std::size_t outputSize,
    const std::size_t outputAlignment) noexcept
{
    const ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        authenticateExactPendingCopyRead(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry || !zone_slots::IsUsableZoneSlot(physicalSlot)
        || entry != &entries_[physicalSlot])
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return WritableOutputIsSeparatedFromRetainedRuntime(
               activeZoneStreamBinding_,
               static_cast<SharedState>(sharedState_),
               output,
               outputSize,
               outputAlignment)
        ? ZoneRuntimeTableStatus::Success
        : ZoneRuntimeTableStatus::InvalidArgument;
}

zone_load::ZoneLoadContextSlot *ZoneRuntimeTable::mutableLifecycle(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->lifecycle_ : nullptr;
}

zone_script_string_ownership::ZoneScriptStringOwnershipController *
ZoneRuntimeTable::mutableScriptStringOwnership(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->scriptStringOwnership_ : nullptr;
}

ZoneRuntimeReceiptCapsule *ZoneRuntimeTable::mutableReceiptCapsule(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->receiptCapsule_ : nullptr;
}

ZoneRuntimeGenerationBinding *ZoneRuntimeTable::mutableGenerationBinding(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->generationBinding_ : nullptr;
}

physical_memory::AllocationReceipt *ZoneRuntimeTable::mutableAllocationReceipt(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->receiptCapsule_.allocationReceipt_ : nullptr;
}

zone_stream_ownership::ZoneStreamGenerationReceipt *
ZoneRuntimeTable::mutableStreamGenerationReceipt(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry
        ? &entry->receiptCapsule_.streamGenerationReceipt_
        : nullptr;
}

zone_pending_copy::PendingCopyAdmissionReceipt *
ZoneRuntimeTable::mutablePendingCopyAdmissionReceipt(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry
        ? &entry->receiptCapsule_.pendingCopyAdmissionReceipt_
        : nullptr;
}

const zone_pending_copy::PendingCopyAdmissionReceipt *
ZoneRuntimeTable::pendingCopyAdmissionReceipt(
    const ZoneRuntimeEntry *const entry) noexcept
{
    return entry
        ? &entry->receiptCapsule_.pendingCopyAdmissionReceipt_
        : nullptr;
}

zone_runtime_storage::ZoneRuntimeStorageBinding *
ZoneRuntimeTable::mutableStorageBinding(
    ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? &entry->receiptCapsule_.storageBinding_ : nullptr;
}

std::uint32_t ZoneRuntimeTable::generationAllocationType(
    const ZoneRuntimeEntry *const entry) noexcept
{
    return entry ? entry->generationBinding_.allocationType_ : 0;
}

bool ZoneRuntimeTable::generationCallbacksMatch(
    const ZoneRuntimeEntry *const entry,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    return entry
        && entry->generationBinding_.callbacksMatch(callbacks);
}

bool ZoneRuntimeTable::generationPlacementMatches(
    const ZoneRuntimeEntry *const entry,
    const script_string_journal::ScriptStringJournal *const journal,
    const script_string_journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t capacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!entry)
        return false;
    const ZoneRuntimeGenerationBinding &binding =
        entry->generationBinding_;
    return binding.placementJournal_ == journal
        && binding.placementEntries_ == storage
        && binding.placementCapacity_ == capacity
        && binding.placementExpectedCount_ == 0
        && expectedCount <= capacity;
}

bool ZoneRuntimeTable::exactRegistryLifecycleCallbackPhaseMatches(
    const ZoneRuntimeEntry &entry) noexcept
{
    using LifecyclePhase = zone_load::ZoneLoadContextPhase;
    using OwnershipPhase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    const ZoneRuntimeGenerationBinding &binding =
        entry.generationBinding_;
    const zone_load::ZoneLoadContextSlot &lifecycle = entry.lifecycle_;
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipController &controller =
            entry.scriptStringOwnership_;
    if (!binding.canonicalFor(
            binding.table_, &lifecycle, entry.key_)
        || !controller.canonicalForBinding(&lifecycle, entry.key_)
        || !controller.serializerRetained() || controller.poisoned())
    {
        return false;
    }

    switch (controller.phase())
    {
    case OwnershipPhase::UnpublishingCallback:
        return binding.executionMode_
                == ZoneRuntimeExecutionMode::Abandoning
            && binding.setupStage_
                >= ZoneRuntimeSetupStage::ScriptStringsBegun
            && lifecycle.phase() == LifecyclePhase::Loading
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::None
            && !lifecycle.cleanupActive();
    case OwnershipPhase::Cleaning:
        return binding.executionMode_
                == ZoneRuntimeExecutionMode::Abandoning
            && binding.setupStage_
                >= ZoneRuntimeSetupStage::ScriptStringsBegun
            && lifecycle.phase() == LifecyclePhase::Abandoning
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::Abandoned
            && lifecycle.cleanupActive();
    case OwnershipPhase::Admitting:
        return binding.executionMode_
                == ZoneRuntimeExecutionMode::Admitting
            && binding.setupStage_
                == ZoneRuntimeSetupStage::AllocationEnded
            && lifecycle.phase() == LifecyclePhase::Live
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::None
            && !lifecycle.cleanupActive();
    case OwnershipPhase::UnloadingCallback:
        return binding.executionMode_
                == ZoneRuntimeExecutionMode::Unloading
            && binding.setupStage_
                == ZoneRuntimeSetupStage::AllocationEnded
            && lifecycle.phase() == LifecyclePhase::Abandoning
            && lifecycle.terminalKind()
                == zone_load::ZoneLoadTerminalKind::Unloaded
            && lifecycle.cleanupActive();
    default:
        return false;
    }
}

void ZoneRuntimeTable::bindGeneration(
    ZoneRuntimeTable *const table,
    ZoneRuntimeEntry *const entry,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    if (entry)
    {
        entry->generationBinding_.bind(
            table, &entry->lifecycle_, key, callbacks);
    }
}

void ZoneRuntimeTable::setGenerationAllocation(
    ZoneRuntimeEntry *const entry,
    const std::uint32_t allocationType) noexcept
{
    if (!entry)
        return;
    entry->generationBinding_.allocationType_ = allocationType;
    entry->generationBinding_.setSetupStage(
        ZoneRuntimeSetupStage::AllocationBegun);
}

void ZoneRuntimeTable::setGenerationSetupStage(
    ZoneRuntimeEntry *const entry,
    const ZoneRuntimeSetupStage stage) noexcept
{
    if (entry)
        entry->generationBinding_.setSetupStage(stage);
}

void ZoneRuntimeTable::setGenerationExecutionMode(
    ZoneRuntimeEntry *const entry,
    const ZoneRuntimeExecutionMode mode) noexcept
{
    if (entry)
        entry->generationBinding_.setExecutionMode(mode);
}

void ZoneRuntimeTable::retainGenerationPlacement(
    ZoneRuntimeEntry *const entry,
    const script_string_journal::ScriptStringJournal *const journal,
    const script_string_journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t capacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!entry)
        return;
    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    binding.placementJournal_ = journal;
    binding.placementEntries_ = storage;
    binding.placementCapacity_ = capacity;
    binding.placementExpectedCount_ = expectedCount;
}

void ZoneRuntimeTable::resetCompositeReceiptsAndBinding(
    ZoneRuntimeEntry *const entry) noexcept
{
    if (!entry)
        return;
    entry->receiptCapsule_.~ZoneRuntimeReceiptCapsule();
    new (&entry->receiptCapsule_) ZoneRuntimeReceiptCapsule{};
    // The binding is the outer enrollment witness and is deliberately reset
    // last, after every contained authority is canonical pristine.
    entry->generationBinding_.reset();
}

zone_script_string_ownership::ZoneScriptStringUnpublishStatus
ZoneRuntimeTable::EnsureBoundGenerationUnreachable(
    void *const context) noexcept
{
    using Result = zone_script_string_ownership::
        ZoneScriptStringUnpublishStatus;
    auto *const entry = static_cast<ZoneRuntimeEntry *>(context);
    if (!entry)
        return Result::UnsafeFailure;
    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    ZoneRuntimeTable *const table = binding.table_;
    const bool exactOwner = table
        && binding.canonicalFor(table, &entry->lifecycle_, entry->key_)
        && entry->key_.slot < table->entries_.size()
        && &table->entries_[entry->key_.slot] == entry
        && binding.executionMode_ == ZoneRuntimeExecutionMode::Abandoning;
    const bool initialUnpublish = entry->lifecycle_.phase()
            == zone_load::ZoneLoadContextPhase::Loading
        && entry->lifecycle_.terminalKind()
            == zone_load::ZoneLoadTerminalKind::None
        && !entry->lifecycle_.cleanupActive();
    const bool cleanupUnpublish = entry->lifecycle_.phase()
            == zone_load::ZoneLoadContextPhase::Abandoning
        && entry->lifecycle_.terminalKind()
            == zone_load::ZoneLoadTerminalKind::Abandoned
        && entry->lifecycle_.cleanupActive()
        && entry->lifecycle_.nextCleanupOperation()
            == zone_load::ZoneLoadCleanupOperation::
                MakePartialAssetsAndStagedReferencesUnreachable;
    if (!exactOwner || (!initialUnpublish && !cleanupUnpublish)
        || binding.callbackMarker_
            != ZoneRuntimeGenerationBinding::CallbackMarker::Idle)
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        return Result::UnsafeFailure;
    }

    const bool discardPending = cleanupUnpublish;

    binding.callbackMarker_ = exactRegistryLifecycleCallbackPhaseMatches(
            *entry)
        ? ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveRegistryBorrow
        : ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
    const Result external = binding.callbacks_.ensureUnreachable(
        binding.callbacks_.context);
    if (external == Result::Retry)
    {
        binding.callbackMarker_ =
            ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
        return Result::Retry;
    }
    if (external != Result::Success)
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        return Result::UnsafeFailure;
    }

    // The external callback authority ends before any table-owned pending
    // admission work resumes.
    binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
        CallbackMarker::ActiveNoRegistry;

    if (discardPending
        && binding.setupStage_ >= ZoneRuntimeSetupStage::PendingCopyBegun)
    {
        const auto pendingStatus =
            zone_pending_copy::TryDiscardPendingCopyAdmission(
                &entry->receiptCapsule_.pendingCopyAdmissionReceipt_,
                entry->key_);
        switch (pendingStatus)
        {
        case zone_pending_copy::PendingCopyStatus::Success:
        case zone_pending_copy::PendingCopyStatus::AlreadyComplete:
            break;
        case zone_pending_copy::PendingCopyStatus::Busy:
        case zone_pending_copy::PendingCopyStatus::Retry:
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return Result::Retry;
        default:
            return Result::UnsafeFailure;
        }
    }

    binding.callbackMarker_ =
        ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
    return Result::Success;
}

zone_load::ZoneLoadCleanupCallbackStatus
ZoneRuntimeTable::PerformBoundGenerationCleanup(
    void *const context,
    const zone_load::ZoneLoadCleanupOperation operation) noexcept
{
    using CleanupResult = zone_load::ZoneLoadCleanupCallbackStatus;
    auto *const entry = static_cast<ZoneRuntimeEntry *>(context);
    if (!entry)
        return CleanupResult::UnsafeFailure;
    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    ZoneRuntimeTable *const table = binding.table_;
    const bool exactOwner = table
        && binding.canonicalFor(table, &entry->lifecycle_, entry->key_)
        && entry->key_.slot < table->entries_.size()
        && &table->entries_[entry->key_.slot] == entry
        && (binding.executionMode_ == ZoneRuntimeExecutionMode::Abandoning
            || binding.executionMode_
                == ZoneRuntimeExecutionMode::Unloading);
    const auto expectedTerminal = binding.executionMode_
            == ZoneRuntimeExecutionMode::Abandoning
        ? zone_load::ZoneLoadTerminalKind::Abandoned
        : zone_load::ZoneLoadTerminalKind::Unloaded;
    if (!exactOwner
        || binding.callbackMarker_
            != ZoneRuntimeGenerationBinding::CallbackMarker::Idle
        || entry->lifecycle_.phase()
            != zone_load::ZoneLoadContextPhase::Abandoning
        || entry->lifecycle_.terminalKind() != expectedTerminal
        || !entry->lifecycle_.cleanupActive()
        || entry->lifecycle_.nextCleanupOperation() != operation)
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        return CleanupResult::UnsafeFailure;
    }

    if (operation
        == zone_load::ZoneLoadCleanupOperation::
            MakePartialAssetsAndStagedReferencesUnreachable)
    {
        switch (EnsureBoundGenerationUnreachable(entry))
        {
        case zone_script_string_ownership::
            ZoneScriptStringUnpublishStatus::Success:
            return CleanupResult::Success;
        case zone_script_string_ownership::
            ZoneScriptStringUnpublishStatus::Retry:
            return CleanupResult::Retry;
        case zone_script_string_ownership::
            ZoneScriptStringUnpublishStatus::UnsafeFailure:
        default:
            return CleanupResult::UnsafeFailure;
        }
    }

    const auto failClosed = [&binding]() noexcept {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        return CleanupResult::UnsafeFailure;
    };
    const auto mapInternal = [&binding, &failClosed](
        const ZoneRuntimeTableStatus status) noexcept {
        if (status == ZoneRuntimeTableStatus::Success)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        if (status == ZoneRuntimeTableStatus::Retry
            || status == ZoneRuntimeTableStatus::Busy)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Retry;
        }
        return failClosed();
    };
    const auto performExternal =
        [&binding, &failClosed, operation, entry]() noexcept {
        binding.callbackMarker_ = exactRegistryLifecycleCallbackPhaseMatches(
                *entry)
            ? ZoneRuntimeGenerationBinding::
                CallbackMarker::ActiveRegistryBorrow
            : ZoneRuntimeGenerationBinding::
                CallbackMarker::ActiveNoRegistry;
        const CleanupResult status =
            binding.callbacks_.performExternalCleanup(
                binding.callbacks_.context,
                operation);
        if (status == CleanupResult::Success)
        {
            binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
                CallbackMarker::ActiveNoRegistry;
            return CleanupResult::Success;
        }
        if (status == CleanupResult::Retry)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Retry;
        }
        return failClosed();
    };

    // The lifecycle/ownership controller has entered a callback, but no
    // registry authority exists until performExternal publishes it.
    binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
        CallbackMarker::ActiveNoRegistry;

    switch (operation)
    {
    case zone_load::ZoneLoadCleanupOperation::
        RemoveLiveAssetsAndReferences:
    {
        const CleanupResult external = performExternal();
        if (external != CleanupResult::Success)
            return external;
        if (binding.setupStage_ < ZoneRuntimeSetupStage::PendingCopyBegun)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        const auto pendingStatus =
            zone_pending_copy::TryDiscardPendingCopyAdmission(
                &entry->receiptCapsule_.pendingCopyAdmissionReceipt_,
                entry->key_);
        switch (pendingStatus)
        {
        case zone_pending_copy::PendingCopyStatus::Success:
        case zone_pending_copy::PendingCopyStatus::AlreadyComplete:
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        case zone_pending_copy::PendingCopyStatus::Busy:
        case zone_pending_copy::PendingCopyStatus::Retry:
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Retry;
        default:
            return failClosed();
        }
    }
    case zone_load::ZoneLoadCleanupOperation::
        InvalidateAliasDirectStreamAndDelayState:
        if (binding.setupStage_
            < ZoneRuntimeSetupStage::StreamGenerationBegun)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        return mapInternal(MapStreamStatus(
            zone_stream_ownership::TryInvalidateZoneStreams(
                &table->activeZoneStreamBinding_,
                &entry->receiptCapsule_.streamGenerationReceipt_,
                entry->key_)));
    case zone_load::ZoneLoadCleanupOperation::
        TearDownNativeArenaWorkspaceAndSidecars:
    {
        // Engine sidecars are external; the placement journal/workspace/arena
        // are table-owned. The convergent external half must become
        // unreachable first, then exact placement destruction may run.
        const CleanupResult external = performExternal();
        if (external != CleanupResult::Success)
            return external;
        if (binding.setupStage_ < ZoneRuntimeSetupStage::StorageBound)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        return mapInternal(MapStorageStatus(
            zone_runtime_storage::TryDestroyZoneRuntimeStorage(
                &entry->receiptCapsule_.storageBinding_)));
    }
    case zone_load::ZoneLoadCleanupOperation::
        EndPhysicalMemoryAllocation:
        if (binding.setupStage_ < ZoneRuntimeSetupStage::AllocationBegun)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        return mapInternal(MapAllocationReceiptStatus(
            pmem_runtime::TryEndAllocationReceipt(
                &entry->receiptCapsule_.allocationReceipt_)));
    case zone_load::ZoneLoadCleanupOperation::FreePhysicalMemory:
        if (binding.setupStage_ < ZoneRuntimeSetupStage::AllocationBegun)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
            return CleanupResult::Success;
        }
        return mapInternal(MapAllocationReceiptStatus(
            pmem_runtime::TryFreeAllocationReceipt(
                &entry->receiptCapsule_.allocationReceipt_)));
    case zone_load::ZoneLoadCleanupOperation::CancelLoadInputAndInflate:
    case zone_load::ZoneLoadCleanupOperation::
        AbortNativeAdapterTransactions:
    case zone_load::ZoneLoadCleanupOperation::ReleaseGeometry:
    case zone_load::ZoneLoadCleanupOperation::
        ClearRegistryLoadingQueueGateAndSignal:
    case zone_load::ZoneLoadCleanupOperation::
        RemoveLiveRegistryAndHandles:
    {
        const CleanupResult external = performExternal();
        if (external == CleanupResult::Success)
        {
            binding.callbackMarker_ =
                ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
        }
        return external;
    }
    case zone_load::ZoneLoadCleanupOperation::ReleaseSlot:
    default:
        return failClosed();
    }
}

void ZoneRuntimeTable::CompleteBoundPendingAdmission(
    void *const context) noexcept
{
    auto *const entry = static_cast<ZoneRuntimeEntry *>(context);
    if (!entry)
        return;
    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    ZoneRuntimeTable *const table = binding.table_;
    const zone_load::ZoneLoadContextKey callbackKey = entry->key_;
    const ZoneRuntimeGenerationBinding::CallbackMarker resumeMarker =
        binding.callbackMarker_;
    if (!table
        || !static_cast<bool>(callbackKey)
        || callbackKey.slot >= table->entries_.size()
        || &table->entries_[callbackKey.slot] != entry
        || !binding.canonicalFor(
            table, &entry->lifecycle_, callbackKey)
        || !table->initialized()
        || (resumeMarker
                != ZoneRuntimeGenerationBinding::CallbackMarker::Idle
            && resumeMarker
                != ZoneRuntimeGenerationBinding::
                    CallbackMarker::ActiveNoRegistry)
        || !exactRegistryLifecycleCallbackPhaseMatches(*entry))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        if (table)
            table->poison();
        return;
    }
    const ZoneRuntimeGenerationCallbacks callbackSnapshot =
        binding.callbacks_;
    if (!entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow(
            zone_script_string_ownership::
                ZoneScriptStringOwnershipPhase::Admitting))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }
    binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
        CallbackMarker::ActiveRegistryBorrow;
    callbackSnapshot.completePendingAdmission(callbackSnapshot.context);
    if (!table->initialized()
        || entry->key_ != callbackKey
        || callbackKey.slot >= table->entries_.size()
        || &table->entries_[callbackKey.slot] != entry
        || !binding.canonicalFor(
            table, &entry->lifecycle_, callbackKey)
        || !binding.callbacksMatch(callbackSnapshot)
        || (binding.callbackMarker_
                != ZoneRuntimeGenerationBinding::
                    CallbackMarker::ActiveRegistryBorrow
            && binding.callbackMarker_
                != ZoneRuntimeGenerationBinding::
                    CallbackMarker::ActiveNoRegistry)
        || !exactRegistryLifecycleCallbackPhaseMatches(*entry))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }
    binding.callbackMarker_ = resumeMarker;
}

void ZoneRuntimeTable::AdmitBoundGeneration(void *const context) noexcept
{
    auto *const entry = static_cast<ZoneRuntimeEntry *>(context);
    if (!entry)
        return;
    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    ZoneRuntimeTable *const table = binding.table_;
    const zone_load::ZoneLoadContextKey callbackKey = entry->key_;
    if (!table
        || !static_cast<bool>(callbackKey)
        || callbackKey.slot >= table->entries_.size()
        || &table->entries_[callbackKey.slot] != entry
        || !binding.canonicalFor(
            table, &entry->lifecycle_, callbackKey)
        || !table->initialized()
        || binding.callbackMarker_
            != ZoneRuntimeGenerationBinding::CallbackMarker::Idle)
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        if (table)
            table->poison();
        return;
    }
    const ZoneRuntimeGenerationCallbacks callbackSnapshot =
        binding.callbacks_;

    binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
        CallbackMarker::ActiveNoRegistry;
    zone_pending_copy::FinalizePreparedPendingCopyAdmission(
        entry->receiptCapsule_.pendingCopyAdmissionReceipt_);
    const auto pendingPhase =
        entry->receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
    if (pendingPhase
            != zone_pending_copy::PendingCopyAdmissionPhase::Admitted
        && pendingPhase
            != zone_pending_copy::PendingCopyAdmissionPhase::Drained)
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }

    // Finalization contains a nested external callback.  Its void result must
    // not hide a poisoned table/controller or altered binding and allow the
    // later live-admission side effect to run.
    if (!table->initialized()
        || entry->key_ != callbackKey
        || callbackKey.slot >= table->entries_.size()
        || &table->entries_[callbackKey.slot] != entry
        || !binding.canonicalFor(
            table, &entry->lifecycle_, callbackKey)
        || !binding.callbacksMatch(callbackSnapshot)
        || binding.callbackMarker_
            != ZoneRuntimeGenerationBinding::
                CallbackMarker::ActiveNoRegistry
        || !exactRegistryLifecycleCallbackPhaseMatches(*entry))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }
    if (!entry->scriptStringOwnership_.tryBeginRegistryCallbackWindow(
            zone_script_string_ownership::
                ZoneScriptStringOwnershipPhase::Admitting))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }
    binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
        CallbackMarker::ActiveRegistryBorrow;
    callbackSnapshot.admitLive(callbackSnapshot.context);
    if (!table->initialized()
        || entry->key_ != callbackKey
        || callbackKey.slot >= table->entries_.size()
        || &table->entries_[callbackKey.slot] != entry
        || !binding.canonicalFor(
            table, &entry->lifecycle_, callbackKey)
        || !binding.callbacksMatch(callbackSnapshot)
        || (binding.callbackMarker_
                != ZoneRuntimeGenerationBinding::
                    CallbackMarker::ActiveRegistryBorrow
            && binding.callbackMarker_
                != ZoneRuntimeGenerationBinding::
                    CallbackMarker::ActiveNoRegistry)
        || !exactRegistryLifecycleCallbackPhaseMatches(*entry))
    {
        binding.callbackMarker_ = ZoneRuntimeGenerationBinding::
            CallbackMarker::ActiveNoRegistry;
        table->poison();
        return;
    }
    binding.callbackMarker_ =
        ZoneRuntimeGenerationBinding::CallbackMarker::Idle;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::completeMutableOperation(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_script_string_ownership::
        ZoneScriptStringOwnershipStatus ownershipStatus) noexcept
{
    const ZoneRuntimeTableStatus status =
        MapOwnershipStatus(ownershipStatus);
    ZoneRuntimeEntry *postEntry = nullptr;
    const ZoneRuntimeTableStatus postAuthentication =
        authenticateExactMutableEntry(physicalSlot, key, &postEntry);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure
        || postAuthentication != ZoneRuntimeTableStatus::Success
        || !postEntry)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return status;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::completeCompositeOperation(
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeTableStatus operationStatus) noexcept
{
    const ZoneRuntimeTableStatus tableAuthentication =
        validateInitializedHeader();
    const ZoneRuntimeTableStatus postAuthentication =
        tableAuthentication == ZoneRuntimeTableStatus::Success
        ? authenticateExactEntry(physicalSlot, key)
        : tableAuthentication;
    if (operationStatus == ZoneRuntimeTableStatus::UnsafeFailure
        || postAuthentication != ZoneRuntimeTableStatus::Success)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return operationStatus;
}

const zone_load::ZoneLoadContextKey &ZoneRuntimeEntry::key() const noexcept
{
    return key_;
}

const zone_load::ZoneLoadContextSlot &ZoneRuntimeEntry::lifecycle()
    const noexcept
{
    return lifecycle_;
}

const zone_script_string_ownership::ZoneScriptStringOwnershipController &
ZoneRuntimeEntry::scriptStringOwnership() const noexcept
{
    return scriptStringOwnership_;
}

ZoneRuntimeSetupStage ZoneRuntimeEntry::setupStage() const noexcept
{
    return generationBinding_.setupStage();
}

ZoneRuntimeExecutionMode ZoneRuntimeEntry::executionMode() const noexcept
{
    return generationBinding_.executionMode();
}

bool ZoneRuntimeEntry::generationBindingPristine() const noexcept
{
    return generationBinding_.isPristine();
}

ZoneRuntimeTableStatus ZoneRuntimeTable::validateEntryBinding(
    const std::uint32_t physicalSlot) noexcept
{
    if (physicalSlot >= entries_.size())
        return ZoneRuntimeTableStatus::UnsafeFailure;

    ZoneRuntimeEntry &entry = entries_[physicalSlot];
    if (physicalSlot
        == static_cast<std::uint32_t>(zone_slots::kDefaultZoneSlot))
    {
        return IsPristineEntry(entry, false, physicalSlot)
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const ZoneRuntimeTableStatus lifecycleStatus =
        ValidateLifecycleAndOwnership(entry, physicalSlot);
    if (lifecycleStatus != ZoneRuntimeTableStatus::Success
        && lifecycleStatus != ZoneRuntimeTableStatus::Busy)
    {
        return lifecycleStatus;
    }
    bool callbackBusy = lifecycleStatus == ZoneRuntimeTableStatus::Busy;
    if (callbackBusy)
    {
        const auto &ownership = entry.scriptStringOwnership_;
        if (!static_cast<bool>(entry.key_)
            || entry.key_.slot != physicalSlot
            || entry.lifecycle_.generation() != entry.key_.generation
            || !IsKnownOwnershipPhase(ownership.phase())
            || ownership.poisoned())
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        if (ownership.phase()
            == zone_script_string_ownership::
                ZoneScriptStringOwnershipPhase::Empty)
        {
            if (!ownership.isEmptyCanonical())
                return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        else if (ownership.key() != entry.key_
            || !ownership.canonicalForBinding(
                &entry.lifecycle_, entry.key_))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }

    ZoneRuntimeGenerationBinding &binding = entry.generationBinding_;
    if (binding.isPristine())
    {
        if (!detail::EntryReceiptsArePristine(entry)
            || (entry.scriptStringOwnership_.phase()
                    == zone_script_string_ownership::
                        ZoneScriptStringOwnershipPhase::Empty
                && entry.lifecycle_.phase()
                    == zone_load::ZoneLoadContextPhase::Abandoning
                && lifecycleStatus
                    != ZoneRuntimeTableStatus::Busy))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        return callbackBusy
            ? ZoneRuntimeTableStatus::Busy
            : ZoneRuntimeTableStatus::Success;
    }
    if (!binding.canonicalFor(this, &entry.lifecycle_, entry.key_))
        return ZoneRuntimeTableStatus::UnsafeFailure;
    // Cleanup/admission callbacks deliberately expose a transient component
    // phase before the lifecycle cursor advances. The exact stable binding
    // and lifecycle callback marker are authenticated above; component state
    // is reauthenticated after the callback returns.
    if (callbackBusy
        || binding.callbackMarker_
            != ZoneRuntimeGenerationBinding::CallbackMarker::Idle)
        return ZoneRuntimeTableStatus::Busy;

    const ZoneRuntimeSetupStage stage = binding.setupStage_;
    const ZoneRuntimeExecutionMode mode = binding.executionMode_;
    const auto lifecyclePhase = entry.lifecycle_.phase();
    const auto terminalKind = entry.lifecycle_.terminalKind();
    bool modeMatches = false;
    switch (mode)
    {
    case ZoneRuntimeExecutionMode::Loading:
        modeMatches = lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Loading
            && terminalKind == zone_load::ZoneLoadTerminalKind::None;
        break;
    case ZoneRuntimeExecutionMode::Admitting:
        modeMatches = (lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Loading
                && terminalKind == zone_load::ZoneLoadTerminalKind::None)
            || (lifecyclePhase == zone_load::ZoneLoadContextPhase::Live
                && terminalKind == zone_load::ZoneLoadTerminalKind::None);
        break;
    case ZoneRuntimeExecutionMode::Live:
        modeMatches = lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Live
            && terminalKind == zone_load::ZoneLoadTerminalKind::None;
        break;
    case ZoneRuntimeExecutionMode::Abandoning:
        modeMatches = (lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Loading
                && terminalKind == zone_load::ZoneLoadTerminalKind::None)
            || (lifecyclePhase
                    == zone_load::ZoneLoadContextPhase::Abandoning
                && terminalKind
                    == zone_load::ZoneLoadTerminalKind::Abandoned)
            || (lifecyclePhase == zone_load::ZoneLoadContextPhase::Empty
                && terminalKind
                    == zone_load::ZoneLoadTerminalKind::Abandoned);
        break;
    case ZoneRuntimeExecutionMode::Unloading:
        modeMatches = (lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Live
                && terminalKind == zone_load::ZoneLoadTerminalKind::None)
            || (lifecyclePhase
                    == zone_load::ZoneLoadContextPhase::Abandoning
                && terminalKind
                    == zone_load::ZoneLoadTerminalKind::Unloaded)
            || (lifecyclePhase == zone_load::ZoneLoadContextPhase::Empty
                && terminalKind
                    == zone_load::ZoneLoadTerminalKind::Unloaded);
        break;
    case ZoneRuntimeExecutionMode::Terminal:
        modeMatches = lifecyclePhase
                == zone_load::ZoneLoadContextPhase::Empty
            && (terminalKind == zone_load::ZoneLoadTerminalKind::Abandoned
                || terminalKind
                    == zone_load::ZoneLoadTerminalKind::Unloaded);
        break;
    case ZoneRuntimeExecutionMode::Passive:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (!modeMatches)
        return ZoneRuntimeTableStatus::UnsafeFailure;

    const auto cleanupPassed = [&](
        const zone_load::ZoneLoadCleanupOperation operation) noexcept {
        return lifecyclePhase == zone_load::ZoneLoadContextPhase::Empty
            || (lifecyclePhase
                    == zone_load::ZoneLoadContextPhase::Abandoning
                && static_cast<std::uint8_t>(
                       entry.lifecycle_.nextCleanupOperation())
                    > static_cast<std::uint8_t>(operation));
    };

    pmem_runtime::AllocationReceiptPhase allocationPhase =
        pmem_runtime::AllocationReceiptPhase::Pristine;
    if (stage >= ZoneRuntimeSetupStage::AllocationBegun)
    {
        allocationPhase = pmem_runtime::AllocationReceiptPhase::Begun;
        if (stage >= ZoneRuntimeSetupStage::AllocationEnded
            || cleanupPassed(
                zone_load::ZoneLoadCleanupOperation::
                    EndPhysicalMemoryAllocation))
        {
            allocationPhase = pmem_runtime::AllocationReceiptPhase::Ended;
        }
        if (cleanupPassed(
                zone_load::ZoneLoadCleanupOperation::FreePhysicalMemory))
        {
            allocationPhase = pmem_runtime::AllocationReceiptPhase::Freed;
        }
    }
    if (pmem_runtime::TryAuthenticateAllocationReceipt(
            &entry.receiptCapsule_.allocationReceipt_,
            binding.allocationType_,
            allocationPhase)
        != pmem_runtime::AllocationReceiptStatus::Success)
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    using OwnershipPhase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    const OwnershipPhase ownershipPhase =
        entry.scriptStringOwnership_.phase();
    bool scriptStorageAttached = false;
    if (stage >= ZoneRuntimeSetupStage::ScriptStringsBegun)
    {
        switch (ownershipPhase)
        {
        case OwnershipPhase::Staging:
        case OwnershipPhase::Sealed:
        case OwnershipPhase::Transferring:
        case OwnershipPhase::Transferred:
        case OwnershipPhase::CommitReady:
        case OwnershipPhase::Unpublishing:
        case OwnershipPhase::RollingBack:
            scriptStorageAttached = true;
            break;
        case OwnershipPhase::OwnershipRolledBack:
        case OwnershipPhase::Cleaning:
        case OwnershipPhase::Admitting:
        case OwnershipPhase::Live:
        case OwnershipPhase::Abandoned:
        case OwnershipPhase::Unloading:
        case OwnershipPhase::Unloaded:
            break;
        case OwnershipPhase::UnpublishingCallback:
        case OwnershipPhase::UnloadingCallback:
        case OwnershipPhase::Empty:
        case OwnershipPhase::UnsafeFailure:
        default:
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }

    zone_runtime_storage::ZoneRuntimeStorageCompositionExpectation
        storageExpectation{};
    auto storageMode =
        zone_runtime_storage::ZoneRuntimeStorageCompositionMode::Pristine;
    if (stage >= ZoneRuntimeSetupStage::StorageBound)
    {
        storageExpectation.journal = binding.placementJournal_;
        storageExpectation.entries = binding.placementEntries_;
        storageExpectation.capacity = binding.placementCapacity_;
        storageExpectation.expectedCount =
            binding.placementExpectedCount_;
        if (cleanupPassed(
                zone_load::ZoneLoadCleanupOperation::
                    TearDownNativeArenaWorkspaceAndSidecars))
        {
            storageMode = zone_runtime_storage::
                ZoneRuntimeStorageCompositionMode::Destroyed;
        }
        else
        {
            storageExpectation.key = entry.key_;
            storageExpectation.arenaZoneIdentity = entry.key_.generation;
            storageMode = stage < ZoneRuntimeSetupStage::ScriptStringsBegun
                ? zone_runtime_storage::
                    ZoneRuntimeStorageCompositionMode::Placed
                : (scriptStorageAttached
                    ? zone_runtime_storage::
                        ZoneRuntimeStorageCompositionMode::Active
                    : zone_runtime_storage::
                        ZoneRuntimeStorageCompositionMode::Detached);
        }
    }
    if (!zone_runtime_storage::AuthenticateZoneRuntimeStorageComposition(
            entry.receiptCapsule_.storageBinding_,
            storageExpectation,
            storageMode))
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    zone_stream_ownership::ZoneStreamCompositionMode streamMode =
        zone_stream_ownership::ZoneStreamCompositionMode::Pristine;
    const zone_load::ZoneLoadContextSlot *streamLifecycle = nullptr;
    zone_load::ZoneLoadContextKey streamKey{};
    if (stage >= ZoneRuntimeSetupStage::StreamGenerationBegun)
    {
        streamLifecycle = &entry.lifecycle_;
        streamKey = entry.key_;
        streamMode = zone_stream_ownership::
            ZoneStreamCompositionMode::NeverBound;
        if (stage >= ZoneRuntimeSetupStage::StreamsBound)
        {
            streamMode = zone_stream_ownership::
                ZoneStreamCompositionMode::Bound;
        }
        if (stage >= ZoneRuntimeSetupStage::StreamsInvalidated
            || cleanupPassed(
                zone_load::ZoneLoadCleanupOperation::
                    InvalidateAliasDirectStreamAndDelayState))
        {
            streamMode = zone_stream_ownership::
                ZoneStreamCompositionMode::Invalidated;
        }
    }
    if (!zone_stream_ownership::AuthenticateZoneStreamComposition(
            activeZoneStreamBinding_,
            entry.receiptCapsule_.streamGenerationReceipt_,
            streamLifecycle,
            streamKey,
            streamMode))
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    using PendingPhase = zone_pending_copy::PendingCopyAdmissionPhase;
    PendingPhase pendingPhase = PendingPhase::Pristine;
    const zone_pending_copy::PendingCopyLedger *expectedLedger = nullptr;
    const zone_load::ZoneLoadContextSlot *pendingLifecycle = nullptr;
    zone_load::ZoneLoadContextKey pendingKey{};
    zone_pending_copy::PendingCopyAdmissionCompletion completion{};
    if (stage >= ZoneRuntimeSetupStage::PendingCopyBegun)
    {
        expectedLedger = &pendingCopyLedger_;
        pendingLifecycle = &entry.lifecycle_;
        pendingKey = entry.key_;
        pendingPhase = stage >= ZoneRuntimeSetupStage::AdmissionPrepared
            ? PendingPhase::Prepared
            : PendingPhase::Collecting;
        if (mode == ZoneRuntimeExecutionMode::Admitting)
        {
            const PendingPhase actual =
                entry.receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
            if (actual != PendingPhase::Prepared
                && actual != PendingPhase::Admitting
                && actual != PendingPhase::Admitted
                && actual != PendingPhase::Drained)
            {
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
            pendingPhase = actual;
        }
        else if (mode == ZoneRuntimeExecutionMode::Live)
        {
            pendingPhase =
                entry.receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
            if (pendingPhase != PendingPhase::Admitted
                && pendingPhase != PendingPhase::Drained)
            {
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
        }
        else if (mode == ZoneRuntimeExecutionMode::Abandoning)
        {
            if (cleanupPassed(
                    zone_load::ZoneLoadCleanupOperation::
                        MakePartialAssetsAndStagedReferencesUnreachable))
            {
                pendingPhase = PendingPhase::Discarded;
            }
        }
        else if (mode == ZoneRuntimeExecutionMode::Unloading)
        {
            pendingPhase =
                entry.receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
            if (pendingPhase != PendingPhase::Drained)
            {
                pendingPhase = cleanupPassed(
                        zone_load::ZoneLoadCleanupOperation::
                            RemoveLiveAssetsAndReferences)
                    ? PendingPhase::Discarded
                    : PendingPhase::Admitted;
            }
        }
        else if (mode == ZoneRuntimeExecutionMode::Terminal)
        {
            pendingPhase =
                entry.receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
            if (pendingPhase != PendingPhase::Discarded
                && pendingPhase != PendingPhase::Drained)
            {
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
        }
        if (pendingPhase == PendingPhase::Prepared
            || pendingPhase == PendingPhase::Admitting)
        {
            completion.context = &entry;
            completion.complete = &CompleteBoundPendingAdmission;
        }
    }
    if (!zone_pending_copy::AuthenticatePendingCopyAdmissionReceipt(
            entry.receiptCapsule_.pendingCopyAdmissionReceipt_,
            expectedLedger,
            pendingLifecycle,
            pendingKey,
            pendingPhase,
            completion))
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    if (stage < ZoneRuntimeSetupStage::ScriptStringsBegun)
    {
        if (ownershipPhase != OwnershipPhase::Empty)
            return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    else if (mode == ZoneRuntimeExecutionMode::Loading)
    {
        if (stage >= ZoneRuntimeSetupStage::AdmissionPrepared)
        {
            if (ownershipPhase != OwnershipPhase::CommitReady)
                return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        else if (ownershipPhase != OwnershipPhase::Staging
            && ownershipPhase != OwnershipPhase::Sealed
            && ownershipPhase != OwnershipPhase::Transferring
            && ownershipPhase != OwnershipPhase::Transferred
            && ownershipPhase != OwnershipPhase::CommitReady)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    else if (mode == ZoneRuntimeExecutionMode::Admitting)
    {
        if (ownershipPhase != OwnershipPhase::CommitReady
            && ownershipPhase != OwnershipPhase::Admitting
            && ownershipPhase != OwnershipPhase::Live)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    else if (mode == ZoneRuntimeExecutionMode::Live
        && ownershipPhase != OwnershipPhase::Live)
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    else if (mode == ZoneRuntimeExecutionMode::Abandoning)
    {
        if (stage < ZoneRuntimeSetupStage::ScriptStringsBegun)
        {
            if (ownershipPhase != OwnershipPhase::Empty)
                return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        else if (ownershipPhase != OwnershipPhase::Staging
            && ownershipPhase != OwnershipPhase::Sealed
            && ownershipPhase != OwnershipPhase::Transferring
            && ownershipPhase != OwnershipPhase::Transferred
            && ownershipPhase != OwnershipPhase::CommitReady
            && ownershipPhase != OwnershipPhase::Unpublishing
            && ownershipPhase != OwnershipPhase::UnpublishingCallback
            && ownershipPhase != OwnershipPhase::RollingBack
            && ownershipPhase != OwnershipPhase::OwnershipRolledBack
            && ownershipPhase != OwnershipPhase::Cleaning
            && ownershipPhase != OwnershipPhase::Abandoned)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    else if (mode == ZoneRuntimeExecutionMode::Unloading)
    {
        if (ownershipPhase != OwnershipPhase::Live
            && ownershipPhase != OwnershipPhase::Unloading
            && ownershipPhase != OwnershipPhase::UnloadingCallback
            && ownershipPhase != OwnershipPhase::Unloaded)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    else if (mode == ZoneRuntimeExecutionMode::Terminal)
    {
        if (stage < ZoneRuntimeSetupStage::ScriptStringsBegun)
        {
            if (ownershipPhase != OwnershipPhase::Empty)
                return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        else if ((terminalKind
                    == zone_load::ZoneLoadTerminalKind::Abandoned
                && ownershipPhase != OwnershipPhase::Abandoned)
            || (terminalKind
                    == zone_load::ZoneLoadTerminalKind::Unloaded
                && ownershipPhase != OwnershipPhase::Unloaded))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }

    if (stage >= ZoneRuntimeSetupStage::ScriptStringsBegun
        && !zone_script_string_ownership::
            AuthenticateZoneScriptStringOwnershipStorage(
                entry.scriptStringOwnership_,
                &entry.lifecycle_,
                entry.key_,
                binding.placementJournal_,
                binding.placementEntries_,
                binding.placementCapacity_,
                binding.placementExpectedCount_,
                scriptStorageAttached
                    ? zone_script_string_ownership::
                        ZoneScriptStringStorageBindingPhase::Attached
                    : zone_script_string_ownership::
                        ZoneScriptStringStorageBindingPhase::Detached))
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus ZoneRuntimeTable::validateSharedComposition() noexcept
{
    std::array<zone_pending_copy::PendingCopyDescriptorBinding,
        zone_pending_copy::kPendingCopyGenerationCapacity>
        pendingBindings{};
    std::size_t pendingBindingCount = 0;
    std::size_t preparedCount = 0;
    std::size_t boundStreamCount = 0;
    const zone_stream_ownership::ZoneStreamGenerationReceipt *
        boundStreamReceipt = nullptr;
    bool callbackBusy = false;

    for (std::uint32_t physicalSlot = 0;
         physicalSlot < entries_.size();
         ++physicalSlot)
    {
        const ZoneRuntimeTableStatus entryStatus =
            validateEntryBinding(physicalSlot);
        if (entryStatus != ZoneRuntimeTableStatus::Success
            && entryStatus != ZoneRuntimeTableStatus::Busy)
        {
            return entryStatus;
        }
        callbackBusy = callbackBusy
            || entryStatus == ZoneRuntimeTableStatus::Busy;

        const ZoneRuntimeEntry &entry = entries_[physicalSlot];
        const auto streamPhase =
            entry.receiptCapsule_.streamGenerationReceipt_.phase();
        if (entry.setupStage() >= ZoneRuntimeSetupStage::StreamsBound
            && entry.setupStage()
                < ZoneRuntimeSetupStage::StreamsInvalidated
            && streamPhase
                == zone_stream_ownership::ZoneStreamGenerationPhase::Bound)
        {
            ++boundStreamCount;
            boundStreamReceipt =
                &entry.receiptCapsule_.streamGenerationReceipt_;
        }

        const auto pendingPhase =
            entry.receiptCapsule_.pendingCopyAdmissionReceipt_.phase();
        if (pendingPhase
                == zone_pending_copy::PendingCopyAdmissionPhase::Collecting
            || pendingPhase
                == zone_pending_copy::PendingCopyAdmissionPhase::Prepared
            || pendingPhase
                == zone_pending_copy::PendingCopyAdmissionPhase::Admitting
            || pendingPhase
                == zone_pending_copy::PendingCopyAdmissionPhase::Admitted)
        {
            if (pendingBindingCount >= pendingBindings.size())
                return ZoneRuntimeTableStatus::UnsafeFailure;
            auto &descriptor = pendingBindings[pendingBindingCount++];
            descriptor.receipt =
                &entry.receiptCapsule_.pendingCopyAdmissionReceipt_;
            descriptor.lifecycle = &entry.lifecycle_;
            descriptor.key = entry.key_;
            descriptor.phase = pendingPhase;
            if (pendingPhase
                    == zone_pending_copy::PendingCopyAdmissionPhase::Prepared
                || pendingPhase
                    == zone_pending_copy::PendingCopyAdmissionPhase::Admitting)
            {
                descriptor.completion.context =
                    const_cast<ZoneRuntimeEntry *>(&entry);
                descriptor.completion.complete =
                    &CompleteBoundPendingAdmission;
            }
            if (pendingPhase
                == zone_pending_copy::PendingCopyAdmissionPhase::Prepared)
            {
                ++preparedCount;
            }
        }
    }

    switch (activeZoneStreamBinding_.phase())
    {
    case zone_stream_ownership::ActiveZoneStreamPhase::Idle:
        if (boundStreamCount != 0
            || !zone_stream_ownership::
                AuthenticatePassiveZoneStreamSingleton(
                    activeZoneStreamBinding_))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        break;
    case zone_stream_ownership::ActiveZoneStreamPhase::Bound:
        if (boundStreamCount != 1
            || activeZoneStreamBinding_.receipt() != boundStreamReceipt)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        break;
    case zone_stream_ownership::ActiveZoneStreamPhase::UnsafeFailure:
    default:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const SharedState sharedState =
        static_cast<SharedState>(sharedState_);
    if (sharedState == SharedState::Pristine)
    {
        if (pendingBindingCount != 0
            || !detail::IsPristineRuntimeReceipt(pendingCopyLedger_)
            || !PendingDrainCallbackIsEmpty(pendingDrainCallback_))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    else if (sharedState == SharedState::Ready)
    {
        if (preparedCount > 1
            || !PendingDrainCallbackIsEmpty(pendingDrainCallback_))
            return ZoneRuntimeTableStatus::UnsafeFailure;
        const auto ledgerPhase = preparedCount == 1
            ? zone_pending_copy::
                PendingCopyLedgerAuthenticationPhase::AdmissionPrepared
            : zone_pending_copy::
                PendingCopyLedgerAuthenticationPhase::Ready;
        const auto ledgerStatus =
            zone_pending_copy::AuthenticatePendingCopyLedgerDescriptors(
                pendingCopyLedger_,
                ledgerPhase,
                pendingBindings.data(),
                pendingBindingCount);
        if (ledgerStatus
            == zone_pending_copy::PendingCopyAuthenticationResult::Mismatch)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        callbackBusy = callbackBusy
            || ledgerStatus
                == zone_pending_copy::
                    PendingCopyAuthenticationResult::CallbackActive;
    }
    else if (sharedState == SharedState::Draining)
    {
        if (preparedCount != 0 || pendingBindingCount == 0
            || !pendingDrainCallback_.consume
            || (pendingDrainCallback_.context
                && !pmem_runtime::StorageIsOutsideManagedMemory(
                    pendingDrainCallback_.context, 1)))
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        const auto ledgerStatus =
            zone_pending_copy::AuthenticatePendingCopyLedgerDescriptors(
                pendingCopyLedger_,
                zone_pending_copy::
                    PendingCopyLedgerAuthenticationPhase::Draining,
                pendingBindings.data(),
                pendingBindingCount);
        if (ledgerStatus
            == zone_pending_copy::PendingCopyAuthenticationResult::Mismatch)
        {
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        callbackBusy = callbackBusy
            || ledgerStatus
                == zone_pending_copy::
                    PendingCopyAuthenticationResult::CallbackActive;
    }
    else
    {
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    return callbackBusy
        ? ZoneRuntimeTableStatus::Busy
        : ZoneRuntimeTableStatus::Success;
}

bool ZoneRuntimeTable::initialized() const noexcept
{
    return state_ == static_cast<std::uint32_t>(TableState::Initialized)
        && HasKnownSharedState(sharedState_);
}

ZoneRuntimeTableStatus
ZoneRuntimeTable::validateInitializedHeader() noexcept
{
    if (!HasKnownState(state_) || !HasKnownSharedState(sharedState_))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    switch (static_cast<TableState>(state_))
    {
    case TableState::Initialized:
    {
        const ZoneRuntimeTableStatus status = validateSharedComposition();
        if (status == ZoneRuntimeTableStatus::UnsafeFailure)
            poison();
        return status;
    }
    case TableState::Poisoned:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    case TableState::Uninitialized:
    default:
        return ZoneRuntimeTableStatus::InvalidState;
    }
}

ZoneRuntimeTableStatus ZoneRuntimeTable::validateReleaseSafety() noexcept
{
    if (!HasKnownState(state_) || !HasKnownSharedState(sharedState_))
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const TableState state = static_cast<TableState>(state_);
    if (state == TableState::Poisoned)
        return ZoneRuntimeTableStatus::UnsafeFailure;
    if (state == TableState::Uninitialized)
    {
        if (sharedState_
                != static_cast<std::uint32_t>(SharedState::Pristine)
            || !detail::IsPristineRuntimeReceipt(
                activeZoneStreamBinding_)
            || !detail::IsPristineRuntimeReceipt(pendingCopyLedger_)
            || !PendingDrainCallbackIsEmpty(pendingDrainCallback_))
        {
            poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        for (std::uint32_t physicalSlot = static_cast<std::uint32_t>(
                 zone_slots::kDefaultZoneSlot);
             physicalSlot < zone_slots::kPhysicalZoneSlotCount;
             ++physicalSlot)
        {
            if (!IsPristineEntry(
                    entries_[physicalSlot], false, physicalSlot))
            {
                poison();
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
        }
        return ZoneRuntimeTableStatus::Success;
    }

    const ZoneRuntimeTableStatus tableStatus = validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    for (const ZoneRuntimeEntry &entry : entries_)
    {
        if (entry.scriptStringOwnership().serializerRetained())
            return ZoneRuntimeTableStatus::Busy;
    }
    return ZoneRuntimeTableStatus::Success;
}

void ZoneRuntimeTable::poison() noexcept
{
    state_ = static_cast<std::uint32_t>(TableState::Poisoned);
}

ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept
{
    return g_productionZoneRuntimeTable;
}

ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
    ZoneRuntimeTable *const table) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    if (!HasKnownState(table->state_)
        || !HasKnownSharedState(table->sharedState_))
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const TableState state = static_cast<TableState>(table->state_);
    if (state == TableState::Poisoned)
        return ZoneRuntimeTableStatus::UnsafeFailure;

    if (state == TableState::Initialized)
    {
        const ZoneRuntimeTableStatus sharedStatus =
            table->validateSharedComposition();
        if (sharedStatus == ZoneRuntimeTableStatus::UnsafeFailure)
        {
            table->poison();
            return sharedStatus;
        }
        if (sharedStatus != ZoneRuntimeTableStatus::Success
            || table->sharedState_
                != static_cast<std::uint32_t>(SharedState::Pristine))
        {
            return ZoneRuntimeTableStatus::InvalidState;
        }
        if (!IsPristineEntry(
                table->entries_[zone_slots::kDefaultZoneSlot],
                false,
                static_cast<std::uint32_t>(
                    zone_slots::kDefaultZoneSlot)))
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }

        bool pristine = true;
        for (std::uint32_t physicalSlot = static_cast<std::uint32_t>(
                 zone_slots::kFirstUsableZoneSlot);
             physicalSlot < zone_slots::kPhysicalZoneSlotCount;
             ++physicalSlot)
        {
            const ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
            const auto bindingStatus =
                table->validateEntryBinding(physicalSlot);
            if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            {
                table->poison();
                return ZoneRuntimeTableStatus::UnsafeFailure;
            }
            if (bindingStatus != ZoneRuntimeTableStatus::Success)
                return ZoneRuntimeTableStatus::InvalidState;
            pristine = pristine
                && IsPristineEntry(entry, true, physicalSlot);
        }
        return pristine
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidState;
    }

    if (table->sharedState_
            != static_cast<std::uint32_t>(SharedState::Pristine)
        || !detail::IsPristineRuntimeReceipt(
            table->activeZoneStreamBinding_)
        || !detail::IsPristineRuntimeReceipt(table->pendingCopyLedger_)
        || !PendingDrainCallbackIsEmpty(table->pendingDrainCallback_))
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    for (std::uint32_t physicalSlot = static_cast<std::uint32_t>(
             zone_slots::kDefaultZoneSlot);
         physicalSlot < zone_slots::kPhysicalZoneSlotCount;
         ++physicalSlot)
    {
        if (!IsPristineEntry(
                table->entries_[physicalSlot], false, physicalSlot))
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }

    for (std::uint32_t physicalSlot = static_cast<std::uint32_t>(
             zone_slots::kFirstUsableZoneSlot);
         physicalSlot < zone_slots::kPhysicalZoneSlotCount;
         ++physicalSlot)
    {
        const auto status = zone_load::TryInitializeZoneLoadContextSlot(
            &table->entries_[physicalSlot].lifecycle_, physicalSlot);
        if (status != zone_load::ZoneLoadContextStatus::Success)
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }

    table->state_ = static_cast<std::uint32_t>(TableState::Initialized);
    return ZoneRuntimeTableStatus::Success;
}

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
bool ZoneRuntimeTableTestAccess::ReceiptCapsulePristine(
    const ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot) noexcept
{
    return table && physicalSlot < table->entries_.size()
        && table->entries_[physicalSlot].receiptCapsule_.isPristine();
}

bool ZoneRuntimeTableTestAccess::SharedResourcesPristine(
    const ZoneRuntimeTable *const table) noexcept
{
    return table
        && detail::IsPristineRuntimeReceipt(
            table->activeZoneStreamBinding_)
        && detail::IsPristineRuntimeReceipt(table->pendingCopyLedger_)
        && PendingDrainCallbackIsEmpty(table->pendingDrainCallback_);
}

void ZoneRuntimeTableTestAccess::SetPendingCopyReadHook(
    const PendingCopyReadHookStage stage,
    void *const context,
    const PendingCopyReadHook hook) noexcept
{
    if (!hook
        || (stage != PendingCopyReadHookStage::BeforeLowerRead
            && stage != PendingCopyReadHookStage::AfterLowerRead))
    {
        g_pendingCopyReadTestHook = {};
        return;
    }
    g_pendingCopyReadTestHook = {stage, context, hook, true};
}
#endif

ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const ZoneRuntimeEntry **const outEntry) noexcept
{
    if (!WritableOutputIsSeparated(
            table,
            outEntry,
            sizeof(*outEntry),
            alignof(const ZoneRuntimeEntry *)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const auto tableStatus = table->validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const auto bindingStatus =
        table->validateEntryBinding(physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
    }
    if (!WritableOutputIsSeparatedFromRetainedRuntime(
            table->activeZoneStreamBinding_,
            static_cast<SharedState>(table->sharedState_),
            outEntry,
            sizeof(*outEntry),
            alignof(const ZoneRuntimeEntry *)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    *outEntry = &entry;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *const inOutKey) noexcept
{
    if (!WritableOutputIsSeparated(
            table,
            inOutKey,
            sizeof(*inOutKey),
            alignof(zone_load::ZoneLoadContextKey)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const auto tableStatus = table->validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const auto bindingStatus =
        table->validateEntryBinding(physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
    }
    if (!WritableOutputIsSeparatedFromRetainedRuntime(
            table->activeZoneStreamBinding_,
            static_cast<SharedState>(table->sharedState_),
            inOutKey,
            sizeof(*inOutKey),
            alignof(zone_load::ZoneLoadContextKey)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    if (entry.lifecycle_.phase() == zone_load::ZoneLoadContextPhase::Empty
        && !IsEmptyOwnership(entry.scriptStringOwnership_))
    {
        return ZoneRuntimeTableStatus::InvalidState;
    }
    if (!entry.generationBinding_.isPristine()
        || !detail::EntryReceiptsArePristine(entry))
    {
        return ZoneRuntimeTableStatus::InvalidState;
    }

    zone_load::ZoneLoadContextKey candidate = *inOutKey;
    if (static_cast<bool>(candidate) && entry.key_ != candidate)
        return ZoneRuntimeTableStatus::StaleKey;
    const auto lifecycleStatus = zone_load::TryClaimZoneLoadContext(
        &entry.lifecycle_, &candidate);
    const auto status = MapLifecycleStatus(lifecycleStatus);
    if (status != ZoneRuntimeTableStatus::Success)
    {
        if (status == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return status;
    }
    if (!static_cast<bool>(candidate)
        || candidate.slot != physicalSlot
        || entry.lifecycle_.generation() != candidate.generation)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    entry.key_ = candidate;
    if (table->authenticateExactEntry(physicalSlot, candidate)
        != ZoneRuntimeTableStatus::Success)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    *inOutKey = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *const outView) noexcept
{
    if (!WritableOutputIsSeparated(
            table,
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimeGenerationView),
            &key))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const auto tableStatus = table->validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const auto bindingStatus =
        table->validateEntryBinding(physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
    }
    if (!WritableOutputIsSeparatedFromRetainedRuntime(
            table->activeZoneStreamBinding_,
            static_cast<SharedState>(table->sharedState_),
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimeGenerationView)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (entry.key_ != key
        || !zone_load::ZoneLoadContextKeyMatches(
            &entry.lifecycle_, key))
    {
        return ZoneRuntimeTableStatus::StaleKey;
    }

    const ZoneRuntimeGenerationView candidate{
        entry.key_,
        &entry,
    };
    *outView = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept
{
    if (!table
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), &callbacks, sizeof(callbacks)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const ZoneRuntimeGenerationCallbacks callbackSnapshot = callbacks;
    if (!CallbacksAreComplete(callbackSnapshot))
        return ZoneRuntimeTableStatus::InvalidArgument;
    if (!pmem_runtime::StorageIsOutsideManagedMemory(
            table, sizeof(*table)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (callbackSnapshot.context
        && (!pmem_runtime::StorageIsOutsideManagedMemory(
                callbackSnapshot.context, 1)
            || !AddressRangesAreDisjoint(
                table,
                sizeof(*table),
                callbackSnapshot.context,
                1)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    if (!binding.isPristine())
    {
        return binding.executionMode() == ZoneRuntimeExecutionMode::Loading
                && binding.setupStage()
                    == ZoneRuntimeSetupStage::CallbacksBound
                && ZoneRuntimeTable::generationCallbacksMatch(
                    entry, callbackSnapshot)
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (entry->lifecycle_.phase()
            != zone_load::ZoneLoadContextPhase::Loading
        || entry->lifecycle_.terminalKind()
            != zone_load::ZoneLoadTerminalKind::None
        || !entry->scriptStringOwnership_.isEmptyCanonical())
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const SharedState sharedState =
        static_cast<SharedState>(table->sharedState_);
    if (sharedState == SharedState::Draining)
        return ZoneRuntimeTableStatus::Busy;
    if (sharedState == SharedState::Pristine)
    {
        const auto pendingStatus =
            zone_pending_copy::TryInitializePendingCopyLedger(
                &table->pendingCopyLedger_);
        const ZoneRuntimeTableStatus status =
            MapPendingStatus(pendingStatus);
        if (pendingStatus
            != zone_pending_copy::PendingCopyStatus::Success)
        {
            return table->completeCompositeOperation(
                physicalSlot, key, status);
        }
        table->sharedState_ =
            static_cast<std::uint32_t>(SharedState::Ready);
    }
    else if (sharedState != SharedState::Ready)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    ZoneRuntimeTable::bindGeneration(
        table, entry, key, callbackSnapshot);
    return table->completeCompositeOperation(
        physicalSlot, key, ZoneRuntimeTableStatus::Success);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const char *const name,
    const std::uint32_t allocationType) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    ZoneRuntimeGenerationBinding &binding = entry->generationBinding_;
    if (binding.isPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;
    // The hidden receipt owns a copied name, but intentionally exposes no
    // name oracle. A second Begin therefore cannot prove an exact retry.
    if (binding.executionMode() != ZoneRuntimeExecutionMode::Loading
        || binding.setupStage()
            != ZoneRuntimeSetupStage::CallbacksBound)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const auto receiptStatus = pmem_runtime::TryBeginAllocationReceipt(
        name,
        allocationType,
        ZoneRuntimeTable::mutableAllocationReceipt(entry));
    const ZoneRuntimeTableStatus status =
        MapAllocationReceiptStatus(receiptStatus);
    if (receiptStatus == pmem_runtime::AllocationReceiptStatus::Success)
    {
        ZoneRuntimeTable::setGenerationAllocation(entry, allocationType);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t size,
    const std::uint32_t alignment,
    const std::uint32_t type,
    pmem_runtime::AllocationResult *const outResult) noexcept
{
    if (!WritableOutputIsSeparated(
            table,
            outResult,
            sizeof(*outResult),
            alignof(pmem_runtime::AllocationResult),
            &key))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (!WritableOutputIsSeparatedFromRetainedRuntime(
            table->activeZoneStreamBinding_,
            static_cast<SharedState>(table->sharedState_),
            outResult,
            sizeof(*outResult),
            alignof(pmem_runtime::AllocationResult)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || entry->setupStage() < ZoneRuntimeSetupStage::AllocationBegun
        || entry->setupStage() >= ZoneRuntimeSetupStage::AllocationEnded)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const pmem_runtime::AllocationResult candidate =
        pmem_runtime::TryAllocate(
            size,
            alignment,
            type,
            ZoneRuntimeTable::generationAllocationType(entry));
    const ZoneRuntimeTableStatus operationStatus =
        MapAllocationStatus(candidate.status);
    const ZoneRuntimeTableStatus status =
        table->completeCompositeOperation(
            physicalSlot, key, operationStatus);
    if (status == ZoneRuntimeTableStatus::Success
        || status == ZoneRuntimeTableStatus::CapacityExceeded)
    {
        *outResult = candidate;
    }
    return status;
}

ZoneRuntimeTableStatus TryBindZoneRuntimeStorage(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    void *const slab,
    const std::size_t slabCapacity,
    const zone_runtime_storage::ZoneRuntimeStoragePlan *const plan) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || entry->setupStage()
            != ZoneRuntimeSetupStage::AllocationBegun)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const auto rangeStatus = pmem_runtime::TryAuthenticateAllocationRange(
        ZoneRuntimeTable::mutableAllocationReceipt(entry),
        ZoneRuntimeTable::generationAllocationType(entry),
        slab,
        slabCapacity,
        pmem_runtime::AllocationReceiptPhase::Begun);
    const ZoneRuntimeTableStatus mappedRange =
        MapAllocationRangeStatus(rangeStatus);
    if (mappedRange != ZoneRuntimeTableStatus::Success)
    {
        return mappedRange == ZoneRuntimeTableStatus::UnsafeFailure
            ? table->completeCompositeOperation(
                physicalSlot, key, mappedRange)
            : mappedRange;
    }

    auto &storage = *ZoneRuntimeTable::mutableStorageBinding(entry);
    const auto storageStatus = zone_runtime_storage::TryBindZoneRuntimeStorage(
        slab, slabCapacity, plan, &storage);
    ZoneRuntimeTableStatus status = MapStorageStatus(storageStatus);
    if (storageStatus != zone_runtime_storage::ZoneRuntimeStorageStatus::Success)
    {
        return table->completeCompositeOperation(
            physicalSlot, key, status);
    }

    auto *const arena = storage.fxNativeArena();
    const auto *const retainedPlan = storage.plan();
    if (!arena || !retainedPlan)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const auto arenaStatus =
        zone_runtime_storage::detail::TryBindFxRuntimeStorage(
            arena,
            storage.fxArenaBacking(),
            retainedPlan->arenaBudget,
            key.generation);
    using FxBindStatus =
        zone_runtime_storage::detail::FxRuntimeStorageBindStatus;
    if (arenaStatus != FxBindStatus::Success)
    {
        switch (arenaStatus)
        {
        case FxBindStatus::Busy:
            status = ZoneRuntimeTableStatus::Busy;
            break;
        case FxBindStatus::InvalidArgument:
        case FxBindStatus::MisalignedStorage:
        case FxBindStatus::SizeOverflow:
            status = ZoneRuntimeTableStatus::InvalidArgument;
            break;
        case FxBindStatus::InsufficientCapacity:
            status = ZoneRuntimeTableStatus::CapacityExceeded;
            break;
        case FxBindStatus::InvalidPhase:
            status = ZoneRuntimeTableStatus::InvalidPhase;
            break;
        case FxBindStatus::TransactionLimit:
        case FxBindStatus::InvalidTransaction:
        default:
            status = ZoneRuntimeTableStatus::UnsafeFailure;
            break;
        }

        const auto destroyStatus =
            zone_runtime_storage::TryDestroyZoneRuntimeStorage(&storage);
        if ((destroyStatus
                != zone_runtime_storage::ZoneRuntimeStorageStatus::Success
                && destroyStatus
                    != zone_runtime_storage::
                        ZoneRuntimeStorageStatus::AlreadyComplete)
            || !zone_runtime_storage::AuthenticateZoneRuntimeStorageBinding(
                storage,
                zone_runtime_storage::
                    ZoneRuntimeStorageBindingPhase::Destroyed))
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
        storage.~ZoneRuntimeStorageBinding();
        new (&storage) zone_runtime_storage::ZoneRuntimeStorageBinding{};
        return table->completeCompositeOperation(
            physicalSlot, key, status);
    }

    ZoneRuntimeTable::retainGenerationPlacement(
        entry,
        storage.scriptStringJournal(),
        storage.scriptStringEntries(),
        retainedPlan->scriptStringCapacity,
        0);
    ZoneRuntimeTable::setGenerationSetupStage(
        entry, ZoneRuntimeSetupStage::StorageBound);
    return table->completeCompositeOperation(
        physicalSlot, key, ZoneRuntimeTableStatus::Success);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage() != ZoneRuntimeSetupStage::StorageBound
            && entry->setupStage()
                != ZoneRuntimeSetupStage::StreamGenerationBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto streamStatus =
        zone_stream_ownership::TryBeginZoneStreamGeneration(
            ZoneRuntimeTable::mutableStreamGenerationReceipt(entry),
            &entry->lifecycle_,
            key);
    const ZoneRuntimeTableStatus status = MapStreamStatus(streamStatus);
    if (streamStatus
        == zone_stream_ownership::ZoneStreamOwnershipStatus::Success)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry,
            ZoneRuntimeSetupStage::StreamGenerationBegun);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryBindZoneRuntimeStreams(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const XZoneMemory *const zoneIdentity,
    const relocation::BlockView *const blocks,
    const std::size_t blockCount) noexcept
{
    if (!table || !zoneIdentity || !blocks
        || blockCount != relocation::kBlockCount
        || reinterpret_cast<std::uintptr_t>(blocks)
                % alignof(relocation::BlockView)
            != 0)
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (!pmem_runtime::StorageIsOutsideManagedMemory(
            zoneIdentity, sizeof(*zoneIdentity))
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), zoneIdentity, sizeof(*zoneIdentity))
        || !AddressRangesAreDisjoint(
            table,
            sizeof(*table),
            blocks,
            sizeof(*blocks) * relocation::kBlockCount))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    relocation::BlockView blockSnapshot[relocation::kBlockCount]{};
    for (std::size_t index = 0;
         index < relocation::kBlockCount;
         ++index)
    {
        blockSnapshot[index] = blocks[index];
    }

    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage()
                != ZoneRuntimeSetupStage::StreamGenerationBegun
            && entry->setupStage()
                != ZoneRuntimeSetupStage::StreamsBound))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const auto *const storage =
        ZoneRuntimeTable::mutableStorageBinding(entry);
    const void *const storageSlab = storage ? storage->slab() : nullptr;
    const std::size_t storageCapacity =
        storage ? storage->slabCapacity() : 0;
    if (!storageSlab || storageCapacity == 0)
    {
        return table->completeCompositeOperation(
            physicalSlot,
            key,
            ZoneRuntimeTableStatus::UnsafeFailure);
    }
    for (std::size_t index = 0; index < blockCount; ++index)
    {
        if (blockSnapshot[index].base == 0
            || blockSnapshot[index].size == 0)
            continue;
        const auto rangeStatus =
            pmem_runtime::TryAuthenticateAllocationRange(
                ZoneRuntimeTable::mutableAllocationReceipt(entry),
                ZoneRuntimeTable::generationAllocationType(entry),
                reinterpret_cast<const void *>(blockSnapshot[index].base),
                blockSnapshot[index].size,
                pmem_runtime::AllocationReceiptPhase::Begun);
        const ZoneRuntimeTableStatus mappedRange =
            MapAllocationRangeStatus(rangeStatus);
        if (mappedRange != ZoneRuntimeTableStatus::Success)
        {
            return mappedRange == ZoneRuntimeTableStatus::UnsafeFailure
                ? table->completeCompositeOperation(
                    physicalSlot, key, mappedRange)
                : mappedRange;
        }
        if (!AddressRangesAreDisjoint(
                storageSlab,
                storageCapacity,
                reinterpret_cast<const void *>(blockSnapshot[index].base),
                blockSnapshot[index].size))
        {
            return ZoneRuntimeTableStatus::InvalidArgument;
        }
    }

    const auto streamStatus = zone_stream_ownership::TryBindZoneStreams(
        &table->activeZoneStreamBinding_,
        ZoneRuntimeTable::mutableStreamGenerationReceipt(entry),
        key,
        zoneIdentity,
        blockSnapshot,
        blockCount);
    const ZoneRuntimeTableStatus status = MapStreamStatus(streamStatus);
    if (streamStatus
        == zone_stream_ownership::ZoneStreamOwnershipStatus::Success)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::StreamsBound);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage() != ZoneRuntimeSetupStage::StreamsBound
            && entry->setupStage()
                != ZoneRuntimeSetupStage::PendingCopyBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (static_cast<SharedState>(table->sharedState_)
        == SharedState::Draining)
    {
        return ZoneRuntimeTableStatus::Busy;
    }

    const auto pendingStatus =
        zone_pending_copy::TryBeginPendingCopyAdmission(
            &table->pendingCopyLedger_,
            ZoneRuntimeTable::mutablePendingCopyAdmissionReceipt(entry),
            &entry->lifecycle_,
            key);
    const ZoneRuntimeTableStatus status = MapPendingStatus(pendingStatus);
    if (pendingStatus == zone_pending_copy::PendingCopyStatus::Success)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::PendingCopyBegun);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const std::uint32_t assetEntryIndex) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage()
                != ZoneRuntimeSetupStage::PendingCopyBegun
            && entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto pendingStatus =
        zone_pending_copy::TryAppendPendingCopyRecord(
            ZoneRuntimeTable::mutablePendingCopyAdmissionReceipt(entry),
            key,
            assetEntryIndex);
    return table->completeCompositeOperation(
        physicalSlot, key, MapPendingStatus(pendingStatus));
}

ZoneRuntimeTableStatus TryGetZoneRuntimePendingCopyView(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &keyArgument,
    ZoneRuntimePendingCopyView *const outView) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!WritableOutputIsSeparated(
            table,
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimePendingCopyView),
            &keyArgument))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactPendingCopyOutput(
            physicalSlot,
            key,
            outView,
            sizeof(*outView),
            alignof(ZoneRuntimePendingCopyView));
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    const ZoneRuntimeEntry *const entry = &table->entries_[physicalSlot];

    const auto *const receipt =
        ZoneRuntimeTable::pendingCopyAdmissionReceipt(entry);
    if (!receipt)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const auto phase = receipt->phase();
    const std::uint32_t recordCount = receipt->recordCount();
    const ZoneRuntimePendingCopyView candidate{key, recordCount, 0};
    if (!candidate)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const ZoneRuntimeEntry *postEntry = nullptr;
    const ZoneRuntimeTableStatus postAuthentication =
        table->authenticateExactPendingCopyRead(
            physicalSlot, key, &postEntry);
    const auto *const postReceipt =
        ZoneRuntimeTable::pendingCopyAdmissionReceipt(postEntry);
    if (postAuthentication != ZoneRuntimeTableStatus::Success
        || postEntry != entry || postReceipt != receipt
        || postReceipt->phase() != phase
        || postReceipt->recordCount() != recordCount)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    *outView = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryReadZoneRuntimePendingCopy(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &keyArgument,
    const std::uint32_t expectedRecordCount,
    const std::uint32_t ordinal,
    zone_pending_copy::PendingCopyRecord *const outRecord) noexcept
{
    if (expectedRecordCount
            > zone_pending_copy::kPendingCopyRecordCapacity
        || ordinal >= expectedRecordCount)
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!WritableOutputIsSeparated(
            table,
            outRecord,
            sizeof(*outRecord),
            alignof(zone_pending_copy::PendingCopyRecord),
            &keyArgument))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactPendingCopyOutput(
            physicalSlot,
            key,
            outRecord,
            sizeof(*outRecord),
            alignof(zone_pending_copy::PendingCopyRecord));
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    const ZoneRuntimeEntry *const entry = &table->entries_[physicalSlot];

    const auto *const receipt =
        ZoneRuntimeTable::pendingCopyAdmissionReceipt(entry);
    if (!receipt)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    const auto phase = receipt->phase();
    if (receipt->recordCount() != expectedRecordCount)
        return ZoneRuntimeTableStatus::CountMismatch;

    zone_pending_copy::PendingCopyRecord candidate{};
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    InvokePendingCopyReadTestHook(
        ZoneRuntimeTableTestAccess::PendingCopyReadHookStage::BeforeLowerRead,
        table,
        physicalSlot,
        key);
#endif
    const auto pendingStatus = zone_pending_copy::TryReadPendingCopyRecord(
        receipt, key, ordinal, &candidate);
    if (pendingStatus != zone_pending_copy::PendingCopyStatus::Success
        || candidate.key != key
        || candidate.assetEntryIndex
            < zone_pending_copy::kFirstAssetEntryIndex
        || candidate.assetEntryIndex
            > zone_pending_copy::kLastAssetEntryIndex
        || candidate.reserved != 0)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    InvokePendingCopyReadTestHook(
        ZoneRuntimeTableTestAccess::PendingCopyReadHookStage::AfterLowerRead,
        table,
        physicalSlot,
        key);
#endif
    const ZoneRuntimeEntry *postEntry = nullptr;
    const ZoneRuntimeTableStatus postAuthentication =
        table->authenticateExactPendingCopyRead(
            physicalSlot, key, &postEntry);
    const auto *const postReceipt =
        ZoneRuntimeTable::pendingCopyAdmissionReceipt(postEntry);
    if (postAuthentication != ZoneRuntimeTableStatus::Success
        || postEntry != entry || postReceipt != receipt
        || postReceipt->phase() != phase
        || postReceipt->recordCount() != expectedRecordCount)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    *outRecord = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun
            && entry->setupStage()
                != ZoneRuntimeSetupStage::AdmissionPrepared)
        || entry->scriptStringOwnership_.phase()
            != zone_script_string_ownership::
                ZoneScriptStringOwnershipPhase::CommitReady)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const zone_pending_copy::PendingCopyAdmissionCompletion completion{
        entry,
        &ZoneRuntimeTable::CompleteBoundPendingAdmission,
    };
    const auto pendingStatus =
        zone_pending_copy::TryPreparePendingCopyAdmission(
            ZoneRuntimeTable::mutablePendingCopyAdmissionReceipt(entry),
            key,
            completion);
    const ZoneRuntimeTableStatus status = MapPendingStatus(pendingStatus);
    if (pendingStatus == zone_pending_copy::PendingCopyStatus::Success)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::AdmissionPrepared);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage()
                != ZoneRuntimeSetupStage::AdmissionPrepared
            && entry->setupStage()
                != ZoneRuntimeSetupStage::StreamsInvalidated))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto streamStatus = zone_stream_ownership::TryInvalidateZoneStreams(
        &table->activeZoneStreamBinding_,
        ZoneRuntimeTable::mutableStreamGenerationReceipt(entry),
        key);
    const ZoneRuntimeTableStatus status = MapStreamStatus(streamStatus);
    if (streamStatus
            == zone_stream_ownership::ZoneStreamOwnershipStatus::Success
        || streamStatus
            == zone_stream_ownership::
                ZoneStreamOwnershipStatus::AlreadyComplete)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::StreamsInvalidated);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
        || (entry->setupStage()
                != ZoneRuntimeSetupStage::StreamsInvalidated
            && entry->setupStage()
                != ZoneRuntimeSetupStage::AllocationEnded))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto receiptStatus = pmem_runtime::TryEndAllocationReceipt(
        ZoneRuntimeTable::mutableAllocationReceipt(entry));
    const ZoneRuntimeTableStatus status =
        MapAllocationReceiptStatus(receiptStatus);
    if (receiptStatus == pmem_runtime::AllocationReceiptStatus::Success
        || receiptStatus
            == pmem_runtime::AllocationReceiptStatus::AlreadyComplete)
    {
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::AllocationEnded);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() == ZoneRuntimeExecutionMode::Live)
        return ZoneRuntimeTableStatus::Success;
    if ((entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            && entry->executionMode()
                != ZoneRuntimeExecutionMode::Admitting)
        || entry->setupStage() != ZoneRuntimeSetupStage::AllocationEnded
        || entry->scriptStringOwnership_.phase()
            != zone_script_string_ownership::
                ZoneScriptStringOwnershipPhase::CommitReady)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    ZoneRuntimeTable::setGenerationExecutionMode(
        entry, ZoneRuntimeExecutionMode::Admitting);
    const zone_script_string_ownership::ZoneScriptStringAdmissionCallback
        admission{
            entry,
            &ZoneRuntimeTable::AdmitBoundGeneration,
        };
    const auto ownershipStatus = zone_script_string_ownership::
        TryCommitZoneScriptStringsAndAdmit(
            &entry->scriptStringOwnership_, admission);
    const ZoneRuntimeTableStatus status =
        MapOwnershipStatus(ownershipStatus);
    if (ownershipStatus
        == zone_script_string_ownership::
            ZoneScriptStringOwnershipStatus::Success)
    {
        ZoneRuntimeTable::setGenerationExecutionMode(
            entry, ZoneRuntimeExecutionMode::Live);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->generationBindingPristine()
        || (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            && entry->executionMode()
                != ZoneRuntimeExecutionMode::Abandoning))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    ZoneRuntimeTable::setGenerationExecutionMode(
        entry, ZoneRuntimeExecutionMode::Abandoning);

    ZoneRuntimeTableStatus status = ZoneRuntimeTableStatus::Success;
    if (entry->setupStage() >= ZoneRuntimeSetupStage::ScriptStringsBegun)
    {
        const zone_script_string_ownership::
            ZoneScriptStringRollbackCallbacks callbacks{
                entry,
                &ZoneRuntimeTable::EnsureBoundGenerationUnreachable,
                &ZoneRuntimeTable::PerformBoundGenerationCleanup,
            };
        status = MapOwnershipStatus(
            zone_script_string_ownership::
                TryBeginZoneScriptStringRollback(
                    &entry->scriptStringOwnership_, callbacks));
    }
    else
    {
        status = MapLifecycleStatus(
            zone_load::TryBeginZoneLoadContextAbandonment(
                &entry->lifecycle_, key));
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryContinueZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() == ZoneRuntimeExecutionMode::Terminal)
    {
        return entry->lifecycle_.phase()
                    == zone_load::ZoneLoadContextPhase::Empty
                && entry->lifecycle_.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Abandoned
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (entry->executionMode() != ZoneRuntimeExecutionMode::Abandoning)
        return ZoneRuntimeTableStatus::InvalidPhase;

    ZoneRuntimeTableStatus status = ZoneRuntimeTableStatus::InvalidPhase;
    if (entry->setupStage() >= ZoneRuntimeSetupStage::ScriptStringsBegun)
    {
        using OwnershipPhase = zone_script_string_ownership::
            ZoneScriptStringOwnershipPhase;
        const zone_script_string_ownership::
            ZoneScriptStringRollbackCallbacks callbacks{
                entry,
                &ZoneRuntimeTable::EnsureBoundGenerationUnreachable,
                &ZoneRuntimeTable::PerformBoundGenerationCleanup,
            };
        switch (entry->scriptStringOwnership_.phase())
        {
        case OwnershipPhase::Staging:
        case OwnershipPhase::Sealed:
        case OwnershipPhase::Transferring:
        case OwnershipPhase::Transferred:
        case OwnershipPhase::CommitReady:
        case OwnershipPhase::Unpublishing:
            status = MapOwnershipStatus(
                zone_script_string_ownership::
                    TryBeginZoneScriptStringRollback(
                        &entry->scriptStringOwnership_, callbacks));
            break;
        case OwnershipPhase::RollingBack:
            status = MapOwnershipStatus(
                zone_script_string_ownership::
                    TryRollbackNextZoneScriptString(
                        &entry->scriptStringOwnership_));
            break;
        case OwnershipPhase::OwnershipRolledBack:
            status = MapOwnershipStatus(
                zone_script_string_ownership::
                    TryFinishZoneScriptStringAbandonment(
                        &entry->scriptStringOwnership_));
            break;
        case OwnershipPhase::Abandoned:
            status = ZoneRuntimeTableStatus::Success;
            break;
        default:
            status = ZoneRuntimeTableStatus::InvalidPhase;
            break;
        }
        if (status == ZoneRuntimeTableStatus::Success
            && entry->scriptStringOwnership_.phase()
                == OwnershipPhase::Abandoned
            && entry->lifecycle_.phase()
                == zone_load::ZoneLoadContextPhase::Empty)
        {
            ZoneRuntimeTable::setGenerationExecutionMode(
                entry, ZoneRuntimeExecutionMode::Terminal);
        }
    }
    else
    {
        if (entry->lifecycle_.phase()
            == zone_load::ZoneLoadContextPhase::Loading)
        {
            status = MapLifecycleStatus(
                zone_load::TryBeginZoneLoadContextAbandonment(
                    &entry->lifecycle_, key));
        }
        else
        {
            status = ZoneRuntimeTableStatus::Success;
        }
        if (status == ZoneRuntimeTableStatus::Success
            && entry->lifecycle_.phase()
                != zone_load::ZoneLoadContextPhase::Empty)
        {
            const zone_load::ZoneLoadCleanupCallbacks callbacks{
                entry,
                &ZoneRuntimeTable::PerformBoundGenerationCleanup,
            };
            status = MapLifecycleStatus(
                zone_load::TryFinishZoneLoadContextAbandonment(
                    &entry->lifecycle_, key, callbacks));
        }
        if (status == ZoneRuntimeTableStatus::Success
            && entry->lifecycle_.phase()
                == zone_load::ZoneLoadContextPhase::Empty)
        {
            ZoneRuntimeTable::setGenerationExecutionMode(
                entry, ZoneRuntimeExecutionMode::Terminal);
        }
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    if (entry->executionMode() == ZoneRuntimeExecutionMode::Terminal)
    {
        return entry->lifecycle_.phase()
                    == zone_load::ZoneLoadContextPhase::Empty
                && entry->lifecycle_.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Unloaded
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidPhase;
    }
    if ((entry->executionMode() != ZoneRuntimeExecutionMode::Live
            && entry->executionMode()
                != ZoneRuntimeExecutionMode::Unloading)
        || entry->setupStage() < ZoneRuntimeSetupStage::ScriptStringsBegun)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (static_cast<SharedState>(table->sharedState_)
        == SharedState::Draining)
    {
        return ZoneRuntimeTableStatus::Busy;
    }

    ZoneRuntimeTable::setGenerationExecutionMode(
        entry, ZoneRuntimeExecutionMode::Unloading);
    const zone_load::ZoneLoadCleanupCallbacks callbacks{
        entry,
        &ZoneRuntimeTable::PerformBoundGenerationCleanup,
    };
    const auto ownershipStatus = zone_script_string_ownership::
        TryUnloadLiveZoneScriptStringOwnership(
            &entry->scriptStringOwnership_,
            &entry->lifecycle_,
            key,
            callbacks);
    const ZoneRuntimeTableStatus status =
        MapLiveUnloadOwnershipStatus(ownershipStatus);
    if (ownershipStatus
        == zone_script_string_ownership::
            ZoneScriptStringOwnershipStatus::Success)
    {
        ZoneRuntimeTable::setGenerationExecutionMode(
            entry, ZoneRuntimeExecutionMode::Terminal);
    }
    return table->completeCompositeOperation(physicalSlot, key, status);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *const table,
    const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept
{
    if (!table
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), &callback, sizeof(callback)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const zone_pending_copy::PendingCopyDrainCallback callbackSnapshot =
        callback;
    if (!callbackSnapshot.consume)
        return ZoneRuntimeTableStatus::InvalidArgument;
    const ZoneRuntimeTableStatus tableStatus =
        table->validateInitializedHeader();
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (callbackSnapshot.context
        && (!pmem_runtime::StorageIsOutsideManagedMemory(
                callbackSnapshot.context, 1)
            || !AddressRangesAreDisjoint(
                table,
                sizeof(*table),
                callbackSnapshot.context,
                1)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }

    const SharedState state =
        static_cast<SharedState>(table->sharedState_);
    if (state == SharedState::Pristine)
        return ZoneRuntimeTableStatus::InvalidPhase;
    if (state == SharedState::Draining)
    {
        return PendingDrainCallbacksMatch(
                table->pendingDrainCallback_, callbackSnapshot)
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (state != SharedState::Ready
        || !PendingDrainCallbackIsEmpty(table->pendingDrainCallback_))
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const auto pendingStatus = zone_pending_copy::TryBeginPendingCopyDrain(
        &table->pendingCopyLedger_);
    const ZoneRuntimeTableStatus status = MapPendingStatus(pendingStatus);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        table->poison();
        return status;
    }
    if (pendingStatus == zone_pending_copy::PendingCopyStatus::Success)
    {
        table->pendingDrainCallback_ = callbackSnapshot;
        table->sharedState_ =
            static_cast<std::uint32_t>(SharedState::Draining);
    }
    if (status == ZoneRuntimeTableStatus::Success)
    {
        const ZoneRuntimeTableStatus post =
            table->validateInitializedHeader();
        if (post != ZoneRuntimeTableStatus::Success)
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    return status;
}

ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy(
    ZoneRuntimeTable *const table) noexcept
{
    const ZoneRuntimeTableStatus tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (static_cast<SharedState>(table->sharedState_)
            != SharedState::Draining
        || !table->pendingDrainCallback_.consume)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    const auto pendingStatus = zone_pending_copy::TryDrainNextPendingCopy(
        &table->pendingCopyLedger_, table->pendingDrainCallback_);
    const ZoneRuntimeTableStatus status = MapPendingStatus(pendingStatus);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        table->poison();
        return status;
    }
    const ZoneRuntimeTableStatus post =
        table->validateInitializedHeader();
    if (post != ZoneRuntimeTableStatus::Success)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return status;
}

ZoneRuntimeTableStatus TryFinishZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *const table) noexcept
{
    const ZoneRuntimeTableStatus tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    const SharedState state =
        static_cast<SharedState>(table->sharedState_);
    if (state != SharedState::Ready && state != SharedState::Draining)
        return ZoneRuntimeTableStatus::InvalidPhase;

    const auto pendingStatus = zone_pending_copy::TryFinishPendingCopyDrain(
        &table->pendingCopyLedger_);
    const ZoneRuntimeTableStatus status = MapPendingStatus(pendingStatus);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        table->poison();
        return status;
    }
    if (pendingStatus == zone_pending_copy::PendingCopyStatus::Success)
    {
        table->pendingDrainCallback_ = {};
        table->sharedState_ =
            static_cast<std::uint32_t>(SharedState::Ready);
    }
    if (status == ZoneRuntimeTableStatus::Success)
    {
        const ZoneRuntimeTableStatus post =
            table->validateInitializedHeader();
        if (post != ZoneRuntimeTableStatus::Success)
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    return status;
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    script_string_journal::ScriptStringJournal *const journal,
    script_string_journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t storageCapacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!table || !journal
        || reinterpret_cast<std::uintptr_t>(journal)
                % alignof(script_string_journal::ScriptStringJournal)
            != 0
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), journal, sizeof(*journal)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    if (expectedCount > storageCapacity)
        return ZoneRuntimeTableStatus::CapacityExceeded;
    if ((storageCapacity == 0) != (storage == nullptr))
        return ZoneRuntimeTableStatus::InvalidArgument;
    if (storageCapacity != 0)
    {
        if (reinterpret_cast<std::uintptr_t>(storage)
                % alignof(
                    script_string_journal::ScriptStringJournalEntry)
            != 0
            || static_cast<std::size_t>(storageCapacity)
                > (std::numeric_limits<std::size_t>::max)()
                    / sizeof(*storage))
        {
            return ZoneRuntimeTableStatus::InvalidArgument;
        }
        const std::size_t storageBytes =
            static_cast<std::size_t>(storageCapacity) * sizeof(*storage);
        if (!AddressRangesAreDisjoint(
                table, sizeof(*table), storage, storageBytes))
        {
            return ZoneRuntimeTableStatus::InvalidArgument;
        }
    }
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    const bool composite = !entry->generationBindingPristine();
    if (composite)
    {
        if (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::PendingCopyBegun)
        {
            return ZoneRuntimeTableStatus::InvalidPhase;
        }
        if (!ZoneRuntimeTable::generationPlacementMatches(
                entry,
                journal,
                storage,
                storageCapacity,
                expectedCount))
        {
            return ZoneRuntimeTableStatus::InvalidArgument;
        }
    }

    const auto ownershipStatus = zone_script_string_ownership::
        TryBeginZoneScriptStringOwnership(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            ZoneRuntimeTable::mutableLifecycle(entry),
            key,
            journal,
            storage,
            storageCapacity,
            expectedCount);
    if (composite
        && ownershipStatus
            == zone_script_string_ownership::
                ZoneScriptStringOwnershipStatus::Success)
    {
        ZoneRuntimeTable::retainGenerationPlacement(
            entry,
            journal,
            storage,
            storageCapacity,
            expectedCount);
        ZoneRuntimeTable::setGenerationSetupStage(
            entry, ZoneRuntimeSetupStage::ScriptStringsBegun);
    }
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_adapter::ScriptStringSourceView &source,
    std::uint32_t *const outStringId) noexcept
{
    if (!WritableOutputIsSeparated(
            table,
            outStringId,
            sizeof(*outStringId),
            alignof(std::uint32_t),
            &key)
        || !AddressRangesAreDisjoint(
            &source, sizeof(source), outStringId, sizeof(*outStringId)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!WritableOutputIsSeparatedFromRetainedRuntime(
            table->activeZoneStreamBinding_,
            static_cast<SharedState>(table->sharedState_),
            outStringId,
            sizeof(*outStringId),
            alignof(std::uint32_t)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const bool composite = !entry->generationBindingPristine();
    if (composite
        && (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    std::uint32_t candidate = 0;
    const auto ownershipStatus = zone_script_string_ownership::
        TryStageZoneScriptString(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            source,
            &candidate);
    const ZoneRuntimeTableStatus status =
        table->completeMutableOperation(
            physicalSlot, key, ownershipStatus);
    if (status == ZoneRuntimeTableStatus::Success)
        *outStringId = candidate;
    return status;
}

ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine()
        && (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto ownershipStatus = zone_script_string_ownership::
        TrySealZoneScriptStrings(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine()
        && (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto ownershipStatus = zone_script_string_ownership::
        TryBeginZoneScriptStringTransfer(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine()
        && (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto ownershipStatus = zone_script_string_ownership::
        TryTransferNextZoneScriptString(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine()
        && (entry->executionMode() != ZoneRuntimeExecutionMode::Loading
            || entry->setupStage()
                != ZoneRuntimeSetupStage::ScriptStringsBegun))
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }
    const auto ownershipStatus = zone_script_string_ownership::
        TryPrepareZoneScriptStringCommit(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_script_string_ownership::
        ZoneScriptStringAdmissionCallback &admission) noexcept
{
    if (!table
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), &admission, sizeof(admission)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const zone_script_string_ownership::ZoneScriptStringAdmissionCallback
        admissionSnapshot = admission;
    const ZoneRuntimeTableStatus callbackContextStatus =
        AuthenticateRetainedLegacyCallbackContext(
            table, admissionSnapshot.context);
    if (callbackContextStatus != ZoneRuntimeTableStatus::Success)
        return callbackContextStatus;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;
    const auto ownershipStatus = zone_script_string_ownership::
        TryCommitZoneScriptStringsAndAdmit(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            admissionSnapshot);
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_script_string_ownership::
        ZoneScriptStringRollbackCallbacks &callbacks) noexcept
{
    if (!table
        || !AddressRangesAreDisjoint(
            table, sizeof(*table), &callbacks, sizeof(callbacks)))
        return ZoneRuntimeTableStatus::InvalidArgument;
    const zone_script_string_ownership::ZoneScriptStringRollbackCallbacks
        callbackSnapshot = callbacks;
    const ZoneRuntimeTableStatus callbackContextStatus =
        AuthenticateRetainedLegacyCallbackContext(
            table, callbackSnapshot.context);
    if (callbackContextStatus != ZoneRuntimeTableStatus::Success)
        return callbackContextStatus;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;
    const auto ownershipStatus = zone_script_string_ownership::
        TryBeginZoneScriptStringRollback(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            callbackSnapshot);
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;
    const auto ownershipStatus = zone_script_string_ownership::
        TryRollbackNextZoneScriptString(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    if (!entry->generationBindingPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;
    const auto ownershipStatus = zone_script_string_ownership::
        TryFinishZoneScriptStringAbandonment(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry));
    return table->completeMutableOperation(
        physicalSlot, key, ownershipStatus);
}

ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept
{
    const auto tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactEntry(physicalSlot, key);
    if (authentication != ZoneRuntimeTableStatus::Success)
    {
        if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return authentication;
    }
    if (!entry.generationBinding_.isPristine())
        return ZoneRuntimeTableStatus::InvalidPhase;

    using OwnershipPhase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    const OwnershipPhase phase = entry.scriptStringOwnership_.phase();
    if (phase == OwnershipPhase::Empty)
    {
        return entry.lifecycle_.phase()
                    == zone_load::ZoneLoadContextPhase::Empty
                && entry.lifecycle_.terminalKind()
                    == zone_load::ZoneLoadTerminalKind::Unloaded
            ? ZoneRuntimeTableStatus::Success
            : ZoneRuntimeTableStatus::InvalidPhase;
    }
    if (phase != OwnershipPhase::Live
        && phase != OwnershipPhase::Unloading
        && phase != OwnershipPhase::Unloaded)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    if (!AddressRangesAreDisjoint(
            table, sizeof(*table), &callbacks, sizeof(callbacks)))
    {
        return ZoneRuntimeTableStatus::InvalidArgument;
    }
    const zone_load::ZoneLoadCleanupCallbacks callbackSnapshot = callbacks;
    const ZoneRuntimeTableStatus callbackContextStatus =
        AuthenticateRetainedLegacyCallbackContext(
            table, callbackSnapshot.context);
    if (callbackContextStatus != ZoneRuntimeTableStatus::Success)
        return callbackContextStatus;

    const auto ownershipStatus =
        zone_script_string_ownership::
            TryUnloadLiveZoneScriptStringOwnership(
                &entry.scriptStringOwnership_,
                &entry.lifecycle_,
                key,
                callbackSnapshot);
    const ZoneRuntimeTableStatus status =
        MapLiveUnloadOwnershipStatus(ownershipStatus);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        table->poison();
        return status;
    }

    const ZoneRuntimeTableStatus postAuthentication =
        table->authenticateExactEntry(physicalSlot, key);
    if (postAuthentication != ZoneRuntimeTableStatus::Success)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    if (status == ZoneRuntimeTableStatus::Success
        && (entry.scriptStringOwnership_.phase()
                != OwnershipPhase::Unloaded
            || entry.lifecycle_.phase()
                != zone_load::ZoneLoadContextPhase::Empty
            || entry.lifecycle_.terminalKind()
                != zone_load::ZoneLoadTerminalKind::Unloaded))
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return status;
}

ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const auto tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    if (!static_cast<bool>(key))
        return ZoneRuntimeTableStatus::InvalidKey;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;
    if (key.slot != physicalSlot)
        return ZoneRuntimeTableStatus::StaleKey;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactEntry(physicalSlot, key);
    if (authentication != ZoneRuntimeTableStatus::Success)
    {
        if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return authentication;
    }
    if (entry.lifecycle_.phase() != zone_load::ZoneLoadContextPhase::Empty)
        return ZoneRuntimeTableStatus::InvalidPhase;
    const zone_load::ZoneLoadTerminalKind terminalKind =
        entry.lifecycle_.terminalKind();
    if (terminalKind == zone_load::ZoneLoadTerminalKind::None)
        return ZoneRuntimeTableStatus::InvalidPhase;

    const bool composite = !entry.generationBindingPristine();
    const ZoneRuntimeSetupStage compositeStage = composite
        ? entry.setupStage()
        : ZoneRuntimeSetupStage::Passive;
    if (composite
        && entry.executionMode()
            != ZoneRuntimeExecutionMode::Terminal)
    {
        return ZoneRuntimeTableStatus::InvalidPhase;
    }

    using OwnershipPhase = zone_script_string_ownership::
        ZoneScriptStringOwnershipPhase;
    const OwnershipPhase ownershipPhase =
        entry.scriptStringOwnership_.phase();
    const bool phaseMatches = ownershipPhase == OwnershipPhase::Empty
        || (terminalKind == zone_load::ZoneLoadTerminalKind::Abandoned
            && ownershipPhase == OwnershipPhase::Abandoned)
        || (terminalKind == zone_load::ZoneLoadTerminalKind::Unloaded
            && ownershipPhase == OwnershipPhase::Unloaded);
    if (!phaseMatches)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const auto ownershipStatus =
        zone_script_string_ownership::
            TryResetTerminalZoneScriptStringOwnership(
                &entry.scriptStringOwnership_,
                &entry.lifecycle_,
                key,
                terminalKind);
    if (ownershipStatus
        != zone_script_string_ownership::
            ZoneScriptStringOwnershipStatus::Success)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    if (composite
        && compositeStage >= ZoneRuntimeSetupStage::PendingCopyBegun)
    {
        const auto pendingStatus =
            zone_pending_copy::TryResetPendingCopyAdmissionReceipt(
                ZoneRuntimeTable::mutablePendingCopyAdmissionReceipt(
                    &entry),
                key);
        if (pendingStatus != zone_pending_copy::PendingCopyStatus::Success
            && pendingStatus
                != zone_pending_copy::PendingCopyStatus::AlreadyComplete)
        {
            table->poison();
            return ZoneRuntimeTableStatus::UnsafeFailure;
        }
    }
    if (composite)
        ZoneRuntimeTable::resetCompositeReceiptsAndBinding(&entry);

    const ZoneRuntimeTableStatus tablePostAuthentication =
        table->validateInitializedHeader();
    const ZoneRuntimeTableStatus postAuthentication =
        tablePostAuthentication == ZoneRuntimeTableStatus::Success
        ? table->authenticateExactEntry(physicalSlot, key)
        : tablePostAuthentication;
    if (postAuthentication != ZoneRuntimeTableStatus::Success
        || !entry.scriptStringOwnership_.isEmptyCanonical()
        || entry.lifecycle_.phase()
            != zone_load::ZoneLoadContextPhase::Empty
        || entry.lifecycle_.terminalKind() != terminalKind
        || entry.key_ != key)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    return ZoneRuntimeTableStatus::Success;
}

} // namespace db::zone_runtime

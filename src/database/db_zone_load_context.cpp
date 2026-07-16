#include <database/db_zone_load_context.h>

#include <limits>

namespace db::zone_load
{
namespace
{
constexpr std::uint8_t kInitializedFlag = UINT8_C(1) << 0;
constexpr std::uint8_t kCleanupActiveFlag = UINT8_C(1) << 1;
constexpr std::uint8_t kCleanupPoisonedFlag = UINT8_C(1) << 2;
constexpr std::uint8_t kKnownFlags =
    kInitializedFlag | kCleanupActiveFlag | kCleanupPoisonedFlag;

[[nodiscard]] constexpr bool IsKnownPhase(
    const ZoneLoadContextPhase phase) noexcept
{
    switch (phase)
    {
    case ZoneLoadContextPhase::Empty:
    case ZoneLoadContextPhase::Loading:
    case ZoneLoadContextPhase::Live:
    case ZoneLoadContextPhase::Abandoning:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownTerminalKind(
    const ZoneLoadTerminalKind terminalKind) noexcept
{
    switch (terminalKind)
    {
    case ZoneLoadTerminalKind::None:
    case ZoneLoadTerminalKind::Abandoned:
    case ZoneLoadTerminalKind::Unloaded:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownCleanupOperation(
    const ZoneLoadCleanupOperation operation) noexcept
{
    switch (operation)
    {
    case ZoneLoadCleanupOperation::CancelLoadInputAndInflate:
    case ZoneLoadCleanupOperation::AbortNativeAdapterTransactions:
    case ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable:
    case ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences:
    case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:
    case ZoneLoadCleanupOperation::ReleaseGeometry:
    case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:
    case ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation:
    case ZoneLoadCleanupOperation::FreePhysicalMemory:
    case ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal:
    case ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles:
    case ZoneLoadCleanupOperation::ReleaseSlot:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownCallbackStatus(
    const ZoneLoadCleanupCallbackStatus status) noexcept
{
    switch (status)
    {
    case ZoneLoadCleanupCallbackStatus::Success:
    case ZoneLoadCleanupCallbackStatus::Retry:
    case ZoneLoadCleanupCallbackStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsNullKey(
    const ZoneLoadContextKey &key) noexcept
{
    return key.generation == 0 && key.slot == kInvalidZoneLoadSlot
        && key.reserved == 0;
}

[[nodiscard]] constexpr bool IsValidKey(
    const ZoneLoadContextKey &key) noexcept
{
    return static_cast<bool>(key);
}

[[nodiscard]] constexpr bool IsAbandonmentCleanupOperation(
    const ZoneLoadCleanupOperation operation) noexcept
{
    switch (operation)
    {
    case ZoneLoadCleanupOperation::CancelLoadInputAndInflate:
    case ZoneLoadCleanupOperation::AbortNativeAdapterTransactions:
    case ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable:
    case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:
    case ZoneLoadCleanupOperation::ReleaseGeometry:
    case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:
    case ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation:
    case ZoneLoadCleanupOperation::FreePhysicalMemory:
    case ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsLiveUnloadCleanupOperation(
    const ZoneLoadCleanupOperation operation) noexcept
{
    switch (operation)
    {
    case ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences:
    case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:
    case ZoneLoadCleanupOperation::ReleaseGeometry:
    case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:
    case ZoneLoadCleanupOperation::FreePhysicalMemory:
    case ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsCleanupOperationForTerminal(
    const ZoneLoadTerminalKind terminalKind,
    const ZoneLoadCleanupOperation operation) noexcept
{
    switch (terminalKind)
    {
    case ZoneLoadTerminalKind::Abandoned:
        return IsAbandonmentCleanupOperation(operation);
    case ZoneLoadTerminalKind::Unloaded:
        return IsLiveUnloadCleanupOperation(operation);
    default:
        return false;
    }
}

[[nodiscard]] ZoneLoadContextStatus NextGeneration(
    const std::uint64_t currentGeneration,
    std::uint64_t *const outGeneration) noexcept
{
    if (!outGeneration)
        return ZoneLoadContextStatus::InvalidArgument;
    if (currentGeneration
        == (std::numeric_limits<std::uint64_t>::max)())
    {
        return ZoneLoadContextStatus::GenerationExhausted;
    }
    const std::uint64_t nextGeneration = currentGeneration + 1;
    if (nextGeneration == 0)
        return ZoneLoadContextStatus::GenerationExhausted;
    *outGeneration = nextGeneration;
    return ZoneLoadContextStatus::Success;
}

[[nodiscard]] bool AdvanceAbandonmentCleanupOperation(
    ZoneLoadCleanupOperation *const operation) noexcept
{
    if (!operation)
        return false;
    switch (*operation)
    {
    case ZoneLoadCleanupOperation::CancelLoadInputAndInflate:
        *operation =
            ZoneLoadCleanupOperation::AbortNativeAdapterTransactions;
        return true;
    case ZoneLoadCleanupOperation::AbortNativeAdapterTransactions:
        *operation =
            ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable;
        return true;
    case ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable:
        *operation =
            ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState;
        return true;
    case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:
        *operation = ZoneLoadCleanupOperation::ReleaseGeometry;
        return true;
    case ZoneLoadCleanupOperation::ReleaseGeometry:
        *operation =
            ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars;
        return true;
    case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:
        *operation =
            ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation;
        return true;
    case ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation:
        *operation = ZoneLoadCleanupOperation::FreePhysicalMemory;
        return true;
    case ZoneLoadCleanupOperation::FreePhysicalMemory:
        *operation =
            ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal;
        return true;
    case ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal:
        *operation = ZoneLoadCleanupOperation::ReleaseSlot;
        return true;
    case ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences:
    case ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles:
    case ZoneLoadCleanupOperation::ReleaseSlot:
    default:
        return false;
    }
}

[[nodiscard]] bool AdvanceLiveUnloadCleanupOperation(
    ZoneLoadCleanupOperation *const operation) noexcept
{
    if (!operation)
        return false;
    switch (*operation)
    {
    case ZoneLoadCleanupOperation::RemoveLiveAssetsAndReferences:
        *operation =
            ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState;
        return true;
    case ZoneLoadCleanupOperation::InvalidateAliasDirectStreamAndDelayState:
        *operation = ZoneLoadCleanupOperation::ReleaseGeometry;
        return true;
    case ZoneLoadCleanupOperation::ReleaseGeometry:
        *operation =
            ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars;
        return true;
    case ZoneLoadCleanupOperation::TearDownNativeArenaWorkspaceAndSidecars:
        *operation = ZoneLoadCleanupOperation::FreePhysicalMemory;
        return true;
    case ZoneLoadCleanupOperation::FreePhysicalMemory:
        *operation =
            ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles;
        return true;
    case ZoneLoadCleanupOperation::RemoveLiveRegistryAndHandles:
        *operation = ZoneLoadCleanupOperation::ReleaseSlot;
        return true;
    case ZoneLoadCleanupOperation::CancelLoadInputAndInflate:
    case ZoneLoadCleanupOperation::AbortNativeAdapterTransactions:
    case ZoneLoadCleanupOperation::MakePartialAssetsAndStagedReferencesUnreachable:
    case ZoneLoadCleanupOperation::EndPhysicalMemoryAllocation:
    case ZoneLoadCleanupOperation::ClearRegistryLoadingQueueGateAndSignal:
    case ZoneLoadCleanupOperation::ReleaseSlot:
    default:
        return false;
    }
}

[[nodiscard]] bool AdvanceCleanupOperation(
    const ZoneLoadTerminalKind terminalKind,
    ZoneLoadCleanupOperation *const operation) noexcept
{
    switch (terminalKind)
    {
    case ZoneLoadTerminalKind::Abandoned:
        return AdvanceAbandonmentCleanupOperation(operation);
    case ZoneLoadTerminalKind::Unloaded:
        return AdvanceLiveUnloadCleanupOperation(operation);
    default:
        return false;
    }
}

[[nodiscard]] bool HasCallbacks(
    const ZoneLoadCleanupCallbacks &callbacks) noexcept
{
    return callbacks.context != nullptr && callbacks.perform != nullptr;
}

} // namespace

bool ZoneLoadContextSlot::isCanonical() const noexcept
{
    if ((flags_ & ~kKnownFlags) != 0
        || !IsKnownPhase(phase_)
        || !IsKnownTerminalKind(terminalKind_)
        || !IsKnownCleanupOperation(nextCleanupOperation_))
    {
        return false;
    }

    const bool isInitialized =
        (flags_ & kInitializedFlag) != 0;
    const bool isCleanupActive =
        (flags_ & kCleanupActiveFlag) != 0;
    const bool isCleanupPoisoned =
        (flags_ & kCleanupPoisonedFlag) != 0;
    if (!isInitialized)
    {
        return generation_ == 0
            && slotIndex_ == kInvalidZoneLoadSlot
            && phase_ == ZoneLoadContextPhase::Empty
            && nextCleanupOperation_
                == ZoneLoadCleanupOperation::
                    CancelLoadInputAndInflate
            && terminalKind_ == ZoneLoadTerminalKind::None
            && !isCleanupActive && !isCleanupPoisoned;
    }
    if (slotIndex_ == kInvalidZoneLoadSlot)
        return false;

    switch (phase_)
    {
    case ZoneLoadContextPhase::Empty:
        return !isCleanupActive && !isCleanupPoisoned
            && nextCleanupOperation_
                == ZoneLoadCleanupOperation::
                    CancelLoadInputAndInflate
            && (generation_ != 0
                || terminalKind_ == ZoneLoadTerminalKind::None);
    case ZoneLoadContextPhase::Loading:
        return generation_ != 0
            && terminalKind_ == ZoneLoadTerminalKind::None
            && nextCleanupOperation_
                == ZoneLoadCleanupOperation::
                    CancelLoadInputAndInflate
            && !isCleanupActive && !isCleanupPoisoned;
    case ZoneLoadContextPhase::Live:
        return generation_ != 0
            && terminalKind_ == ZoneLoadTerminalKind::None
            && nextCleanupOperation_
                == ZoneLoadCleanupOperation::
                    RemoveLiveAssetsAndReferences
            && !isCleanupActive && !isCleanupPoisoned;
    case ZoneLoadContextPhase::Abandoning:
        return generation_ != 0
            && terminalKind_ != ZoneLoadTerminalKind::None
            && !(
                isCleanupActive
                && isCleanupPoisoned)
            && IsCleanupOperationForTerminal(
                terminalKind_, nextCleanupOperation_);
    default:
        return false;
    }
}

ZoneLoadContextStatus ZoneLoadContextSlot::validate() const noexcept
{
    if (!isCanonical()
        || (flags_ & kInitializedFlag) == 0)
    {
        return ZoneLoadContextStatus::InvalidState;
    }
    if ((flags_ & kCleanupActiveFlag) != 0)
        return ZoneLoadContextStatus::Busy;
    if ((flags_ & kCleanupPoisonedFlag) != 0)
        return ZoneLoadContextStatus::UnsafeFailure;
    return ZoneLoadContextStatus::Success;
}

ZoneLoadContextStatus ZoneLoadContextSlot::validateKey(
    const ZoneLoadContextKey &key) const noexcept
{
    if (!IsValidKey(key))
        return ZoneLoadContextStatus::InvalidKey;
    if (key.slot != slotIndex_
        || key.generation != generation_)
    {
        return ZoneLoadContextStatus::StaleKey;
    }
    return ZoneLoadContextStatus::Success;
}

void ZoneLoadContextSlot::poisonCleanup() noexcept
{
    flags_ = static_cast<std::uint8_t>(
        (flags_ & ~kCleanupActiveFlag)
        | kCleanupPoisonedFlag);
}

ZoneLoadContextStatus ZoneLoadContextSlot::runCleanup(
    const ZoneLoadCleanupCallbacks &callbacks) noexcept
{
    if ((flags_ & kCleanupPoisonedFlag) != 0)
        return ZoneLoadContextStatus::UnsafeFailure;
    if (!HasCallbacks(callbacks))
        return ZoneLoadContextStatus::InvalidArgument;

    while (nextCleanupOperation_
        != ZoneLoadCleanupOperation::ReleaseSlot)
    {
        const ZoneLoadCleanupOperation operation =
            nextCleanupOperation_;
        flags_ = static_cast<std::uint8_t>(
            flags_ | kCleanupActiveFlag);
        const ZoneLoadCleanupCallbackStatus callbackStatus =
            callbacks.perform(callbacks.context, operation);
        flags_ = static_cast<std::uint8_t>(
            flags_ & ~kCleanupActiveFlag);

        if (!IsKnownCallbackStatus(callbackStatus)
            || callbackStatus
                == ZoneLoadCleanupCallbackStatus::UnsafeFailure)
        {
            poisonCleanup();
            return ZoneLoadContextStatus::UnsafeFailure;
        }
        if (callbackStatus == ZoneLoadCleanupCallbackStatus::Retry)
            return ZoneLoadContextStatus::Retry;
        if (!AdvanceCleanupOperation(
                terminalKind_, &nextCleanupOperation_))
        {
            poisonCleanup();
            return ZoneLoadContextStatus::UnsafeFailure;
        }
    }

    // Slot release is controller-owned and always follows the successful final
    // callback in the selected recipe. Preserve generation/terminalKind_ as a
    // receipt so an exact final retry is idempotent until the next claim.
    phase_ = ZoneLoadContextPhase::Empty;
    nextCleanupOperation_ =
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate;
    flags_ = kInitializedFlag;
    return ZoneLoadContextStatus::Success;
}

bool ZoneLoadContextSlot::initialized() const noexcept
{
    return isCanonical()
        && (flags_ & kInitializedFlag) != 0;
}

std::uint32_t ZoneLoadContextSlot::slotIndex() const noexcept
{
    return slotIndex_;
}

std::uint64_t ZoneLoadContextSlot::generation() const noexcept
{
    return generation_;
}

ZoneLoadContextPhase ZoneLoadContextSlot::phase() const noexcept
{
    return phase_;
}

ZoneLoadTerminalKind ZoneLoadContextSlot::terminalKind() const noexcept
{
    return terminalKind_;
}

ZoneLoadCleanupOperation
ZoneLoadContextSlot::nextCleanupOperation() const noexcept
{
    return nextCleanupOperation_;
}

bool ZoneLoadContextSlot::cleanupActive() const noexcept
{
    return (flags_ & kCleanupActiveFlag) != 0;
}

bool ZoneLoadContextSlot::cleanupPoisoned() const noexcept
{
    return (flags_ & kCleanupPoisonedFlag) != 0;
}

ZoneLoadContextStatus TryInitializeZoneLoadContextSlot(
    ZoneLoadContextSlot *const slot,
    const std::uint32_t slotIndex,
    const std::uint64_t initialGeneration) noexcept
{
    if (!slot || slotIndex == kInvalidZoneLoadSlot)
        return ZoneLoadContextStatus::InvalidArgument;
    if (!slot->isCanonical())
        return ZoneLoadContextStatus::InvalidState;
    if ((slot->flags_ & kInitializedFlag) != 0)
        return ZoneLoadContextStatus::InvalidPhase;

    slot->generation_ = initialGeneration;
    slot->slotIndex_ = slotIndex;
    slot->phase_ = ZoneLoadContextPhase::Empty;
    slot->nextCleanupOperation_ =
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate;
    slot->terminalKind_ = ZoneLoadTerminalKind::None;
    slot->flags_ = kInitializedFlag;
    return ZoneLoadContextStatus::Success;
}

ZoneLoadContextStatus TryClaimZoneLoadContext(
    ZoneLoadContextSlot *const slot,
    ZoneLoadContextKey *const inOutKey) noexcept
{
    if (!inOutKey)
        return ZoneLoadContextStatus::InvalidArgument;
    if (!slot)
        return ZoneLoadContextStatus::InvalidArgument;
    const ZoneLoadContextStatus slotStatus = slot->validate();
    if (slotStatus != ZoneLoadContextStatus::Success)
        return slotStatus;

    if (IsValidKey(*inOutKey))
    {
        const ZoneLoadContextStatus keyStatus =
            slot->validateKey(*inOutKey);
        if (keyStatus != ZoneLoadContextStatus::Success)
            return keyStatus;
        if (slot->phase_ == ZoneLoadContextPhase::Loading)
            return ZoneLoadContextStatus::Success;
        return slot->phase_ == ZoneLoadContextPhase::Empty
            ? ZoneLoadContextStatus::StaleKey
            : ZoneLoadContextStatus::InvalidPhase;
    }
    if (!IsNullKey(*inOutKey))
        return ZoneLoadContextStatus::InvalidKey;
    if (slot->phase_ != ZoneLoadContextPhase::Empty)
        return ZoneLoadContextStatus::InvalidPhase;

    std::uint64_t nextGeneration = 0;
    const ZoneLoadContextStatus generationStatus =
        NextGeneration(slot->generation_, &nextGeneration);
    if (generationStatus != ZoneLoadContextStatus::Success)
        return generationStatus;

    const ZoneLoadContextKey candidate{
        nextGeneration,
        slot->slotIndex_,
        0};
    slot->generation_ = nextGeneration;
    slot->phase_ = ZoneLoadContextPhase::Loading;
    slot->nextCleanupOperation_ =
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate;
    slot->terminalKind_ = ZoneLoadTerminalKind::None;
    slot->flags_ = kInitializedFlag;
    *inOutKey = candidate;
    return ZoneLoadContextStatus::Success;
}

ZoneLoadContextStatus TryCommitZoneLoadContext(
    ZoneLoadContextSlot *const slot,
    const ZoneLoadContextKey &key) noexcept
{
    if (!slot)
        return ZoneLoadContextStatus::InvalidArgument;
    ZoneLoadContextStatus status = slot->validate();
    if (status == ZoneLoadContextStatus::Success)
        status = slot->validateKey(key);
    if (status != ZoneLoadContextStatus::Success)
        return status;
    if (slot->phase_ == ZoneLoadContextPhase::Loading)
    {
        slot->phase_ = ZoneLoadContextPhase::Live;
        slot->nextCleanupOperation_ =
            ZoneLoadCleanupOperation::
                RemoveLiveAssetsAndReferences;
        return ZoneLoadContextStatus::Success;
    }
    if (slot->phase_ == ZoneLoadContextPhase::Live)
        return ZoneLoadContextStatus::Success;
    if (slot->phase_ == ZoneLoadContextPhase::Empty)
        return ZoneLoadContextStatus::StaleKey;
    return ZoneLoadContextStatus::InvalidPhase;
}

ZoneLoadContextStatus TryBeginZoneLoadContextAbandonment(
    ZoneLoadContextSlot *const slot,
    const ZoneLoadContextKey &key) noexcept
{
    if (!slot)
        return ZoneLoadContextStatus::InvalidArgument;
    ZoneLoadContextStatus status = slot->validate();
    if (status == ZoneLoadContextStatus::Success)
        status = slot->validateKey(key);
    if (status != ZoneLoadContextStatus::Success)
        return status;

    if (slot->phase_ == ZoneLoadContextPhase::Empty)
    {
        return slot->terminalKind_
                == ZoneLoadTerminalKind::Abandoned
            ? ZoneLoadContextStatus::Success
            : ZoneLoadContextStatus::InvalidPhase;
    }
    if (slot->phase_ == ZoneLoadContextPhase::Abandoning)
    {
        if (slot->terminalKind_
            != ZoneLoadTerminalKind::Abandoned)
        {
            return ZoneLoadContextStatus::InvalidPhase;
        }
        return ZoneLoadContextStatus::Success;
    }
    if (slot->phase_ != ZoneLoadContextPhase::Loading)
        return ZoneLoadContextStatus::InvalidPhase;

    slot->phase_ = ZoneLoadContextPhase::Abandoning;
    slot->nextCleanupOperation_ =
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate;
    slot->terminalKind_ = ZoneLoadTerminalKind::Abandoned;
    slot->flags_ = kInitializedFlag;
    return ZoneLoadContextStatus::Success;
}

ZoneLoadContextStatus TryFinishZoneLoadContextAbandonment(
    ZoneLoadContextSlot *const slot,
    const ZoneLoadContextKey &key,
    const ZoneLoadCleanupCallbacks &callbacks) noexcept
{
    if (!slot)
        return ZoneLoadContextStatus::InvalidArgument;
    ZoneLoadContextStatus status = slot->validate();
    if (status == ZoneLoadContextStatus::Success)
        status = slot->validateKey(key);
    if (status != ZoneLoadContextStatus::Success)
        return status;

    if (slot->phase_ == ZoneLoadContextPhase::Empty)
    {
        return slot->terminalKind_
                == ZoneLoadTerminalKind::Abandoned
            ? ZoneLoadContextStatus::Success
            : ZoneLoadContextStatus::InvalidPhase;
    }
    if (slot->phase_ != ZoneLoadContextPhase::Abandoning
        || slot->terminalKind_
            != ZoneLoadTerminalKind::Abandoned)
    {
        return ZoneLoadContextStatus::InvalidPhase;
    }
    return slot->runCleanup(callbacks);
}

ZoneLoadContextStatus TryUnloadZoneLoadContext(
    ZoneLoadContextSlot *const slot,
    const ZoneLoadContextKey &key,
    const ZoneLoadCleanupCallbacks &callbacks) noexcept
{
    if (!slot)
        return ZoneLoadContextStatus::InvalidArgument;
    ZoneLoadContextStatus status = slot->validate();
    if (status == ZoneLoadContextStatus::Success)
        status = slot->validateKey(key);
    if (status != ZoneLoadContextStatus::Success)
        return status;

    if (slot->phase_ == ZoneLoadContextPhase::Empty)
    {
        return slot->terminalKind_
                == ZoneLoadTerminalKind::Unloaded
            ? ZoneLoadContextStatus::Success
            : ZoneLoadContextStatus::InvalidPhase;
    }
    if (slot->phase_ == ZoneLoadContextPhase::Live)
    {
        if (!HasCallbacks(callbacks))
            return ZoneLoadContextStatus::InvalidArgument;
        slot->phase_ = ZoneLoadContextPhase::Abandoning;
        slot->nextCleanupOperation_ =
            ZoneLoadCleanupOperation::
                RemoveLiveAssetsAndReferences;
        slot->terminalKind_ = ZoneLoadTerminalKind::Unloaded;
        slot->flags_ = kInitializedFlag;
    }
    else if (slot->phase_ != ZoneLoadContextPhase::Abandoning
        || slot->terminalKind_ != ZoneLoadTerminalKind::Unloaded)
    {
        return ZoneLoadContextStatus::InvalidPhase;
    }
    return slot->runCleanup(callbacks);
}

bool ZoneLoadContextKeyMatches(
    const ZoneLoadContextSlot *const slot,
    const ZoneLoadContextKey &key) noexcept
{
    return slot && slot->isCanonical()
        && (slot->flags_ & kInitializedFlag) != 0
        && slot->phase_ != ZoneLoadContextPhase::Empty
        && IsValidKey(key)
        && key.slot == slot->slotIndex_
        && key.generation == slot->generation_;
}
} // namespace db::zone_load

#include <database/db_zone_runtime_table.h>

namespace db::zone_runtime
{
enum class TableState : std::uint32_t
{
    Uninitialized,
    Initialized,
    Poisoned,
};

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
        && IsEmptyOwnership(entry.scriptStringOwnership());
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

[[nodiscard]] ZoneRuntimeTableStatus ValidateEntryBinding(
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

[[nodiscard]] ZoneRuntimeTableStatus AuthenticateExactEntry(
    const ZoneRuntimeEntry &entry,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    const ZoneRuntimeTableStatus bindingStatus =
        ValidateEntryBinding(entry, physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
        return bindingStatus;
    if (entry.key() != key
        || entry.lifecycle().slotIndex() != physicalSlot
        || entry.lifecycle().generation() != key.generation)
    {
        return ZoneRuntimeTableStatus::StaleKey;
    }
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTable g_productionZoneRuntimeTable{};
} // namespace

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
        AuthenticateExactEntry(entry, physicalSlot, key);
    if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)
        poison();
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    *outEntry = &entry;
    return ZoneRuntimeTableStatus::Success;
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

bool ZoneRuntimeTable::initialized() const noexcept
{
    return state_ == static_cast<std::uint32_t>(TableState::Initialized)
        && reserved_ == 0;
}

ZoneRuntimeTableStatus
ZoneRuntimeTable::validateInitializedHeader() noexcept
{
    if (!HasKnownState(state_) || reserved_ != 0)
    {
        poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }
    switch (static_cast<TableState>(state_))
    {
    case TableState::Initialized:
        return ZoneRuntimeTableStatus::Success;
    case TableState::Poisoned:
        return ZoneRuntimeTableStatus::UnsafeFailure;
    case TableState::Uninitialized:
    default:
        return ZoneRuntimeTableStatus::InvalidState;
    }
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
    if (!HasKnownState(table->state_) || table->reserved_ != 0)
    {
        table->poison();
        return ZoneRuntimeTableStatus::UnsafeFailure;
    }

    const TableState state = static_cast<TableState>(table->state_);
    if (state == TableState::Poisoned)
        return ZoneRuntimeTableStatus::UnsafeFailure;

    if (state == TableState::Initialized)
    {
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
                ValidateEntryBinding(entry, physicalSlot);
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

ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const ZoneRuntimeEntry **const outEntry) noexcept
{
    if (!outEntry)
        return ZoneRuntimeTableStatus::InvalidArgument;
    const auto tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const auto bindingStatus =
        ValidateEntryBinding(entry, physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
    }

    *outEntry = &entry;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *const inOutKey) noexcept
{
    if (!inOutKey)
        return ZoneRuntimeTableStatus::InvalidArgument;
    const auto tableStatus = table
        ? table->validateInitializedHeader()
        : ZoneRuntimeTableStatus::InvalidArgument;
    if (tableStatus != ZoneRuntimeTableStatus::Success)
        return tableStatus;
    const auto slotStatus = ValidateUsableSlot(physicalSlot);
    if (slotStatus != ZoneRuntimeTableStatus::Success)
        return slotStatus;

    ZoneRuntimeEntry &entry = table->entries_[physicalSlot];
    const auto bindingStatus =
        ValidateEntryBinding(entry, physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
    }

    if (entry.lifecycle_.phase() == zone_load::ZoneLoadContextPhase::Empty
        && !IsEmptyOwnership(entry.scriptStringOwnership_))
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
    *inOutKey = candidate;
    return ZoneRuntimeTableStatus::Success;
}

ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *const outView) noexcept
{
    if (!outView)
        return ZoneRuntimeTableStatus::InvalidArgument;
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
    const auto bindingStatus =
        ValidateEntryBinding(entry, physicalSlot);
    if (bindingStatus != ZoneRuntimeTableStatus::Success)
    {
        if (bindingStatus == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return bindingStatus;
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

ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership(
    ZoneRuntimeTable *const table,
    const std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    script_string_journal::ScriptStringJournal *const journal,
    script_string_journal::ScriptStringJournalEntry *const storage,
    const std::uint32_t storageCapacity,
    const std::uint32_t expectedCount) noexcept
{
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

    const auto ownershipStatus = zone_script_string_ownership::
        TryBeginZoneScriptStringOwnership(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            ZoneRuntimeTable::mutableLifecycle(entry),
            key,
            journal,
            storage,
            storageCapacity,
            expectedCount);
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
    if (!table || !outStringId)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;

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
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    const auto ownershipStatus = zone_script_string_ownership::
        TryCommitZoneScriptStringsAndAdmit(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            admission);
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
    if (!table)
        return ZoneRuntimeTableStatus::InvalidArgument;
    ZoneRuntimeEntry *entry = nullptr;
    const ZoneRuntimeTableStatus authentication =
        table->authenticateExactMutableEntry(physicalSlot, key, &entry);
    if (authentication != ZoneRuntimeTableStatus::Success)
        return authentication;
    const auto ownershipStatus = zone_script_string_ownership::
        TryBeginZoneScriptStringRollback(
            ZoneRuntimeTable::mutableScriptStringOwnership(entry),
            callbacks);
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
        AuthenticateExactEntry(entry, physicalSlot, key);
    if (authentication != ZoneRuntimeTableStatus::Success)
    {
        if (authentication == ZoneRuntimeTableStatus::UnsafeFailure)
            table->poison();
        return authentication;
    }

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

    const auto ownershipStatus =
        zone_script_string_ownership::
            TryUnloadLiveZoneScriptStringOwnership(
                &entry.scriptStringOwnership_,
                &entry.lifecycle_,
                key,
                callbacks);
    const ZoneRuntimeTableStatus status =
        MapOwnershipStatus(ownershipStatus);
    if (status == ZoneRuntimeTableStatus::UnsafeFailure)
    {
        table->poison();
        return status;
    }

    const ZoneRuntimeTableStatus postAuthentication =
        AuthenticateExactEntry(entry, physicalSlot, key);
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
        AuthenticateExactEntry(entry, physicalSlot, key);
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

    const ZoneRuntimeTableStatus postAuthentication =
        AuthenticateExactEntry(entry, physicalSlot, key);
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

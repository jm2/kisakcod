#include <database/db_script_string_adapter.h>

#include <script/scr_string_transaction.h>

namespace db::script_string_adapter
{
namespace
{
using script_string_journal::ScriptStringAcquireCallbackStatus;
using script_string_journal::ScriptStringJournalCallbacks;
using script_string_journal::ScriptStringReleaseCallbackStatus;
using script_string_journal::ScriptStringTransferCallbackStatus;

struct AcquireOrdinaryContext final
{
    ScriptStringSourceView source{};
    std::uint32_t acquiredStringId = 0;
};

ScriptStringAcquireCallbackStatus AcquireOrdinary(
    void *const context,
    std::uint32_t *const outStringId) noexcept
{
    if (!context || !outStringId)
        return ScriptStringAcquireCallbackStatus::RejectedNoChange;

    auto &acquire = *static_cast<AcquireOrdinaryContext *>(context);
    const ScriptStringSourceView &source = acquire.source;
    const script_string::AcquireResult result =
        script_string::TryAcquireOrdinaryStringOfSize(
            source.bytes, source.byteCount, source.type);
    switch (result.status)
    {
    case script_string::AcquireStatus::Acquired:
        if (!script_string::IsCurrentRuntimeStringId(result.stringId))
            return ScriptStringAcquireCallbackStatus::UnsafeFailure;
        *outStringId = result.stringId;
        acquire.acquiredStringId = result.stringId;
        return ScriptStringAcquireCallbackStatus::Acquired;
    case script_string::AcquireStatus::InvalidArgumentNoChange:
    case script_string::AcquireStatus::CapacityNoChange:
    case script_string::AcquireStatus::RefCountExhaustedNoChange:
        return ScriptStringAcquireCallbackStatus::RejectedNoChange;
    case script_string::AcquireStatus::UnsafeFailure:
    default:
        return ScriptStringAcquireCallbackStatus::UnsafeFailure;
    }
}

ScriptStringTransferCallbackStatus TransferToDatabaseUser(
    void *,
    const std::uint32_t stringId) noexcept
{
    if (!script_string::IsCurrentRuntimeStringId(stringId))
        return ScriptStringTransferCallbackStatus::UnsafeFailure;

    switch (script_string::TryTransferOrdinaryToDatabaseUser(stringId))
    {
    case script_string::TransferStatus::DatabaseUserClaimed:
        return ScriptStringTransferCallbackStatus::DatabaseUserClaimed;
    case script_string::TransferStatus::DuplicateReleased:
        return ScriptStringTransferCallbackStatus::DuplicateReleased;
    case script_string::TransferStatus::OwnershipMismatchNoChange:
    case script_string::TransferStatus::UnsafeFailure:
    default:
        return ScriptStringTransferCallbackStatus::UnsafeFailure;
    }
}

ScriptStringReleaseCallbackStatus RemoveOrdinary(
    void *,
    const std::uint32_t stringId) noexcept
{
    if (!script_string::IsCurrentRuntimeStringId(stringId))
        return ScriptStringReleaseCallbackStatus::UnsafeFailure;
    return script_string::TryRemoveOrdinaryReference(stringId)
            == script_string::ReleaseStatus::Success
        ? ScriptStringReleaseCallbackStatus::Success
        : ScriptStringReleaseCallbackStatus::UnsafeFailure;
}

ScriptStringReleaseCallbackStatus RemoveDatabaseUser(
    void *,
    const std::uint32_t stringId) noexcept
{
    if (!script_string::IsCurrentRuntimeStringId(stringId))
        return ScriptStringReleaseCallbackStatus::UnsafeFailure;
    return script_string::TryRemoveDatabaseUserReference(stringId)
            == script_string::ReleaseStatus::Success
        ? ScriptStringReleaseCallbackStatus::Success
        : ScriptStringReleaseCallbackStatus::UnsafeFailure;
}

ScriptStringJournalCallbacks MakeCallbacks(void *const context) noexcept
{
    return {
        context,
        AcquireOrdinary,
        TransferToDatabaseUser,
        RemoveOrdinary,
        RemoveDatabaseUser,
    };
}
} // namespace

script_string_journal::ScriptStringJournalStatus
TryStageScriptStringFromSource(
    script_string_journal::ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction,
    const ScriptStringSourceView &source,
    std::uint32_t *const outStringId) noexcept
{
    if (!script_string_transaction::OwnsScriptStringTransaction(transaction))
        return script_string_journal::ScriptStringJournalStatus::InvalidState;
    AcquireOrdinaryContext callbackContext{source, 0};
    const script_string_journal::ScriptStringJournalStatus status =
        script_string_journal::TryStageScriptString(
        journal,
        key,
        MakeCallbacks(&callbackContext));
    if (status == script_string_journal::ScriptStringJournalStatus::Success
        && outStringId)
    {
        *outStringId = callbackContext.acquiredStringId;
    }
    return status;
}

script_string_journal::ScriptStringJournalStatus
TryTransferNextScriptStringToDatabaseUser(
    script_string_journal::ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction) noexcept
{
    if (!script_string_transaction::OwnsScriptStringTransaction(transaction))
        return script_string_journal::ScriptStringJournalStatus::InvalidState;
    return script_string_journal::TryTransferNextScriptString(
        journal, key, MakeCallbacks(nullptr));
}

script_string_journal::ScriptStringJournalStatus
TryRollbackNextScriptStringOwnership(
    script_string_journal::ScriptStringJournal *const journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction) noexcept
{
    if (!script_string_transaction::OwnsScriptStringTransaction(transaction))
        return script_string_journal::ScriptStringJournalStatus::InvalidState;
    return script_string_journal::TryRollbackNextScriptString(
        journal, key, MakeCallbacks(nullptr));
}

} // namespace db::script_string_adapter

#pragma once

#include <database/db_script_string_journal.h>
#include <database/db_script_string_transaction.h>

#include <cstdint>

namespace db::script_string_adapter
{
struct ScriptStringSourceView final
{
    const char *bytes = nullptr;
    std::uint32_t byteCount = 0;
    int type = 0;
};

// The callback table remains private so callers cannot accidentally combine
// journal bookkeeping with a reporting SL_* ownership operation. A future
// whole-zone controller must bind one token to one journal/key from
// initialization through terminal commit or rollback; these callback-bearing
// primitives alone do not enforce that lifetime.
//
// When outStringId is non-null, staging publishes the acquired runtime ID only
// after the journal has durably appended the matching ownership entry. The
// caller's output is unchanged on every non-Success result. Omitting the
// output remains useful for ownership-only rollback fixtures.
[[nodiscard]] script_string_journal::ScriptStringJournalStatus
TryStageScriptStringFromSource(
    script_string_journal::ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction,
    const ScriptStringSourceView &source,
    std::uint32_t *outStringId = nullptr) noexcept;

[[nodiscard]] script_string_journal::ScriptStringJournalStatus
TryTransferNextScriptStringToDatabaseUser(
    script_string_journal::ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction) noexcept;

[[nodiscard]] script_string_journal::ScriptStringJournalStatus
TryRollbackNextScriptStringOwnership(
    script_string_journal::ScriptStringJournal *journal,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_transaction::ScriptStringTransactionToken &transaction) noexcept;

} // namespace db::script_string_adapter

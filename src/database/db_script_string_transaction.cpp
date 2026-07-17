#include <database/db_script_string_transaction.h>

#include <qcommon/sys_sync.h>

namespace db::script_string_transaction
{
namespace
{
std::uint32_t s_nextSerial = 0;
std::uint32_t s_activeSerial = 0;
}

bool ScriptStringTransactionToken::active() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const bool active = active_;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return active;
}

std::uint32_t ScriptStringTransactionToken::serial() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const std::uint32_t serial = serial_;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return serial;
}

ScriptStringTransactionStatus TryBeginScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    if (!token)
        return ScriptStringTransactionStatus::InvalidArgument;

    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    if (token->active_ || token->serial_ != 0
        || token->reserved_[0] != 0 || token->reserved_[1] != 0
        || token->reserved_[2] != 0)
    {
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return ScriptStringTransactionStatus::InvalidToken;
    }

    if (s_activeSerial != 0)
    {
        // Recursive acquisition on the owning thread must not create an
        // overlapping journal. Other threads remain blocked at Enter until
        // the active transaction reaches Finish.
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return ScriptStringTransactionStatus::Busy;
    }

    ++s_nextSerial;
    if (s_nextSerial == 0)
        ++s_nextSerial;
    s_activeSerial = s_nextSerial;
    token->serial_ = s_activeSerial;
    token->active_ = true;
    return ScriptStringTransactionStatus::Success;
}

ScriptStringTransactionStatus FinishScriptStringTransaction(
    ScriptStringTransactionToken *const token) noexcept
{
    if (!token)
        return ScriptStringTransactionStatus::InvalidArgument;

    // Correct owners re-enter recursively. A foreign thread waits for the
    // owner to finish, then observes the cleared token under this mutex and
    // fails without ever unlocking another thread's base acquisition.
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    if (!token->active_ || token->serial_ == 0
        || token->reserved_[0] != 0 || token->reserved_[1] != 0
        || token->reserved_[2] != 0 || s_activeSerial != token->serial_)
    {
        // Fail closed: an invalid finish must retain the base acquisition.
        Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
        return ScriptStringTransactionStatus::InvalidToken;
    }

    s_activeSerial = 0;
    token->serial_ = 0;
    token->active_ = false;
    // Drop this validation acquisition, then the base acquisition retained by
    // TryBeginScriptStringTransaction.
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return ScriptStringTransactionStatus::Success;
}

bool OwnsScriptStringTransaction(
    const ScriptStringTransactionToken &token) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    const bool owns = token.active_ && token.serial_ != 0
        && token.reserved_[0] == 0 && token.reserved_[1] == 0
        && token.reserved_[2] == 0
        && token.serial_ == s_activeSerial;
    Sys_LeaveCriticalSection(CRITSECT_DB_SCRIPT_STRING_TRANSACTION);
    return owns;
}

} // namespace db::script_string_transaction

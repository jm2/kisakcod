#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

namespace db::script_string_transaction
{
enum class ScriptStringTransactionStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidToken,
};

class ScriptStringTransactionToken final
{
public:
    ScriptStringTransactionToken() noexcept = default;
    ~ScriptStringTransactionToken() noexcept = default;

    ScriptStringTransactionToken(const ScriptStringTransactionToken &) = delete;
    ScriptStringTransactionToken &operator=(
        const ScriptStringTransactionToken &) = delete;
    ScriptStringTransactionToken(ScriptStringTransactionToken &&) = delete;
    ScriptStringTransactionToken &operator=(
        ScriptStringTransactionToken &&) = delete;

    // These snapshots authenticate under the serializer. A foreign caller can
    // block until the owning transaction reaches Finish.
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint32_t serial() const noexcept;
    // Includes reserved-byte validation so a containing controller cannot
    // silently reuse or discard a corrupted inactive token.
    [[nodiscard]] bool canonicalInactive() const noexcept;

private:
    friend ScriptStringTransactionStatus TryBeginScriptStringTransaction(
        ScriptStringTransactionToken *token) noexcept;
    friend ScriptStringTransactionStatus FinishScriptStringTransaction(
        ScriptStringTransactionToken *token) noexcept;
    friend bool OwnsScriptStringTransaction(
        const ScriptStringTransactionToken &token) noexcept;

    std::uint32_t serial_ = 0;
    bool active_ = false;
    std::uint8_t reserved_[3]{};
};

RUNTIME_SIZE(ScriptStringTransactionToken, 0x8, 0x8);

// This dedicated recursive serializer is the outermost DB ownership lock:
// transaction -> db_hashCritSect -> CRITSECT_SCRIPT_STRING -> memory tree.
// Begin deliberately retains its base acquisition. Finish is valid only on
// that same thread after terminal journal commit/rollback and explicitly
// releases it. The token destructor performs no implicit unlock.
//
// This is a foundation boundary, not yet a production DB-load guard: the
// legacy raw user-4/user-8 mutation paths still need to be enrolled by the
// whole-zone ownership controller before the adapter can replace them.
[[nodiscard]] ScriptStringTransactionStatus TryBeginScriptStringTransaction(
    ScriptStringTransactionToken *token) noexcept;

[[nodiscard]] ScriptStringTransactionStatus FinishScriptStringTransaction(
    ScriptStringTransactionToken *token) noexcept;

// Authenticates the token under the serializer. The owning thread re-enters
// recursively; a foreign caller may block until the active transaction ends,
// then observes the cleared token and returns false.
[[nodiscard]] bool OwnsScriptStringTransaction(
    const ScriptStringTransactionToken &token) noexcept;

} // namespace db::script_string_transaction

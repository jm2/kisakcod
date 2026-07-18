#pragma once

#include <cstdint>

namespace script_string
{
inline constexpr std::uint32_t kDatabaseUserMask = UINT32_C(4);
inline constexpr std::uint32_t kCurrentRuntimeStringLimit = UINT32_C(65536);

[[nodiscard]] constexpr bool IsCurrentRuntimeStringId(
    const std::uint32_t stringId) noexcept
{
    return stringId > 0 && stringId < kCurrentRuntimeStringLimit;
}

enum class AcquireStatus : std::uint8_t
{
    Acquired,
    InvalidArgumentNoChange,
    CapacityNoChange,
    RefCountExhaustedNoChange,
    UnsafeFailure,
};

struct AcquireResult final
{
    AcquireStatus status = AcquireStatus::InvalidArgumentNoChange;
    std::uint32_t stringId = 0;
};

enum class TransferStatus : std::uint8_t
{
    DatabaseUserClaimed,
    DuplicateReleased,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

enum class ReleaseStatus : std::uint8_t
{
    Success,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

// These operations are the report-free ownership boundary used by the
// fast-file transaction journal. They never call Com_Error, MT_Error, strlen,
// or any other nonlocal reporting path. Every ID-taking operation validates
// the current 16-bit runtime domain before looking up a RefString.
[[nodiscard]] AcquireResult TryAcquireOrdinaryStringOfSize(
    const char *bytes,
    std::uint32_t byteCount,
    int type) noexcept;

[[nodiscard]] TransferStatus TryTransferOrdinaryToDatabaseUser(
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveOrdinaryReference(
    std::uint32_t stringId) noexcept;

[[nodiscard]] ReleaseStatus TryRemoveDatabaseUserReference(
    std::uint32_t stringId) noexcept;

} // namespace script_string

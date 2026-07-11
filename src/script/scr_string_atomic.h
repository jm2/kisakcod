#pragma once

#include <cstdint>

#include <universal/sys_atomic.h>

namespace scr_string_atomic
{
inline constexpr std::uint32_t kRefCountMask = UINT32_C(0x0000ffff);
inline constexpr std::uint32_t kUserMask = UINT32_C(0x00ff0000);
inline constexpr std::uint32_t kByteLengthMask = UINT32_C(0xff000000);
inline constexpr std::uint32_t kUserShift = 16;
inline constexpr std::uint32_t kByteLengthShift = 24;
inline constexpr std::uint16_t kMaxRefCount = UINT16_MAX;

constexpr std::uint16_t RefCount(const std::uint32_t value) noexcept
{
    return static_cast<std::uint16_t>(value & kRefCountMask);
}

constexpr std::uint8_t User(const std::uint32_t value) noexcept
{
    return static_cast<std::uint8_t>((value & kUserMask) >> kUserShift);
}

constexpr std::uint8_t ByteLength(const std::uint32_t value) noexcept
{
    return static_cast<std::uint8_t>(
        (value & kByteLengthMask) >> kByteLengthShift);
}

constexpr std::uint32_t Pack(
    const std::uint16_t refCount,
    const std::uint8_t user,
    const std::uint8_t byteLength) noexcept
{
    return static_cast<std::uint32_t>(refCount)
        | (static_cast<std::uint32_t>(user) << kUserShift)
        | (static_cast<std::uint32_t>(byteLength) << kByteLengthShift);
}

inline std::uint32_t Load(const volatile std::uint32_t *const value) noexcept
{
    return Sys_AtomicLoad(value);
}

inline bool TryAddRef(volatile std::uint32_t *const value) noexcept
{
    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0 || refCount == kMaxRefCount)
            return false;

        const std::uint32_t desired = observed + 1;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
            return true;

        observed = actual;
    }
}

enum class RemoveRefAttempt : std::uint8_t
{
    Removed,
    LastReference,
    Invalid,
};

// Keep the zero transition available to a caller that owns the string-table
// lock. That caller can unlink the hash entry before publishing the lock again,
// so a concurrent intern never observes a still-linked zero-count string.
inline RemoveRefAttempt TryRemoveRefUnlessLast(
    volatile std::uint32_t *const value) noexcept
{
    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return RemoveRefAttempt::Invalid;
        if (refCount == 1)
            return RemoveRefAttempt::LastReference;

        const std::uint32_t desired = observed - 1;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
            return RemoveRefAttempt::Removed;

        observed = actual;
    }
}

struct RemoveRefResult
{
    bool success;
    bool reachedZero;
};

inline RemoveRefResult TryRemoveRef(
    volatile std::uint32_t *const value) noexcept
{
    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return {false, false};
        if (refCount == 1 && User(observed) != 0)
            return {false, false};

        const std::uint32_t desired = observed - 1;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
            return {true, refCount == 1};

        observed = actual;
    }
}

enum class RemoveUserRefStatus : std::uint8_t
{
    Removed,
    NotPresent,
    Invalid,
};

struct RemoveUserRefResult
{
    RemoveUserRefStatus status;
    bool reachedZero;
};

inline RemoveUserRefResult RemoveUserRef(
    volatile std::uint32_t *const value,
    const std::uint8_t user) noexcept
{
    if (user == 0)
        return {RemoveUserRefStatus::Invalid, false};

    const std::uint32_t clearMask =
        ~(static_cast<std::uint32_t>(user) << kUserShift);
    std::uint32_t observed = Load(value);
    for (;;)
    {
        if ((User(observed) & user) == 0)
            return {RemoveUserRefStatus::NotPresent, false};

        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return {RemoveUserRefStatus::Invalid, false};

        const std::uint32_t desired = (observed & clearMask) - 1;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
            return {RemoveUserRefStatus::Removed, refCount == 1};

        observed = actual;
    }
}

enum class AddUserRefResult : std::uint8_t
{
    Added,
    AlreadyPresent,
    Invalid,
};

inline AddUserRefResult AddUserRef(
    volatile std::uint32_t *const value,
    const std::uint8_t user) noexcept
{
    const std::uint32_t userBits =
        static_cast<std::uint32_t>(user) << kUserShift;
    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return AddUserRefResult::Invalid;
        if (user != 0 && (User(observed) & user) != 0)
            return AddUserRefResult::AlreadyPresent;
        if (refCount == kMaxRefCount)
            return AddUserRefResult::Invalid;

        const std::uint32_t desired = (observed | userBits) + 1;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
            return AddUserRefResult::Added;

        observed = actual;
    }
}

enum class TransferRefToUserResult : std::uint8_t
{
    ClaimedUser,
    ReleasedDuplicate,
    Invalid,
};

inline TransferRefToUserResult TransferRefToUser(
    volatile std::uint32_t *const value,
    const std::uint8_t user) noexcept
{
    if (user == 0)
        return TransferRefToUserResult::Invalid;

    const std::uint32_t userBits =
        static_cast<std::uint32_t>(user) << kUserShift;
    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return TransferRefToUserResult::Invalid;

        const bool alreadyPresent = (User(observed) & user) != 0;
        if (alreadyPresent && refCount == 1)
            return TransferRefToUserResult::Invalid;

        const std::uint32_t desired = alreadyPresent
            ? observed - 1
            : observed | userBits;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
        {
            return alreadyPresent
                ? TransferRefToUserResult::ReleasedDuplicate
                : TransferRefToUserResult::ClaimedUser;
        }

        observed = actual;
    }
}

enum class TransferUserResult : std::uint8_t
{
    Transferred,
    ReleasedDuplicate,
    NotPresent,
    Invalid,
};

inline TransferUserResult TransferUser(
    volatile std::uint32_t *const value,
    const std::uint8_t from,
    const std::uint8_t to) noexcept
{
    if (from == 0 || to == 0)
        return TransferUserResult::Invalid;

    std::uint32_t observed = Load(value);
    for (;;)
    {
        const std::uint8_t currentUser = User(observed);
        if ((currentUser & from) == 0)
            return TransferUserResult::NotPresent;

        const std::uint16_t refCount = RefCount(observed);
        if (refCount == 0)
            return TransferUserResult::Invalid;

        const std::uint8_t destinationOnly = static_cast<std::uint8_t>(
            to & static_cast<std::uint8_t>(~from));
        const bool destinationAlreadyPresent =
            (currentUser & destinationOnly) != 0;
        if (destinationAlreadyPresent && refCount == 1)
            return TransferUserResult::Invalid;

        const std::uint8_t desiredUser = static_cast<std::uint8_t>(
            (currentUser & static_cast<std::uint8_t>(~from)) | to);
        std::uint32_t desired =
            (observed & ~kUserMask)
            | (static_cast<std::uint32_t>(desiredUser) << kUserShift);
        if (destinationAlreadyPresent)
            --desired;
        const std::uint32_t actual =
            Sys_AtomicCompareExchange(value, desired, observed);
        if (actual == observed)
        {
            return destinationAlreadyPresent
                ? TransferUserResult::ReleasedDuplicate
                : TransferUserResult::Transferred;
        }

        observed = actual;
    }
}

static_assert((kRefCountMask & kUserMask) == 0);
static_assert((kRefCountMask & kByteLengthMask) == 0);
static_assert((kUserMask & kByteLengthMask) == 0);
static_assert((kRefCountMask | kUserMask | kByteLengthMask) == UINT32_MAX);
} // namespace scr_string_atomic

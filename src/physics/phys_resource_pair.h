#pragma once

#include <cstdint>

namespace physics::allocation
{
using ResourceHandle = void *;

enum class ResourcePairStatus : std::uint8_t
{
    Success,
    InvalidCallbacks,
    PrimaryUnavailable,
    CompanionUnavailable,
    PrimaryCleanupFailed,
};

struct ResourcePairCallbacks
{
    void *context = nullptr;
    ResourceHandle (*createPrimary)(void *context) noexcept = nullptr;
    ResourceHandle (*createCompanion)(
        void *context,
        ResourceHandle primary) noexcept = nullptr;
    // Returns true only after releasing primary. A false result must leave the
    // handle valid, unmodified, and wholly owned for explicit recovery.
    bool (*destroyPrimary)(
        void *context,
        ResourceHandle primary) noexcept = nullptr;
};

struct [[nodiscard]] ResourcePairResult
{
    ResourcePairStatus status = ResourcePairStatus::InvalidCallbacks;
    ResourceHandle primary = nullptr;
    ResourceHandle companion = nullptr;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == ResourcePairStatus::Success;
    }
};

// Creates a primary resource and, when requested, a companion resource. A
// failed companion acquisition synchronously tries to return the primary
// through the supplied destroy callback. When that cleanup fails, the result
// retains the primary handle so the caller can recover or terminate without
// losing ownership. Success transfers all returned ownership to the caller;
// this helper performs no implicit cleanup after returning success.
[[nodiscard]] ResourcePairResult TryCreateResourcePair(
    const ResourcePairCallbacks &callbacks,
    bool companionRequired) noexcept;
} // namespace physics::allocation

#pragma once

// The renderer and client threads share cpose_t::cullIn.  Keep the storage a
// plain, exact-width word so the frozen cpose_t layout does not acquire a
// platform-dependent std::atomic representation, and centralize every state
// transition here.

#include <cstdint>

#include <universal/sys_atomic.h>

namespace cg::pose_atomic
{
inline constexpr std::uint32_t kIdle = 0u;
inline constexpr std::uint32_t kUsed = 1u;
inline constexpr std::uint32_t kCulled = 2u;

// A late "used" notification must not downgrade a stronger culled request.
inline void MarkUsed(volatile std::uint32_t *const state) noexcept
{
    (void)Sys_AtomicCompareExchange(state, kUsed, kIdle);
}

inline void MarkCulled(volatile std::uint32_t *const state) noexcept
{
    Sys_AtomicStore(state, kCulled);
}

// Claim exactly one pending request.  A producer racing after this exchange
// leaves its request published for the next consumer instead of being erased
// by a separate load/store pair.
inline std::uint32_t Consume(volatile std::uint32_t *const state) noexcept
{
    return Sys_AtomicExchange(state, kIdle);
}

inline std::uint32_t Peek(const volatile std::uint32_t *const state) noexcept
{
    return Sys_AtomicLoad(state);
}

inline void Reset(volatile std::uint32_t *const state) noexcept
{
    Sys_AtomicStore(state, kIdle);
}
} // namespace cg::pose_atomic

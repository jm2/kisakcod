#pragma once

#include "fx_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

struct dxBody;

namespace fx::physics
{
using BodyToken = std::uint32_t;

constexpr BodyToken INVALID_BODY_TOKEN = 0;
constexpr std::size_t BODY_LIMIT = 512;

static_assert(MAX_ELEMS <=
                  static_cast<std::size_t>(
                      (std::numeric_limits<std::uint16_t>::max)()),
              "FX element indices must fit in the portable sidecar API");
static_assert(BODY_LIMIT <= MAX_ELEMS,
              "FX body ownership cannot exceed the element pool");

enum class SidecarStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    Uninitialized,
    OwnerOutOfRange,
    ZeroToken,
    NotBound,
    StaleToken,
    AlreadyBound,
    DuplicateBody,
    CapacityExceeded,
    ActiveCountCorrupt,
    CorruptGeneration,
    OwnershipMismatch,
    NotEmpty,
    DestinationNotVacant,
    GenerationRelationMismatch,
    TransactionProvenanceMismatch,
};

enum class TransactionRole : std::uint8_t
{
    None,
    PreparedReplacement,
    RollbackSnapshot,
};

struct [[nodiscard]] TokenResult
{
    SidecarStatus status = SidecarStatus::InvalidArgument;
    BodyToken token = INVALID_BODY_TOKEN;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == SidecarStatus::Success;
    }
};

struct [[nodiscard]] BodyResult
{
    SidecarStatus status = SidecarStatus::InvalidArgument;
    dxBody *body = nullptr;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == SidecarStatus::Success;
    }
};

struct [[nodiscard]] IndexedBodyResult
{
    SidecarStatus status = SidecarStatus::InvalidArgument;
    dxBody *body = nullptr;
    std::size_t ownerIndex = MAX_ELEMS;
    BodyToken token = INVALID_BODY_TOKEN;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == SidecarStatus::Success;
    }
};

struct BodySlot
{
    dxBody *body = nullptr;
    BodyToken generation = INVALID_BODY_TOKEN;
};

class BodySidecar;

[[nodiscard]] inline SidecarStatus Validate(
    const BodySidecar *sidecar) noexcept;
[[nodiscard]] inline SidecarStatus ValidateVacantOwner(
    const BodySidecar *sidecar,
    std::size_t ownerIndex) noexcept;
[[nodiscard]] inline SidecarStatus ValidateSemanticOwnership(
    const BodySidecar *sidecar,
    const std::array<BodyToken, MAX_ELEMS> &expectedTokens) noexcept;
[[nodiscard]] inline SidecarStatus ValidateVacantDestination(
    const BodySidecar *destination) noexcept;
[[nodiscard]] inline SidecarStatus ValidateDisjointOwnership(
    const BodySidecar *first,
    const BodySidecar *second) noexcept;
[[nodiscard]] inline SidecarStatus ValidateReplacementRelation(
    const BodySidecar *base,
    const BodySidecar *replacement) noexcept;
[[nodiscard]] inline SidecarStatus ResetEmpty(
    BodySidecar *sidecar) noexcept;
[[nodiscard]] inline TokenResult Bind(
    BodySidecar *sidecar,
    std::size_t ownerIndex,
    dxBody *body) noexcept;
[[nodiscard]] inline BodyResult Resolve(
    const BodySidecar *sidecar,
    std::size_t ownerIndex,
    BodyToken token) noexcept;
[[nodiscard]] inline BodyResult Take(
    BodySidecar *sidecar,
    std::size_t ownerIndex,
    BodyToken token) noexcept;
[[nodiscard]] inline IndexedBodyResult TakeFirst(
    BodySidecar *sidecar) noexcept;
[[nodiscard]] inline SidecarStatus PrepareReplacement(
    const BodySidecar *live,
    BodySidecar *staged) noexcept;
[[nodiscard]] inline SidecarStatus PublishReplacement(
    BodySidecar *live,
    BodySidecar *staged,
    BodySidecar *rollback) noexcept;
[[nodiscard]] inline SidecarStatus RollbackReplacement(
    BodySidecar *live,
    BodySidecar *rollback,
    BodySidecar *discarded) noexcept;

struct SidecarTestAccess;

namespace detail
{
inline std::atomic<std::uint64_t> bodySidecarLifetimeCounter{0};

[[nodiscard]] inline std::uint64_t AcquireLifetimeNonce() noexcept
{
    for (;;)
    {
        const std::uint64_t nonce =
            bodySidecarLifetimeCounter.fetch_add(
                1u, std::memory_order_relaxed) + 1u;
        if (nonce != 0)
            return nonce;
    }
}
} // namespace detail

// This object owns its body-pointer registrations. It deliberately cannot be
// copied or moved: replacement publication must transfer each registration
// through the checked transaction helpers below. The caller owns body
// destruction and serializes all API calls with CRITSECT_PHYSICS.
class BodySidecar final
{
  public:
    BodySidecar() noexcept = default;
    ~BodySidecar() noexcept
    {
        assert(destructionCheckDisabledForTest_ || activeCount_ == 0);
        assert(destructionCheckDisabledForTest_
            || transactionRole_ == TransactionRole::None);
    }

    BodySidecar(const BodySidecar &) = delete;
    BodySidecar &operator=(const BodySidecar &) = delete;
    BodySidecar(BodySidecar &&) = delete;
    BodySidecar &operator=(BodySidecar &&) = delete;

    [[nodiscard]] std::size_t ActiveCount() const noexcept
    {
        return activeCount_;
    }

    [[nodiscard]] bool IsInitialized() const noexcept
    {
        return initialized_;
    }

  private:
    friend SidecarStatus Validate(const BodySidecar *) noexcept;
    friend SidecarStatus ValidateVacantOwner(
        const BodySidecar *, std::size_t) noexcept;
    friend SidecarStatus ValidateSemanticOwnership(
        const BodySidecar *,
        const std::array<BodyToken, MAX_ELEMS> &) noexcept;
    friend SidecarStatus ValidateVacantDestination(
        const BodySidecar *) noexcept;
    friend SidecarStatus ValidateDisjointOwnership(
        const BodySidecar *, const BodySidecar *) noexcept;
    friend SidecarStatus ValidateReplacementRelation(
        const BodySidecar *, const BodySidecar *) noexcept;
    friend SidecarStatus ResetEmpty(BodySidecar *) noexcept;
    friend TokenResult Bind(
        BodySidecar *, std::size_t, dxBody *) noexcept;
    friend BodyResult Resolve(
        const BodySidecar *, std::size_t, BodyToken) noexcept;
    friend BodyResult Take(
        BodySidecar *, std::size_t, BodyToken) noexcept;
    friend IndexedBodyResult TakeFirst(BodySidecar *) noexcept;
    friend SidecarStatus PrepareReplacement(
        const BodySidecar *, BodySidecar *) noexcept;
    friend SidecarStatus PublishReplacement(
        BodySidecar *, BodySidecar *, BodySidecar *) noexcept;
    friend SidecarStatus RollbackReplacement(
        BodySidecar *, BodySidecar *, BodySidecar *) noexcept;
    friend struct SidecarTestAccess;

    std::array<BodySlot, MAX_ELEMS> slots_{};
    std::size_t activeCount_ = 0;
    const std::uint64_t lifetimeNonce_ = detail::AcquireLifetimeNonce();
    std::uint64_t revision_ = 0;
    const BodySidecar *transactionPeer_ = nullptr;
    std::uint64_t transactionPeerLifetimeNonce_ = 0;
    std::uint64_t transactionRevision_ = 0;
    TransactionRole transactionRole_ = TransactionRole::None;
    bool initialized_ = false;
    bool destructionCheckDisabledForTest_ = false;
};

#ifdef KISAK_FX_PHYSICS_SIDECAR_TESTING
// Tests opt in before including this header. Production code has no mutation
// escape hatch around BodySidecar's checked ownership API.
struct SidecarTestAccess
{
    static void SetActiveCount(
        BodySidecar *const sidecar,
        const std::size_t activeCount) noexcept
    {
        if (sidecar)
            sidecar->activeCount_ = activeCount;
    }

    static void SetSlot(
        BodySidecar *const sidecar,
        const std::size_t ownerIndex,
        dxBody *const body,
        const BodyToken generation) noexcept
    {
        if (sidecar && ownerIndex < MAX_ELEMS)
            sidecar->slots_[ownerIndex] = {body, generation};
    }

    [[nodiscard]] static BodySlot GetSlot(
        const BodySidecar *const sidecar,
        const std::size_t ownerIndex) noexcept
    {
        return sidecar && ownerIndex < MAX_ELEMS
            ? sidecar->slots_[ownerIndex]
            : BodySlot{};
    }

    [[nodiscard]] static std::uint64_t GetRevision(
        const BodySidecar *const sidecar) noexcept
    {
        return sidecar ? sidecar->revision_ : 0;
    }

    [[nodiscard]] static std::uint64_t GetLifetimeNonce(
        const BodySidecar *const sidecar) noexcept
    {
        return sidecar ? sidecar->lifetimeNonce_ : 0;
    }

    [[nodiscard]] static TransactionRole GetTransactionRole(
        const BodySidecar *const sidecar) noexcept
    {
        return sidecar
            ? sidecar->transactionRole_
            : TransactionRole::None;
    }

    static void SetTransactionState(
        BodySidecar *const sidecar,
        const TransactionRole role,
        const BodySidecar *const peer,
        const std::uint64_t revision) noexcept
    {
        if (sidecar)
        {
            sidecar->transactionRole_ = role;
            sidecar->transactionPeer_ = peer;
            sidecar->transactionPeerLifetimeNonce_ =
                peer ? peer->lifetimeNonce_ : 0;
            sidecar->transactionRevision_ = revision;
        }
    }

    static void DisableDestructionCheck(
        BodySidecar *const sidecar) noexcept
    {
        if (sidecar)
            sidecar->destructionCheckDisabledForTest_ = true;
    }
};
#endif

// FxElem retains its legacy signed 32-bit field. Moving token bits through
// memcpy avoids implementation-defined signed conversions for tokens whose
// high bit is set.
[[nodiscard]] inline BodyToken TokenFromLegacyField(
    const std::int32_t legacyField) noexcept
{
    BodyToken token = INVALID_BODY_TOKEN;
    std::memcpy(&token, &legacyField, sizeof(token));
    return token;
}

[[nodiscard]] inline std::int32_t TokenToLegacyField(
    const BodyToken token) noexcept
{
    std::int32_t legacyField = 0;
    std::memcpy(&legacyField, &token, sizeof(legacyField));
    return legacyField;
}

[[nodiscard]] inline BodyToken NextGeneration(
    const BodyToken generation) noexcept
{
    // FxElem's frozen field limits tokens to 32 bits. Zero remains invalid,
    // but a token retained across 2^32 ownership advances can repeat.
    BodyToken next = generation + 1u;
    if (next == INVALID_BODY_TOKEN)
        next = 1u;
    return next;
}

[[nodiscard]] inline std::uint64_t NextRevision(
    const std::uint64_t revision) noexcept
{
    std::uint64_t next = revision + 1u;
    if (next == 0)
        next = 1u;
    return next;
}

[[nodiscard]] inline SidecarStatus Validate(
    const BodySidecar *const sidecar) noexcept
{
    if (!sidecar)
        return SidecarStatus::InvalidArgument;
    if (!sidecar->initialized_)
        return SidecarStatus::Uninitialized;
    if (sidecar->activeCount_ > BODY_LIMIT)
        return SidecarStatus::ActiveCountCorrupt;
    if (sidecar->transactionRole_ == TransactionRole::None)
    {
        if (sidecar->transactionPeer_
            || sidecar->transactionPeerLifetimeNonce_ != 0
            || sidecar->transactionRevision_ != 0)
        {
            return SidecarStatus::TransactionProvenanceMismatch;
        }
    }
    else if ((sidecar->transactionRole_
                  != TransactionRole::PreparedReplacement
              && sidecar->transactionRole_
                  != TransactionRole::RollbackSnapshot)
        || !sidecar->transactionPeer_
        || sidecar->transactionPeerLifetimeNonce_ == 0
        || sidecar->transactionRevision_ == 0)
    {
        return SidecarStatus::TransactionProvenanceMismatch;
    }

    std::array<dxBody *, BODY_LIMIT> bodies{};
    std::size_t bodyCount = 0;
    for (const BodySlot &slot : sidecar->slots_)
    {
        if (!slot.body)
            continue;
        if (slot.generation == INVALID_BODY_TOKEN)
            return SidecarStatus::CorruptGeneration;
        if (bodyCount == bodies.size())
            return SidecarStatus::ActiveCountCorrupt;
        bodies[bodyCount++] = slot.body;
    }
    if (bodyCount != sidecar->activeCount_)
        return SidecarStatus::ActiveCountCorrupt;

    std::sort(
        bodies.begin(), bodies.begin() + bodyCount,
        std::less<dxBody *>{});
    for (std::size_t index = 1; index < bodyCount; ++index)
    {
        if (bodies[index - 1] == bodies[index])
            return SidecarStatus::DuplicateBody;
    }
    return SidecarStatus::Success;
}

// Spawn admission needs a bounded preflight before allocating a native body.
// A vacant owner's generation can legitimately be nonzero after Take or
// ResetEmpty, so only the body registration determines owner vacancy here.
[[nodiscard]] inline SidecarStatus ValidateVacantOwner(
    const BodySidecar *const sidecar,
    const std::size_t ownerIndex) noexcept
{
    if (!sidecar)
        return SidecarStatus::InvalidArgument;
    if (!sidecar->initialized_)
        return SidecarStatus::Uninitialized;
    if (ownerIndex >= MAX_ELEMS)
        return SidecarStatus::OwnerOutOfRange;
    if (sidecar->activeCount_ > BODY_LIMIT)
        return SidecarStatus::ActiveCountCorrupt;
    if (sidecar->slots_[ownerIndex].body)
        return SidecarStatus::AlreadyBound;
    return SidecarStatus::Success;
}

// The caller builds expectedTokens from the validated live FX graph: zero for
// non-physics slots, and the FxElem token for every physics-model owner.
[[nodiscard]] inline SidecarStatus ValidateSemanticOwnership(
    const BodySidecar *const sidecar,
    const std::array<BodyToken, MAX_ELEMS> &expectedTokens) noexcept
{
    const SidecarStatus structuralStatus = Validate(sidecar);
    if (structuralStatus != SidecarStatus::Success)
        return structuralStatus;

    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        const BodySlot &slot = sidecar->slots_[index];
        const BodyToken expected = expectedTokens[index];
        if (expected == INVALID_BODY_TOKEN)
        {
            if (slot.body)
                return SidecarStatus::OwnershipMismatch;
        }
        else if (!slot.body || slot.generation != expected)
        {
            return SidecarStatus::OwnershipMismatch;
        }
    }
    return SidecarStatus::Success;
}

[[nodiscard]] inline SidecarStatus ValidateVacantDestination(
    const BodySidecar *const destination) noexcept
{
    if (!destination)
        return SidecarStatus::InvalidArgument;
    if (destination->activeCount_ != 0
        || destination->transactionRole_ != TransactionRole::None
        || destination->transactionPeer_
        || destination->transactionPeerLifetimeNonce_ != 0
        || destination->transactionRevision_ != 0)
    {
        return SidecarStatus::DestinationNotVacant;
    }
    for (const BodySlot &slot : destination->slots_)
    {
        if (slot.body)
            return SidecarStatus::DestinationNotVacant;
    }
    return SidecarStatus::Success;
}

[[nodiscard]] inline SidecarStatus ValidateDisjointOwnership(
    const BodySidecar *const first,
    const BodySidecar *const second) noexcept
{
    if (!first || !second || first == second)
        return SidecarStatus::InvalidArgument;
    const SidecarStatus firstStatus = Validate(first);
    if (firstStatus != SidecarStatus::Success)
        return firstStatus;
    const SidecarStatus secondStatus = Validate(second);
    if (secondStatus != SidecarStatus::Success)
        return secondStatus;

    std::array<dxBody *, BODY_LIMIT> secondBodies{};
    std::size_t secondBodyCount = 0;
    const std::size_t secondTargetCount = second->activeCount_;
    for (const BodySlot &slot : second->slots_)
    {
        if (secondBodyCount == secondTargetCount)
            break;
        if (slot.body)
            secondBodies[secondBodyCount++] = slot.body;
    }
    std::sort(
        secondBodies.begin(), secondBodies.begin() + secondBodyCount,
        std::less<dxBody *>{});
    std::size_t firstBodyCount = 0;
    const std::size_t firstTargetCount = first->activeCount_;
    for (const BodySlot &slot : first->slots_)
    {
        if (firstBodyCount == firstTargetCount)
            break;
        if (!slot.body)
            continue;
        if (std::binary_search(
                secondBodies.begin(),
                secondBodies.begin() + secondBodyCount,
                slot.body,
                std::less<dxBody *>{}))
        {
            return SidecarStatus::DuplicateBody;
        }
        ++firstBodyCount;
    }
    return SidecarStatus::Success;
}

// A replacement preserves each base slot's generation while empty or advances
// it exactly once while owning a newly bound body. This checks the per-slot
// generation relation only; Publish/Rollback separately require the exact
// source pointer and captured mutation revision.
[[nodiscard]] inline SidecarStatus ValidateReplacementRelation(
    const BodySidecar *const base,
    const BodySidecar *const replacement) noexcept
{
    const SidecarStatus ownershipStatus =
        ValidateDisjointOwnership(base, replacement);
    if (ownershipStatus != SidecarStatus::Success)
        return ownershipStatus;

    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        const BodySlot &baseSlot = base->slots_[index];
        const BodySlot &replacementSlot = replacement->slots_[index];
        const BodyToken expectedGeneration = replacementSlot.body
            ? NextGeneration(baseSlot.generation)
            : baseSlot.generation;
        if (replacementSlot.generation != expectedGeneration)
            return SidecarStatus::GenerationRelationMismatch;
    }
    return SidecarStatus::Success;
}

// Reset never abandons a native body. The owner must first Take and destroy
// every live body. An initialized reset advances all generations so tokens
// retained by stale work cannot become valid again on the next bind.
[[nodiscard]] inline SidecarStatus ResetEmpty(
    BodySidecar *const sidecar) noexcept
{
    if (!sidecar)
        return SidecarStatus::InvalidArgument;
    if (sidecar->initialized_)
    {
        const SidecarStatus status = Validate(sidecar);
        if (status != SidecarStatus::Success)
            return status;
        if (sidecar->activeCount_ != 0)
            return SidecarStatus::NotEmpty;
        for (BodySlot &slot : sidecar->slots_)
            slot.generation = NextGeneration(slot.generation);
    }
    else
    {
        if (sidecar->activeCount_ != 0)
            return SidecarStatus::ActiveCountCorrupt;
        for (const BodySlot &slot : sidecar->slots_)
        {
            if (slot.body)
                return SidecarStatus::NotEmpty;
        }
        for (BodySlot &slot : sidecar->slots_)
            slot.generation = INVALID_BODY_TOKEN;
    }
    sidecar->revision_ = NextRevision(sidecar->revision_);
    sidecar->transactionPeer_ = nullptr;
    sidecar->transactionPeerLifetimeNonce_ = 0;
    sidecar->transactionRevision_ = 0;
    sidecar->transactionRole_ = TransactionRole::None;
    sidecar->initialized_ = true;
    return SidecarStatus::Success;
}

[[nodiscard]] inline TokenResult Bind(
    BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    dxBody *const body) noexcept
{
    if (!sidecar || !body)
        return {SidecarStatus::InvalidArgument, INVALID_BODY_TOKEN};
    if (ownerIndex >= MAX_ELEMS)
        return {SidecarStatus::OwnerOutOfRange, INVALID_BODY_TOKEN};

    const SidecarStatus structuralStatus = Validate(sidecar);
    if (structuralStatus != SidecarStatus::Success)
        return {structuralStatus, INVALID_BODY_TOKEN};
    if (sidecar->slots_[ownerIndex].body)
        return {SidecarStatus::AlreadyBound, INVALID_BODY_TOKEN};
    if (sidecar->activeCount_ == BODY_LIMIT)
        return {SidecarStatus::CapacityExceeded, INVALID_BODY_TOKEN};
    for (const BodySlot &slot : sidecar->slots_)
    {
        if (slot.body == body)
            return {SidecarStatus::DuplicateBody, INVALID_BODY_TOKEN};
    }

    BodySlot &slot = sidecar->slots_[ownerIndex];
    const BodyToken token = NextGeneration(slot.generation);
    slot.generation = token;
    slot.body = body;
    ++sidecar->activeCount_;
    sidecar->revision_ = NextRevision(sidecar->revision_);
    if (sidecar->transactionRole_ == TransactionRole::RollbackSnapshot)
    {
        sidecar->transactionPeer_ = nullptr;
        sidecar->transactionPeerLifetimeNonce_ = 0;
        sidecar->transactionRevision_ = 0;
        sidecar->transactionRole_ = TransactionRole::None;
    }
    return {SidecarStatus::Success, token};
}

// Resolve is the hot draw path. Bind/Take and lifecycle validation maintain the
// whole-sidecar invariant, so resolution performs only bounded local checks.
[[nodiscard]] inline BodyResult Resolve(
    const BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    const BodyToken token) noexcept
{
    if (!sidecar)
        return {SidecarStatus::InvalidArgument, nullptr};
    if (!sidecar->initialized_)
        return {SidecarStatus::Uninitialized, nullptr};
    if (ownerIndex >= MAX_ELEMS)
        return {SidecarStatus::OwnerOutOfRange, nullptr};
    if (token == INVALID_BODY_TOKEN)
        return {SidecarStatus::ZeroToken, nullptr};
    if (sidecar->activeCount_ > BODY_LIMIT)
        return {SidecarStatus::ActiveCountCorrupt, nullptr};

    const BodySlot &slot = sidecar->slots_[ownerIndex];
    if (!slot.body)
        return {SidecarStatus::NotBound, nullptr};
    if (slot.generation != token)
        return {SidecarStatus::StaleToken, nullptr};
    return {SidecarStatus::Success, slot.body};
}

[[nodiscard]] inline BodyResult Take(
    BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    const BodyToken token) noexcept
{
    if (!sidecar)
        return {SidecarStatus::InvalidArgument, nullptr};
    if (ownerIndex >= MAX_ELEMS)
        return {SidecarStatus::OwnerOutOfRange, nullptr};
    if (token == INVALID_BODY_TOKEN)
        return {SidecarStatus::ZeroToken, nullptr};

    const SidecarStatus structuralStatus = Validate(sidecar);
    if (structuralStatus != SidecarStatus::Success)
        return {structuralStatus, nullptr};
    BodySlot &slot = sidecar->slots_[ownerIndex];
    if (!slot.body)
        return {SidecarStatus::NotBound, nullptr};
    if (slot.generation != token)
        return {SidecarStatus::StaleToken, nullptr};

    dxBody *const body = slot.body;
    slot.body = nullptr;
    slot.generation = NextGeneration(slot.generation);
    --sidecar->activeCount_;
    sidecar->revision_ = NextRevision(sidecar->revision_);
    sidecar->transactionPeer_ = nullptr;
    sidecar->transactionPeerLifetimeNonce_ = 0;
    sidecar->transactionRevision_ = 0;
    sidecar->transactionRole_ = TransactionRole::None;
    return {SidecarStatus::Success, body};
}

// Reset/shutdown and archive commit/rollback cleanup can drain registrations
// even when their owning FxElem graph is no longer published. The native body
// remains caller-owned and must be destroyed after this transfer returns.
[[nodiscard]] inline IndexedBodyResult TakeFirst(
    BodySidecar *const sidecar) noexcept
{
    if (!sidecar)
    {
        return {SidecarStatus::InvalidArgument, nullptr, MAX_ELEMS,
                INVALID_BODY_TOKEN};
    }
    const SidecarStatus structuralStatus = Validate(sidecar);
    if (structuralStatus != SidecarStatus::Success)
    {
        return {structuralStatus, nullptr, MAX_ELEMS,
                INVALID_BODY_TOKEN};
    }

    for (std::size_t ownerIndex = 0; ownerIndex < MAX_ELEMS; ++ownerIndex)
    {
        BodySlot &slot = sidecar->slots_[ownerIndex];
        if (!slot.body)
            continue;
        dxBody *const body = slot.body;
        const BodyToken token = slot.generation;
        slot.body = nullptr;
        slot.generation = NextGeneration(slot.generation);
        --sidecar->activeCount_;
        sidecar->revision_ = NextRevision(sidecar->revision_);
        sidecar->transactionPeer_ = nullptr;
        sidecar->transactionPeerLifetimeNonce_ = 0;
        sidecar->transactionRevision_ = 0;
        sidecar->transactionRole_ = TransactionRole::None;
        return {SidecarStatus::Success, body, ownerIndex, token};
    }
    return {SidecarStatus::NotBound, nullptr, MAX_ELEMS,
            INVALID_BODY_TOKEN};
}

// Staging starts bodyless but inherits every generation from the live state.
// Bind advances selected slots, so restored owners receive fresh tokens.
[[nodiscard]] inline SidecarStatus PrepareReplacement(
    const BodySidecar *const live,
    BodySidecar *const staged) noexcept
{
    if (!live || !staged || live == staged)
        return SidecarStatus::InvalidArgument;
    const SidecarStatus liveStatus = Validate(live);
    if (liveStatus != SidecarStatus::Success)
        return liveStatus;
    if (live->transactionRole_ != TransactionRole::None)
        return SidecarStatus::TransactionProvenanceMismatch;
    const SidecarStatus destinationStatus =
        ValidateVacantDestination(staged);
    if (destinationStatus != SidecarStatus::Success)
        return destinationStatus;

    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        staged->slots_[index].body = nullptr;
        staged->slots_[index].generation =
            live->slots_[index].generation;
    }
    staged->activeCount_ = 0;
    staged->revision_ = NextRevision(staged->revision_);
    staged->transactionPeer_ = live;
    staged->transactionPeerLifetimeNonce_ = live->lifetimeNonce_;
    staged->transactionRevision_ = live->revision_;
    staged->transactionRole_ = TransactionRole::PreparedReplacement;
    staged->initialized_ = true;
    return SidecarStatus::Success;
}

// Publish transfers staged registrations to live and old live registrations
// to rollback. All validation precedes the no-fail per-entry transfer.
[[nodiscard]] inline SidecarStatus PublishReplacement(
    BodySidecar *const live,
    BodySidecar *const staged,
    BodySidecar *const rollback) noexcept
{
    if (!live || !staged || !rollback
        || live == staged || live == rollback || staged == rollback)
    {
        return SidecarStatus::InvalidArgument;
    }
    if (live->transactionRole_ != TransactionRole::None
        || staged->transactionRole_
            != TransactionRole::PreparedReplacement
        || staged->transactionPeer_ != live
        || staged->transactionPeerLifetimeNonce_ != live->lifetimeNonce_
        || staged->transactionRevision_ != live->revision_)
    {
        return SidecarStatus::TransactionProvenanceMismatch;
    }
    const SidecarStatus relationStatus =
        ValidateReplacementRelation(live, staged);
    if (relationStatus != SidecarStatus::Success)
        return relationStatus;
    const SidecarStatus destinationStatus =
        ValidateVacantDestination(rollback);
    if (destinationStatus != SidecarStatus::Success)
        return destinationStatus;

    const std::size_t previousCount = live->activeCount_;
    const bool previousInitialized = live->initialized_;
    const std::size_t replacementCount = staged->activeCount_;
    const bool replacementInitialized = staged->initialized_;
    const std::uint64_t publishedRevision =
        NextRevision(live->revision_);
    const std::uint64_t rollbackRevision =
        NextRevision(rollback->revision_);
    const std::uint64_t consumedStagedRevision =
        NextRevision(staged->revision_);
    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        rollback->slots_[index] = live->slots_[index];
        live->slots_[index] = staged->slots_[index];
        staged->slots_[index].body = nullptr;
    }
    rollback->activeCount_ = previousCount;
    rollback->revision_ = rollbackRevision;
    rollback->transactionPeer_ = live;
    rollback->transactionPeerLifetimeNonce_ = live->lifetimeNonce_;
    rollback->transactionRevision_ = publishedRevision;
    rollback->transactionRole_ = TransactionRole::RollbackSnapshot;
    rollback->initialized_ = previousInitialized;
    live->activeCount_ = replacementCount;
    live->revision_ = publishedRevision;
    live->transactionPeer_ = nullptr;
    live->transactionPeerLifetimeNonce_ = 0;
    live->transactionRevision_ = 0;
    live->transactionRole_ = TransactionRole::None;
    live->initialized_ = replacementInitialized;
    staged->activeCount_ = 0;
    staged->revision_ = consumedStagedRevision;
    staged->transactionPeer_ = nullptr;
    staged->transactionPeerLifetimeNonce_ = 0;
    staged->transactionRevision_ = 0;
    staged->transactionRole_ = TransactionRole::None;
    return SidecarStatus::Success;
}

// Rollback proves the current live state is still the exact descendant of the
// saved base, then transfers new registrations to discarded and restores old
// registrations to live. The caller destroys bodies owned by discarded.
[[nodiscard]] inline SidecarStatus RollbackReplacement(
    BodySidecar *const live,
    BodySidecar *const rollback,
    BodySidecar *const discarded) noexcept
{
    if (!live || !rollback || !discarded
        || live == rollback || live == discarded
        || rollback == discarded)
    {
        return SidecarStatus::InvalidArgument;
    }
    if (live->transactionRole_ != TransactionRole::None
        || rollback->transactionRole_
            != TransactionRole::RollbackSnapshot
        || rollback->transactionPeer_ != live
        || rollback->transactionPeerLifetimeNonce_ != live->lifetimeNonce_
        || rollback->transactionRevision_ != live->revision_)
    {
        return SidecarStatus::TransactionProvenanceMismatch;
    }
    const SidecarStatus relationStatus =
        ValidateReplacementRelation(rollback, live);
    if (relationStatus != SidecarStatus::Success)
        return relationStatus;
    const SidecarStatus destinationStatus =
        ValidateVacantDestination(discarded);
    if (destinationStatus != SidecarStatus::Success)
        return destinationStatus;

    const std::size_t replacementCount = live->activeCount_;
    const bool replacementInitialized = live->initialized_;
    const std::size_t previousCount = rollback->activeCount_;
    const bool previousInitialized = rollback->initialized_;
    const std::uint64_t restoredRevision =
        NextRevision(live->revision_);
    const std::uint64_t discardedRevision =
        NextRevision(discarded->revision_);
    const std::uint64_t consumedRollbackRevision =
        NextRevision(rollback->revision_);
    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        discarded->slots_[index] = live->slots_[index];
        live->slots_[index] = rollback->slots_[index];
        rollback->slots_[index].body = nullptr;
    }
    discarded->activeCount_ = replacementCount;
    discarded->revision_ = discardedRevision;
    discarded->transactionPeer_ = nullptr;
    discarded->transactionPeerLifetimeNonce_ = 0;
    discarded->transactionRevision_ = 0;
    discarded->transactionRole_ = TransactionRole::None;
    discarded->initialized_ = replacementInitialized;
    live->activeCount_ = previousCount;
    live->revision_ = restoredRevision;
    live->transactionPeer_ = nullptr;
    live->transactionPeerLifetimeNonce_ = 0;
    live->transactionRevision_ = 0;
    live->transactionRole_ = TransactionRole::None;
    live->initialized_ = previousInitialized;
    rollback->activeCount_ = 0;
    rollback->revision_ = consumedRollbackRevision;
    rollback->transactionPeer_ = nullptr;
    rollback->transactionPeerLifetimeNonce_ = 0;
    rollback->transactionRevision_ = 0;
    rollback->transactionRole_ = TransactionRole::None;
    return SidecarStatus::Success;
}
} // namespace fx::physics

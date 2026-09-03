// SPDX-License-Identifier: GPL-3.0
//
// phys_obj_id.h - generation-checked native-width physics-body ownership
// token for the cpose_t, BreakablePiece, and DynEntityClient ownership
// families. This is the Priority-7 native64 ABI seam: the legacy `int32_t`
// field stays bit-identical (so DynEntityClient's frozen 12-byte save image
// and BreakablePiece's 0xC size, as well as MP cpose_t's 0x64/0x68 layout,
// are preserved), but the native dxBody* pointer NEVER lives in the field.
// Instead the field carries a 32-bit token ((generation << 16) | ownerIndex)
// and the actual native pointer lives in a sidecar table indexed by
// ownerIndex. Stale tokens (after a body is destroyed and the slot reused)
// are rejected by generation mismatch.
//
// This is the public seam between the runtime and the on-disk / wire layout.
// Usage from producers:
//   phys_obj_id::TokenResult bind = g_cposeBodySidecar.Bind(centNum, body);
//   if (bind) cent->pose.physObjId = bind.token;
// Usage from consumers:
//   dxBody *body = CG_ResolvePhysObjId(cent->pose.physObjId);
//   if (body) Phys_ObjDestroy(PHYS_WORLD_FX, body);
// Usage from release sites (close out a body and clear the field):
//   dxBody *body = nullptr;
//   if (phys_obj_id::Consume(g_cposeBodySidecar, cent->pose.physObjId, &body))
//       Phys_ObjDestroy(PHYS_WORLD_FX, body);
//   cent->pose.physObjId = 0;
//
// The 32-bit token field carries a strict sentinel contract that matches the
// legacy physObjId conventions already used by the engine:
//   0x00000000  =>  no body (NULL / cleared)
//   0xFFFFFFFF  =>  dead (creation failed; never retriggered)
//   gen:16|idx:16 (gen != 0)  =>  valid ownership token
// On 32-bit ILP32 the pointer fits in the field and would never have needed
// this indirection, but the token layout is identical across 32-bit and
// 64-bit builds so saved bytes round-trip on every target.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <universal/kisak_abi.h>

namespace phys_obj_id
{
// Token layout: bits 0..15 are ownerIndex, bits 16..31 are generation.
// Generation==0 is reserved for INVALID_BODY_TOKEN, so the first
// generation handed out by Bind is 1 and the highest gen is 0xFFFF.
using BodyToken = std::uint32_t;
using Generation = std::uint16_t;
using OwnerIndex = std::uint16_t;

constexpr BodyToken INVALID_BODY_TOKEN = 0x00000000u;
constexpr BodyToken DEAD_BODY_TOKEN    = 0xFFFFFFFFu;

constexpr std::size_t kGenerationShift = 16u;
constexpr BodyToken kGenerationMask    = 0xFFFF0000u;
constexpr BodyToken kOwnerMask         = 0x0000FFFFu;

[[nodiscard]] constexpr bool IsNull(BodyToken token) noexcept
{
    return token == INVALID_BODY_TOKEN;
}

[[nodiscard]] constexpr bool IsDead(BodyToken token) noexcept
{
    return token == DEAD_BODY_TOKEN;
}

[[nodiscard]] constexpr bool IsLive(BodyToken token) noexcept
{
    return !IsNull(token) && !IsDead(token);
}

[[nodiscard]] constexpr Generation GenerationOf(BodyToken token) noexcept
{
    return static_cast<Generation>((token & kGenerationMask) >> kGenerationShift);
}

[[nodiscard]] constexpr OwnerIndex OwnerOf(BodyToken token) noexcept
{
    return static_cast<OwnerIndex>(token & kOwnerMask);
}

[[nodiscard]] constexpr BodyToken PackToken(Generation gen, OwnerIndex idx) noexcept
{
    return (static_cast<BodyToken>(gen) << kGenerationShift) | static_cast<BodyToken>(idx);
}

enum class Status : std::uint8_t
{
    Success,
    InvalidArgument,
    CapacityExceeded,
    NotBound,
    Stale,
    AlreadyBound,
    OwnershipMismatch,
};

struct [[nodiscard]] TokenResult
{
    Status status = Status::InvalidArgument;
    BodyToken token = INVALID_BODY_TOKEN;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == Status::Success;
    }
};

struct [[nodiscard]] BodyResult
{
    Status status = Status::InvalidArgument;
    void *body = nullptr;

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return status == Status::Success;
    }
};

struct Slot
{
    void *body = nullptr;
    Generation generation = 0;
};

// A fixed-capacity sidecar that pairs native body pointers with generation
// tokens. All operations are O(1) and intended to be called under the
// CRITSECT_PHYSICS lock that already protects the legacy physObjId field.
// The Capacity template parameter is the maximum number of owners that can
// simultaneously hold a binding; choosing too small a Capacity is a build-time
// static_assert failure downstream.
template <std::size_t Capacity>
class BodySidecar
{
    static_assert(Capacity > 0, "phys_obj_id sidecar capacity must be positive");
    static_assert(Capacity <= (kOwnerMask + 1u),
                  "phys_obj_id sidecar capacity must fit in 16-bit owner index");

  public:
    constexpr BodySidecar() noexcept = default;

    BodySidecar(const BodySidecar &) = delete;
    BodySidecar &operator=(const BodySidecar &) = delete;
    BodySidecar(BodySidecar &&) = delete;
    BodySidecar &operator=(BodySidecar &&) = delete;

    [[nodiscard]] constexpr std::size_t CapacityValue() const noexcept
    {
        return Capacity;
    }

    // Bind an owner to a fresh body. The owner's slot must be vacant (no
    // current binding); reusing a live slot is an AlreadyBound error to
    // prevent silently evading a release path. The generation is rolled
    // forward so any stale token recorded in a saved byte stream cannot
    // accidentally resolve to a new body.
    //
    // GCC note: these member-template instantiations have identical bodies
    // for different Capacity values, so GCC can merge their constprop
    // clones and inline Resolve<8> into a Resolve<4> caller. It then warns
    // -Warray-bounds because the merged clone's owner range [0, 8) exceeds
    // the caller's smaller object, even though owner is bounds-checked
    // against *this->Capacity above. This is a false positive; silence
    // -Warray-bounds locally around the slot accesses (GCC only, not
    // Clang, which does not clone-merge these).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
    [[nodiscard]] TokenResult Bind(OwnerIndex owner, void *body) noexcept
    {
        if (owner >= Capacity || body == nullptr)
            return {Status::InvalidArgument, INVALID_BODY_TOKEN};
        if (slots_[owner].body != nullptr)
            return {Status::AlreadyBound, INVALID_BODY_TOKEN};

        const Generation nextGen = NextGeneration(slots_[owner].generation);
        slots_[owner].body = body;
        slots_[owner].generation = nextGen;
        return {Status::Success, PackToken(nextGen, owner)};
    }

    // Resolve a token to a body pointer. Returns Success only if the token
    // matches the current binding exactly, including the generation.
    [[nodiscard]] BodyResult Resolve(BodyToken token) const noexcept
    {
        if (IsNull(token) || IsDead(token))
            return {Status::InvalidArgument, nullptr};
        const OwnerIndex owner = OwnerOf(token);
        if (owner >= Capacity)
            return {Status::InvalidArgument, nullptr};
        const Slot &slot = slots_[owner];
        if (slot.body == nullptr)
            return {Status::NotBound, nullptr};
        if (slot.generation != GenerationOf(token))
            return {Status::Stale, nullptr};
        return {Status::Success, slot.body};
    }

    // Resolve a token, then atomically release the binding. Returns Success
    // only if the binding was live and matched the token; the caller owns
    // the published body pointer and is responsible for destroying it.
    [[nodiscard]] BodyResult Release(BodyToken token) noexcept
    {
        if (IsNull(token) || IsDead(token))
            return {Status::InvalidArgument, nullptr};
        const OwnerIndex owner = OwnerOf(token);
        if (owner >= Capacity)
            return {Status::InvalidArgument, nullptr};
        if (slots_[owner].body == nullptr)
            return {Status::NotBound, nullptr};
        if (slots_[owner].generation != GenerationOf(token))
            return {Status::Stale, nullptr};
        void *body = slots_[owner].body;
        const Generation retired = slots_[owner].generation;
        slots_[owner].body = nullptr;
        // Generation is rolled forward so the next Bind on this slot starts
        // from a fresh token and any stale token emitted by a save-image
        // replay cannot reactivate the just-released binding.
        slots_[owner].generation = NextGeneration(retired);
        return {Status::Success, body};
    }

    // Force a release by owner index, regardless of the recorded token.
    // Used by cleanup paths that need to drop every active binding without
    // round-tripping through a possibly-stale token. Returns Success only
    // if the slot was live; remaining-callers that try to Resolve this
    // owner will see NotBound / Stale.
    [[nodiscard]] BodyResult ReleaseByOwner(OwnerIndex owner) noexcept
    {
        if (owner >= Capacity)
            return {Status::InvalidArgument, nullptr};
        Slot &slot = slots_[owner];
        if (slot.body == nullptr)
            return {Status::NotBound, nullptr};
        void *body = slot.body;
        const Generation retired = slot.generation;
        slots_[owner].body = nullptr;
        slots_[owner].generation = NextGeneration(retired);
        return {Status::Success, body};
    }

    // True if the owner slot currently holds a binding.
    [[nodiscard]] bool IsBound(OwnerIndex owner) const noexcept
    {
        if (owner >= Capacity)
            return false;
        return slots_[owner].body != nullptr;
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    // Test/debug helper: copy every slot into a contiguous buffer. Returns
    // the number of slots written so the caller can size the output.
    [[nodiscard]] std::size_t SnapshotSlots(Slot *outSlots, std::size_t outCapacity) const noexcept
    {
        const std::size_t count = (outCapacity < Capacity) ? outCapacity : Capacity;
        for (std::size_t i = 0; i < count; ++i)
            outSlots[i] = slots_[i];
        return count;
    }

    // Drop every active binding. The caller owns the live body pointers and
    // must destroy them before calling Reset; this is the unsupported-test
    // escape hatch used after the engine has already torn down physics.
    void ResetForTesting() noexcept
    {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            slots_[i].body = nullptr;
            slots_[i].generation = 0;
        }
    }

  private:
    [[nodiscard]] static constexpr Generation NextGeneration(Generation current) noexcept
    {
        // 0 is reserved for INVALID_BODY_TOKEN, so wrap from 0xFFFF back to 1
        // rather than 0. A token retained across 2^16 ownership advances on
        // the same slot can repeat; the actual reuse is bounded by the
        // natural rate of body churn, which is well below that.
        const Generation next = static_cast<Generation>(current + 1u);
        return next == 0 ? 1 : next;
    }

    Slot slots_[Capacity]{};
};

// Convenience helpers that pair the legacy 32-bit field with the sidecar
// table. Producer caller:
//   phys_obj_id::WriteBind(g_cposeBodySidecar, &cent->pose.physObjId, centNum, body);
// Consumer caller:
//   dxBody *body = phys_obj_id::ReadResolve<dxBody>(g_cposeBodySidecar, cent->pose.physObjId);
// Release caller (drops the binding on success; field is cleared to
// INVALID_BODY_TOKEN so the next producer sees a vacant slot):
//   dxBody *body = nullptr;
//   if (phys_obj_id::ConsumeRelease<dxBody>(g_cposeBodySidecar, &cent->pose.physObjId, &body))
//       Phys_ObjDestroy(PHYS_WORLD_FX, body);

template <std::size_t Capacity>
[[nodiscard]] inline TokenResult WriteBind(
    BodySidecar<Capacity> &sidecar,
    BodyToken *field,
    OwnerIndex owner,
    void *body) noexcept
{
    if (field == nullptr)
        return {Status::InvalidArgument, INVALID_BODY_TOKEN};
    const TokenResult bind = sidecar.Bind(owner, body);
    if (bind)
        *field = bind.token;
    return bind;
}

template <class Body, std::size_t Capacity>
[[nodiscard]] inline Body *ReadResolve(
    const BodySidecar<Capacity> &sidecar,
    BodyToken token) noexcept
{
    if (IsNull(token) || IsDead(token))
        return nullptr;
    const BodyResult r = sidecar.Resolve(token);
    return r ? static_cast<Body *>(r.body) : nullptr;
}

template <class Body, std::size_t Capacity>
[[nodiscard]] inline bool ConsumeRelease(
    BodySidecar<Capacity> &sidecar,
    BodyToken *field,
    Body **outBody) noexcept
{
    if (outBody == nullptr)
        return false;
    *outBody = nullptr;
    if (field == nullptr)
        return false;
    const BodyToken token = *field;
    *field = INVALID_BODY_TOKEN;
    if (IsNull(token) || IsDead(token))
        return false;
    const BodyResult r = sidecar.Release(token);
    if (!r)
        return false;
    *outBody = static_cast<Body *>(r.body);
    return true;
}
} // namespace phys_obj_id

// Priority-7 native64 ABI seam: MP cpose_t::physObjId, BreakablePiece::
// physObjId, and DynEntityClient::physObjId stay bit-identical 32-bit
// fields (so the on-disk saved image and the runtime struct layout never
// widen), but the native dxBody* pointer is parked in a generation-checked
// sidecar indexed by the owner handle. SP cpose_t::physObjId is already
// uintptr_t; the SP path only needs to stop narrowing through `int` locals
// in cgame/cg_ents.cpp and cgame/cg_snapshot.cpp. The DynEntityClient and
// BreakablePiece sidecars are used on both MP and SP because their
// physObjId fields are frozen at int32_t; the SP cpose sidecar is filled
// in only when the engine binary contains a cpose_t::physObjId writer
// (MP-only; the SP path keeps the native pointer in the field).
constexpr std::size_t kCposeBodySidecarCapacity = 1024;
constexpr std::size_t kBreakablePieceBodySidecarCapacity = 100;
constexpr std::size_t kDynEntClientBodySidecarCapacity = 8192;

// DynEntityClient owner-key packing: owner = drawType * perDrawType +
// dynEntId. The runtime bind path (DynEntity_client.cpp) and the save-image
// replay path (DynEntity_load_obj.cpp) must derive the key through this
// helper so the two sites cannot drift. A draw type that ever loads
// >= kDynEntPhysObjIdOwnerPerDrawType entities would collide its keys into
// the next draw type's slot range; the load paths assert the bound.
constexpr std::uint32_t kDynEntPhysObjIdOwnerPerDrawType = 4096u;
static_assert(
    2u * kDynEntPhysObjIdOwnerPerDrawType <= kDynEntClientBodySidecarCapacity,
    "dynent sidecar capacity must cover both draw-type key ranges");

[[nodiscard]] inline phys_obj_id::OwnerIndex DynEntPhysObjId_MakeOwnerIndex(
    std::uint32_t drawType,
    std::uint16_t dynEntId) noexcept
{
    return static_cast<phys_obj_id::OwnerIndex>(
        drawType * kDynEntPhysObjIdOwnerPerDrawType + dynEntId);
}

extern phys_obj_id::BodySidecar<kCposeBodySidecarCapacity> g_cposeBodySidecar;
extern phys_obj_id::BodySidecar<kBreakablePieceBodySidecarCapacity> g_breakablePieceBodySidecar;
extern phys_obj_id::BodySidecar<kDynEntClientBodySidecarCapacity> g_dynEntClientBodySidecar;

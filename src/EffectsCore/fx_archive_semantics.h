#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct FxEffect;
struct FxElem;
struct FxElemDef;
struct FxSystem;
struct XModel;

namespace fx::archive
{
inline constexpr std::size_t FX_ARCHIVE_PHYSICS_BODY_LIMIT = 512u;
inline constexpr std::uint32_t FX_ARCHIVE_INVALID_PHYSICS_TOKEN = 0u;

// Platform-neutral byte layout used to inspect the renderer-owned FxElemDef
// without including its legacy Direct3D-facing definition.  A production TU
// that sees the complete type must static-assert these values against
// sizeof(FxElemDef) and offsetof for every named member.
namespace layout
{
inline constexpr std::size_t ELEM_DEF_STRIDE =
    KISAK_ARCH_64BIT ? 0x120u : 0xFCu;
inline constexpr std::size_t ELEM_DEF_FLAGS_OFFSET = 0x00u;
inline constexpr std::size_t ELEM_DEF_SPAWN_OFFSET = 0x04u;
inline constexpr std::size_t ELEM_DEF_SPAWN_DELAY_OFFSET = 0x28u;
inline constexpr std::size_t ELEM_DEF_LIFE_SPAN_OFFSET = 0x30u;
inline constexpr std::size_t ELEM_DEF_ELEM_TYPE_OFFSET = 0xB0u;
inline constexpr std::size_t ELEM_DEF_VISUAL_COUNT_OFFSET = 0xB1u;
inline constexpr std::size_t ELEM_DEF_VISUALS_OFFSET =
    KISAK_ARCH_64BIT ? 0xC8u : 0xBCu;
inline constexpr std::size_t ELEM_DEF_TRAIL_DEF_OFFSET =
    KISAK_ARCH_64BIT ? 0x110u : 0xF4u;
} // namespace layout

// Describes the two active members that definition-aware conversion must
// establish after semantic preflight. TRAIL is not valid in an ordinary
// FxElem chain, so malformed TRAIL elements are rejected before callbacks;
// retaining the kind keeps the definition-selected lifetime contract complete
// if a future valid owner class uses the same payload representation.
enum class FxArchiveElemPayloadKind : std::uint8_t
{
    OriginLighting,
    PhysicsLighting,
    OriginTrailTexCoord,
};

// Invoked exactly once for every reachable ordinary or spotlight FxElem in a
// second traversal, after a complete callback-free semantic preflight.  It is
// still ordered immediately after bounded definition lookup and before any
// activation-sensitive access in that traversal.  Returning false rejects the
// complete validation.  The callback may only begin the selected union-member
// lifetime: it must preserve the complete FxElem object representation and
// must not mutate the system, effect, graph links, definitions, or any nested
// semantic state.  The helper verifies the FxElem representation after each
// callback.  The callback must also remain noexcept, allocation-free, and
// report-free.
using FxArchivePrepareElemPayloadCallback = bool (*)(
    void *context,
    FxSystem *system,
    FxEffect *effect,
    FxElem *elem,
    const FxElemDef *elemDef,
    FxArchiveElemPayloadKind payloadKind) noexcept;

// A fully validated physics-bearing element.  token preserves the unsigned
// bit representation stored in FxElem::physObjId; zero is rejected before a
// descriptor is offered to the sink.
struct FxArchiveSemanticPhysicsDescriptor
{
    FxElem *elem = nullptr;
    const XModel *model = nullptr;
    std::size_t ownerIndex = 0;
    std::uint32_t token = 0;
};

// Invoked in deterministic effect/element traversal order after the element,
// selected model, owner index, and token have all validated.  physicsIndex is
// zero-based and strictly less than the final physicsBodyCount.  Returning
// false rejects the complete validation; partial sink output is caller-owned
// and must be ignored on failure.
using FxArchiveSemanticPhysicsSinkCallback = bool (*)(
    void *context,
    const FxArchiveSemanticPhysicsDescriptor &descriptor,
    std::size_t physicsIndex) noexcept;

struct FxArchiveSemanticCallbacks
{
    void *context = nullptr;
    FxArchivePrepareElemPayloadCallback prepareElemPayload = nullptr;
    FxArchiveSemanticPhysicsSinkCallback acceptPhysics = nullptr;
};

struct FxArchiveSemanticResult
{
    std::uint32_t physicsBodyCount = 0;
    std::int16_t spotLightBoltDobj = -1;
};

// Validates one already linked and structurally stable native FX graph.  The
// function is bounded, allocation-free, lock-free, non-reporting, and does not
// capture or mutate native physics state.  It does not validate allocation
// bitmaps or ownership completeness; callers must first run the pool-graph
// validator appropriate to their staging transaction.
//
// Resolved FxEffectDef pointers are a trust/lifetime boundary, not untrusted
// wire values.  The caller must ensure each definition and every nested range
// followed by this function remains readable and stable for this call.  The
// same synchronization requirement applies to any later operation that
// dereferences pointers retained in the validated graph; successful return
// does not grant definition ownership or make an arbitrary pointer safe.
//
// Null system/result or malformed state returns false without changing
// outResult and without invoking either callback.  After a successful
// callback-free preflight, a rejected callback returns false and may leave a
// prepared prefix in hidden caller-owned staging; a physics sink may likewise
// have received a validated prefix.  Callers must keep both unpublished unless
// this function succeeds.
[[nodiscard]] bool TryValidateFxArchiveSemanticsNoReport(
    FxSystem *system,
    const FxArchiveSemanticCallbacks &callbacks,
    FxArchiveSemanticResult *outResult) noexcept;
} // namespace fx::archive

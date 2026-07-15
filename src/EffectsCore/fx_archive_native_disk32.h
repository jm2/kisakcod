#pragma once

#include <EffectsCore/fx_archive_buffers_disk32.h>
#include <EffectsCore/fx_archive_semantics.h>
#include <EffectsCore/fx_pool_graph.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fx::archive
{
enum class FxArchiveDisk32WorkspacePhase : std::uint8_t
{
    Empty,
    StructurallyValid,
    Ready
};

enum class FxArchiveDisk32ReadyStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    InvalidPhase,
    InvalidGraph,
    InvalidSemantics
};

enum class FxArchiveDisk32StructuralStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    InvalidSystem,
    InvalidFreeLists,
    DefinitionNotFound,
    InvalidEffect,
    InvalidPoolRecord,
    InvalidGraph
};

using FxArchiveDisk32ResolveDefinitionCallback =
    const FxEffectDef *(*)(
        void *context,
        EffectDefinitionKey32 key) noexcept;

struct FxArchiveDisk32Resolver
{
    void *context = nullptr;
    FxArchiveDisk32ResolveDefinitionCallback resolve = nullptr;
};

class alignas(8) FxArchiveDisk32NativeWorkspace;
struct FxArchiveDisk32StructuralView;
struct FxArchiveDisk32ReadyView;
struct FxArchiveDisk32ReadyPhysicsDescriptor;

using FxArchiveDisk32ReadyPhysicsSinkCallback = bool (*)(
    void *context,
    const FxArchiveDisk32ReadyPhysicsDescriptor &descriptor,
    std::size_t physicsIndex) noexcept;

// Builds a report-free native structural image in caller-owned heap storage.
// The resolver is required even when the active-effect ring is empty; it is
// called only for active records, and returned definition pointers are stored
// as opaque identities without ever being dereferenced at this phase.
//
// Every call with a nonnull workspace first resets its phase to Empty. Failure
// may leave hidden partial staging bytes but never exposes a structural view;
// Success is the only path that sets StructurallyValid, after local pointer
// relinking, visibility-selector round-trip, and complete allocation-graph
// validation. Same-workspace resolver reentry is rejected. Concurrent access
// to one workspace remains the caller's synchronization responsibility.
[[nodiscard]] FxArchiveDisk32StructuralStatus
TryBuildFxArchiveDisk32StructuralImage(
    const FxSystemDisk32 &sourceSystem,
    const FxSystemBuffersDisk32 &sourceBuffers,
    const FxArchiveDisk32Resolver &resolver,
    FxArchiveDisk32NativeWorkspace *workspace) noexcept;

// Commits the output view only for StructurallyValid or Ready storage. Null
// arguments or an Empty workspace return false without changing the output.
[[nodiscard]] bool TryGetFxArchiveDisk32StructuralView(
    const FxArchiveDisk32NativeWorkspace *workspace,
    FxArchiveDisk32StructuralView *outView) noexcept;

// Performs the definition-aware, report-free semantic pass. Every definition
// pointer resolved by the structural builder, its element-definition array,
// and any referenced trail/model payload must remain readable and unchanged
// throughout this call and every later operation that dereferences the staged
// identities. Production restore satisfies that contract by retaining its
// effect-table lifecycle lease through semantic and physics staging.
//
// Only StructurallyValid storage may enter this transition. The phase becomes
// Empty before any definition-selected union lifetime changes; any failure
// therefore requires a complete structural rebuild. Success canonicalizes
// active effect frame counters and the spotlight bolt, records the validated
// physics-body count, and publishes Ready as the final workspace mutation.
[[nodiscard]] FxArchiveDisk32ReadyStatus
TryFinalizeFxArchiveDisk32NativeImage(
    FxArchiveDisk32NativeWorkspace *workspace) noexcept;

// Commits the output only for Ready storage. Null arguments or every other
// phase return false without changing the caller's view.
[[nodiscard]] bool TryGetFxArchiveDisk32ReadyView(
    const FxArchiveDisk32NativeWorkspace *workspace,
    FxArchiveDisk32ReadyView *outView) noexcept;

// Enumerates the definition-validated physics elements of one Ready image in
// deterministic semantic traversal order.  The operation is logically const:
// it never changes the graph or workspace phase, and a rejected sink may be
// retried against the same Ready image.  The sink can observe a provisional
// prefix when it returns false, so caller-owned output must remain unpublished
// unless this function succeeds.  Same-workspace mutating reentry is rejected.
//
// The caller must retain the effect-definition lifetime lease and synchronize
// the workspace for the complete synchronous call.  The sink must not mutate
// or rebuild the workspace or any reachable graph/definition storage.  Copied
// descriptor pointers remain valid only until the workspace is rebuilt or
// destroyed and while the corresponding asset lease remains held.
[[nodiscard]] bool TryEnumerateFxArchiveDisk32ReadyPhysics(
    const FxArchiveDisk32NativeWorkspace *workspace,
    void *context,
    FxArchiveDisk32ReadyPhysicsSinkCallback acceptPhysics) noexcept;

// Heap-owned native staging storage. Copy and move are disabled because the
// linked FxSystem contains pointers into this exact object's buffer member.
// Allocate this object through the checked archive-workspace allocator rather
// than placing its roughly 300 KiB image on a thread stack. Explicit 8-byte
// alignment normalizes the private ILP32 layout across compilers whose native
// uint64_t alignment differs and is enforced by that allocator.
class alignas(8) FxArchiveDisk32NativeWorkspace final
{
public:
    FxArchiveDisk32NativeWorkspace() noexcept = default;
    ~FxArchiveDisk32NativeWorkspace() noexcept = default;

    FxArchiveDisk32NativeWorkspace(
        const FxArchiveDisk32NativeWorkspace &) = delete;
    FxArchiveDisk32NativeWorkspace &operator=(
        const FxArchiveDisk32NativeWorkspace &) = delete;
    FxArchiveDisk32NativeWorkspace(
        FxArchiveDisk32NativeWorkspace &&) = delete;
    FxArchiveDisk32NativeWorkspace &operator=(
        FxArchiveDisk32NativeWorkspace &&) = delete;

    [[nodiscard]] FxArchiveDisk32WorkspacePhase phase() const noexcept
    {
        return phase_;
    }

private:
    friend FxArchiveDisk32StructuralStatus
    TryBuildFxArchiveDisk32StructuralImage(
        const FxSystemDisk32 &sourceSystem,
        const FxSystemBuffersDisk32 &sourceBuffers,
        const FxArchiveDisk32Resolver &resolver,
        FxArchiveDisk32NativeWorkspace *workspace) noexcept;
    friend bool TryGetFxArchiveDisk32StructuralView(
        const FxArchiveDisk32NativeWorkspace *workspace,
        FxArchiveDisk32StructuralView *outView) noexcept;
    friend FxArchiveDisk32ReadyStatus
    TryFinalizeFxArchiveDisk32NativeImage(
        FxArchiveDisk32NativeWorkspace *workspace) noexcept;
    friend bool TryGetFxArchiveDisk32ReadyView(
        const FxArchiveDisk32NativeWorkspace *workspace,
        FxArchiveDisk32ReadyView *outView) noexcept;
    friend bool TryEnumerateFxArchiveDisk32ReadyPhysics(
        const FxArchiveDisk32NativeWorkspace *workspace,
        void *context,
        FxArchiveDisk32ReadyPhysicsSinkCallback acceptPhysics) noexcept;

    FxSystem system_{};
    FxSystemBuffers buffers_{};
    FxSystemBuffersDisk32PoolStates poolStates_{};
    FxPoolAllocationGraphScratch graphScratch_{};
    FxSystemDisk32Metadata metadata_{};
    FxArchiveDisk32WorkspacePhase phase_ =
        FxArchiveDisk32WorkspacePhase::Empty;
    mutable bool building_ = false;
    std::uint32_t physicsBodyCount_ = 0;
};

// A structural view is deliberately read-only and non-publishable. Definition
// pointers are opaque identities at this phase, and no definition-dependent
// FxElem payload member may be accessed until a later pass reaches Ready.
// Constness is necessarily shallow because legacy FxSystem contains pointer
// members; callers must not cast away const or mutate any reachable staging
// object. Rebuilding or destroying the workspace invalidates every pointer.
struct FxArchiveDisk32StructuralView
{
    const FxSystem *system = nullptr;
    const FxSystemBuffers *buffers = nullptr;
    const FxSystemBuffersDisk32PoolStates *poolStates = nullptr;
    const FxSystemDisk32Metadata *metadata = nullptr;
};

// Ready storage has completed definition-dependent union selection and the
// shared archive semantic oracle. Rebuilding, a failed re-finalization, or
// destroying the workspace invalidates every returned pointer. As with the
// structural view, callers must treat all transitively reachable staging as
// read-only even though legacy pointer members make the C++ constness shallow.
struct FxArchiveDisk32ReadyView
{
    const FxSystem *system = nullptr;
    const FxSystemBuffers *buffers = nullptr;
    const FxSystemBuffersDisk32PoolStates *poolStates = nullptr;
    const FxSystemDisk32Metadata *metadata = nullptr;
    std::uint32_t physicsBodyCount = 0;
};

// A shallow, read-only snapshot of one validated physics element.  elem points
// into the Ready workspace; model remains owned by the caller-retained asset
// lease.  ownerIndex is the native physical FxElem pool slot and token keeps
// the complete unsigned legacy object representation.
struct FxArchiveDisk32ReadyPhysicsDescriptor
{
    const FxElem *elem = nullptr;
    const XModel *model = nullptr;
    std::size_t ownerIndex = 0;
    std::uint32_t token = 0;
};

RUNTIME_SIZE(FxArchiveDisk32NativeWorkspace, 0x4BD90, 0x4FDD8);
static_assert(
    std::is_nothrow_default_constructible_v<
        FxArchiveDisk32NativeWorkspace>);
static_assert(
    std::is_nothrow_destructible_v<FxArchiveDisk32NativeWorkspace>);
} // namespace fx::archive

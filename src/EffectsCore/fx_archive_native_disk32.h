#pragma once

#include <EffectsCore/fx_archive_buffers_disk32.h>
#include <EffectsCore/fx_pool_graph.h>

#include <cstdint>
#include <type_traits>

namespace fx::archive
{
// Ready is reserved for a later definition-aware semantic conversion pass.
// This checkpoint can publish only StructurallyValid staging storage.
enum class FxArchiveDisk32WorkspacePhase : std::uint8_t
{
    Empty,
    StructurallyValid,
    Ready
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

    FxSystem system_{};
    FxSystemBuffers buffers_{};
    FxSystemBuffersDisk32PoolStates poolStates_{};
    FxPoolAllocationGraphScratch graphScratch_{};
    FxSystemDisk32Metadata metadata_{};
    FxArchiveDisk32WorkspacePhase phase_ =
        FxArchiveDisk32WorkspacePhase::Empty;
    bool building_ = false;
};

// A structural view is deliberately read-only and non-publishable. Definition
// pointers are opaque identities at this phase, and no definition-dependent
// FxElem payload member may be accessed until a later pass reaches Ready.
// Rebuilding or destroying the workspace invalidates every returned pointer.
struct FxArchiveDisk32StructuralView
{
    const FxSystem *system = nullptr;
    const FxSystemBuffers *buffers = nullptr;
    const FxSystemBuffersDisk32PoolStates *poolStates = nullptr;
    const FxSystemDisk32Metadata *metadata = nullptr;
};

RUNTIME_SIZE(FxArchiveDisk32NativeWorkspace, 0x4BD90, 0x4FDD8);
static_assert(
    std::is_nothrow_default_constructible_v<
        FxArchiveDisk32NativeWorkspace>);
static_assert(
    std::is_nothrow_destructible_v<FxArchiveDisk32NativeWorkspace>);
} // namespace fx::archive

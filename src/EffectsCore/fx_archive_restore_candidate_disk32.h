#pragma once

#include <EffectsCore/fx_archive_reader_disk32.h>
#include <EffectsCore/fx_archive_restore_workspace.h>
#include <EffectsCore/fx_pool_graph.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fx::archive
{
enum class FxArchiveRestoreCandidateDisk32Phase : std::uint8_t
{
    Empty,
    Ready,
};

enum class FxArchiveRestoreCandidateDisk32Status : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidLease,
    InvalidGraph,
    InvalidPhysics,
};

// Mutable production-facing copy of one validated reader physics record.
// elem is always remapped into the candidate's own FxSystemBuffers image;
// model remains protected by the caller-retained effect-table lease while the
// candidate is built.
struct FxArchiveRestoreCandidateDisk32PhysicsBody
{
    FxElem *elem = nullptr;
    const XModel *model = nullptr;
    BodyState state{};
    std::size_t ownerIndex = 0;
    std::uint32_t token = 0;
};

RUNTIME_SIZE(FxArchiveRestoreCandidateDisk32PhysicsBody, 0x80, 0x90);
static_assert(
    std::is_standard_layout_v<
        FxArchiveRestoreCandidateDisk32PhysicsBody>);

class alignas(8) FxArchiveRestoreCandidateDisk32Workspace;

struct FxArchiveRestoreCandidateDisk32ReadyView
{
    FxSystem *system = nullptr;
    FxSystemBuffers *buffers = nullptr;
    ArchiveAddress32 archivedSystemAddress{};
    FxArchiveRestoreCandidateDisk32PhysicsBody *physicsBodies = nullptr;
    std::uint32_t physicsBodyCount = 0;
};

RUNTIME_SIZE(FxArchiveRestoreCandidateDisk32ReadyView, 0x14, 0x28);

// Copies one reader Ready image into independently owned mutable candidate
// storage. The caller must retain the exact active effect-table lease used by
// the reader for this entire synchronous call. The builder checks the private
// stored lease identity and holds the reader's operation gate until the copy
// and final revalidation finish, so lifecycle callbacks cannot rebuild the
// source or obtain a detached view during materialization.
//
// The builder is bounded, allocation-free, lock-free, and report-free. Every
// idle attempt invalidates earlier candidate views before inspecting source
// state. It validates the complete source graph and semantic traversal,
// requires exact descriptor provenance and body-state canonicalization, then
// repeats the graph/semantic checks after relinking the independent copy.
// Partial copies remain hidden; Ready is the final workspace mutation.
[[nodiscard]] FxArchiveRestoreCandidateDisk32Status
TryBuildFxArchiveRestoreCandidateDisk32(
    FxArchiveDisk32ReaderWorkspace *sourceWorkspace,
    const EffectTableRestoreLease &lease,
    FxArchiveRestoreCandidateDisk32Workspace *candidateWorkspace) noexcept;

// Commits a mutable candidate view only while the workspace is Ready. Every
// pointer remains valid until the next build attempt or workspace destruction.
// Concurrent access to one workspace remains caller-synchronized.
[[nodiscard]] bool TryGetFxArchiveRestoreCandidateDisk32ReadyView(
    FxArchiveRestoreCandidateDisk32Workspace *workspace,
    FxArchiveRestoreCandidateDisk32ReadyView *outView) noexcept;

// Roughly 375--400 KiB of heap-only mutable staging. Copy and move are
// disabled because both FxSystem and physics entries point into this exact
// object's buffer member. The established archive workspace allocator checks
// the signed allocation ABI and required alignment for this type.
class alignas(8) FxArchiveRestoreCandidateDisk32Workspace final
{
public:
    FxArchiveRestoreCandidateDisk32Workspace() noexcept = default;
    ~FxArchiveRestoreCandidateDisk32Workspace() noexcept = default;

    FxArchiveRestoreCandidateDisk32Workspace(
        const FxArchiveRestoreCandidateDisk32Workspace &) = delete;
    FxArchiveRestoreCandidateDisk32Workspace &operator=(
        const FxArchiveRestoreCandidateDisk32Workspace &) = delete;
    FxArchiveRestoreCandidateDisk32Workspace(
        FxArchiveRestoreCandidateDisk32Workspace &&) = delete;
    FxArchiveRestoreCandidateDisk32Workspace &operator=(
        FxArchiveRestoreCandidateDisk32Workspace &&) = delete;

    [[nodiscard]] FxArchiveRestoreCandidateDisk32Phase phase() const noexcept
    {
        return phase_;
    }

private:
    friend FxArchiveRestoreCandidateDisk32Status
    TryBuildFxArchiveRestoreCandidateDisk32(
        FxArchiveDisk32ReaderWorkspace *sourceWorkspace,
        const EffectTableRestoreLease &lease,
        FxArchiveRestoreCandidateDisk32Workspace *candidateWorkspace)
        noexcept;
    friend bool TryGetFxArchiveRestoreCandidateDisk32ReadyView(
        FxArchiveRestoreCandidateDisk32Workspace *workspace,
        FxArchiveRestoreCandidateDisk32ReadyView *outView) noexcept;

    FxSystem system_{};
    FxSystemBuffers buffers_{};
    FxSystemBuffersDisk32PoolStates poolStates_{};
    FxSystemDisk32Metadata metadata_{};
    FxPoolAllocationGraphScratch graphScratch_{};
    FxArchiveRestoreCandidateDisk32PhysicsBody
        physicsBodies_[FX_ARCHIVE_PHYSICS_BODY_LIMIT]{};
    ArchiveAddress32 archivedSystemAddress_{};
    std::uint32_t physicsBodyCount_ = 0;
    FxArchiveRestoreCandidateDisk32Phase phase_ =
        FxArchiveRestoreCandidateDisk32Phase::Empty;
    bool operating_ = false;
};

static_assert(
    alignof(FxArchiveRestoreCandidateDisk32Workspace) == 8);
static_assert(
    IsSupportedArchiveRestoreWorkspace<
        FxArchiveRestoreCandidateDisk32Workspace>);
static_assert(
    std::is_nothrow_default_constructible_v<
        FxArchiveRestoreCandidateDisk32Workspace>);
static_assert(
    std::is_nothrow_destructible_v<
        FxArchiveRestoreCandidateDisk32Workspace>);
static_assert(
    std::is_trivially_destructible_v<
        FxArchiveRestoreCandidateDisk32Workspace>);
RUNTIME_SIZE(FxArchiveRestoreCandidateDisk32Workspace, 0x5BD98, 0x61DE8);
} // namespace fx::archive

#pragma once

#include <EffectsCore/fx_archive_body_state_disk32.h>
#include <EffectsCore/fx_archive_native_disk32.h>
#include <EffectsCore/fx_effect_table_restore.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct MemoryFile;

namespace fx::archive
{
enum class FxArchiveDisk32ReaderPhase : std::uint8_t
{
    Empty,
    Ready,
};

enum class FxArchiveDisk32ReaderStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidLease,
    InvalidMemoryFile,
    TruncatedInput,
    InvalidStructuralImage,
    InvalidSemanticImage,
    InvalidRelocation,
    InvalidBodyState,
};

struct FxArchiveDisk32ReaderPhysicsBody
{
    FxArchiveDisk32ReadyPhysicsDescriptor descriptor{};
    BodyState state{};
};

RUNTIME_SIZE(FxArchiveDisk32ReaderPhysicsBody, 0x80, 0x90);
static_assert(
    alignof(FxArchiveDisk32ReaderPhysicsBody)
    == (KISAK_ARCH_64BIT ? 8u : 4u));

// Fixed-layout copy of the exact public lease identity. Splitting the serial
// avoids GCC/MSVC i386 disagreement over uint64_t member alignment while
// retaining every ownership bit needed by Ready-view validation.
struct FxArchiveDisk32ReaderLeaseIdentity
{
    const void *identity = nullptr;
    const void *ownerCookie = nullptr;
    std::uint32_t lifecycleGeneration = 0;
    std::uint32_t serialLow = 0;
    std::uint32_t serialHigh = 0;
};

RUNTIME_SIZE(FxArchiveDisk32ReaderLeaseIdentity, 0x14, 0x20);
static_assert(
    std::is_standard_layout_v<FxArchiveDisk32ReaderLeaseIdentity>);
static_assert(
    std::is_trivially_copyable_v<FxArchiveDisk32ReaderLeaseIdentity>);

class alignas(8) FxArchiveDisk32ReaderWorkspace;
struct FxArchiveDisk32ReaderReadyView;
enum class FxArchiveRestoreCandidateDisk32Status : std::uint8_t;
class FxArchiveRestoreCandidateDisk32Workspace;

[[nodiscard]] FxArchiveRestoreCandidateDisk32Status
TryBuildFxArchiveRestoreCandidateDisk32(
    FxArchiveDisk32ReaderWorkspace *sourceWorkspace,
    const EffectTableRestoreLease &lease,
    FxArchiveRestoreCandidateDisk32Workspace *candidateWorkspace) noexcept;

// Reads the fixed FX archive tail after the caller has restored the effect
// table and retained its exact same-thread lease. The logical wire order is
// FxSystemDisk32, FxSystemBuffersDisk32, one nonzero legacy ArchiveAddress32,
// then exactly the semantics-derived number of BodyStateDisk32 records.
//
// Every idle call invalidates earlier views before reading. Partial raw/zlib
// input, semantic callback prefixes, and decoded-body prefixes remain hidden
// on failure; retry requires a freshly initialized or repositioned MemoryFile
// because the streaming cursor is not rolled back. The lease is borrowed and
// is never released or abandoned by this helper. Production graph publication
// and live physics are deliberately outside this portable staging boundary.
// All access to one workspace, including phase/view reads, requires caller
// synchronization; the reentry guard is not a cross-thread lock.
[[nodiscard]] FxArchiveDisk32ReaderStatus TryReadFxArchiveDisk32NoReport(
    MemoryFile *memFile,
    const EffectTableRestoreLease &lease,
    FxArchiveDisk32ReaderWorkspace *workspace) noexcept;

// Commits a shallow read-only view only while the workspace is Ready and the
// caller still owns the exact active lease used to build it. The returned
// graph/element/model pointers remain valid only until workspace rebuild or
// destruction and while that external definition lease remains retained.
[[nodiscard]] bool TryGetFxArchiveDisk32ReaderReadyView(
    const FxArchiveDisk32ReaderWorkspace *workspace,
    const EffectTableRestoreLease &lease,
    FxArchiveDisk32ReaderReadyView *outView) noexcept;

// Large heap-owned staging transaction. Copy and move are disabled because
// the embedded native graph contains pointers into this exact object's
// buffers. Do not place this roughly 650--700 KiB object on the Windows x86
// thread stack; production integration must use the checked archive allocator.
class alignas(8) FxArchiveDisk32ReaderWorkspace final
{
public:
    FxArchiveDisk32ReaderWorkspace() noexcept = default;
    ~FxArchiveDisk32ReaderWorkspace() noexcept = default;

    FxArchiveDisk32ReaderWorkspace(
        const FxArchiveDisk32ReaderWorkspace &) = delete;
    FxArchiveDisk32ReaderWorkspace &operator=(
        const FxArchiveDisk32ReaderWorkspace &) = delete;
    FxArchiveDisk32ReaderWorkspace(
        FxArchiveDisk32ReaderWorkspace &&) = delete;
    FxArchiveDisk32ReaderWorkspace &operator=(
        FxArchiveDisk32ReaderWorkspace &&) = delete;

    [[nodiscard]] FxArchiveDisk32ReaderPhase phase() const noexcept
    {
        return phase_;
    }

private:
    friend FxArchiveDisk32ReaderStatus TryReadFxArchiveDisk32NoReport(
        MemoryFile *memFile,
        const EffectTableRestoreLease &lease,
        FxArchiveDisk32ReaderWorkspace *workspace) noexcept;
    friend bool TryGetFxArchiveDisk32ReaderReadyView(
        const FxArchiveDisk32ReaderWorkspace *workspace,
        const EffectTableRestoreLease &lease,
        FxArchiveDisk32ReaderReadyView *outView) noexcept;
    friend FxArchiveRestoreCandidateDisk32Status
    TryBuildFxArchiveRestoreCandidateDisk32(
        FxArchiveDisk32ReaderWorkspace *sourceWorkspace,
        const EffectTableRestoreLease &lease,
        FxArchiveRestoreCandidateDisk32Workspace *candidateWorkspace)
        noexcept;

    FxSystemDisk32 sourceSystem_{};
    FxSystemBuffersDisk32 sourceBuffers_{};
    FxArchiveDisk32NativeWorkspace nativeWorkspace_{};
    FxArchiveDisk32ReaderLeaseIdentity lease_{};
    ArchiveAddress32 archivedSystemAddress_{};
    BodyStateDisk32 bodyScratch_{};
    FxArchiveDisk32ReaderPhysicsBody
        physicsBodies_[FX_ARCHIVE_PHYSICS_BODY_LIMIT]{};
    std::uint32_t physicsBodyCount_ = 0;
    FxArchiveDisk32ReaderPhase phase_ = FxArchiveDisk32ReaderPhase::Empty;
    mutable bool operating_ = false;
};

struct FxArchiveDisk32ReaderReadyView
{
    FxArchiveDisk32ReadyView graph{};
    ArchiveAddress32 archivedSystemAddress{};
    const FxArchiveDisk32ReaderPhysicsBody *physicsBodies = nullptr;
    std::uint32_t physicsBodyCount = 0;
};

static_assert(alignof(FxArchiveDisk32ReaderWorkspace) == 8);
static_assert(
    std::is_nothrow_default_constructible_v<
        FxArchiveDisk32ReaderWorkspace>);
static_assert(
    std::is_nothrow_destructible_v<FxArchiveDisk32ReaderWorkspace>);
static_assert(
    std::is_trivially_destructible_v<FxArchiveDisk32ReaderWorkspace>);
RUNTIME_SIZE(FxArchiveDisk32ReaderWorkspace, 0xA3D00, 0xA9D58);
} // namespace fx::archive

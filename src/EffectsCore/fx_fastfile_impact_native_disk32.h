#pragma once

#include <EffectsCore/fx_fastfile_native_disk32.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

struct FxImpactTable;

namespace fx::fastfile
{
inline constexpr std::size_t kFxFastFileImpactDisk32HandleCount =
    kImpactSurfaceCount *
    (kImpactNonFleshEffectCount + kImpactFleshEffectCount);
inline constexpr std::size_t kFxFastFileImpactDisk32JournalCount =
    1u + kFxFastFileImpactDisk32HandleCount;

struct FxFastFileImpactTableDisk32View final
{
    const FxImpactTableDisk32 *impactTable = nullptr;
    FxFastFileDisk32Span<FxImpactEntryDisk32> entries{};
    FxFastFileDisk32Provenance provenance{};
};

class FxFastFileImpactNativeDisk32Workspace;

class alignas(8) FxFastFileImpactNativeDisk32Plan final
{
  public:
    constexpr FxFastFileImpactNativeDisk32Plan() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t outputBytes() const noexcept
    {
        return outputBytes_;
    }

    [[nodiscard]] constexpr std::uint32_t outputAlignment() const noexcept
    {
        return outputAlignment_;
    }

    [[nodiscard]] constexpr std::uint32_t entryCount() const noexcept
    {
        return entryCount_;
    }

    [[nodiscard]] constexpr std::uint32_t
    resolvedReferenceCount() const noexcept
    {
        return resolvedReferenceCount_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return workspaceIdentity_ != nullptr && serial_ != 0;
    }

  private:
    friend FxFastFileNativeDisk32Status
    TryPlanFxImpactTableDisk32(FxFastFileImpactNativeDisk32Workspace *,
                               const FxFastFileImpactTableDisk32View &,
                               const FxFastFileDisk32Resolvers &,
                               FxFastFileImpactNativeDisk32Plan *) noexcept;
    friend FxFastFileNativeDisk32Status
    TryMaterializeFxImpactTableDisk32(FxFastFileImpactNativeDisk32Workspace *,
                                      const FxFastFileImpactNativeDisk32Plan &,
                                      void *,
                                      std::size_t,
                                      FxImpactTable **) noexcept;
    friend class FxFastFileImpactNativeDisk32Workspace;

    const FxFastFileImpactNativeDisk32Workspace *workspaceIdentity_ = nullptr;
#if !KISAK_ARCH_64BIT
    // Match MSVC x86's 8-byte placement for the following uint64_t members.
    std::uint32_t workspaceIdentityPadding_ = 0;
#endif
    std::uint64_t serial_ = 0;
    std::uint64_t sourceFingerprint_ = 0;
    std::uint32_t outputBytes_ = 0;
    std::uint32_t outputAlignment_ = 0;
    std::uint32_t entryCount_ = 0;
    std::uint32_t resolvedReferenceCount_ = 0;
    std::uint32_t entriesOffset_ = 0;
    std::uint32_t nameOffset_ = 0;
    std::uint32_t nameBytes_ = 0;
};

// Heap-oriented scratch for one two-pass conversion.  Journal slot zero owns
// the resolved root name; each remaining slot maps one physical effect-handle
// token in entry/nonflesh/flesh order.  Null tokens keep an empty slot and do
// not invoke the resolver, preserving exact source-slot identity for every
// non-null callback even when token words repeat.
class alignas(8) FxFastFileImpactNativeDisk32Workspace final
{
  public:
    FxFastFileImpactNativeDisk32Workspace() noexcept = default;
    ~FxFastFileImpactNativeDisk32Workspace() noexcept = default;

    FxFastFileImpactNativeDisk32Workspace(
        const FxFastFileImpactNativeDisk32Workspace &) = delete;
    FxFastFileImpactNativeDisk32Workspace &
    operator=(const FxFastFileImpactNativeDisk32Workspace &) = delete;
    FxFastFileImpactNativeDisk32Workspace(
        FxFastFileImpactNativeDisk32Workspace &&) = delete;
    FxFastFileImpactNativeDisk32Workspace &
    operator=(FxFastFileImpactNativeDisk32Workspace &&) = delete;

    [[nodiscard]] FxFastFileNativeDisk32Phase phase() const noexcept
    {
        return phase_;
    }

  private:
    friend FxFastFileNativeDisk32Status
    TryPlanFxImpactTableDisk32(FxFastFileImpactNativeDisk32Workspace *,
                               const FxFastFileImpactTableDisk32View &,
                               const FxFastFileDisk32Resolvers &,
                               FxFastFileImpactNativeDisk32Plan *) noexcept;
    friend FxFastFileNativeDisk32Status
    TryMaterializeFxImpactTableDisk32(FxFastFileImpactNativeDisk32Workspace *,
                                      const FxFastFileImpactNativeDisk32Plan &,
                                      void *,
                                      std::size_t,
                                      FxImpactTable **) noexcept;

    FxFastFileImpactTableDisk32View source_{};
    FxImpactTableDisk32 sourceHeaderSnapshot_{};
    FxImpactEntryDisk32 sourceEntrySnapshots_[kImpactSurfaceCount]{};
    FxFastFileImpactNativeDisk32Plan plan_{};
    FxFastFileDisk32ResolvedReference
        resolved_[kFxFastFileImpactDisk32JournalCount]{};
    std::uint64_t nextSerial_ = 1;
    std::uint32_t resolvedCount_ = 0;
    FxFastFileNativeDisk32Phase phase_ = FxFastFileNativeDisk32Phase::Empty;
    bool operating_ = false;
};

// Plans one legacy-compatible impact table.  Its non-null table token must
// correspond to exactly twelve provenance-validated Disk32 entries; legacy
// db_load treated this field as a boolean, so inline/shared/alias spellings are
// all accepted.  The root name and every non-null effect handle are resolved
// exactly once while the workspace's reentry gate is held.  Each resolved
// effect handle must identify one aligned, complete native FxEffectDef extent.
// The source header, all twelve source entries, and the resolver-returned name
// span must remain readable and byte-for-byte immutable throughout planning and
// for as long as the workspace remains Planned, including through every
// matching materialization attempt.  Failure leaves outPlan unchanged.
[[nodiscard]] FxFastFileNativeDisk32Status
TryPlanFxImpactTableDisk32(FxFastFileImpactNativeDisk32Workspace *workspace,
                           const FxFastFileImpactTableDisk32View &source,
                           const FxFastFileDisk32Resolvers &resolvers,
                           FxFastFileImpactNativeDisk32Plan *outPlan) noexcept;

// Writes FxImpactTable, its twelve FxImpactEntry records, and an owned root
// name into one aligned caller-owned blob.  Materialization performs no
// callbacks.  Every failure preserves both storage and outTable; success
// consumes the exact workspace/serial/source-bound plan.  The source header,
// entries, and resolver-returned name described above remain caller-owned and
// must stay readable and immutable while the workspace remains Planned and
// throughout this call.
[[nodiscard]] FxFastFileNativeDisk32Status TryMaterializeFxImpactTableDisk32(
    FxFastFileImpactNativeDisk32Workspace *workspace,
    const FxFastFileImpactNativeDisk32Plan &plan,
    void *storage,
    std::size_t capacity,
    FxImpactTable **outTable) noexcept;

static_assert(kFxFastFileImpactDisk32HandleCount == 396,
              "impact fast-file handle count drift");
static_assert(std::is_nothrow_default_constructible_v<
              FxFastFileImpactNativeDisk32Workspace>);
static_assert(
    std::is_nothrow_destructible_v<FxFastFileImpactNativeDisk32Workspace>);
static_assert(alignof(FxFastFileImpactNativeDisk32Plan) == 8);
static_assert(alignof(FxFastFileImpactNativeDisk32Workspace) == 8);
RUNTIME_SIZE(FxFastFileImpactNativeDisk32Plan, 0x38, 0x38);
RUNTIME_SIZE(FxFastFileImpactNativeDisk32Workspace, 0x1300, 0x1F78);
} // namespace fx::fastfile

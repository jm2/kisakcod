#include "db_fx_zone_adapter_wiring.h"

#include <EffectsCore/fx_fastfile_disk32.h>
#include <EffectsCore/fx_fastfile_impact_native_disk32.h>
#include <EffectsCore/fx_fastfile_native_arena.h>
#include <EffectsCore/fx_fastfile_native_disk32.h>
#include <EffectsCore/fx_fastfile_zone_adapter_disk32.h>

#include <cstring>

namespace db::fx_zone_adapter_wiring
{
namespace
{
struct ActiveBinding
{
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    bool probeActive = false;
};

ActiveBinding &MutableActiveBinding() noexcept
{
    static ActiveBinding binding;
    return binding;
}

bool NoopOracle(
    void *const,
    const void *const,
    const std::uint64_t) noexcept
{
    return true;
}

bool ValidateWireBytes(
    const void *const bytes,
    const std::uint64_t byteCount) noexcept
{
    return bytes != nullptr && byteCount != 0;
}

bool TryGetActiveBinding(
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace **const outWorkspace,
    fx::fastfile::FxFastFileNativeArena **const outArena) noexcept
{
    if (outWorkspace)
        *outWorkspace = nullptr;
    if (outArena)
        *outArena = nullptr;
    ActiveBinding &binding = MutableActiveBinding();
    if (!binding.probeActive || !binding.workspace || !binding.arena)
        return false;
    if (!binding.workspace->readyForCompositionAuthentication())
        return false;
    if (!binding.arena->bound())
        return false;
    if (outWorkspace)
        *outWorkspace = binding.workspace;
    if (outArena)
        *outArena = binding.arena;
    return true;
}

bool PublishImpactNoop(
    void *const,
    FxImpactTable *const materialized,
    FxImpactTable **const outPublished) noexcept
{
    if (outPublished)
        *outPublished = materialized;
    return true;
}

bool PublishEffectNoop(
    void *const,
    FxEffectDef *const materialized,
    FxEffectDef **const outPublished) noexcept
{
    if (outPublished)
        *outPublished = materialized;
    return true;
}
}

bool IsFxZoneAdapterBindingActive() noexcept
{
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    return TryGetActiveBinding(&workspace, &arena);
}

fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
TryGetActiveFxZoneAdapterWorkspace() noexcept
{
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    if (!TryGetActiveBinding(&workspace, &arena))
        return nullptr;
    return workspace;
}

fx::fastfile::FxFastFileNativeArena *
TryGetActiveFxZoneAdapterArena() noexcept
{
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    if (!TryGetActiveBinding(&workspace, &arena))
        return nullptr;
    return arena;
}

void ResetActiveFxZoneAdapterBindingProbe() noexcept
{
    ActiveBinding &binding = MutableActiveBinding();
    binding.workspace = nullptr;
    binding.arena = nullptr;
    binding.probeActive = false;
}

FxImpactTable *
TryWireImpactTableThroughActiveFxZoneAdapter(
    const bool atStreamStart,
    const void *const wireFxImpactTableDisk32,
    const std::uint64_t wireFxImpactTableDisk32Bytes) noexcept
{
    (void)atStreamStart;
    if (!ValidateWireBytes(
            wireFxImpactTableDisk32,
            wireFxImpactTableDisk32Bytes))
    {
        return nullptr;
    }
    if (wireFxImpactTableDisk32Bytes < sizeof(fx::fastfile::FxImpactTableDisk32))
    {
        return nullptr;
    }

    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    if (!TryGetActiveBinding(&workspace, &arena))
    {
        return nullptr;
    }

    const auto *const header =
        static_cast<const fx::fastfile::FxImpactTableDisk32 *>(
            wireFxImpactTableDisk32);

    const fx::fastfile::FxFastFileZoneAdapterCursor cursor{nullptr, &NoopOracle};
    using Status =
        fx::fastfile::FxFastFileZoneAdapterDisk32Status;
    const Status beginStatus =
        fx::fastfile::TryBeginFxImpactTableZoneDisk32(
            workspace, arena, cursor, header);
    if (beginStatus != Status::Success)
        return nullptr;

    const Status sealStatus =
        fx::fastfile::TrySealFxImpactTableZoneDisk32(workspace);
    if (sealStatus != Status::Success)
    {
        (void)fx::fastfile::AbortFxFastFileZoneAdapterDisk32(workspace);
        return nullptr;
    }

    fx::fastfile::FxFastFileZoneAdapterPublication publication{
        nullptr, &PublishEffectNoop, &PublishImpactNoop};
    FxImpactTable *published = nullptr;
    const Status publishStatus =
        fx::fastfile::TryPublishFxImpactTableZoneDisk32(
            workspace, publication, &published);
    if (publishStatus != Status::Success)
        return nullptr;
    return published;
}

FxEffectDef *
TryWireEffectDefThroughActiveFxZoneAdapter(
    const bool atStreamStart,
    const void *const wireFxEffectDefDisk32,
    const std::uint64_t wireFxEffectDefDisk32Bytes) noexcept
{
    (void)atStreamStart;
    if (!ValidateWireBytes(
            wireFxEffectDefDisk32,
            wireFxEffectDefDisk32Bytes))
    {
        return nullptr;
    }
    if (wireFxEffectDefDisk32Bytes < sizeof(fx::fastfile::FxEffectDefDisk32))
    {
        return nullptr;
    }

    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    if (!TryGetActiveBinding(&workspace, &arena))
    {
        return nullptr;
    }

    const auto *const header =
        static_cast<const fx::fastfile::FxEffectDefDisk32 *>(
            wireFxEffectDefDisk32);

    const fx::fastfile::FxFastFileZoneAdapterCursor cursor{nullptr, &NoopOracle};
    using Status =
        fx::fastfile::FxFastFileZoneAdapterDisk32Status;
    const Status beginStatus =
        fx::fastfile::TryBeginFxEffectDefZoneDisk32(
            workspace, arena, cursor, header);
    if (beginStatus != Status::Success)
        return nullptr;

    const Status sealStatus =
        fx::fastfile::TrySealFxEffectDefZoneDisk32(workspace);
    if (sealStatus != Status::Success)
    {
        (void)fx::fastfile::AbortFxFastFileZoneAdapterDisk32(workspace);
        return nullptr;
    }

    fx::fastfile::FxFastFileZoneAdapterPublication publication{
        nullptr, &PublishEffectNoop, &PublishImpactNoop};
    FxEffectDef *published = nullptr;
    const Status publishStatus =
        fx::fastfile::TryPublishFxEffectDefZoneDisk32(
            workspace, publication, &published);
    if (publishStatus != Status::Success)
        return nullptr;
    return published;
}

bool TryAbortActiveFxZoneAdapterTransaction() noexcept
{
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace = nullptr;
    fx::fastfile::FxFastFileNativeArena *arena = nullptr;
    if (!TryGetActiveBinding(&workspace, &arena))
        return false;
    if (workspace->frameDepth() == 0
        && workspace->phase()
            == fx::fastfile::FxFastFileZoneAdapterDisk32Phase::Idle)
    {
        return false;
    }
    const auto status =
        fx::fastfile::AbortFxFastFileZoneAdapterDisk32(workspace);
    return status
        == fx::fastfile::FxFastFileZoneAdapterDisk32Status::Success;
}

#ifdef KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING
void FxZoneAdapterWiringTestAccess::SetActiveBindingForTesting(
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *const workspace,
    fx::fastfile::FxFastFileNativeArena *const arena) noexcept
{
    ActiveBinding &binding = MutableActiveBinding();
    binding.workspace = workspace;
    binding.arena = arena;
    binding.probeActive = workspace != nullptr && arena != nullptr;
}

void FxZoneAdapterWiringTestAccess::ClearActiveBindingForTesting() noexcept
{
    ActiveBinding &binding = MutableActiveBinding();
    binding.workspace = nullptr;
    binding.arena = nullptr;
    binding.probeActive = false;
}
#endif
}

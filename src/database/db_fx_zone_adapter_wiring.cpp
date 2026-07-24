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
    return false;
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
}

FxImpactTable *
TryWireImpactTableThroughActiveFxZoneAdapter(
    bool atStreamStart,
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
    return nullptr;
}

FxEffectDef *
TryWireEffectDefThroughActiveFxZoneAdapter(
    bool atStreamStart,
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
    return nullptr;
}

bool TryAbortActiveFxZoneAdapterTransaction() noexcept
{
    return false;
}
}

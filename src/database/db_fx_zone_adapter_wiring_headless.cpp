#include "db_fx_zone_adapter_wiring.h"

#if !defined(KISAK_DEDI_HEADLESS)
#error "The unavailable FX zone-adapter wiring boundary is headless-only"
#endif

namespace db::fx_zone_adapter_wiring
{
bool IsFxZoneAdapterBindingActive() noexcept
{
    return false;
}

fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
TryGetActiveFxZoneAdapterWorkspace() noexcept
{
    return nullptr;
}

fx::fastfile::FxFastFileNativeArena *
TryGetActiveFxZoneAdapterArena() noexcept
{
    return nullptr;
}

void ResetActiveFxZoneAdapterBindingProbe() noexcept
{
}

bool TryEnrollActiveFxZoneAdapterBinding(
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *,
    fx::fastfile::FxFastFileNativeArena *) noexcept
{
    return false;
}

bool TryClearActiveFxZoneAdapterBinding(
    const fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *,
    const fx::fastfile::FxFastFileNativeArena *) noexcept
{
    return false;
}

FxImpactTable *
TryWireImpactTableThroughActiveFxZoneAdapter(
    bool,
    const void *,
    std::uint64_t) noexcept
{
    return nullptr;
}

FxEffectDef *
TryWireEffectDefThroughActiveFxZoneAdapter(
    bool,
    const void *,
    std::uint64_t) noexcept
{
    return nullptr;
}

bool TryAbortActiveFxZoneAdapterTransaction() noexcept
{
    return false;
}
} // namespace db::fx_zone_adapter_wiring

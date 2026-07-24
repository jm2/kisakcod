#pragma once

#include <cstdint>

struct FxEffectDef;
struct FxImpactTable;

namespace fx::fastfile
{
class FxFastFileZoneAdapterDisk32Workspace;
class FxFastFileNativeArena;
}

namespace db::fx_zone_adapter_wiring
{
[[nodiscard]] bool IsFxZoneAdapterBindingActive() noexcept;

[[nodiscard]] fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
TryGetActiveFxZoneAdapterWorkspace() noexcept;

[[nodiscard]] fx::fastfile::FxFastFileNativeArena *
TryGetActiveFxZoneAdapterArena() noexcept;

void ResetActiveFxZoneAdapterBindingProbe() noexcept;

[[nodiscard]] FxImpactTable *
TryWireImpactTableThroughActiveFxZoneAdapter(
    bool atStreamStart,
    const void *wireFxImpactTableDisk32,
    std::uint64_t wireFxImpactTableDisk32Bytes) noexcept;

[[nodiscard]] FxEffectDef *
TryWireEffectDefThroughActiveFxZoneAdapter(
    bool atStreamStart,
    const void *wireFxEffectDefDisk32,
    std::uint64_t wireFxEffectDefDisk32Bytes) noexcept;

[[nodiscard]] bool TryAbortActiveFxZoneAdapterTransaction() noexcept;

#ifdef KISAK_DB_FX_ZONE_ADAPTER_WIRING_TESTING
// Test-only injection seam. Production callers have no path through this
// boundary; the wiring module's active binding is normally populated by the
// zone runtime-table controller after a successful bound-storage receipt. The
// test hook mirrors that receipt without crossing the runtime-table thread
// safety contract.
struct FxZoneAdapterWiringTestAccess final
{
    static void SetActiveBindingForTesting(
        fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace,
        fx::fastfile::FxFastFileNativeArena *arena) noexcept;

    static void ClearActiveBindingForTesting() noexcept;
};
#endif
}

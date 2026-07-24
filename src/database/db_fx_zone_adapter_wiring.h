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
}

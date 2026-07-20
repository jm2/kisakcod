#include <database/db_zone_runtime_storage_fx_bridge.h>

#if !defined(KISAK_DEDI_HEADLESS)
#error "The unavailable FX runtime-storage bridge is headless-only"
#endif

namespace db::zone_runtime_storage::detail
{
FxRuntimeStorageLayout GetFxRuntimeStorageLayout() noexcept
{
    // The current headless profile deliberately excludes EffectsCore.  A
    // zero-alignment layout makes TryPlanZoneRuntimeStorage fail closed before
    // placement while still allowing the production-neutral runtime table to
    // authenticate its pristine storage receipt.
    return {};
}

fx::fastfile::FxFastFileNativeArena *
ConstructFxRuntimeArena(void *) noexcept
{
    return nullptr;
}

fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
ConstructFxRuntimeWorkspace(void *) noexcept
{
    return nullptr;
}

FxRuntimeStorageBindStatus TryBindFxRuntimeStorage(
    fx::fastfile::FxFastFileNativeArena *,
    void *,
    std::uint32_t,
    std::uint64_t) noexcept
{
    return FxRuntimeStorageBindStatus::InvalidPhase;
}

bool AuthenticateStableFxRuntimeStorage(
    const fx::fastfile::FxFastFileNativeArena *,
    const fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *,
    const void *,
    std::uint32_t,
    std::uint64_t) noexcept
{
    return false;
}

FxRuntimeStorageDestroyStatus TryPrepareFxRuntimeStorageDestroy(
    fx::fastfile::FxFastFileNativeArena *,
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *,
    const void *,
    std::uint32_t) noexcept
{
    return FxRuntimeStorageDestroyStatus::InvalidBinding;
}

void DestroyFxRuntimeWorkspace(
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *) noexcept
{
}

void DestroyFxRuntimeArena(
    fx::fastfile::FxFastFileNativeArena *) noexcept
{
}
} // namespace db::zone_runtime_storage::detail

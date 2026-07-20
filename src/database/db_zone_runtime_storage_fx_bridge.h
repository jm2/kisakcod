#pragma once

#include <cstdint>

namespace fx::fastfile
{
class FxFastFileNativeArena;
class FxFastFileZoneAdapterDisk32Workspace;
} // namespace fx::fastfile

namespace db::zone_runtime_storage::detail
{
// Private cross-domain bridge: the public runtime-storage API stays neutral,
// while this implementation boundary owns the complete FX types required for
// placement construction and checked teardown.
struct FxRuntimeStorageLayout final
{
    std::uint32_t arenaBytes = 0;
    std::uint32_t arenaAlignment = 0;
    std::uint32_t workspaceBytes = 0;
    std::uint32_t workspaceAlignment = 0;
    std::uint32_t backingAlignment = 0;
};

enum class FxRuntimeStorageDestroyStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidBinding,
    InvalidPhase,
    ArenaFailed,
};

enum class FxRuntimeStorageBindStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidPhase,
    MisalignedStorage,
    SizeOverflow,
    InsufficientCapacity,
    TransactionLimit,
    InvalidTransaction,
};

[[nodiscard]] FxRuntimeStorageLayout GetFxRuntimeStorageLayout() noexcept;
[[nodiscard]] fx::fastfile::FxFastFileNativeArena *
ConstructFxRuntimeArena(void *storage) noexcept;
[[nodiscard]] fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *
ConstructFxRuntimeWorkspace(void *storage) noexcept;

// Binds the placed arena without exposing its EffectsCore definition to the
// database controller or headless source dependency surface.
[[nodiscard]] FxRuntimeStorageBindStatus TryBindFxRuntimeStorage(
    fx::fastfile::FxFastFileNativeArena *arena,
    void *backing,
    std::uint32_t budget,
    std::uint64_t zoneIdentity) noexcept;

// Read-only stable-boundary authentication for the storage composition. The
// workspace must be Idle and non-operating; the arena must name the exact
// planned backing, budget, and nonzero outer-controller identity with no open
// transaction. No FX pointer or mutation authority is returned.
[[nodiscard]] bool AuthenticateStableFxRuntimeStorage(
    const fx::fastfile::FxFastFileNativeArena *arena,
    const fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace,
    const void *expectedBacking,
    std::uint32_t expectedBudget,
    std::uint64_t expectedZoneIdentity) noexcept;

// Validates an Idle workspace and transaction-free arena. A bound arena must
// name the exact planned backing and budget. TryUnbind is the sole mutation.
[[nodiscard]] FxRuntimeStorageDestroyStatus TryPrepareFxRuntimeStorageDestroy(
    fx::fastfile::FxFastFileNativeArena *arena,
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace,
    const void *expectedBacking,
    std::uint32_t expectedBudget) noexcept;

void DestroyFxRuntimeWorkspace(
    fx::fastfile::FxFastFileZoneAdapterDisk32Workspace *workspace) noexcept;
void DestroyFxRuntimeArena(
    fx::fastfile::FxFastFileNativeArena *arena) noexcept;
} // namespace db::zone_runtime_storage::detail

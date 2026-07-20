#include "fx_fastfile_native_arena.h"
#include "fx_fastfile_zone_adapter_disk32.h"

#include <database/db_zone_runtime_storage_fx_bridge.h>

#include <limits>
#include <new>
#include <type_traits>

namespace db::zone_runtime_storage::detail
{
namespace
{
using Arena = fx::fastfile::FxFastFileNativeArena;
using ArenaStatus = fx::fastfile::FxFastFileNativeArenaStatus;
using Phase = fx::fastfile::FxFastFileZoneAdapterDisk32Phase;
using Status = FxRuntimeStorageDestroyStatus;
using Workspace = fx::fastfile::FxFastFileZoneAdapterDisk32Workspace;

static_assert(std::is_nothrow_default_constructible_v<Arena>);
static_assert(std::is_nothrow_destructible_v<Arena>);
static_assert(std::is_nothrow_default_constructible_v<Workspace>);
static_assert(std::is_nothrow_destructible_v<Workspace>);
static_assert(alignof(Arena) <= UINT32_MAX);
static_assert(alignof(Workspace) <= UINT32_MAX);
static_assert(
    fx::fastfile::kFxFastFileNativeArenaStorageAlignment <= UINT32_MAX);
} // namespace

FxRuntimeStorageLayout GetFxRuntimeStorageLayout() noexcept
{
    return {
        static_cast<std::uint32_t>(sizeof(Arena)),
        static_cast<std::uint32_t>(alignof(Arena)),
        static_cast<std::uint32_t>(sizeof(Workspace)),
        static_cast<std::uint32_t>(alignof(Workspace)),
        static_cast<std::uint32_t>(
            fx::fastfile::kFxFastFileNativeArenaStorageAlignment)};
}

Arena *ConstructFxRuntimeArena(void *const storage) noexcept
{
    return ::new (storage) Arena{};
}

Workspace *ConstructFxRuntimeWorkspace(void *const storage) noexcept
{
    return ::new (storage) Workspace{};
}

FxRuntimeStorageBindStatus TryBindFxRuntimeStorage(
    Arena *const arena,
    void *const backing,
    const std::uint32_t budget,
    const std::uint64_t zoneIdentity) noexcept
{
    if (!arena)
        return FxRuntimeStorageBindStatus::InvalidArgument;
    switch (arena->TryBind(backing, budget, zoneIdentity))
    {
    case ArenaStatus::Success:
        return FxRuntimeStorageBindStatus::Success;
    case ArenaStatus::Busy:
        return FxRuntimeStorageBindStatus::Busy;
    case ArenaStatus::InvalidArgument:
        return FxRuntimeStorageBindStatus::InvalidArgument;
    case ArenaStatus::InvalidPhase:
        return FxRuntimeStorageBindStatus::InvalidPhase;
    case ArenaStatus::MisalignedStorage:
        return FxRuntimeStorageBindStatus::MisalignedStorage;
    case ArenaStatus::SizeOverflow:
        return FxRuntimeStorageBindStatus::SizeOverflow;
    case ArenaStatus::InsufficientCapacity:
        return FxRuntimeStorageBindStatus::InsufficientCapacity;
    case ArenaStatus::TransactionLimit:
        return FxRuntimeStorageBindStatus::TransactionLimit;
    case ArenaStatus::InvalidTransaction:
    default:
        return FxRuntimeStorageBindStatus::InvalidTransaction;
    }
}

bool AuthenticateStableFxRuntimeStorage(
    const Arena *const arena,
    const Workspace *const workspace,
    const void *const expectedBacking,
    const std::uint32_t expectedBudget,
    const std::uint64_t expectedZoneIdentity) noexcept
{
    if (!arena || !workspace || !expectedBacking || expectedBudget == 0
        || expectedZoneIdentity == 0)
    {
        return false;
    }

    // This predicate authenticates the complete depth-zero reset topology
    // without indexing a frame through an untrusted depth witness.
    if (!workspace->readyForCompositionAuthentication())
        return false;

    return arena->readyForCompositionAuthentication()
        && arena->bound() && arena->storage() == expectedBacking
        && arena->zoneIdentity() == expectedZoneIdentity
        && arena->capacity() == expectedBudget
        && arena->usedBytes() <= arena->capacity()
        && arena->committedBytes() <= arena->usedBytes()
        && arena->openTransactionDepth() == 0;
}

FxRuntimeStorageDestroyStatus TryPrepareFxRuntimeStorageDestroy(
    Arena *const arena,
    Workspace *const workspace,
    const void *const expectedBacking,
    const std::uint32_t expectedBudget) noexcept
{
    if (!arena || !workspace || !expectedBacking || expectedBudget == 0)
        return Status::InvalidBinding;
    if (!workspace->readyForDestruction()
        || workspace->frameDepth() != 0 || workspace->phase() != Phase::Idle
        || arena->openTransactionDepth() != 0)
    {
        return Status::InvalidPhase;
    }

    if (arena->bound())
    {
        if (arena->storage() != expectedBacking || arena->zoneIdentity() == 0
            || arena->capacity() != expectedBudget
            || arena->usedBytes() > arena->capacity()
            || arena->committedBytes() > arena->usedBytes())
        {
            return Status::InvalidBinding;
        }
        const ArenaStatus unbindStatus = arena->TryUnbind();
        if (unbindStatus == ArenaStatus::Busy)
            return Status::Busy;
        return unbindStatus == ArenaStatus::Success
            ? Status::Success
            : Status::ArenaFailed;
    }

    return arena->storage() == nullptr && arena->zoneIdentity() == 0
            && arena->capacity() == 0 && arena->usedBytes() == 0
            && arena->committedBytes() == 0
        ? Status::Success
        : Status::InvalidBinding;
}

void DestroyFxRuntimeWorkspace(Workspace *const workspace) noexcept
{
    workspace->~FxFastFileZoneAdapterDisk32Workspace();
}

void DestroyFxRuntimeArena(Arena *const arena) noexcept
{
    arena->~FxFastFileNativeArena();
}
} // namespace db::zone_runtime_storage::detail

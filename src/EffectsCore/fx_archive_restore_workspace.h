#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

namespace fx::archive
{
using ArchiveRestoreWorkspaceAllocateCallback =
    void *(*)(void *context, int byteCount) noexcept;
using ArchiveRestoreWorkspaceFreeCallback =
    void (*)(void *context, void *storage) noexcept;

struct ArchiveRestoreWorkspaceMemoryCallbacks
{
    void *context = nullptr;
    ArchiveRestoreWorkspaceAllocateCallback allocate = nullptr;
    ArchiveRestoreWorkspaceFreeCallback free = nullptr;
};

// Keeps the engine's signed allocation ABI behind one checked conversion. The
// output is changed only when the conversion succeeds.
[[nodiscard]] constexpr bool TryNarrowArchiveRestoreWorkspaceSize(
    const std::size_t byteCount,
    int *const outByteCount) noexcept
{
    if (!outByteCount
        || byteCount > static_cast<std::size_t>(
            (std::numeric_limits<int>::max)()))
    {
        return false;
    }

    *outByteCount = static_cast<int>(byteCount);
    return true;
}

template <typename Workspace>
inline constexpr bool IsSupportedArchiveRestoreWorkspace =
    std::is_object_v<Workspace>
    && !std::is_array_v<Workspace>
    && std::is_nothrow_default_constructible_v<Workspace>
    && std::is_nothrow_destructible_v<Workspace>
    && alignof(Workspace) <= alignof(std::max_align_t);

template <typename Workspace>
concept SupportedArchiveRestoreWorkspace =
    IsSupportedArchiveRestoreWorkspace<Workspace>;

template <SupportedArchiveRestoreWorkspace Workspace>
[[nodiscard]] constexpr bool TryGetArchiveRestoreWorkspaceAllocationSize(
    int *const outByteCount) noexcept
{
    return TryNarrowArchiveRestoreWorkspaceSize(
        sizeof(Workspace), outByteCount);
}

// The callbacks deliberately remain caller-owned adapters so this helper has
// no dependency on the engine allocator. Both are required before allocation:
// every accepted storage address must have a matching release path.
template <SupportedArchiveRestoreWorkspace Workspace>
[[nodiscard]] inline Workspace *AllocateArchiveRestoreWorkspace(
    const ArchiveRestoreWorkspaceMemoryCallbacks &callbacks) noexcept
{
    if (!callbacks.allocate || !callbacks.free)
        return nullptr;

    int byteCount = 0;
    if (!TryGetArchiveRestoreWorkspaceAllocationSize<Workspace>(&byteCount))
        return nullptr;

    void *const storage = callbacks.allocate(callbacks.context, byteCount);
    if (!storage)
        return nullptr;

    if (reinterpret_cast<std::uintptr_t>(storage) % alignof(Workspace) != 0)
    {
        callbacks.free(callbacks.context, storage);
        return nullptr;
    }

    return ::new (storage) Workspace();
}

// A missing release callback leaves a live object untouched so the caller can
// retry with the correct adapter. Successful finalization always destroys the
// workspace before returning its storage.
template <SupportedArchiveRestoreWorkspace Workspace>
[[nodiscard]] inline bool DestroyArchiveRestoreWorkspace(
    Workspace *const workspace,
    const ArchiveRestoreWorkspaceMemoryCallbacks &callbacks) noexcept
{
    if (!workspace)
        return true;
    if (!callbacks.free)
        return false;

    workspace->~Workspace();
    callbacks.free(callbacks.context, workspace);
    return true;
}
} // namespace fx::archive

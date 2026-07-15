#pragma once

#include <EffectsCore/fx_archive_key.h>

#include <cstddef>
#include <cstdint>

struct MemoryFile;

namespace fx::archive
{
inline constexpr std::size_t EFFECT_TABLE_RESTORE_CAPACITY = 1024;
inline constexpr std::size_t EFFECT_TABLE_RESTORE_NAME_CAPACITY = 64;

enum class EffectTableRestoreStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidState,
    LifecycleChanged,
    TruncatedInput,
    NameTooLong,
    InvalidName,
    CapacityExceeded,
    InvalidKey,
    ConflictingDuplicate,
    RegistrationFailed,
    OwnerMismatch,
    UnsafeFailure,
};

struct EffectTableRestoreLease
{
    const void *identity = nullptr;
    std::uint32_t lifecycleGeneration = 0;
    std::uint64_t serial = 0;
    const void *ownerCookie = nullptr;
};

using EffectTableRestoreValidateLifecycleCallback =
    bool (*)(void *context, const void *identity,
            std::uint32_t lifecycleGeneration) noexcept;
using EffectTableRestoreRegisterEffectCallback =
    const void *(*)(void *context, const char *name) noexcept;

struct EffectTableRestoreCallbacks
{
    void *context = nullptr;
    EffectTableRestoreValidateLifecycleCallback validateLifecycle = nullptr;
    EffectTableRestoreRegisterEffectCallback registerEffect = nullptr;
};

struct EffectTableRestoreResult
{
    EffectTableRestoreStatus status = EffectTableRestoreStatus::InvalidArgument;
    EffectTableRestoreLease lease{};
    std::size_t entryCount = 0;
};

[[nodiscard]] bool EffectTableRestoreNameIsValid(const char *name) noexcept;

[[nodiscard]] EffectTableRestoreResult RestoreEffectTableNoReport(
    MemoryFile *memFile,
    const void *identity,
    std::uint32_t lifecycleGeneration,
    const EffectTableRestoreCallbacks &callbacks) noexcept;

[[nodiscard]] bool EffectTableRestoreGetEntry(
    const EffectTableRestoreLease &lease,
    std::size_t index,
    EffectDefinitionKey32 *key,
    const void **definition) noexcept;

[[nodiscard]] const void *EffectTableRestoreFind(
    const EffectTableRestoreLease &lease,
    EffectDefinitionKey32 key) noexcept;

[[nodiscard]] EffectTableRestoreStatus ReleaseEffectTableRestore(
    const EffectTableRestoreLease &lease) noexcept;

void AbandonCurrentThreadEffectTableRestoreForError() noexcept;

[[nodiscard]] bool EffectTableRestoreLeaseIsActive() noexcept;
} // namespace fx::archive

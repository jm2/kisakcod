#include <EffectsCore/fx_effect_table_save.h>

#include <EffectsCore/fx_effect_table_restore.h>

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace fx::archive
{
struct EffectTableSaveSnapshot final
{
    struct Record
    {
        char name[EFFECT_TABLE_RESTORE_NAME_CAPACITY];
        std::uint32_t key;
    };

    enum class Phase : std::uint8_t
    {
        Capturing,
        Validated,
        Writing,
        Written,
        Failed,
    };

    Record records[EFFECT_TABLE_RESTORE_CAPACITY];
    std::size_t entryCount;
    Phase phase;
    EffectTableSaveStatus status;

    EffectTableSaveSnapshot() noexcept
        : entryCount(0),
          phase(Phase::Capturing),
          status(EffectTableSaveStatus::Success)
    {
    }

    ~EffectTableSaveSnapshot() noexcept = default;
};

static_assert(EFFECT_TABLE_RESTORE_CAPACITY == 1024);
static_assert(EFFECT_TABLE_RESTORE_NAME_CAPACITY == 64);
RUNTIME_SIZE(EffectTableSaveSnapshot::Record, 0x44, 0x44);
RUNTIME_SIZE(EffectTableSaveSnapshot, 0x11008, 0x11010);

namespace
{
EffectTableSaveStatus Fail(
    EffectTableSaveSnapshot *const snapshot,
    const EffectTableSaveStatus status) noexcept
{
    if (snapshot->status == EffectTableSaveStatus::Success)
        snapshot->status = status;
    return snapshot->status;
}

bool BoundedNameLength(
    const char *const name,
    std::size_t *const length) noexcept
{
    for (std::size_t index = 0;
         index < EFFECT_TABLE_RESTORE_NAME_CAPACITY;
         ++index)
    {
        if (name[index] == '\0')
        {
            *length = index;
            return true;
        }
    }
    return false;
}

bool RecordNamesEqual(
    const EffectTableSaveSnapshot::Record &left,
    const EffectTableSaveSnapshot::Record &right) noexcept
{
    for (std::size_t index = 0;
         index < EFFECT_TABLE_RESTORE_NAME_CAPACITY;
         ++index)
    {
        if (left.name[index] != right.name[index])
            return false;
        if (left.name[index] == '\0')
            return true;
    }
    return false;
}

EffectTableSaveStatus WriteBytes(
    EffectTableSaveSnapshot *const snapshot,
    const EffectTableSaveCallbacks &callbacks,
    const void *const data,
    const std::size_t byteCount) noexcept
{
    const bool written = callbacks.write(
        callbacks.context,
        data,
        byteCount);

    // A writer may call back into this API. Such reentry poisons the one-shot
    // operation instead of allowing a nested write to observe Validated.
    if (snapshot->status != EffectTableSaveStatus::Success)
    {
        snapshot->phase = EffectTableSaveSnapshot::Phase::Failed;
        return snapshot->status;
    }
    if (snapshot->phase != EffectTableSaveSnapshot::Phase::Writing)
    {
        const EffectTableSaveStatus status =
            Fail(snapshot, EffectTableSaveStatus::InvalidState);
        snapshot->phase = EffectTableSaveSnapshot::Phase::Failed;
        return status;
    }
    if (!written)
    {
        const EffectTableSaveStatus status =
            Fail(snapshot, EffectTableSaveStatus::WriterFailed);
        snapshot->phase = EffectTableSaveSnapshot::Phase::Failed;
        return status;
    }
    return EffectTableSaveStatus::Success;
}
} // namespace

std::size_t EffectTableSaveSnapshotSize() noexcept
{
    return sizeof(EffectTableSaveSnapshot);
}

std::size_t EffectTableSaveSnapshotAlignment() noexcept
{
    return alignof(EffectTableSaveSnapshot);
}

EffectTableSaveSnapshot *ConstructEffectTableSaveSnapshot(
    void *const storage,
    const std::size_t storageSize) noexcept
{
    if (!storage || storageSize < sizeof(EffectTableSaveSnapshot)
        || reinterpret_cast<std::uintptr_t>(storage)
                % alignof(EffectTableSaveSnapshot)
            != 0)
    {
        return nullptr;
    }

    return ::new (storage) EffectTableSaveSnapshot();
}

bool DestroyEffectTableSaveSnapshot(
    EffectTableSaveSnapshot *const snapshot) noexcept
{
    if (!snapshot)
        return true;
    if (snapshot->phase == EffectTableSaveSnapshot::Phase::Writing)
        return false;
    snapshot->~EffectTableSaveSnapshot();
    return true;
}

EffectTableSaveStatus AppendEffectTableSaveEntryNoReport(
    EffectTableSaveSnapshot *const snapshot,
    const char *const name,
    const std::uintptr_t key) noexcept
{
    if (!snapshot)
        return EffectTableSaveStatus::InvalidArgument;
    if (snapshot->status != EffectTableSaveStatus::Success)
        return snapshot->status;
    if (snapshot->phase != EffectTableSaveSnapshot::Phase::Capturing)
        return Fail(snapshot, EffectTableSaveStatus::InvalidState);
    if (!name)
        return Fail(snapshot, EffectTableSaveStatus::InvalidArgument);
    if (key == 0
        || key > static_cast<std::uintptr_t>(
            (std::numeric_limits<std::uint32_t>::max)()))
    {
        return Fail(snapshot, EffectTableSaveStatus::InvalidKey);
    }
    if (snapshot->entryCount >= EFFECT_TABLE_RESTORE_CAPACITY)
        return Fail(snapshot, EffectTableSaveStatus::CapacityExceeded);

    std::size_t nameLength = 0;
    if (!BoundedNameLength(name, &nameLength))
        return Fail(snapshot, EffectTableSaveStatus::NameTooLong);

    EffectTableSaveSnapshot::Record &record =
        snapshot->records[snapshot->entryCount];
    std::memcpy(record.name, name, nameLength + 1u);
    record.key = static_cast<std::uint32_t>(key);
    ++snapshot->entryCount;
    return EffectTableSaveStatus::Success;
}

EffectTableSaveStatus ValidateEffectTableSaveSnapshotNoReport(
    EffectTableSaveSnapshot *const snapshot) noexcept
{
    if (!snapshot)
        return EffectTableSaveStatus::InvalidArgument;
    if (snapshot->status != EffectTableSaveStatus::Success)
        return snapshot->status;
    if (snapshot->phase != EffectTableSaveSnapshot::Phase::Capturing)
        return Fail(snapshot, EffectTableSaveStatus::InvalidState);

    for (std::size_t index = 0; index < snapshot->entryCount; ++index)
    {
        const EffectTableSaveSnapshot::Record &record =
            snapshot->records[index];
        std::size_t nameLength = 0;
        if (!BoundedNameLength(record.name, &nameLength))
            return Fail(snapshot, EffectTableSaveStatus::NameTooLong);
        if (!EffectTableRestoreNameIsValid(record.name))
            return Fail(snapshot, EffectTableSaveStatus::InvalidName);
        if (record.key == 0)
            return Fail(snapshot, EffectTableSaveStatus::InvalidKey);

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const EffectTableSaveSnapshot::Record &other =
                snapshot->records[previous];
            if (other.key == record.key
                && !RecordNamesEqual(other, record))
            {
                return Fail(
                    snapshot,
                    EffectTableSaveStatus::ConflictingDuplicate);
            }
        }
    }

    snapshot->phase = EffectTableSaveSnapshot::Phase::Validated;
    return EffectTableSaveStatus::Success;
}

EffectTableSaveStatus WriteEffectTableSaveSnapshotNoReport(
    EffectTableSaveSnapshot *const snapshot,
    const EffectTableSaveCallbacks &callbacks) noexcept
{
    if (!snapshot)
        return EffectTableSaveStatus::InvalidArgument;
    if (snapshot->status != EffectTableSaveStatus::Success)
        return snapshot->status;
    if (snapshot->phase != EffectTableSaveSnapshot::Phase::Validated)
        return Fail(snapshot, EffectTableSaveStatus::InvalidState);

    const EffectTableSaveCallbacks stableCallbacks = callbacks;
    if (!stableCallbacks.write)
        return Fail(snapshot, EffectTableSaveStatus::InvalidArgument);

    snapshot->phase = EffectTableSaveSnapshot::Phase::Writing;
    for (std::size_t index = 0; index < snapshot->entryCount; ++index)
    {
        const EffectTableSaveSnapshot::Record &record =
            snapshot->records[index];
        std::size_t nameLength = 0;
        if (!BoundedNameLength(record.name, &nameLength)
            || record.key == 0)
        {
            const EffectTableSaveStatus status =
                Fail(snapshot, EffectTableSaveStatus::InvalidState);
            snapshot->phase = EffectTableSaveSnapshot::Phase::Failed;
            return status;
        }

        EffectTableSaveStatus status = WriteBytes(
            snapshot,
            stableCallbacks,
            record.name,
            nameLength + 1u);
        if (status != EffectTableSaveStatus::Success)
            return status;

        const std::uint8_t keyBytes[4]{
            static_cast<std::uint8_t>(record.key),
            static_cast<std::uint8_t>(record.key >> 8u),
            static_cast<std::uint8_t>(record.key >> 16u),
            static_cast<std::uint8_t>(record.key >> 24u),
        };
        status = WriteBytes(
            snapshot,
            stableCallbacks,
            keyBytes,
            sizeof(keyBytes));
        if (status != EffectTableSaveStatus::Success)
            return status;
    }

    const std::uint8_t terminator = 0;
    const EffectTableSaveStatus status = WriteBytes(
        snapshot,
        stableCallbacks,
        &terminator,
        sizeof(terminator));
    if (status != EffectTableSaveStatus::Success)
        return status;

    snapshot->phase = EffectTableSaveSnapshot::Phase::Written;
    return EffectTableSaveStatus::Success;
}

std::size_t EffectTableSaveEntryCount(
    const EffectTableSaveSnapshot *const snapshot) noexcept
{
    return snapshot ? snapshot->entryCount : 0;
}
} // namespace fx::archive

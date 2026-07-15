#include <EffectsCore/fx_effect_table_restore.h>

#include <universal/memfile.h>
#include <universal/sys_atomic.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fx::archive
{
namespace
{
enum class OwnerPhase : std::uint8_t
{
    Idle,
    Active,
    Closing,
};

struct EffectTableRestoreOwner
{
    OwnerPhase phase;
    EffectTableRestoreLease lease;
    EffectTableRestoreCallbacks callbacks;
};

// These arrays deliberately have static storage and no explicit initializer.
// They are the one bounded restore workspace and remain in BSS rather than
// consuming either the heap or a large engine-thread stack frame.
char g_restoreNames[EFFECT_TABLE_RESTORE_CAPACITY]
                   [EFFECT_TABLE_RESTORE_NAME_CAPACITY];
std::uint8_t g_restoreNameLengths[EFFECT_TABLE_RESTORE_CAPACITY];
std::uint32_t g_restoreKeys[EFFECT_TABLE_RESTORE_CAPACITY];
const void *g_restoreDefinitions[EFFECT_TABLE_RESTORE_CAPACITY];
char g_capacityProbe[EFFECT_TABLE_RESTORE_NAME_CAPACITY];
std::size_t g_restoreEntryCount;
std::uint64_t g_restoreSerial;

// nullptr is Open, a thread-local owner address is Active, and this address is
// Closing.  Closing is a real, stable sentinel so no active owner cookie can
// alias it on any supported ABI.
std::uint8_t g_closingSentinel;
void *volatile g_restoreOwnerCookie;

thread_local EffectTableRestoreOwner g_currentThreadOwner;

void *CurrentThreadOwnerCookie() noexcept
{
    return static_cast<void *>(&g_currentThreadOwner);
}

void *ClosingCookie() noexcept
{
    return static_cast<void *>(&g_closingSentinel);
}

void *LoadOwnerCookie() noexcept
{
    return Sys_AtomicCompareExchangePointer(
        &g_restoreOwnerCookie,
        static_cast<void *>(nullptr),
        static_cast<void *>(nullptr));
}

bool LeaseEquals(
    const EffectTableRestoreLease &left,
    const EffectTableRestoreLease &right) noexcept
{
    return left.identity == right.identity
        && left.lifecycleGeneration == right.lifecycleGeneration
        && left.serial == right.serial
        && left.ownerCookie == right.ownerCookie;
}

bool CurrentThreadOwnsLease(
    const EffectTableRestoreLease &lease,
    const bool allowClosing) noexcept
{
    const OwnerPhase phase = g_currentThreadOwner.phase;
    if (phase != OwnerPhase::Active
        && !(allowClosing && phase == OwnerPhase::Closing))
    {
        return false;
    }
    if (!LeaseEquals(g_currentThreadOwner.lease, lease)
        || lease.ownerCookie != CurrentThreadOwnerCookie())
    {
        return false;
    }
    const void *const expectedCookie = phase == OwnerPhase::Active
        ? lease.ownerCookie
        : ClosingCookie();
    return LoadOwnerCookie() == expectedCookie;
}

void ClearRestoreWorkspace() noexcept
{
    // Publish an empty table before clearing its backing storage.  Only the
    // exact owner can read entries, and the gate is already Closing here.
    g_restoreEntryCount = 0;
    std::memset(g_restoreNames, 0, sizeof(g_restoreNames));
    std::memset(g_restoreNameLengths, 0, sizeof(g_restoreNameLengths));
    std::memset(g_restoreKeys, 0, sizeof(g_restoreKeys));
    for (std::size_t index = 0; index < EFFECT_TABLE_RESTORE_CAPACITY; ++index)
        g_restoreDefinitions[index] = nullptr;
    std::memset(g_capacityProbe, 0, sizeof(g_capacityProbe));
}

void ClearCurrentThreadOwner() noexcept
{
    g_currentThreadOwner.phase = OwnerPhase::Idle;
    g_currentThreadOwner.lease = EffectTableRestoreLease{};
    g_currentThreadOwner.callbacks = EffectTableRestoreCallbacks{};
}

EffectTableRestoreStatus CloseCurrentThreadLease(
    const EffectTableRestoreLease &lease) noexcept
{
    if (!CurrentThreadOwnsLease(lease, true))
        return EffectTableRestoreStatus::OwnerMismatch;

    if (g_currentThreadOwner.phase == OwnerPhase::Active)
    {
        void *const oldCookie = Sys_AtomicCompareExchangePointer(
            &g_restoreOwnerCookie,
            ClosingCookie(),
            CurrentThreadOwnerCookie());
        if (oldCookie != CurrentThreadOwnerCookie())
            return EffectTableRestoreStatus::OwnerMismatch;
        g_currentThreadOwner.phase = OwnerPhase::Closing;
    }

    // A failed reopen leaves the TLS state and lease intact.  A later Release
    // or Abandon can therefore safely retry the Closing phase.
    if (LoadOwnerCookie() != ClosingCookie())
        return EffectTableRestoreStatus::OwnerMismatch;

    ClearRestoreWorkspace();
    void *const oldCookie = Sys_AtomicCompareExchangePointer(
        &g_restoreOwnerCookie,
        static_cast<void *>(nullptr),
        ClosingCookie());
    if (oldCookie != ClosingCookie())
        return EffectTableRestoreStatus::UnsafeFailure;

    // Reopen the process-wide gate before erasing the only retry token.
    ClearCurrentThreadOwner();
    return EffectTableRestoreStatus::Success;
}

EffectTableRestoreResult FailureResult(
    const EffectTableRestoreStatus status) noexcept
{
    EffectTableRestoreResult result{};
    result.status = status;
    return result;
}

EffectTableRestoreResult FailOwnedRestore(
    const EffectTableRestoreLease &lease,
    const EffectTableRestoreStatus status) noexcept
{
    const EffectTableRestoreStatus closeStatus =
        CloseCurrentThreadLease(lease);
    if (closeStatus == EffectTableRestoreStatus::Success)
        return FailureResult(status);

    EffectTableRestoreResult result = FailureResult(closeStatus);
    if (CurrentThreadOwnsLease(lease, true))
        result.lease = lease;
    return result;
}

EffectTableRestoreStatus MapNameReadFailure(
    const MemFileReadStatus status) noexcept
{
    switch (status)
    {
    case MemFileReadStatus::InvalidArgument:
        return EffectTableRestoreStatus::InvalidArgument;
    case MemFileReadStatus::InvalidState:
        return EffectTableRestoreStatus::InvalidState;
    case MemFileReadStatus::Overflow:
        return EffectTableRestoreStatus::TruncatedInput;
    case MemFileReadStatus::OutputTooSmall:
        return EffectTableRestoreStatus::NameTooLong;
    case MemFileReadStatus::Success:
        break;
    }
    return EffectTableRestoreStatus::UnsafeFailure;
}

EffectTableRestoreStatus MapDataReadFailure(
    const MemFileReadStatus status) noexcept
{
    switch (status)
    {
    case MemFileReadStatus::InvalidArgument:
        return EffectTableRestoreStatus::InvalidArgument;
    case MemFileReadStatus::InvalidState:
        return EffectTableRestoreStatus::InvalidState;
    case MemFileReadStatus::Overflow:
    case MemFileReadStatus::OutputTooSmall:
        return EffectTableRestoreStatus::TruncatedInput;
    case MemFileReadStatus::Success:
        break;
    }
    return EffectTableRestoreStatus::UnsafeFailure;
}

EffectTableRestoreStatus ValidateActiveLifecycle(
    const EffectTableRestoreLease &lease) noexcept
{
    if (!CurrentThreadOwnsLease(lease, false))
        return EffectTableRestoreStatus::OwnerMismatch;

    const EffectTableRestoreCallbacks callbacks =
        g_currentThreadOwner.callbacks;
    const bool valid = callbacks.validateLifecycle(
        callbacks.context,
        lease.identity,
        lease.lifecycleGeneration);

    // A callback may invoke an error-abandon path.  Check ownership before any
    // subsequent workspace write so an outer restore cannot corrupt a nested
    // restore that acquired the same TLS address with a newer serial.
    if (!CurrentThreadOwnsLease(lease, false))
        return EffectTableRestoreStatus::OwnerMismatch;
    return valid ? EffectTableRestoreStatus::Success
                 : EffectTableRestoreStatus::LifecycleChanged;
}

EffectTableRestoreStatus ParseEffectTable(
    MemoryFile *const memFile,
    const EffectTableRestoreLease &lease,
    std::size_t *const uniqueEntryCount) noexcept
{
    std::size_t rawEntryCount = 0;
    std::size_t entryCount = 0;

    for (;;)
    {
        if (!CurrentThreadOwnsLease(lease, false))
            return EffectTableRestoreStatus::OwnerMismatch;

        if (rawEntryCount == EFFECT_TABLE_RESTORE_CAPACITY)
        {
            std::size_t probeLength = 0;
            const MemFileReadStatus probeStatus =
                MemFile_TryReadCStringNoReport(
                    memFile,
                    g_capacityProbe,
                    sizeof(g_capacityProbe),
                    &probeLength);
            if (probeStatus == MemFileReadStatus::Success)
            {
                if (probeLength == 0)
                {
                    *uniqueEntryCount = entryCount;
                    return EffectTableRestoreStatus::Success;
                }
                return EffectTableRestoreStatus::CapacityExceeded;
            }
            if (probeStatus == MemFileReadStatus::OutputTooSmall)
                return EffectTableRestoreStatus::CapacityExceeded;
            return MapNameReadFailure(probeStatus);
        }

        std::size_t nameLength = 0;
        const MemFileReadStatus nameStatus =
            MemFile_TryReadCStringNoReport(
                memFile,
                g_restoreNames[entryCount],
                EFFECT_TABLE_RESTORE_NAME_CAPACITY,
                &nameLength);
        if (nameStatus != MemFileReadStatus::Success)
            return MapNameReadFailure(nameStatus);
        if (nameLength == 0)
        {
            *uniqueEntryCount = entryCount;
            return EffectTableRestoreStatus::Success;
        }

        ++rawEntryCount;
        if (!EffectTableRestoreNameIsValid(g_restoreNames[entryCount]))
            return EffectTableRestoreStatus::InvalidName;

        std::uint8_t keyBytes[4] = {};
        const MemFileReadStatus keyStatus =
            MemFile_TryReadDataNoReport(
                memFile,
                static_cast<int>(sizeof(keyBytes)),
                keyBytes);
        if (keyStatus != MemFileReadStatus::Success)
            return MapDataReadFailure(keyStatus);

        const std::uint32_t key =
            static_cast<std::uint32_t>(keyBytes[0])
            | (static_cast<std::uint32_t>(keyBytes[1]) << 8u)
            | (static_cast<std::uint32_t>(keyBytes[2]) << 16u)
            | (static_cast<std::uint32_t>(keyBytes[3]) << 24u);
        if (key == 0)
            return EffectTableRestoreStatus::InvalidKey;

        bool duplicate = false;
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            if (g_restoreKeys[index] != key)
                continue;
            if (g_restoreNameLengths[index] != nameLength
                || std::memcmp(
                       g_restoreNames[index],
                       g_restoreNames[entryCount],
                       nameLength + 1)
                    != 0)
            {
                return EffectTableRestoreStatus::ConflictingDuplicate;
            }
            duplicate = true;
            break;
        }
        if (duplicate)
            continue;

        g_restoreNameLengths[entryCount] =
            static_cast<std::uint8_t>(nameLength);
        g_restoreKeys[entryCount] = key;
        ++entryCount;
    }
}

unsigned char AsciiUpper(const unsigned char value) noexcept
{
    return value >= static_cast<unsigned char>('a')
            && value <= static_cast<unsigned char>('z')
        ? static_cast<unsigned char>(value - ('a' - 'A'))
        : value;
}

bool ComponentStemEquals(
    const char *const name,
    const std::size_t componentStart,
    const std::size_t stemLength,
    const char *const reserved,
    const std::size_t reservedLength) noexcept
{
    if (stemLength != reservedLength)
        return false;
    for (std::size_t index = 0; index < stemLength; ++index)
    {
        if (AsciiUpper(static_cast<unsigned char>(
                name[componentStart + index]))
            != static_cast<unsigned char>(reserved[index]))
        {
            return false;
        }
    }
    return true;
}

bool ComponentHasReservedWindowsStem(
    const char *const name,
    const std::size_t componentStart,
    const std::size_t componentEnd) noexcept
{
    // Win32 recognizes DOS device names even when an extension follows. It
    // also normalizes spaces before that extension, so compare the trimmed
    // stem rather than only the complete component.
    std::size_t stemEnd = componentStart;
    while (stemEnd < componentEnd && name[stemEnd] != '.')
        ++stemEnd;
    while (stemEnd > componentStart && name[stemEnd - 1] == ' ')
        --stemEnd;
    const std::size_t stemLength = stemEnd - componentStart;

    if (ComponentStemEquals(
            name, componentStart, stemLength, "CON", 3)
        || ComponentStemEquals(
            name, componentStart, stemLength, "PRN", 3)
        || ComponentStemEquals(
            name, componentStart, stemLength, "AUX", 3)
        || ComponentStemEquals(
            name, componentStart, stemLength, "NUL", 3)
        || ComponentStemEquals(
            name, componentStart, stemLength, "CONIN$", 6)
        || ComponentStemEquals(
            name, componentStart, stemLength, "CONOUT$", 7)
        || ComponentStemEquals(
            name, componentStart, stemLength, "CLOCK$", 6))
    {
        return true;
    }

    if (stemLength != 4 && stemLength != 5)
        return false;
    const unsigned char first = AsciiUpper(
        static_cast<unsigned char>(name[componentStart]));
    const unsigned char second = AsciiUpper(
        static_cast<unsigned char>(name[componentStart + 1]));
    const unsigned char third = AsciiUpper(
        static_cast<unsigned char>(name[componentStart + 2]));
    const bool portPrefix =
        (first == 'C' && second == 'O' && third == 'M')
        || (first == 'L' && second == 'P' && third == 'T');
    if (!portPrefix)
        return false;

    const unsigned char suffix =
        static_cast<unsigned char>(name[componentStart + 3]);
    if (stemLength == 4)
    {
        // Windows also treats the ISO-8859-1 superscript digits as reserved
        // COM/LPT port suffixes.
        return (suffix >= '1' && suffix <= '9')
            || suffix == 0xB9u || suffix == 0xB2u || suffix == 0xB3u;
    }

    // Reject the equivalent UTF-8 spelling as well, so the path remains safe
    // if a Windows build opts into the UTF-8 active code page.
    const unsigned char utf8Suffix =
        static_cast<unsigned char>(name[componentStart + 4]);
    return suffix == 0xC2u
        && (utf8Suffix == 0xB9u || utf8Suffix == 0xB2u
            || utf8Suffix == 0xB3u);
}
} // namespace

bool EffectTableRestoreNameIsValid(const char *const name) noexcept
{
    if (!name || !name[0])
        return false;

    std::size_t componentStart = 0;
    for (std::size_t index = 0;
         index < EFFECT_TABLE_RESTORE_NAME_CAPACITY;
         ++index)
    {
        const unsigned char value =
            static_cast<unsigned char>(name[index]);
        if (value == 0)
        {
            const std::size_t componentLength = index - componentStart;
            return componentLength != 0
                && !(componentLength == 1 && name[componentStart] == '.')
                && !(componentLength == 2 && name[componentStart] == '.'
                    && name[componentStart + 1] == '.')
                && name[index - 1] != '.'
                && name[index - 1] != ' '
                && !ComponentHasReservedWindowsStem(
                    name, componentStart, index);
        }
        if (value < 0x20u || value == 0x7Fu || value == ':'
            || value == '"' || value == '<' || value == '>'
            || value == '|' || value == '?' || value == '*')
        {
            return false;
        }
        if (value == '/' || value == '\\')
        {
            const std::size_t componentLength = index - componentStart;
            if (componentLength == 0
                || (componentLength == 1 && name[componentStart] == '.')
                || (componentLength == 2 && name[componentStart] == '.'
                    && name[componentStart + 1] == '.')
                || name[index - 1] == '.'
                || name[index - 1] == ' '
                || ComponentHasReservedWindowsStem(
                    name, componentStart, index))
            {
                return false;
            }
            componentStart = index + 1;
        }
    }
    return false;
}

EffectTableRestoreResult RestoreEffectTableNoReport(
    MemoryFile *const memFile,
    const void *const identity,
    const std::uint32_t lifecycleGeneration,
    const EffectTableRestoreCallbacks &callbacks) noexcept
{
    const EffectTableRestoreCallbacks stableCallbacks = callbacks;
    if (!memFile || !identity || !stableCallbacks.validateLifecycle
        || !stableCallbacks.registerEffect)
    {
        return FailureResult(EffectTableRestoreStatus::InvalidArgument);
    }
    if (g_currentThreadOwner.phase != OwnerPhase::Idle)
        return FailureResult(EffectTableRestoreStatus::Busy);

    if (!stableCallbacks.validateLifecycle(
            stableCallbacks.context, identity, lifecycleGeneration))
    {
        return FailureResult(EffectTableRestoreStatus::LifecycleChanged);
    }
    if (g_currentThreadOwner.phase != OwnerPhase::Idle)
        return FailureResult(EffectTableRestoreStatus::Busy);

    void *const ownerCookie = CurrentThreadOwnerCookie();
    void *const oldCookie = Sys_AtomicCompareExchangePointer(
        &g_restoreOwnerCookie,
        ownerCookie,
        static_cast<void *>(nullptr));
    if (oldCookie != nullptr)
        return FailureResult(EffectTableRestoreStatus::Busy);

    ++g_restoreSerial;
    if (g_restoreSerial == 0)
        ++g_restoreSerial;

    EffectTableRestoreLease lease{};
    lease.identity = identity;
    lease.lifecycleGeneration = lifecycleGeneration;
    lease.serial = g_restoreSerial;
    lease.ownerCookie = ownerCookie;
    g_currentThreadOwner.phase = OwnerPhase::Active;
    g_currentThreadOwner.lease = lease;
    g_currentThreadOwner.callbacks = stableCallbacks;

    ClearRestoreWorkspace();

    EffectTableRestoreStatus status = ValidateActiveLifecycle(lease);
    if (status != EffectTableRestoreStatus::Success)
        return FailOwnedRestore(lease, status);

    std::size_t entryCount = 0;
    status = ParseEffectTable(memFile, lease, &entryCount);
    if (status != EffectTableRestoreStatus::Success)
        return FailOwnedRestore(lease, status);

    status = ValidateActiveLifecycle(lease);
    if (status != EffectTableRestoreStatus::Success)
        return FailOwnedRestore(lease, status);

    for (std::size_t index = 0; index < entryCount; ++index)
    {
        status = ValidateActiveLifecycle(lease);
        if (status != EffectTableRestoreStatus::Success)
            return FailOwnedRestore(lease, status);

        const void *const definition = stableCallbacks.registerEffect(
            stableCallbacks.context, g_restoreNames[index]);
        if (!CurrentThreadOwnsLease(lease, false))
        {
            return FailOwnedRestore(
                lease, EffectTableRestoreStatus::OwnerMismatch);
        }
        if (!definition)
        {
            return FailOwnedRestore(
                lease, EffectTableRestoreStatus::RegistrationFailed);
        }

        status = ValidateActiveLifecycle(lease);
        if (status != EffectTableRestoreStatus::Success)
            return FailOwnedRestore(lease, status);
        g_restoreDefinitions[index] = definition;
    }

    status = ValidateActiveLifecycle(lease);
    if (status != EffectTableRestoreStatus::Success)
        return FailOwnedRestore(lease, status);

    // Every parse, lifecycle, and registration check has succeeded.  Make the
    // completed table observable with one scalar publication.
    g_restoreEntryCount = entryCount;

    EffectTableRestoreResult result{};
    result.status = EffectTableRestoreStatus::Success;
    result.lease = lease;
    result.entryCount = entryCount;
    return result;
}

bool EffectTableRestoreGetEntry(
    const EffectTableRestoreLease &lease,
    const std::size_t index,
    std::uint32_t *const key,
    const void **const definition) noexcept
{
    if (!key || !definition || !CurrentThreadOwnsLease(lease, false)
        || index >= g_restoreEntryCount)
    {
        return false;
    }
    if (ValidateActiveLifecycle(lease) != EffectTableRestoreStatus::Success)
        return false;

    *key = g_restoreKeys[index];
    *definition = g_restoreDefinitions[index];
    return true;
}

const void *EffectTableRestoreFind(
    const EffectTableRestoreLease &lease,
    const std::uint32_t key) noexcept
{
    if (key == 0 || !CurrentThreadOwnsLease(lease, false)
        || ValidateActiveLifecycle(lease)
            != EffectTableRestoreStatus::Success)
    {
        return nullptr;
    }

    for (std::size_t index = 0; index < g_restoreEntryCount; ++index)
    {
        if (g_restoreKeys[index] == key)
            return g_restoreDefinitions[index];
    }
    return nullptr;
}

EffectTableRestoreStatus ReleaseEffectTableRestore(
    const EffectTableRestoreLease &lease) noexcept
{
    if (!CurrentThreadOwnsLease(lease, true))
        return EffectTableRestoreStatus::OwnerMismatch;

    if (g_currentThreadOwner.phase == OwnerPhase::Closing)
        return CloseCurrentThreadLease(lease);

    const EffectTableRestoreStatus lifecycleStatus =
        ValidateActiveLifecycle(lease);
    if (lifecycleStatus == EffectTableRestoreStatus::OwnerMismatch)
        return lifecycleStatus;

    const EffectTableRestoreStatus closeStatus =
        CloseCurrentThreadLease(lease);
    if (closeStatus != EffectTableRestoreStatus::Success)
        return closeStatus;
    return lifecycleStatus;
}

void AbandonCurrentThreadEffectTableRestoreForError() noexcept
{
    if (g_currentThreadOwner.phase == OwnerPhase::Idle)
        return;
    (void)CloseCurrentThreadLease(g_currentThreadOwner.lease);
}

bool EffectTableRestoreLeaseIsActive() noexcept
{
    return LoadOwnerCookie() != nullptr;
}
} // namespace fx::archive

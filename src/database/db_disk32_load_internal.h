#pragma once

// Steps shared by the generated 64-bit family loaders (db_disk32_loaders.h,
// from db_disk32.schema) and the hand-written parts of custom families
// (db_disk32_stringtable.cpp). Internal to the family translation units. See
// docs/design/FASTFILE_LOADER.md.

#include <universal/kisak_abi.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32.h>
#include <database/db_disk32_load.h>
#include <qcommon/com_error.h>

#include <cstddef>
#include <cstdint>

namespace db::disk32_load
{
constexpr std::uint32_t kTempBlock = 0;
constexpr std::uint32_t kVirtualBlock = 4;

inline bool Drop(const char *message)
{
    Com_Error(ERR_DROP, "%s", message);
    return false;
}

// Streams size disk bytes at the current position of the current block, as
// Load_Stream does for the 32-bit loader, and reports whether they arrived.
inline bool StreamBytes(std::uint8_t *at, std::int32_t size)
{
    if (!at)
        return false;
    Load_Stream(true, at, size);
    return DB_GetStreamPos() == at + size;
}

// A pointer step's disk32 slot in block 4, once the step has filled the
// native slot: a later offset token may name the slot, as the 32-bit loader
// reads the pointer back out of the slot it streamed (DB_RecordPointerSlot).
inline void RecordDiskSlot(const void *diskSlot, DBAliasKind kind, const void *pointer)
{
    if (pointer)
        DB_RecordPointerSlot(diskSlot, kind, pointer);
}

// The address field had in the streamed bytes at record, where disk is the
// mirror they were copied into: the disk32 slot a pointer step records.
template <typename Disk, typename Field>
const std::uint8_t *SlotOf(const std::uint8_t *record, const Disk &disk, const Field &field)
{
    return record + (reinterpret_cast<const std::uint8_t *>(&field) - reinterpret_cast<const std::uint8_t *>(&disk));
}

// count native elements of T in zone-lifetime storage (DB_AllocZoneNative);
// exhaustion is an error, not a fallback. count is checked by the caller.
template <typename T>
T *AllocNative(std::int32_t count)
{
    auto *const storage = reinterpret_cast<T *>(
        DB_AllocZoneNative(static_cast<std::size_t>(count) * sizeof(T), alignof(T)));
    if (!storage)
        Drop("Fast-file native storage is exhausted");
    return storage;
}

// Load_XString: a null token, an inline string streamed here, or an offset
// token naming a string an earlier record streamed.
inline bool LoadXString(disk32::Ptr32<const char> field, const char **out)
{
    *out = nullptr;
    if (field.token.isNull())
        return true;
    if (field.token.isInline())
    {
        char *const text = reinterpret_cast<char *>(DB_AllocStreamPos(0));
        char *cursor = text;
        if (!text || !Load_XStringCustom(&cursor))
            return false;
        *out = text;
        return true;
    }
    std::uintptr_t address = 0;
    std::uint32_t byteCount = 0;
    const db::relocation::Status status = DB_ResolveOffsetCString(
        field.token, db::relocation::BlockBit(kVirtualBlock), &address, &byteCount);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file string offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<const char *>(address);
    return true;
}
} // namespace db::disk32_load

#endif // KISAK_ARCH_64BIT

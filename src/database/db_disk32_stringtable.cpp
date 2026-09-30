#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_load_internal.h>
#include <database/db_disk32_mirrors.h> // generated from db_disk32.schema
#include <database/db_validation.h>
#include <qcommon/com_error.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// The second wave-1 family (docs/design/FASTFILE_LOADER.md). Each step mirrors
// Load_StringTablePtr/Load_StringTable in db_load.cpp: no stream push, so the
// 16-byte record, its name, its value tokens and their strings stream at the
// current position of block 4 in the retail order and alignment. The disk
// array holds 4-byte string tokens and the native one 8-byte pointers, so the
// native table and its values are converted into zone-lifetime native storage
// (DB_AllocZoneNative). String bytes stay in block 4. The disk record stays
// the completed-object identity that later offset tokens name; the alias
// registry maps it to the native table, so no alias points at disk bytes.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace
{
using db::disk32_load::Drop;
using db::disk32_load::LoadXString;
using db::disk32_load::StreamBytes;

constexpr auto kRecordBytes = static_cast<std::uint32_t>(sizeof(disk32::StringTableDisk32));
constexpr auto kTokenBytes = static_cast<std::uint32_t>(sizeof(disk32::PointerToken));

// Streams the record at `record` and converts it; *out is the native table.
bool LoadStringTable(std::uint8_t *record, StringTable **out)
{
    disk32::StringTableDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(kRecordBytes)))
        return false;
    std::memcpy(&disk, record, sizeof(disk));

    // As in the 32-bit loader, any non-null values token means the tokens
    // follow inline, and they must be present exactly when the table has
    // cells. Both checks precede every other read.
    std::int32_t count = 0;
    if (!db::validation::CheckedCountProduct(disk.rowCount, disk.columnCount, &count))
        return Drop("Invalid fast-file string-table size");
    if ((count != 0) == disk.values.token.isNull())
        return Drop("Invalid fast-file string-table values");

    const char *name = nullptr;
    if (!LoadXString(disk.name, &name))
        return false;
    if (!name || !*name)
        return Drop("Fast-file string table has no name"); // the asset pool hashes it

    const std::uint8_t *tokens = nullptr;
    if (count)
    {
        std::int32_t tokenBytes = 0;
        if (!db::validation::CheckedArrayBytes(count, kTokenBytes, &tokenBytes))
            return Drop("Invalid fast-file string-table size");
        std::uint8_t *const at = DB_AllocStreamPos(3);
        if (!StreamBytes(at, tokenBytes))
            return false;
        tokens = at;
    }

    // count <= INT32_MAX / 4, so this cannot overflow a 64-bit size_t.
    const std::size_t nativeBytes = sizeof(StringTable) + static_cast<std::size_t>(count) * sizeof(const char *);
    std::uint8_t *const storage = DB_AllocZoneNative(nativeBytes, alignof(StringTable));
    if (!storage)
        return Drop("Fast-file native storage is exhausted");
    auto *const table = reinterpret_cast<StringTable *>(storage);
    auto *const values = count ? reinterpret_cast<const char **>(storage + sizeof(StringTable)) : nullptr;
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::Ptr32<const char> token{};
        std::memcpy(&token, tokens + static_cast<std::size_t>(index) * kTokenBytes, sizeof(token));
        if (!LoadXString(token, &values[index]))
            return false;
    }
    table->name = name;
    table->columnCount = disk.columnCount;
    table->rowCount = disk.rowCount;
    table->values = values;
    *out = table;
    return true;
}

void LoadStringTablePtr(disk32::PointerToken token, StringTable **slot)
{
    if (token.isNull())
        return;
    if (!token.isInline())
    {
        // The 32-bit loader sends every other token, -2 included, to the
        // alias registry; it must name a completed table in block 4.
        std::uintptr_t native = 0;
        const db::relocation::Status status = DB_ResolveCompletedObjectNative(
            token, DBAliasKind::StringTable, kRecordBytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<StringTable *>(native);
        return;
    }
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!record)
        return;
    const DBAliasHandle completed = DB_RegisterPointerSlot(record, DBAliasKind::StringTable);
    StringTable *table = nullptr;
    if (!completed || !LoadStringTable(record, &table)
        || !DB_CompleteObject(completed, DBAliasKind::StringTable, record, kRecordBytes, kRecordBytes, table))
    {
        return;
    }
    XAssetHeader header;
    header.stringTable = table;
    Load_StringTableAsset(&header);
    if (!header.stringTable)
    {
        Drop("Fast-file string table was not registered");
        return;
    }
    *slot = header.stringTable;
}
} // namespace

void __cdecl DB_LoadStringTablePtrDisk32(bool atStreamStart, StringTable **slot)
{
    // Asset headers arrive inside the already-streamed XAsset array; a native
    // slot is never the 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file string-table header request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file string-table header slot holds no disk32 token");
        return;
    }
    LoadStringTablePtr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
}

#endif // KISAK_ARCH_64BIT

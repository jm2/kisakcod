#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

#include <cstddef>
#include <cstdint>
#include <cstring>

// The second wave-1 family (docs/design/FASTFILE_LOADER.md). Its header slot
// and completed-object pointer step are generated from its schema entry; the
// record body below is custom. It mirrors Load_StringTable in db_load.cpp: the
// 16-byte record, its name, its value tokens and their strings stream at the
// current position of block 4 in the retail order and alignment. The disk
// array holds 4-byte string tokens and the native one 8-byte pointers, so the
// native table and its values are converted into zone-lifetime native storage
// (DB_AllocZoneNative). String bytes stay in block 4. Frames hold no
// destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr auto kRecordBytes = static_cast<std::uint32_t>(sizeof(disk32::StringTableDisk32));
constexpr auto kTokenBytes = static_cast<std::uint32_t>(sizeof(disk32::PointerToken));

// columnCount * rowCount, checked. As in the 32-bit loader, any non-null
// values token means the tokens follow inline, and a table with cells has
// them. A table without cells may still have the token (retail ships one).
bool CheckedValueCount(const disk32::StringTableDisk32 &disk, std::int32_t *count)
{
    if (!db::validation::CheckedCountProduct(disk.rowCount, disk.columnCount, count))
        return Drop("Invalid fast-file string-table size");
    if (*count != 0 && disk.values.token.isNull())
        return Drop("Invalid fast-file string-table values");
    return true;
}

bool LoadName(disk32::Ptr32<const char> field, const char **name)
{
    if (!LoadXString(field, name))
        return false;
    if (!*name || !**name)
        return Drop("Fast-file string table has no name"); // the asset pool hashes it
    return true;
}

// Streams the 4-aligned array of count disk32 string tokens. A present empty
// array still aligns the stream, as the 32-bit AllocLoad does.
bool StreamValueTokens(disk32::PointerToken values, std::int32_t count, const std::uint8_t **tokens)
{
    *tokens = nullptr;
    if (values.isNull())
        return true;
    if (!count)
        return DB_AllocStreamPos(3) != nullptr;
    std::int32_t tokenBytes = 0;
    if (!db::validation::CheckedArrayBytes(count, kTokenBytes, &tokenBytes))
        return Drop("Invalid fast-file string-table size");
    std::uint8_t *const at = DB_AllocStreamPos(3);
    if (!StreamBytes(at, tokenBytes))
        return false;
    *tokens = at;
    return true;
}

// Resolves each disk32 token into its native 8-byte pointer.
bool LoadValues(const std::uint8_t *tokens, std::int32_t count, const char **values)
{
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::Ptr32<const char> token{};
        std::memcpy(&token, tokens + static_cast<std::size_t>(index) * kTokenBytes, sizeof(token));
        if (!LoadXString(token, &values[index]))
            return false;
    }
    return true;
}
} // namespace

// Streams the record at `record` and converts it; *out is the native table.
// The count checks precede every string read.
bool LoadStringTable(std::uint8_t *record, StringTable **out)
{
    disk32::StringTableDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(kRecordBytes)))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    std::int32_t count = 0;
    const char *name = nullptr;
    const std::uint8_t *tokens = nullptr;
    if (!CheckedValueCount(disk, &count) || !LoadName(disk.name, &name) || !StreamValueTokens(disk.values.token, count, &tokens))
        return false;

    // count <= INT32_MAX / 4, so this cannot overflow a 64-bit size_t.
    const std::size_t nativeBytes = sizeof(StringTable) + static_cast<std::size_t>(count) * sizeof(const char *);
    std::uint8_t *const storage = DB_AllocZoneNative(nativeBytes, alignof(StringTable));
    if (!storage)
        return Drop("Fast-file native storage is exhausted");
    auto *const table = reinterpret_cast<StringTable *>(storage);
    auto *const values = count ? reinterpret_cast<const char **>(storage + sizeof(StringTable)) : nullptr;
    if (!LoadValues(tokens, count, values))
        return false;
    table->name = name;
    table->columnCount = disk.columnCount;
    table->rowCount = disk.rowCount;
    table->values = values;
    *out = table;
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadStringTablePtrDisk32(bool atStreamStart, StringTable **slot)
{
    db::disk32_load::LoadStringTableHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT

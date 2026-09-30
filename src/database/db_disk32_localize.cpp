#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_mirrors.h> // generated from db_disk32.schema
#include <qcommon/com_error.h>

#include <cstdint>
#include <cstring>

// Wave-2 family LocalizeEntry (docs/design/FASTFILE_LOADER.md). It follows the
// RawFile loader in db_disk32_load.cpp step for step. Each step mirrors
// Load_LocalizeEntryPtr/Load_LocalizeEntry in db_load.cpp: the same stream
// pushes, alignments and reads, so block offsets stay the retail ones. The
// 8-byte disk32 record is read into its mirror and converted into a native
// LocalizeEntry (16 bytes), and both strings resolve at full width. The native
// header is a temporary because Load_LocalizeEntryAsset copies it into the
// asset pool. Frames hold no destructors, since a production
// Com_Error(ERR_DROP) longjmps out of them.
//
// Drop, StreamBytes and LoadXString repeat the RawFile loader's helpers, and
// LoadLocalizeEntryPtr repeats its alias and pool steps with the family's kind
// and pool call. They are the shared part that moves into the generator.
namespace
{
constexpr std::uint32_t kTempBlock = 0;
constexpr std::uint32_t kVirtualBlock = 4;

bool Drop(const char *message)
{
    Com_Error(ERR_DROP, "%s", message);
    return false;
}

// Streams size disk bytes at the current position of the current block, as
// Load_Stream does for the 32-bit loader, and reports whether they arrived.
bool StreamBytes(std::uint8_t *at, std::int32_t size)
{
    if (!at)
        return false;
    Load_Stream(true, at, size);
    return DB_GetStreamPos() == at + size;
}

// Load_XString: a null token, an inline string streamed here, or an offset
// token naming a string an earlier record streamed.
bool LoadXString(disk32::Ptr32<const char> field, const char **out)
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

bool LoadLocalizeEntry(LocalizeEntry *out)
{
    disk32::LocalizeEntryDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));

    // The 32-bit loader streams value before name. Both are C strings the
    // stream code bounds and terminates. A null value stays null: it is an
    // untranslated entry, and every SE_GetString caller handles that.
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.value, &out->value) || !LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file localized string has no name"); // the asset pool hashes it
    DB_PopStreamPos();
    return true;
}

void LoadLocalizeEntryPtr(disk32::PointerToken token, LocalizeEntry **slot)
{
    if (token.isOffset())
    {
        std::uintptr_t pointer = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::LocalizeEntry, 0, &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<LocalizeEntry *>(pointer);
        return;
    }
    if (token.isNull() || !DB_AllocStreamPos(3))
        return;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::LocalizeEntry) : DBAliasHandle{};
    LocalizeEntry native{};
    if ((token.isSharedInline() && !inserted) || !LoadLocalizeEntry(&native))
        return;
    XAssetHeader header;
    header.localize = &native;
    Load_LocalizeEntryAsset(&header);
    *slot = header.localize;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::LocalizeEntry, header.localize);
}
} // namespace

void __cdecl DB_LoadLocalizeEntryPtrDisk32(bool atStreamStart, LocalizeEntry **slot)
{
    // Asset headers arrive inside the already-streamed XAsset array; a native
    // slot is never the 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file localized-string request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file localized-string slot holds no disk32 token");
        return;
    }
    DB_PushStreamPos(kTempBlock);
    LoadLocalizeEntryPtr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
    DB_PopStreamPos();
}

#endif // KISAK_ARCH_64BIT

#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/database.h>
#include <database/db_disk32_mirrors.h> // generated from db_disk32.schema
#include <qcommon/com_error.h>

#include <cstdint>
#include <cstring>

// Wave-2 family MapEnts (docs/design/FASTFILE_LOADER.md). It follows the
// RawFile loader in db_disk32_load.cpp step for step. Each step mirrors
// Load_MapEntsPtr/Load_MapEnts in db_load.cpp: the same stream pushes,
// alignments and reads, so block offsets stay the retail ones. The 12-byte
// disk32 record is read into its mirror and converted into a native MapEnts
// (24 bytes). The name resolves at full width, and the entity string points
// straight at its zone bytes, which are the same at both widths. The native
// header is a temporary because Load_MapEntsAsset copies it into the asset
// pool. Frames hold no destructors, since a production Com_Error(ERR_DROP)
// longjmps out of them.
//
// Drop, StreamBytes and LoadXString repeat the RawFile loader's helpers, and
// LoadMapEntsPtr repeats its alias and pool steps with the family's kind and
// pool call. They are the shared part that moves into the generator.
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

bool LoadMapEnts(MapEnts *out)
{
    disk32::MapEntsDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));

    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file map entities have no name"); // the asset pool hashes it
    // numEntityChars counts the entity string's bytes, terminator included
    // (MapEnts_GetFromString builds it so), and G_ParseSpawnVars parses the
    // string up to that terminator. As in the 32-bit loader, any non-null
    // token means the bytes follow inline.
    out->numEntityChars = disk.numEntityChars;
    if (disk.entityString.token.isNull())
    {
        if (disk.numEntityChars != 0)
            return Drop("Fast-file map entities count characters but have no entity string");
    }
    else
    {
        std::uint8_t *const bytes = DB_AllocStreamPos(0);
        if (disk.numEntityChars <= 0)
            return Drop("Invalid fast-file entity-string length");
        if (!StreamBytes(bytes, disk.numEntityChars))
            return false;
        if (bytes[disk.numEntityChars - 1] != '\0')
            return Drop("Fast-file entity string is not terminated");
        out->entityString = reinterpret_cast<char *>(bytes);
    }
    DB_PopStreamPos();
    return true;
}

void LoadMapEntsPtr(disk32::PointerToken token, MapEnts **slot)
{
    if (token.isOffset())
    {
        std::uintptr_t pointer = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::MapEnts, 0, &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<MapEnts *>(pointer);
        return;
    }
    if (token.isNull() || !DB_AllocStreamPos(3))
        return;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::MapEnts) : DBAliasHandle{};
    MapEnts native{};
    if ((token.isSharedInline() && !inserted) || !LoadMapEnts(&native))
        return;
    XAssetHeader header;
    header.mapEnts = &native;
    Load_MapEntsAsset(&header);
    *slot = header.mapEnts;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::MapEnts, header.mapEnts);
}
} // namespace

void __cdecl DB_LoadMapEntsPtrDisk32(bool atStreamStart, MapEnts **slot)
{
    // Headers arrive inside the already-streamed XAsset array, and a nested
    // slot inside an already-converted parent; a native slot is never the
    // 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file map-entities request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file map-entities slot holds no disk32 token");
        return;
    }
    DB_PushStreamPos(kTempBlock);
    LoadMapEntsPtr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
    DB_PopStreamPos();
}

#endif // KISAK_ARCH_64BIT
